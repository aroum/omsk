#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../sw_config.h"
#include "../sequencer/sequencer.h"
#include "audio.h"
#include "pico/mutex.h"
#include "pico/util/queue.h"
#include "synth.h"
#include "../tables/vcf_table_manager.h"
#include "../tables/noise_lut_data.h"

typedef bool boolean;
uint8_t g_midi_ch = 0;
static mutex_t synth_mutex;
static bool hold_mode_active = false;
static bool hold_latch[128] = {false};
static uint8_t hold_latch_q[128] = {0};
static uint8_t chord_note_mode[128] = {0};

// Tracking active notes for voice stealing (specifically for chords)
static uint8_t chord_active_voice_order[4] = {0};
static int chord_active_voice_count = 0;

static void chord_get_intervals(uint8_t mode, int *count, int intervals[4]) {
  switch (mode) {
  case CHORD_MIN2:
    *count = 2;
    intervals[0] = 0;
    intervals[1] = 1;
    break;
  case CHORD_MAJ2:
    *count = 2;
    intervals[0] = 0;
    intervals[1] = 2;
    break;
  case CHORD_MIN3:
    *count = 2;
    intervals[0] = 0;
    intervals[1] = 3;
    break;
  case CHORD_MAJ3:
    *count = 2;
    intervals[0] = 0;
    intervals[1] = 4;
    break;
  case CHORD_P4:
    *count = 2;
    intervals[0] = 0;
    intervals[1] = 5;
    break;
  case CHORD_TRITONE:
    *count = 2;
    intervals[0] = 0;
    intervals[1] = 6;
    break;
  case CHORD_P5:
    *count = 2;
    intervals[0] = 0;
    intervals[1] = 7;
    break;
  case CHORD_MIN6:
    *count = 2;
    intervals[0] = 0;
    intervals[1] = 8;
    break;
  case CHORD_MAJ6:
    *count = 2;
    intervals[0] = 0;
    intervals[1] = 9;
    break;
  case CHORD_INT_MIN7:
    *count = 2;
    intervals[0] = 0;
    intervals[1] = 10;
    break;
  case CHORD_INT_MAJ7:
    *count = 2;
    intervals[0] = 0;
    intervals[1] = 11;
    break;
  case CHORD_OCT:
    *count = 2;
    intervals[0] = 0;
    intervals[1] = 12;
    break;
  case CHORD_MAJ:
    *count = 3;
    intervals[0] = 0;
    intervals[1] = 4;
    intervals[2] = 7;
    break;
  case CHORD_MIN:
    *count = 3;
    intervals[0] = 0;
    intervals[1] = 3;
    intervals[2] = 7;
    break;
  case CHORD_DIM:
    *count = 3;
    intervals[0] = 0;
    intervals[1] = 3;
    intervals[2] = 6;
    break;
  case CHORD_AUG:
    *count = 3;
    intervals[0] = 0;
    intervals[1] = 4;
    intervals[2] = 8;
    break;
  case CHORD_SUS2:
    *count = 3;
    intervals[0] = 0;
    intervals[1] = 2;
    intervals[2] = 7;
    break;
  case CHORD_SUS4:
    *count = 3;
    intervals[0] = 0;
    intervals[1] = 5;
    intervals[2] = 7;
    break;
  case CHORD_MAJ7:
    *count = 4;
    intervals[0] = 0;
    intervals[1] = 4;
    intervals[2] = 7;
    intervals[3] = 11;
    break;
  case CHORD_DOM7:
    *count = 4;
    intervals[0] = 0;
    intervals[1] = 4;
    intervals[2] = 7;
    intervals[3] = 10;
    break;
  case CHORD_MIN7:
    *count = 4;
    intervals[0] = 0;
    intervals[1] = 3;
    intervals[2] = 7;
    intervals[3] = 10;
    break;
  case CHORD_M7B5:
    *count = 4;
    intervals[0] = 0;
    intervals[1] = 3;
    intervals[2] = 6;
    intervals[3] = 10;
    break;
  case CHORD_DIM7:
    *count = 4;
    intervals[0] = 0;
    intervals[1] = 3;
    intervals[2] = 6;
    intervals[3] = 9;
    break;
  case CHORD_MIN_MAJ7:
    *count = 4;
    intervals[0] = 0;
    intervals[1] = 3;
    intervals[2] = 7;
    intervals[3] = 11;
    break;
  case CHORD_AUG_MAJ7:
    *count = 4;
    intervals[0] = 0;
    intervals[1] = 4;
    intervals[2] = 8;
    intervals[3] = 11;
    break;
  case CHORD_AUG7:
    *count = 4;
    intervals[0] = 0;
    intervals[1] = 4;
    intervals[2] = 8;
    intervals[3] = 10;
    break;
  case CHORD_ADD9:
    *count = 4;
    intervals[0] = 0;
    intervals[1] = 4;
    intervals[2] = 7;
    intervals[3] = 14;
    break;
  case CHORD_6:
    *count = 4;
    intervals[0] = 0;
    intervals[1] = 4;
    intervals[2] = 7;
    intervals[3] = 9;
    break;
  case CHORD_M6:
    *count = 4;
    intervals[0] = 0;
    intervals[1] = 3;
    intervals[2] = 7;
    intervals[3] = 9;
    break;
  case CHORD_69:
    *count = 4;
    intervals[0] = 0;
    intervals[1] = 4;
    intervals[2] = 9;
    intervals[3] = 14;
    break;
  case CHORD_7SUS4:
    *count = 4;
    intervals[0] = 0;
    intervals[1] = 5;
    intervals[2] = 7;
    intervals[3] = 10;
    break;
  case CHORD_MAJ7SUS4:
    *count = 4;
    intervals[0] = 0;
    intervals[1] = 5;
    intervals[2] = 7;
    intervals[3] = 11;
    break;
  default:
    *count = 1;
    intervals[0] = 0;
    break;
  }
}

typedef enum {
  CMD_NOTE_ON,
  CMD_NOTE_OFF,
  CMD_MIDI_NOTE_ON,
  CMD_MIDI_NOTE_OFF,
  CMD_PITCH_BEND,
  CMD_AFTERTOUCH,
  CMD_CC,
  CMD_PARAM_CHANGE,
  CMD_ALL_NOTES_OFF
} SynthCommandType;

typedef struct {
  SynthCommandType type;
  uint8_t channel;
  uint8_t d1; // note, control, param_id
  uint8_t d2; // velocity, value
} SynthCommand;

static queue_t synth_cmd_queue;

#ifndef __not_in_flash_func
#define __not_in_flash_func(x) x
#endif



#include "../gen/omsk_core.h"



static void pra_synth_process_commands(void);
static uint16_t pra_synth_get_param_value(ParamID pid);

static const int STEP_VOICES = 4;

volatile uint32_t pra_synth_sample_count = 0;

static float step_engine_modwheel = 0.0f;
static float step_engine_aftertouch = 0.0f;
static float step_engine_breath = 0.0f;

static void chord_note_on(uint8_t note, uint8_t velocity);
static void chord_note_off(uint8_t note);


// --- ADV Scale Quantizer ---
// Scale interval patterns (12-semitone chromatic mask, bit 0 = root)
// 1 means the note is IN the scale.
static const uint16_t adv_scale_masks[] = {
    0xFFF,  // OFF / Chromatic: all 12 notes
    0x5AB,  // Major:           W W H W W W H  (C D E F G A B)
    0x5AD,  // Minor (Natural): W H W W H W W  (C D Eb F G Ab Bb)
    0x4AD,  // Harmonic Minor:  W H W W H 3H H (C D Eb F G Ab B)
    0x6AD,  // Melodic Minor:   W H W W W W H  (C D Eb F G A B)
    0x56B,  // Dorian:          W H W W W H W  (C D Eb F G A Bb)
    0x2D7,  // Locrian:         H W W H W W W  (C Db Eb F Gb Ab Bb)
    0x5EB,  // Lydian:          W W W H W W H  (C D E F# G A B)
    0x52B,  // Blues:           W+H H H H W+H  (C Eb F F# G Bb)
    0x4A9,  // Major Pentatonic: W W 3H W 3H   (C D E G A)
    0x4A5,  // Minor Pentatonic: 3H W W 3H W   (C Eb F G Bb)
    0x492,  // Augmented:       3H H 3H H 3H H (C Eb E G Ab B)
};

// Quantize a MIDI note to the nearest note in scale
// root_key: 0=C, 1=C#, ..., 11=B
// Returns quantized note (same or moved up/down to closest scale note)
static uint8_t adv_quantize_note(uint8_t note, uint8_t scale, uint8_t root_key) {
    if (scale == 0 || scale >= ADV_SCALE_COUNT) return note; // OFF: pass through
    if (root_key >= 12) root_key = 0;

    uint16_t mask = adv_scale_masks[scale];
    if (!mask) return note;

    int octave = note / 12;
    int pitch_class = note % 12;
    // Relative pitch class (offset from root)
    int rel = (pitch_class - (int)root_key + 12) % 12;

    // If already in scale, return as-is
    if (mask & (1u << rel)) return note;

    // Find nearest scale note (search up and down)
    for (int d = 1; d <= 6; d++) {
        int up   = (rel + d) % 12;
        int down = (rel - d + 12) % 12;
        if (mask & (1u << up)) {
            int target = ((int)root_key + up + 12 * octave) % 128;
            if (target < 0) target = 0;
            if (target > 127) target = 127;
            return (uint8_t)target;
        }
        if (mask & (1u << down)) {
            int target = ((int)root_key + down + 12 * octave) % 128;
            if (target < 0) target = 0;
            if (target > 127) target = 127;
            return (uint8_t)target;
        }
    }
    return note; // fallback
}

// --- Noise Color biquad state (Pink -> White -> Blue), как в python_demo ---
static float noise_b0 = 1.0f, noise_b1 = 0.0f, noise_b2 = 0.0f;
static float noise_a1 = 0.0f, noise_a2 = 0.0f;
static float noise_z1 = 0.0f, noise_z2 = 0.0f;
static uint8_t noise_prev_color = 0xFF;
static uint8_t noise_mode =
    0; // 0 = white, 1 = LPF (pink/brown), 2 = HPF (blue)

static inline void noise_update_coeffs(uint8_t color, float sampleRate) {
  if (color == noise_prev_color)
    return;
  noise_prev_color = color;

  noise_mode = g_noise_mode_lut[color];
  if (noise_mode == 0) {
    return;
  }

  const float *coeffs = g_noise_filter_lut[color];
  noise_b0 = coeffs[0];
  noise_b1 = coeffs[1];
  noise_b2 = coeffs[2];
  noise_a1 = coeffs[3];
  noise_a2 = coeffs[4];
}

static inline float noise_process_sample(float x) {
  if (noise_mode == 0) {
    return x;
  }
  float v = x - noise_a1 * noise_z1 - noise_a2 * noise_z2;
  float y = noise_b0 * v + noise_b1 * noise_z1 + noise_b2 * noise_z2;
  noise_z2 = noise_z1;
  noise_z1 = v;
  return y;
}

static float mix_volume_lut[128];
static bool mix_volume_lut_init = false;

static inline float mix_volume_gain(uint8_t v) {
  if (!mix_volume_lut_init) {
    for (int i = 0; i < 128; i++) {
      mix_volume_lut[i] = sqrtf((float)i / 127.0f);
    }
    mix_volume_lut_init = true;
  }
  return mix_volume_lut[v];
}

static inline float step_engine_tri(float p) {
  float x = p - floorf(p);
  float v = 4.0f * fabsf(x - 0.5f) - 1.0f;
  return v;
}

static inline float step_engine_saw(float p) {
  float x = p - floorf(p);
  return 2.0f * x - 1.0f;
}

static inline float step_engine_square_pw(float p, float pw) {
  float x = p - floorf(p);
  return (x < pw) ? 1.0f : -1.0f;
}

#include "omsk_pam4_table.h"







static inline float step_engine_clampf(float v, float lo, float hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}



static inline float step_engine_mod_amount(uint8_t depth, float src) {
  if (depth == 64)
    return 0.0f;
  float amt = ((float)depth - 64.0f) / 64.0f;
  return amt * src * 127.0f;
  // If I have +100% depth, and src is +1.0, I want to add +127 (full sweep)?
  // Or just reasonable amount?
  // Let's say full sweep.
  // If amt=1.0, src=1.0 -> +127.0.
  // So 1.0 * 1.0 * 127.0.
}

static inline float step_engine_apply_mod_full(ParamID param, float base, float lfo1,
                                          float lfo2, float eg1, float eg2,
                                          float mw, float at, float br) {
  float v = base;
  if (params.mod_matrix[param][SRC_LFO1] != 64) v += step_engine_mod_amount(params.mod_matrix[param][SRC_LFO1], lfo1);
  if (params.mod_matrix[param][SRC_LFO2] != 64) v += step_engine_mod_amount(params.mod_matrix[param][SRC_LFO2], lfo2);
  if (params.mod_matrix[param][SRC_EG1] != 64) v += step_engine_mod_amount(params.mod_matrix[param][SRC_EG1], eg1);
  if (params.mod_matrix[param][SRC_EG2] != 64) v += step_engine_mod_amount(params.mod_matrix[param][SRC_EG2], eg2);
  if (params.mod_matrix[param][SRC_MODWHEEL] != 64) v += step_engine_mod_amount(params.mod_matrix[param][SRC_MODWHEEL], mw);
  if (params.mod_matrix[param][SRC_AFTERTOUCH] != 64) v += step_engine_mod_amount(params.mod_matrix[param][SRC_AFTERTOUCH], at);
  if (params.mod_matrix[param][SRC_BREATH] != 64) v += step_engine_mod_amount(params.mod_matrix[param][SRC_BREATH], br);
  return step_engine_clampf(v, 0.0f, 127.0f);
}

static inline float step_engine_apply_mod(ParamID param, float base, float lfo1,
                                          float lfo2, float eg1, float eg2) {
  return step_engine_apply_mod_full(param, base, lfo1, lfo2, eg1, eg2,
                                    step_engine_modwheel,
                                    step_engine_aftertouch, step_engine_breath);
}

extern "C" uint16_t pra_synth_get_modulated_param_value(ParamID pid) {
  mutex_enter_blocking(&synth_mutex);
  uint16_t val = (uint16_t)step_engine_apply_mod_full(
      pid, (float)pra_synth_get_param_value(pid), 0, 0, 0, 0, 
      step_engine_modwheel, step_engine_aftertouch, step_engine_breath);
  mutex_exit(&synth_mutex);
  return val;
}

static uint16_t pra_synth_get_param_value(ParamID pid) {
  switch (pid) {
  case PARAM_VCO1_TRANSPOSE:
    return params.vco1_transpose;
  case PARAM_VCO1_DETUNE:
    return params.vco1_detune;
  case PARAM_VCO1_WAVE:
    return params.vco1_wave;
  case PARAM_VCO1_SHAPE:
    return params.vco1_shape;
  case PARAM_VCO2_TRANSPOSE:
    return params.vco2_transpose;
  case PARAM_VCO2_DETUNE:
    return params.vco2_detune;
  case PARAM_VCO2_WAVE:
    return params.vco2_wave;
  case PARAM_VCO2_SHAPE:
    return params.vco2_shape;
  case PARAM_VCF1_CUTOFF:
    return params.vcf1_cutoff;
  case PARAM_VCF1_RES:
    return params.vcf1_res;
  case PARAM_VCF1_DRIVE:
    return params.vcf1_drive;
  case PARAM_VCF1_MIX:
    return params.vcf1_mix;
  case PARAM_VCF2_CUTOFF:
    return params.vcf2_cutoff;
  case PARAM_VCF2_RES:
    return params.vcf2_res;
  case PARAM_VCF2_DRIVE:
    return params.vcf2_drive;
  case PARAM_VCF2_MIX:
    return params.vcf2_mix;
  case PARAM_VCF_KEY_TRACK:
    return params.vcf_key_track;
  case PARAM_LFO1_RATE:
    return params.lfo1_rate;
  case PARAM_LFO1_SMOOTH:
    return params.lfo1_smooth;
  case PARAM_LFO1_WAVE:
    return params.lfo1_wave;
  case PARAM_LFO1_SHAPE:
    return params.lfo1_shape;
  case PARAM_LFO2_RATE:
    return params.lfo2_rate;
  case PARAM_LFO2_SMOOTH:
    return params.lfo2_smooth;
  case PARAM_LFO2_WAVE:
    return params.lfo2_wave;
  case PARAM_LFO2_SHAPE:
    return params.lfo2_shape;
  case PARAM_EG1_ATTACK:
    return params.eg1_attack;
  case PARAM_EG1_DECAY:
    return params.eg1_decay;
  case PARAM_EG1_SUSTAIN:
    return params.eg1_sustain;
  case PARAM_EG1_RELEASE:
    return params.eg1_release;
  case PARAM_EG2_ATTACK:
    return params.eg2_attack;
  case PARAM_EG2_DECAY:
    return params.eg2_decay;
  case PARAM_EG2_SUSTAIN:
    return params.eg2_sustain;
  case PARAM_EG2_RELEASE:
    return params.eg2_release;
  case PARAM_MIX_VCO1_VOL:
    return params.mix_vco_balance;
  case PARAM_MIX_VCO2_VOL:
    return params.mix_osc_noise;
  case PARAM_MIX_PHASE2:
    return params.mix_phase2;
  case PARAM_MIX_NOISE_VOL:
    return params.mix_master;
  case PARAM_NOISE_COLOR:
    return params.noise_color;
  case PARAM_GLIDE_POLY:
    return params.glide_poly;
  case PARAM_GLIDE_TIME:
    return params.glide_time;
  case PARAM_GLIDE_SLOPE:
    return params.glide_slope;
  case PARAM_GLIDE_MODE:
    return params.glide_mode;
  case PARAM_FX1_TIME:
    return params.fx1_time;
  case PARAM_FX1_FEEDBACK:
    return params.fx1_feedback;
  case PARAM_FX1_SPREAD:
    return params.fx1_spread;
  case PARAM_FX1_MIX:
    return params.fx1_mix;
  case PARAM_FX2_TIME:
    return params.fx2_time;
  case PARAM_FX2_FEEDBACK:
    return params.fx2_feedback;
  case PARAM_MOD_ROUTING1:
    return params.mod_routing1;
  case PARAM_MOD_DEPTH1:
    return params.mod_depth1;
  case PARAM_MOD_ROUTING2:
    return params.mod_routing2;
  case PARAM_MOD_DEPTH2:
    return params.mod_depth2;
  case PARAM_FX2_TONE:
    return params.fx2_tone;
  case PARAM_FX2_MIX:
    return params.fx2_mix;
  case PARAM_ARP_RATE:
    return params.arp_rate;
  case PARAM_ADV_MIDI_CH:
    return params.midi_channel;
  case PARAM_EG1_ATTACK_CURVE:
    return params.eg1_attack_curve;
  case PARAM_EG1_DECAY_CURVE:
    return params.eg1_decay_curve;
  case PARAM_EG1_RELEASE_CURVE:
    return params.eg1_release_curve;
  case PARAM_EG2_ATTACK_CURVE:
    return params.eg2_attack_curve;
  case PARAM_EG2_DECAY_CURVE:
    return params.eg2_decay_curve;
  case PARAM_EG2_RELEASE_CURVE:
    return params.eg2_release_curve;
  case PARAM_ARP_MODE:
    return params.arp_mode;
  case PARAM_ARP_SWING:
    return params.arp_swing;
  case PARAM_ARP_OCT:
    return params.arp_oct;
  case PARAM_CHORD_MODE:
    return params.chord_mode;
  case PARAM_ADV_TEMPO:
    return params.adv_tempo;
  case PARAM_ADV_SCALE:
    return params.adv_scale;
  case PARAM_PITCH_BEND_RANGE:
    return params.pitch_bend_range;
  case PARAM_AMP_GAIN:
    return params.amp_gain;
  default:
    return 0;
  }
}

static void pra_synth_update_omsk_modulated_params() {
  for (int p = 0; p < PARAM_COUNT; p++) {
    ParamID pid = (ParamID)p;
    // Check if modulated by "slow" sources (ModWheel, AT, Breath)
    if (params.mod_matrix[pid][SRC_MODWHEEL] != 64 ||
        params.mod_matrix[pid][SRC_AFTERTOUCH] != 64 ||
        params.mod_matrix[pid][SRC_BREATH] != 64) {

      float base = (float)pra_synth_get_param_value(pid);
      float modulated = step_engine_apply_mod(
          pid, base, 0, 0, 0, 0); // LFO/EG are 0, ModWheel used internally in step_engine_apply_mod()
      omsk_core_set_param((uint8_t)pid, (uint8_t)modulated);
    }
  }
}


static void __not_in_flash_func(pra_synth_process_commands)(void) {
  SynthCommand cmd;
  while (queue_try_remove(&synth_cmd_queue, &cmd)) {
    // Route all commands through omsk_core_* API
    switch (cmd.type) {
    case CMD_NOTE_ON:
    case CMD_MIDI_NOTE_ON: {
      if (cmd.type == CMD_MIDI_NOTE_ON && cmd.channel != g_midi_ch)
        break;
      uint8_t input_note = cmd.d1;
      uint8_t vel = (cmd.type == CMD_NOTE_ON) ? 100 : cmd.d2;
      uint8_t q_note = adv_quantize_note(input_note, params.adv_scale,
                                        params.adv_scale_key);

      if (hold_mode_active && params.arp_mode == 0) {
        if (hold_latch[input_note]) {
          chord_note_off(hold_latch_q[input_note]);
          hold_latch[input_note] = false;
        } else {
          chord_note_on(q_note, vel);
          hold_latch[input_note] = true;
          hold_latch_q[input_note] = q_note;
        }
      } else {
        chord_note_on(q_note, vel);
      }
      break;
    }
    case CMD_NOTE_OFF:
    case CMD_MIDI_NOTE_OFF:
      if (cmd.type == CMD_MIDI_NOTE_OFF && cmd.channel != g_midi_ch)
        break;
      if (!hold_mode_active || params.arp_mode > 0) {
        chord_note_off(adv_quantize_note(cmd.d1, params.adv_scale,
                                        params.adv_scale_key));
      }
      break;
    case CMD_PITCH_BEND:
      if (cmd.channel == g_midi_ch)
        omsk_core_pitch_bend(cmd.d1, cmd.d2);
      break;
    case CMD_AFTERTOUCH:
      if (cmd.channel == g_midi_ch)
        step_engine_aftertouch = (float)(cmd.d1 & 0x7F) / 127.0f;
      break;
    case CMD_CC:
      if (cmd.channel == g_midi_ch) {
        if (cmd.d1 == 1)
          step_engine_modwheel = (float)(cmd.d2 & 0x7F) / 127.0f;
        if (cmd.d1 == 2)
          step_engine_breath = (float)(cmd.d2 & 0x7F) / 127.0f;
        if (cmd.d1 == 64)
          omsk_core_set_sustain(cmd.d2 >= 64);
      }
      break;
    case CMD_ALL_NOTES_OFF:
      omsk_core_all_notes_off();
      break;
    default:
      break;
    }
  }

  static float last_mw = -1.0f;
  static float last_at = -1.0f;
  static float last_br = -1.0f;
  if (step_engine_modwheel != last_mw || step_engine_aftertouch != last_at ||
      step_engine_breath != last_br) {
    last_mw = step_engine_modwheel;
    last_at = step_engine_aftertouch;
    last_br = step_engine_breath;
    omsk_core_set_modwheel((uint8_t)(step_engine_modwheel * 127.0f));
    omsk_core_set_aftertouch((uint8_t)(step_engine_aftertouch * 127.0f));
    omsk_core_set_breath((uint8_t)(step_engine_breath * 127.0f));
    pra_synth_update_omsk_modulated_params();
  }
}

extern "C" void pra_synth_init(void) {
  // VCF LUTs are now static in ROM/Flash
  mutex_init(&synth_mutex);
  // Always init the command queue — used by all engine variants.
  // 128 slots to absorb DAW bursts (clock + notes + CCs) without blocking Core 0.
  queue_init(&synth_cmd_queue, sizeof(SynthCommand), 128);
  omsk_core_init();
}

extern "C" int16_t pra_synth_get_sample(void) {
  mutex_enter_blocking(&synth_mutex);
  pra_synth_process_commands();
  float l = 0.0f;
  float r = 0.0f;
  omsk_core_process(&l, &r);
  mutex_exit(&synth_mutex);

  float s = l;
  int32_t v = (int32_t)(s * 32767.0f);
  if (v > 32767)
    v = 32767;
  if (v < -32768)
    v = -32768;
  return (int16_t)v;
}

extern "C" void pra_synth_get_stereo(int16_t *left, int16_t *right) {
  static uint32_t sample_counter = 0;
  
  mutex_enter_blocking(&synth_mutex);
  
  // Process commands at a lower rate to reduce mutex lock time and CPU load
  if ((sample_counter++ & 15) == 0) {
    pra_synth_process_commands();
  }
  
  float l = 0.0f;
  float r = 0.0f;
  omsk_core_process(&l, &r);
  mutex_exit(&synth_mutex);

  // Convert float -1..1 to int16
  int32_t il = (int32_t)(l * 32767.0f);
  int32_t ir = (int32_t)(r * 32767.0f);
  if (il > 32767) il = 32767;
  else if (il < -32768) il = -32768;
  if (ir > 32767) ir = 32767;
  else if (ir < -32768) ir = -32768;
  if (left) *left = (int16_t)il;
  if (right) *right = (int16_t)ir;
}



static void engine_note_on(uint8_t note, uint8_t velocity) {
  omsk_core_note_on(note, velocity);
}

static void engine_note_off(uint8_t note) {
  omsk_core_note_off(note);
}



static void chord_track_note_on(uint8_t note) {
  // If note already tracked, don't count twice (though should not happen)
  for (int i = 0; i < chord_active_voice_count; i++) {
    if (chord_active_voice_order[i] == note) return;
  }

  // If pool full, steal oldest
  if (chord_active_voice_count >= 4) {
    uint8_t oldest = chord_active_voice_order[0];
    engine_note_off(oldest);
    // Move others down
    for (int j = 0; j < 3; j++) chord_active_voice_order[j] = chord_active_voice_order[j+1];
    chord_active_voice_count--;
  }
  
  // Add new
  chord_active_voice_order[chord_active_voice_count++] = note;
}

static void chord_track_note_off(uint8_t note) {
  for (int i = 0; i < chord_active_voice_count; i++) {
    if (chord_active_voice_order[i] == note) {
      // Remove it and shift
      for (int j = i; j < chord_active_voice_count - 1; j++) {
        chord_active_voice_order[j] = chord_active_voice_order[j+1];
      }
      chord_active_voice_count--;
      return;
    }
  }
}

static void chord_note_on(uint8_t note, uint8_t velocity) {
  if (params.chord_mode == CHORD_OFF) {
    chord_note_mode[note] = CHORD_OFF;
    chord_track_note_on(note);
    engine_note_on(note, velocity);
    return;
  }
  uint8_t mode = params.chord_mode;
  chord_note_mode[note] = mode;
  int intervals[4];
  int count = 0;
  chord_get_intervals(mode, &count, intervals);
  for (int i = 0; i < count; i++) {
    int n = (int)note + intervals[i];
    if (n >= 0 && n < 128) {
      chord_track_note_on((uint8_t)n);
      engine_note_on((uint8_t)n, velocity);
    }
  }
}

static void chord_note_off(uint8_t note) {
  uint8_t mode = chord_note_mode[note];
  if (mode == CHORD_OFF) {
    chord_track_note_off(note);
    engine_note_off(note);
    return;
  }
  int intervals[4];
  int count = 0;
  chord_get_intervals(mode, &count, intervals);
  for (int i = 0; i < count; i++) {
    int n = (int)note + intervals[i];
    if (n >= 0 && n < 128) {
      chord_track_note_off((uint8_t)n);
      engine_note_off((uint8_t)n);
    }
  }
  chord_note_mode[note] = CHORD_OFF;
}

// Helper: try to enqueue a command, spinning at most ~20µs for critical events.
// Using queue_add_blocking would freeze Core 0 if the queue fills (e.g. DAW CC flood).
static inline void synth_queue_push_critical(const SynthCommand *cmd) {
  // Spin up to ~20µs for note on/off — losing a note is worse than a short delay.
  uint32_t deadline = time_us_32() + 20;
  while (!queue_try_add(&synth_cmd_queue, cmd)) {
    if ((int32_t)(time_us_32() - deadline) >= 0) break; // give up after 20µs
  }
}

static inline void synth_queue_push_param(const SynthCommand *cmd) {
  // Non-blocking for parameter events: drop if full (next value will overwrite anyway).
  queue_try_add(&synth_cmd_queue, cmd);
}

extern "C" void pra_synth_note_on(uint8_t note) {
  SynthCommand cmd = {.type = CMD_NOTE_ON, .channel = 0, .d1 = note, .d2 = 100};
  synth_queue_push_critical(&cmd);
}

extern "C" void pra_synth_note_off(uint8_t note) {
  SynthCommand cmd = {.type = CMD_NOTE_OFF, .channel = 0, .d1 = note, .d2 = 0};
  synth_queue_push_critical(&cmd);
}

extern "C" void pra_synth_midi_note_on(uint8_t channel, uint8_t note,
                                       uint8_t velocity) {
  SynthCommand cmd = {
      .type = CMD_MIDI_NOTE_ON, .channel = channel, .d1 = note, .d2 = velocity};
  synth_queue_push_critical(&cmd);
}

extern "C" void pra_synth_midi_note_off(uint8_t channel, uint8_t note,
                                        uint8_t velocity) {
  (void)velocity;
  SynthCommand cmd = {
      .type = CMD_MIDI_NOTE_OFF, .channel = channel, .d1 = note, .d2 = 0};
  synth_queue_push_critical(&cmd);
}

extern "C" void pra_synth_midi_pitch_bend(uint8_t channel, uint8_t lsb,
                                          uint8_t msb) {
  SynthCommand cmd = {
      .type = CMD_PITCH_BEND, .channel = channel, .d1 = lsb, .d2 = msb};
  synth_queue_push_param(&cmd);
}

extern "C" void pra_synth_midi_aftertouch(uint8_t channel, uint8_t pressure) {
  SynthCommand cmd = {
      .type = CMD_AFTERTOUCH, .channel = channel, .d1 = pressure, .d2 = 0};
  synth_queue_push_param(&cmd);
}

extern "C" void pra_synth_midi_control_change(uint8_t channel,
                                              uint8_t control_number,
                                              uint8_t value) {
  if (channel == g_midi_ch) {
    if (control_number == 64) {
      omsk_core_set_sustain(value >= 64);
    }
  }
  SynthCommand cmd = {
      .type = CMD_CC, .channel = channel, .d1 = control_number, .d2 = value};
  synth_queue_push_param(&cmd);
}

extern "C" void pra_synth_update_control(void) {
  mutex_enter_blocking(&synth_mutex);
  pra_synth_process_commands();
  omsk_core_update_control();
  mutex_exit(&synth_mutex);
}

extern "C" void pra_synth_all_notes_off(void) {
  SynthCommand cmd = {
      .type = CMD_ALL_NOTES_OFF, .channel = 0, .d1 = 0, .d2 = 0};
  // All Notes Off is critical — spin-wait to ensure it's delivered.
  synth_queue_push_critical(&cmd);
}




// End of file cleanup

extern "C" void pra_synth_set_hold_mode(bool active) {
  if (active == hold_mode_active) return;
  
  // We handle deactivation within pra_synth_process_commands context to avoid race conditions
  // OR we can just add a command for it. Since we want immediate effect on screen, 
  // we set the flag here, but clearing latched notes requires sending note offs.
  
  hold_mode_active = active;
  
  if (!active) {
    // If turning OFF, we must release all latched notes.
    mutex_enter_blocking(&synth_mutex);
    for (int i = 0; i < 128; i++) {
      if (hold_latch[i]) {
        omsk_core_note_off(hold_latch_q[i]);
        hold_latch[i] = false;
      }
    }
    mutex_exit(&synth_mutex);
  }
}

extern "C" bool pra_synth_is_hold_mode(void) {
  return hold_mode_active;
}




extern "C" void pra_synth_set_midi_channel(uint8_t ch) {
  g_midi_ch = ch & 0x0F;
}

extern "C" void pra_synth_param_change(ParamID param, uint16_t value) {
  if (param == PARAM_ADV_MIDI_CH) {
      pra_synth_set_midi_channel(value);
  }

  mutex_enter_blocking(&synth_mutex);
  omsk_core_set_param((uint8_t)param, value);
  mutex_exit(&synth_mutex);
}
