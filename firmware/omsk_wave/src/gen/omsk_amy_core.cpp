#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../sw_config.h"

#if CFG_ENGINE_AMY

#include "../synth/synth.h"
#include "../synth/synth_defs.h"
#include "omsk_core.h"
#include "pico/float.h"
#include "pico/stdlib.h"
#include "../tables/omsk_wavetables.h"
#include "../../../shared/hardware/midi_helpers.h"
extern "C" {
#include "../leds/leds.h"
}

#ifndef __not_in_flash_func
#define __not_in_flash_func(x) x
#endif

extern "C" {
#include "amy.h"
}

// =============================================================================
// Internal state
// =============================================================================

// (ring buffer handles all async rendering; no separate index needed)

// Ring buffer for async AMY rendering (background fills, audio core reads)
#define RING_BUF_SIZE 2048
static float ring_buf_l[RING_BUF_SIZE];
static float ring_buf_r[RING_BUF_SIZE];
static volatile uint32_t write_ptr = 0;
static volatile uint32_t read_ptr = 0;

// Voice allocator: note → AMY oscillator index
#define MAX_POLY_OSCS 16
static uint8_t note_to_osc[128];
static uint8_t next_osc = 0;

// Cached global wave type (updated by omsk_core_set_param)
static uint16_t g_amy_wave = SAW_UP;

// =============================================================================
// Helpers
// =============================================================================

// Map VCO wave param (0-127 morph) to an AMY wave type
static uint16_t vco_wave_to_amy(uint8_t wave_param) {
    // Our VCO morph: 0=SIN .. 32=SAW .. 64=TRI .. 96=SQR .. 127=PAM
    // Map 6 segments to AMY basic waveforms
    if (wave_param < 21)  return SINE;
    if (wave_param < 43)  return SAW_UP;
    if (wave_param < 64)  return TRIANGLE;
    if (wave_param < 85)  return SAW_DOWN;
    if (wave_param < 107) return PULSE;
    return NOISE;
}

// ADSR param (0-127) → time in AMY samples (@ 44100)
// Exponential mapping: 0→5ms, 64→250ms, 127→5000ms
static uint32_t adsr_param_to_samples(uint8_t p) {
    // log-scale: t_ms = 5 * (1000)^(p/127)
    float t_ms = 5.0f * powf(1000.0f, (float)p / 127.0f);
    return (uint32_t)(t_ms * (44100.0f / 1000.0f));
}

// =============================================================================
// Core API
// =============================================================================

extern "C" {

void omsk_core_init(void) {
    amy_config_t config = amy_default_config();
    config.max_oscs    = MAX_POLY_OSCS + 2;
    config.max_voices  = MAX_POLY_OSCS;
    config.max_synths  = 1;

    // We handle audio output ourselves
    config.audio              = AMY_AUDIO_IS_NONE;
    config.platform.multicore = 0;
    config.platform.multithread = 0;

    // Disable heavy optional features we don't use
    config.features.reverb        = 0;
    config.features.chorus        = 0;
    config.features.echo          = 0;
    config.features.partials      = 0;
    config.features.default_synths = 0;
    config.features.startup_bleep = 0;

    leds_set_all(255, 128, 0); // Amber: AMY init started
    leds_show();

    amy_start(config);
    amy_global.volume = 1.0f;

    // Reset ring buffer and voice table
    write_ptr = 0;
    read_ptr  = 0;
    for (int i = 0; i < 128; i++) note_to_osc[i] = 255;
    next_osc = 0;

    // Determine initial wave from params
    g_amy_wave = vco_wave_to_amy(params.vco1_wave);

    leds_set_all(0, 255, 0); // Green: AMY init done
    leds_show();
}

// ---------------------------------------------------------------------------
// Background fill: called from Core 1 idle time (between I2S samples)
// Renders AMY blocks into the ring buffer when space is available.
// ---------------------------------------------------------------------------
void omsk_core_background_process(void) {
    uint32_t current_read  = read_ptr;
    uint32_t current_write = write_ptr;

    uint32_t used;
    if (current_write >= current_read) {
        used = current_write - current_read;
    } else {
        used = RING_BUF_SIZE - (current_read - current_write);
    }

    uint32_t free_space = RING_BUF_SIZE - 1 - used;

    if (free_space >= (uint32_t)AMY_BLOCK_SIZE) {
        int16_t* samples = amy_simple_fill_buffer();
        const float inv_32768 = 1.0f / 32768.0f;

        uint32_t w = current_write;
        for (int i = 0; i < AMY_BLOCK_SIZE; i++) {
            ring_buf_l[w] = (float)samples[i * 2]     * inv_32768;
            ring_buf_r[w] = (float)samples[i * 2 + 1] * inv_32768;
            w = (w + 1) % RING_BUF_SIZE;
        }
        write_ptr = w;
    }
}

// ---------------------------------------------------------------------------
// Audio sample fetch: called per-sample from Core 1 audio thread
// ---------------------------------------------------------------------------
void __not_in_flash_func(omsk_core_process)(float *out_l, float *out_r) {
    uint32_t r = read_ptr;
    if (r != write_ptr) {
        *out_l = ring_buf_l[r];
        *out_r = ring_buf_r[r];
        read_ptr = (r + 1) % RING_BUF_SIZE;
    } else {
        *out_l = 0.0f;
        *out_r = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// Note On: allocate oscillator, program waveform + ADSR, trigger note
// Uses EG2 (AMP envelope) for amplitude shaping.
// ---------------------------------------------------------------------------
void omsk_core_note_on(uint8_t note, uint8_t velocity) {
    // Allocate next oscillator (round-robin)
    uint8_t osc = next_osc;
    note_to_osc[note & 0x7F] = osc;
    next_osc = (next_osc + 1) % MAX_POLY_OSCS;

    // Read EG2 (AMP) params from global SynthParams
    uint32_t atk_s = adsr_param_to_samples(params.eg2_attack);
    uint32_t dec_s = adsr_param_to_samples(params.eg2_decay);
    float    sus   = (float)params.eg2_sustain / 127.0f;
    // Release will be sent on note_off

    // --- Program waveform, frequency and envelope ---
    float trans_semi = roundf(((float)params.vco1_transpose - 64.0f) / 64.0f * 5.0f) * 12.0f;
    float detune_semi = detune_table[params.vco1_detune & 0x7F] / 100.0f;

    amy_event e = amy_default_event();
    e.osc       = osc;
    e.wave      = g_amy_wave;
    e.midi_note = (float)note + trans_semi;
    // e.detune    = detune_semi;
    e.velocity  = (float)velocity / 127.0f;

    // Amplitude driven entirely by EG0 (no constant component)
    e.amp_coefs[COEF_CONST] = 0.0f;
    e.amp_coefs[COEF_EG0]   = 1.0f;

    // ADSR breakpoints for EG0:
    //   t=0           → attack start (implicit 0)
    //   t=atk_s       → peak (1.0)
    //   t=atk_s+dec_s → decay end = sustain level
    // AMY holds the last value until next event (= sustain)
    e.eg0_times[0]  = atk_s;
    e.eg0_values[0] = 1.0f;
    e.eg0_times[1]  = atk_s + dec_s;
    e.eg0_values[1] = sus;

    // Mark EG0 breakpoint set as programmed
    e.bp_is_set[0] = 1;

    amy_add_event(&e);
}

// ---------------------------------------------------------------------------
// Note Off: send release envelope then silence oscillator
// ---------------------------------------------------------------------------
void omsk_core_note_off(uint8_t note) {
    uint8_t osc = note_to_osc[note & 0x7F];
    if (osc == 255) return;

    uint32_t rel_s = adsr_param_to_samples(params.eg2_release);

    // Release: AMY interpolates from current EG level to 0 over rel_s samples
    amy_event e = amy_default_event();
    e.osc      = osc;
    e.velocity = 0.0f; // signals note-off to AMY

    e.amp_coefs[COEF_CONST] = 0.0f;
    e.amp_coefs[COEF_EG0]   = 1.0f;

    // One breakpoint: reach 0 in rel_s samples from now
    e.eg0_times[0]  = rel_s;
    e.eg0_values[0] = 0.0f;
    e.bp_is_set[0]  = 1;

    amy_add_event(&e);
    note_to_osc[note & 0x7F] = 255;
}

// ---------------------------------------------------------------------------
// Parameter updates from encoders / UI
// Only VCO and global gain params are meaningful for AMY mode.
// VCF, LFO, FX are placeholders (no-op) in this mode.
// ---------------------------------------------------------------------------
void omsk_core_set_param(uint8_t param_id, uint16_t value) {
    switch ((ParamID)param_id) {
        // VCO1 waveform → AMY wave type
        case PARAM_VCO1_WAVE:
            g_amy_wave = vco_wave_to_amy((uint8_t)value);
            break;

        case PARAM_VCO1_TRANSPOSE:
        case PARAM_VCO1_DETUNE:
        {
            float trans_semi = roundf(((float)params.vco1_transpose - 64.0f) / 64.0f * 5.0f) * 12.0f;
            float detune_semi = detune_table[params.vco1_detune & 0x7F] / 100.0f;
            // Update all active oscillators
            for (int i = 0; i < 128; i++) {
                if (note_to_osc[i] != 255) {
                    amy_event e = amy_default_event();
                    e.osc = note_to_osc[i];
                    e.midi_note = (float)i + trans_semi;
                    // e.detune = detune_semi;
                    amy_add_event(&e);
                }
            }
            break;
        }

        // VCO2 ignored in AMY mode (single oscillator per voice)
        case PARAM_VCO2_TRANSPOSE:
        case PARAM_VCO2_DETUNE:
        case PARAM_VCO2_WAVE:
            break;
        // EG params: stored in params struct, read lazily on next note_on/note_off
        // (no action here; params struct is updated by the caller before this fn)
        case PARAM_EG2_ATTACK:
        case PARAM_EG2_DECAY:
        case PARAM_EG2_SUSTAIN:
        case PARAM_EG2_RELEASE:
            break;
        // Global gain: update AMY volume
        case PARAM_AMP_GAIN:
            amy_global.volume = (float)value / 100.0f;
            break;
        // VCF, LFO, FX, EG1, MIXER, etc. → placeholders, sound bypass via AMY
        default:
            break;
    }
}

void omsk_core_update_control(void) {
    // No control-rate processing needed — AMY handles its own modulation
}

void omsk_core_all_notes_off(void) {
    amy_reset_oscs();
    for (int i = 0; i < 128; i++) note_to_osc[i] = 255;
    next_osc = 0;
}

void omsk_core_pitch_bend(uint8_t lsb, uint8_t msb) {
    // Convert 14-bit pitch bend value to AMY pitch_bend (in semitones/12 = octave fraction)
    float bend_semi = midi_pitch_bend_to_semitones(lsb, msb, (float)params.pitch_bend_range);
    amy_global.pitch_bend = bend_semi / 12.0f; // AMY pitch_bend is in octave fractions
}

void omsk_core_set_pitch_bend_range(uint8_t semitones) {
    (void)semitones; // stored in params, read by pitch_bend handler
}

void omsk_core_set_sustain(bool on) {
    // Send sustain pedal to AMY (via all active oscs is not ideal; ignore for now)
    (void)on;
}

void omsk_core_set_modwheel(uint8_t value)   { (void)value; }
void omsk_core_set_aftertouch(uint8_t value) { (void)value; }
void omsk_core_set_breath(uint8_t value)     { (void)value; }

} // extern "C"

#endif // CFG_ENGINE_AMY
