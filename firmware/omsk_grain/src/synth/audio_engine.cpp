#include "audio_engine.h"
#include "fx.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../sw_config.h"
#if __has_include("../tables/audio_data.h")
#include "../tables/audio_data.h"
#else
#include "../tables/audio_data_stub.h"
#endif
#include "../tables/eg_lut.h"
#include "fast_math.h"

// ------------------------------------------------------------------ //
//  LOOKUP TABLES
// ------------------------------------------------------------------ //

static float SINE_LUT[512];
static float POW2_LUT[512]; // for 2^x, x in [0, 10]
static float NOTE_FREQ_MULT_LUT[128];
static float SEMI_PITCH_LUT[49]; // -24 to +24
static float OCT_PITCH_LUT[7];   // -3 to +3
static bool luts_init = false;

static void init_engine_luts() {
    if (luts_init) return;
    for (int i = 0; i < 512; i++) {
        SINE_LUT[i] = sinf(i * 2.0f * M_PI / 512.0f);
        POW2_LUT[i] = powf(2.0f, (i / 511.0f) * 10.0f); // Map 0..1 to 2^0..2^10
    }
    for (int i = 0; i < 128; i++) {
        NOTE_FREQ_MULT_LUT[i] = powf(2.0f, (i - 60.0f) / 12.0f);
    }
    for (int i = 0; i < 49; i++) {
        SEMI_PITCH_LUT[i] = powf(2.0f, (i - 24) / 12.0f);
    }
    for (int i = 0; i < 7; i++) {
        OCT_PITCH_LUT[i] = powf(2.0f, i - 3);
    }
    luts_init = true;
}

static inline float get_sine_lut(float phase) {
    while (phase < 0) phase += 1.0f;
    while (phase >= 1.0f) phase -= 1.0f;
    int idx = (int)(phase * 511.99f);
    return SINE_LUT[idx & 511];
}

static inline float get_exp_freq(float ratio) {
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    int idx = (int)(ratio * 511.99f);
    return 20.0f * POW2_LUT[idx & 511];
}

static float get_curved_env(float phase, float curve) {
    if (phase < 0.0f) phase = 0.0f;
    if (phase > 1.0f) phase = 1.0f;
    int c_idx = (int)((curve + 1.0f) * 10.0f + 0.5f);
    if (c_idx < 0) c_idx = 0;
    if (c_idx >= NUM_EG_CURVES) c_idx = NUM_EG_CURVES - 1;
    int t_idx = (int)(phase * (EG_LUT_SIZE - 1.0f));
    return EG_LUT[c_idx][t_idx];
}

// ------------------------------------------------------------------ //
//  ENGINE
// ------------------------------------------------------------------ //

GranularEngine::GranularEngine(int vid) : voice_id(vid) {
    init_engine_luts();
    memset(&p, 0, sizeof(p));
    memset(mod_src, 0, sizeof(mod_src));
    memset(mod_amt, 0, sizeof(mod_amt));
    
    p.pos = 0.5f;
    p.size = 0.1f;
    p.dens = 20.0f;
    p.pitch = 1.0f;
    p.max_grains = 32;
    p.grain_amp = 0.8f;
    p.keytrack = 1.0f; // Enable pitch keytracking by default (100%)
    p.cutoff = 20000.0f;
    p.res = 0.7f;
    p.filt_type = 3; // Off
    p.atk = 0.01f;
    p.rel = 0.3f;
    p.vol = 0.7f;
    p.master_vol = 1.0f;
    p.midi_ch = 1;
    p.midi_mode = 0;
    p.viz_scale = 1.0f;
    p.fx_bc = 16.0f;
    p.fx_ds = 1.0f;
    
    playback_pos = 0;
    next_grain_time = 0;
    is_triggered = false;
    current_note = -1;
    last_note = 60; // Default to reference C4 note (1.0x ratio)
    pitch_bend_semitones = 0.0f;
    modwheel_val = 0.0f;
    aftertouch_val = 0.0f;
    sustain_active = false;
    is_sustained = false;
    lin_env = 0;

    master_env = 0;
    lfo_phases[0] = 0;
    lfo_phases[1] = 0;
    
    fx_processor.reset();
}

void GranularEngine::set_trigger(bool triggered) {
    is_triggered = triggered;
}

float GranularEngine::get_modulated_param(int param_id, float base_val, float lo, float hi) {
    if (param_id >= 64) return base_val;
    uint8_t src = mod_src[param_id];
    if (src == 0) return base_val;
    float amt = mod_amt[param_id];
    float mod = get_mod_val(src);
    
    float mod_val;
    if (src == 4) { // EG (bipolar -1..1)
        // get_mod_val returns 0..1, map to -1..1
        mod_val = (mod - 0.5f) * 2.0f;
    } else { // Jitter, LFOs (bipolar -1..1)
        mod_val = mod * 2.0f - 1.0f;
    }
    
    if (param_id == 29) { // PARAM_CUTOFF
        // Logarithmic modulation
        float ratio = log2f(base_val / 20.0f) / 10.0f; // 10 octaves
        ratio += amt * mod_val;
        return get_exp_freq(ratio);
    }
    
    float range = hi - lo;
    float result = base_val + amt * mod_val * range;
    
    if (result < lo) result = lo;
    if (result > hi) result = hi;
    return result;
}

float GranularEngine::get_mod_val(int mod_src_id) {
    switch(mod_src_id) {
        case 1: return (float)(rand() % 1000) / 1000.0f; // Jitter
        case 2: return get_lfo_val(0) * 0.5f + 0.5f;     // LFO1
        case 3: return get_lfo_val(1) * 0.5f + 0.5f;     // LFO2
        case 4: { // EG
            if (master_env >= 0.999f || is_triggered) {
                return get_curved_env(master_env, p.atk_curve);
            } else {
                float t_norm = 1.0f - master_env;
                return 1.0f - get_curved_env(t_norm, -p.rel_curve);
            }
        }
        case 5: return modwheel_val;                     // Modwheel
        case 6: return aftertouch_val;                   // Aftertouch
        default: return 0;

    }
}

bool GranularEngine::is_lfo_used(int lfo_idx) {
    uint8_t target_src = (lfo_idx == 0) ? 2 : 3;
    for (int i = 0; i < 64; i++) {
        if (mod_src[i] == target_src) return true;
    }
    return false;
}

void GranularEngine::process(int frames, float* out_l, float* out_r, const float* fm_signal, const float* rm_signal, float rm_amt) {
    // 1. Optimization: Voice skipping
    if (!is_triggered && master_env <= 0.001f) {
        for (int g = 0; g < MAX_GRAINS_PER_VOICE; g++) {
            active_grains[g].active = false;
        }
        memset(out_l, 0, frames * sizeof(float));
        memset(out_r, 0, frames * sizeof(float));
        memset(last_buffer, 0, frames * sizeof(float));
        return;
    }

    // Keep track of the last active MIDI note
    if (current_note >= 0) {
        last_note = current_note;
    }

    float dt = (float)frames / SAMPLE_RATE;
    
    // 2. Optimization: LFO phase update
    if (is_lfo_used(0)) lfo_phases[0] = fmodf(lfo_phases[0] + p.lfo1_rate * dt, 1.0f);
    if (is_lfo_used(1)) lfo_phases[1] = fmodf(lfo_phases[1] + p.lfo2_rate * dt, 1.0f);

    float target = is_triggered ? 1.0f : 0.0f;
    if (target > master_env) {
        master_env += dt / (p.atk + 0.001f);
        if (master_env > target) master_env = target;
    } else {
        master_env -= dt / (p.rel + 0.001f);
        if (master_env < target) master_env = target;
    }

    float mod_cutoff = get_modulated_param(29, p.cutoff, 20.0f, 20000.0f);
    
    // Keytrack logic
    int note_for_track = (current_note >= 0) ? current_note : last_note;
    if (p.filt_key > 0.01f && note_for_track >= 0) {
        float freq_mult = 1.0f;
        if (note_for_track < 128) freq_mult = NOTE_FREQ_MULT_LUT[note_for_track];
        mod_cutoff *= (1.0f - p.filt_key) + (freq_mult * p.filt_key);
        if (mod_cutoff > 20000.0f) mod_cutoff = 20000.0f;
    }

    float mod_res_q  = get_modulated_param(30, p.res, 0.5f, 13.0f);
    float mod_res    = 1.0f - 0.5f / mod_res_q;
    if (mod_res < 0.0f) mod_res = 0.0f;
    if (mod_res > 0.99f) mod_res = 0.99f;
    float mod_vol    = get_modulated_param(41, p.vol, 0.0f, 1.0f);

    float curved_env = 0.0f;
    if (master_env > 0.001f) {
        if (master_env >= 0.999f || is_triggered) {
            curved_env = get_curved_env(master_env, p.atk_curve);
        } else {
            float t_norm = 1.0f - master_env;
            curved_env = 1.0f - get_curved_env(t_norm, -p.rel_curve);
        }
    }
    float gain = mod_vol * curved_env * p.master_vol;

    if (gain <= 0.001f) {
        if (is_triggered) {
            playback_pos = fmodf(playback_pos + p.scan * dt, 1.0f);
            if (playback_pos < 0.0f) playback_pos += 1.0f;
        }
        memset(out_l, 0, frames * sizeof(float));
        memset(out_r, 0, frames * sizeof(float));
        memset(last_buffer, 0, frames * sizeof(float));
        return;
    }

    if (is_triggered) {
        playback_pos = fmodf(playback_pos + p.scan * dt, 1.0f);
        if (playback_pos < 0.0f) playback_pos += 1.0f;
    }
    next_grain_time -= frames;
    if (next_grain_time <= 0 && num_active_grains() < p.max_grains) {
        spawn_grain();
        float dens = get_modulated_param(2, p.dens, 0.1f, 100.0f);
        next_grain_time = SAMPLE_RATE / dens;
    }

    for (int i = 0; i < frames; i++) {
        float l = 0, r = 0;
        
        float fm = 0;
        if (fm_signal) fm = fm_signal[i];

        for (int g = 0; g < MAX_GRAINS_PER_VOICE; g++) {
            if (active_grains[g].active) {
                active_grains[g].process(fm, &l, &r);
            }
        }
        
        // 3. RM Bypass Logic
        if (rm_signal && rm_amt > 0.001f) {
            float rm = rm_signal[i];
            float rm_mix = (1.0f - rm_amt) + (rm * rm_amt * 5.0f); // Multiply the signal by RM amount
            l = l * rm_mix;
            r = r * rm_mix;
        }

        if (p.filt_type < 3) {
            l = filter_l.process(l, mod_cutoff, mod_res, p.filt_type, SAMPLE_RATE);
            r = filter_r.process(r, mod_cutoff, mod_res, p.filt_type, SAMPLE_RATE);
        }

        // FX moved to global Synth::process

        out_l[i] = l * gain;
        out_r[i] = r * gain;
        last_buffer[i] = (out_l[i] + out_r[i]) * 0.5f;
    }
}

void GranularEngine::spawn_grain() {
    for (int i = 0; i < MAX_GRAINS_PER_VOICE; i++) {
        if (!active_grains[i].active) {
            float mod_pos   = get_modulated_param(0, p.pos, 0.0f, 1.0f);
            float mod_size  = get_modulated_param(1, p.size, 0.01f, 2.0f);
            // Apply JIT pitch modulation as integer semitone/octave steps depending on pitch mode
            float effective_pitch = p.pitch;
            if (p.pitch_mode == 1) { // SEMI (semitone steps)
                int semi_idx = (int)p.pitch + 24;
                // Clamp base index
                if (semi_idx < 0) semi_idx = 0;
                if (semi_idx > 48) semi_idx = 48;
                // Get JIT value (0..1)
                float jit_val = get_mod_val(1);
                // Convert to bipolar -1..1 and scale to integer steps
                int jit_steps = (int)roundf((jit_val * 2.0f - 1.0f) * 24.0f); // +/-24 semitones max
                int new_idx = semi_idx + jit_steps;
                if (new_idx < 0) new_idx = 0;
                if (new_idx > 48) new_idx = 48;
                effective_pitch = SEMI_PITCH_LUT[new_idx];
            } else if (p.pitch_mode == 2) { // OCT (octave steps)
                int oct_idx = (int)p.pitch + 3;
                if (oct_idx < 0) oct_idx = 0;
                if (oct_idx > 6) oct_idx = 6;
                float jit_val = get_mod_val(1);
                int jit_steps = (int)roundf((jit_val * 2.0f - 1.0f) * 3.0f); // +/-3 octaves max
                int new_idx = oct_idx + jit_steps;
                if (new_idx < 0) new_idx = 0;
                if (new_idx > 6) new_idx = 6;
                effective_pitch = OCT_PITCH_LUT[new_idx];
            } else {
                // SPEED mode – continuous pitch, apply normal modulation later
                effective_pitch = p.pitch;
            }
            // Apply other pitch modulation sources (e.g., EG, LFO) via get_modulated_param as before
            float mod_pitch = get_modulated_param(3, effective_pitch, 0.01f, 10.0f);
            
            // Apply pitch keytracking based on the MIDI note.
            // Reference note is C4 (note 60).
            int note_for_pitch = (current_note >= 0) ? current_note : last_note;
            if (p.keytrack > 0.001f && note_for_pitch >= 0) {
                float keytrack_mult = fast_exp2(p.keytrack * (note_for_pitch - 60.0f) / 12.0f);
                mod_pitch *= keytrack_mult;
            }

            // Apply Pitch Bend
            if (pitch_bend_semitones != 0.0f) {
                mod_pitch *= fast_exp2(pitch_bend_semitones / 12.0f);
            }

            
            float start_idx_ratio = fmodf(mod_pos + playback_pos, 1.0f);
            if (start_idx_ratio < 0.0f) start_idx_ratio += 1.0f;
            float start_idx = start_idx_ratio * raw_len(p.sample_idx);
            float length_frames = mod_size * SAMPLE_RATE;
            bool reverse = (float)(rand() % 100) < (p.direction * 100.0f);
            float rand_val = (float)rand() / (float)RAND_MAX;
            float grain_pan = 0.5f + (rand_val - 0.5f) * p.spread;
            
            active_grains[i].init(p.sample_idx, start_idx, length_frames, mod_pitch, grain_pan, reverse, (int)p.shape, p.grain_amp);
            break;
        }
    }
}

float GranularEngine::get_lfo_val(int lfo_idx) {
    float phase = lfo_phases[lfo_idx];
    float wave = (lfo_idx == 0) ? p.lfo1_wave : p.lfo2_wave;
    
    switch((int)wave) {
        case 0: return get_sine_lut(phase);
        case 1: return (phase < 0.5f) ? (phase * 4.0f - 1.0f) : (3.0f - phase * 4.0f); // Triangle
        case 2: return phase * 2.0f - 1.0f; // Saw
        case 3: return 1.0f - phase * 2.0f; // RSAW (Reverse Saw)
        case 4: return (float)(rand() % 2000 - 1000) / 1000.0f; // S&H (approx)
        default: return 0;
    }
}

int GranularEngine::num_active_grains() {
    int count = 0;
    for (int i = 0; i < MAX_GRAINS_PER_VOICE; i++) {
        if (active_grains[i].active) count++;
    }
    return count;
}

float GranularEngine::grain_pos_ratio(int grain_idx) {
    if (grain_idx >= MAX_GRAINS_PER_VOICE) return 0.0f;
    const Grain& g = active_grains[grain_idx];
    if (!g.active) return 0.0f;
    
    uint32_t data_len = raw_len(g.sample_id);
    if (data_len == 0) return 0.0f;
    
    return g.current_idx / (float)data_len;
}

float GranularEngine::grain_pitch(int grain_idx) {
    if (grain_idx >= MAX_GRAINS_PER_VOICE) return 1.0f;
    return active_grains[grain_idx].pitch;
}

float GranularEngine::grain_pan(int grain_idx) {
    if (grain_idx >= MAX_GRAINS_PER_VOICE) return 0.5f;
    return active_grains[grain_idx].pan;
}

