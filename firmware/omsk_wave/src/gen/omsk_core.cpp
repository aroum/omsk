#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../sw_config.h"

#include "../filt/pra32-u2-filter.h"
#include "../fx/pra32-u2-chorus-fx.h"
#include "../fx/pra32-u2-delay-fx.h"
#include "../synth/pra32-u2-common.h"
#include "../synth/synth.h"
#include "../synth/synth_defs.h"
#include "../tables/noise_lut_data.h"
#include "../tables/omsk_lut_data.h"
#include "../tables/omsk_wavetables.h"
#include "../tables/omsk_glide_slope_table.h"
#include "../tables/vcf_table_manager.h"
#include "../tables/vco_lut_data.h"
#include "omsk_core.h"
#include "pico/float.h"
#include "pico/stdlib.h"
#include "../../../shared/hardware/midi_helpers.h"

#ifndef __not_in_flash_func
#define __not_in_flash_func(x) x
#endif

// CMSIS-DSP
#include "arm_math.h"

extern "C" uint8_t seq_get_bpm(void);

#define SAMPLE_RATE 48000.0f
#define INV_SAMPLE_RATE (1.0f / SAMPLE_RATE)
#define MAX_VOICES 4
#define F_PI 3.14159265358979323846f

typedef enum {
  W_SIN = 0,
  W_TRI = 1,
  W_SAW = 2,
  W_RSAW = 3,
  W_SQR = 4,
  W_PULSE = 5,
  W_PAM = 6
} WaveType;

typedef enum {
  OMSK_EG_IDLE = 0,
  OMSK_EG_ATTACK,
  OMSK_EG_DECAY,
  OMSK_EG_SUSTAIN,
  OMSK_EG_RELEASE
} EGState;

typedef struct {
  EGState state;
  float level;
  float attack_inc;
  float decay_inc;
  float release_inc;
  float sustain_level;
  uint8_t velocity;
} OmskEG;

// Oscillator Structure
typedef struct {
  uint32_t phase;        // 32-bit Phase Accumulator
  uint32_t phase_inc;    // Phase Increment
  float freq;            // Current frequency
  uint8_t wave;          // Waveform Type (0-127)
  uint8_t shape;         // Shape Parameter (0-127)
  int8_t transpose;      // Transpose (-60 to +60 semitones)
  int8_t detune;         // Detune (-128 to +127 -> mapped via table)
  float detune_factor;   // Cached detune factor
  float pitch_mult_base; // Base combined scale (transpose + detune), NO pitch
                         // bend
  float current_pitch_mult; // Interpolated base multiplier
  float output;             // Last output sample

  // Strategy caching for generate_wave
  uint8_t strategy; // 0: Morph, 1: Pulse, 2: PAM, 3: Hybrid
  WaveType type_a, type_b;
  float morph_t;
  float pulse_pw;
  uint32_t pulse_offset;
  float pulse_const;
  uint8_t pam_idx;
  int16_t baked_wave[2][256];
  uint8_t active_bank;
  uint16_t bake_progress; // 256 = done. 0-255 = currently baking
  uint8_t bake_w;
  uint8_t bake_s;
  int bake_lvl;
  float bake_freq;
} OmskOsc;

// LFO Structure
typedef struct {
  uint32_t phase;
  uint32_t phase_inc;
  float rate; // Hz
  uint8_t wave;
  uint8_t shape;
  uint8_t smooth; // Smoothing amount
  float output;
  float target;  // Target for smoothing
  float current; // Current smoothed value
  float smooth_alpha;
} OmskLFO;

// Voice Structure
typedef struct {
  OmskOsc vco1;
  OmskOsc vco2;
  OmskEG eg1;
  OmskEG eg2;
  uint8_t note;
  uint8_t velocity;
  bool active;
  bool gate;
  bool sustain_hold;
  float amp_env;         // Legacy
  uint32_t age;          // Timestamp or counter for voice stealing
  float steal_fade;      // For micro-fading when voice is stolen
  float last_mix_output; // Last generated sample for de-clicking
  uint8_t target_note;
  uint8_t target_velocity;
  bool is_stealing;

  // Glide & Unison state
  float freq_start;
  float freq_target;
  float glide_elapsed;
  float unison_detune;
  float unison_detune_factor;

  // Cached values for control rate optimization
  float cached_g1, cached_g2, cached_g3;
  float cached_f1, cached_f2;
  uint8_t cached_s1, cached_s2;
  float cached_vcf1_mix, cached_vcf2_mix;
  float cached_vcf1_drive, cached_vcf2_drive;
  int cached_level1, cached_level2;
  float cached_s1_out, cached_s2_out;
  float cached_velocity_gain;

  PRA32_U2_Filter vcf1;
  PRA32_U2_Filter vcf2;
} OmskVoice;

// Global Engine State
typedef struct {
  OmskVoice voices[MAX_VOICES];
  OmskLFO lfo1;
  OmskLFO lfo2;

  // Global params
  uint8_t vco1_wave;
  uint8_t vco1_shape;
  int8_t vco1_transpose;
  uint8_t vco1_transpose_raw;
  int8_t vco1_detune;

  uint8_t vco2_wave;
  uint8_t vco2_shape;
  int8_t vco2_transpose;
  uint8_t vco2_transpose_raw;
  int8_t vco2_detune;

  uint8_t lfo1_rate;
  uint8_t lfo1_wave;
  uint8_t lfo1_shape;
  uint8_t lfo1_smooth;

  uint8_t lfo2_rate;
  uint8_t lfo2_wave;
  uint8_t lfo2_shape;
  uint8_t lfo2_smooth;

  uint8_t mix_vco1;
  uint8_t mix_vco2;
  uint8_t mix_noise;
  uint8_t mix_phase2;
  uint8_t noise_color;

  uint8_t route_vco1;
  uint8_t route_vco2;
  uint8_t route_noise;

  uint8_t vcf1_cutoff;
  uint8_t vcf1_res;
  uint8_t vcf1_drive;
  uint8_t vcf1_mix;
  uint8_t vcf2_cutoff;
  uint8_t vcf2_res;
  uint8_t vcf2_drive;
  uint8_t vcf2_mix;
  uint8_t vcf_key_track;

  // EGs
  uint8_t eg1_attack;
  uint8_t eg1_decay;
  uint8_t eg1_sustain;
  uint8_t eg1_release;
  uint8_t eg2_attack;
  uint8_t eg2_decay;
  uint8_t eg2_sustain;
  uint8_t eg2_release;

  // FX
  uint8_t fx1_time;
  uint8_t fx1_feedback;
  uint8_t fx1_spread;
  uint8_t fx1_mix;
  uint8_t fx2_time;
  uint8_t fx2_feedback;
  uint8_t fx2_tone;
  uint8_t fx2_mix;

  PRA32_U2_ChorusFx chorus;
  PRA32_U2_DelayFx delay;

  float target_pitch_bend;
  float current_pitch_bend;
  uint8_t pitch_bend_range;
  bool sustain_pedal;
  uint8_t pan; // 0=full left, 64=center, 127=full right
  float pan_l; // Pre-calculated
  float pan_r; // Pre-calculated

  uint32_t sample_counter;
  float gain_vco1;
  float gain_vco2;
  float gain_noise;
  uint32_t global_age;
  float current_gain;
  float target_gain;

  // Global Modulation Sources
  float modwheel;
  float aftertouch;
  float breath;
  float master_gain_base;
  float master_gain;
  uint8_t chord_mode;
  uint8_t mod_active_mask[PARAM_COUNT];
  float mod_depth_float[PARAM_COUNT][SRC_COUNT];
  uint8_t mod_routing1;
  uint8_t mod_routing2;
  float mod_depth1_f;
  float mod_depth2_f;

  // Glide & Unison params
  uint8_t glide_poly;
  uint8_t glide_time;
  uint8_t glide_slope;
  uint8_t glide_mode;
  uint8_t active_voices_count;
  uint32_t mix_phase_offset;
} OmskEngine;

static OmskEngine engine;

static inline float midi_to_freq(uint8_t note) {
  if (note > 127)
    note = 127;
  return midi_to_freq_lut[note];
}

static inline float get_detune_factor(int8_t detune_idx) {
  int idx = detune_idx;
  if (idx < 0)
    idx = 0;
  if (idx > 127)
    idx = 127;
  return detune_factor_lut[idx];
}

// 1. Soft-Clip LUT for main output (1025 float entry to allow safe linear
// interpolation)
static float g_soft_clip_lut[1025];

static void init_soft_clip_lut(void) {
  for (int i = 0; i < 1024; i++) {
    float x = ((float)i / 1023.0f) * 1.5f; // Map 0..1 to 0..1.5
    if (x > 1.25f)
      g_soft_clip_lut[i] = 0.99f;
    else
      g_soft_clip_lut[i] = x * (1.0f - (x * x) * 0.15f); // Simplified cubic
  }
  g_soft_clip_lut[1024] =
      g_soft_clip_lut[1023]; // Safety guard for interpolation
}

// Optimized fast soft-clip using LUT with linear interpolation
static inline float omsk_soft_clip_fast(float x) {
  float abs_x = (x < 0) ? -x : x;
  if (abs_x >= 1.5f)
    return (x > 0) ? 0.99f : -0.99f;

  float x_scaled = abs_x * (1023.0f / 1.5f);
  int idx = (int)x_scaled;
  float frac = x_scaled - (float)idx;
  float val =
      g_soft_clip_lut[idx] * (1.0f - frac) + g_soft_clip_lut[idx + 1] * frac;
  return (x > 0) ? val : -val;
}

static float transpose_factor_lut[121]; // values from -60 to 60

static inline float omsk_eg_calc_inc(uint8_t param) {
  return eg_inc_lut[param & 127];
}

static inline void update_vco_pitch(OmskOsc *vco);
static inline void update_voice_frequencies(int i, float target_f,
                                            bool immediate);

static inline void omsk_eg_process(OmskEG *eg) {
  switch (eg->state) {
  case OMSK_EG_ATTACK:
    eg->level += eg->attack_inc;
    if (eg->level >= 1.0f) {
      eg->level = 1.0f;
      eg->state = OMSK_EG_DECAY;
    }
    break;
  case OMSK_EG_DECAY:
    if (eg->level > eg->sustain_level) {
      eg->level -= eg->decay_inc;
      if (eg->level <= eg->sustain_level) {
        eg->level = eg->sustain_level;
        eg->state = OMSK_EG_SUSTAIN;
      }
    } else {
      eg->state = OMSK_EG_SUSTAIN;
    }
    break;
  case OMSK_EG_SUSTAIN:
    break;
  case OMSK_EG_RELEASE:
    eg->level -= eg->release_inc;
    if (eg->level <= 0.0f) {
      eg->level = 0.0f;
      eg->state = OMSK_EG_IDLE;
    }
    break;
  case OMSK_EG_IDLE:
  default:
    break;
  }
}

static inline float omsk_get_mod_amount(uint8_t depth, float src) {
  if (depth == 64)
    return 0.0f;
  float amt = ((float)depth - 64.0f) / 64.0f;
  return amt * src * 127.0f;
}

static inline float __not_in_flash_func(omsk_apply_mod_fast)(
    uint8_t param_id, float base, float eg1, float eg2, float lfo1, float lfo2,
    float mw, float at, float br) {
  uint8_t mask = engine.mod_active_mask[param_id];
  if (mask == 0)
    return base;

  float v = base;
  float *depths = engine.mod_depth_float[param_id];
  if (mask & (1 << SRC_EG1))
    v += depths[SRC_EG1] * eg1;
  if (mask & (1 << SRC_EG2))
    v += depths[SRC_EG2] * eg2;
  if (mask & (1 << SRC_LFO1))
    v += depths[SRC_LFO1] * lfo1;
  if (mask & (1 << SRC_LFO2))
    v += depths[SRC_LFO2] * lfo2;
  if (mask & (1 << SRC_MODWHEEL))
    v += depths[SRC_MODWHEEL] * mw;
  if (mask & (1 << SRC_AFTERTOUCH))
    v += depths[SRC_AFTERTOUCH] * at;
  if (mask & (1 << SRC_BREATH))
    v += depths[SRC_BREATH] * br;

  if (v < 0.0f)
    v = 0.0f;
  if (v > 127.0f)
    v = 127.0f;
  return v;
}

static const float pitch_fine_lut[128] = {
    1.000000f, 1.000451f, 1.000903f, 1.001355f, 1.001807f, 1.002259f, 1.002711f,
    1.003164f, 1.003617f, 1.004070f, 1.004523f, 1.004976f, 1.005430f, 1.005884f,
    1.006338f, 1.006792f, 1.007246f, 1.007701f, 1.008156f, 1.008611f, 1.009066f,
    1.009522f, 1.009977f, 1.010433f, 1.011346f, 1.011802f, 1.012259f, 1.012716f,
    1.013173f, 1.013630f, 1.014088f, 1.014545f, 1.015003f, 1.015461f, 1.015920f,
    1.016378f, 1.016837f, 1.017296f, 1.017755f, 1.018215f, 1.018674f, 1.019134f,
    1.019594f, 1.020054f, 1.020515f, 1.020975f, 1.021436f, 1.021897f, 1.022358f,
    1.022820f, 1.023282f, 1.023743f, 1.024205f, 1.024668f, 1.025130f, 1.025593f,
    1.026056f, 1.026519f, 1.026982f, 1.027446f, 1.027910f, 1.028374f, 1.028838f,
    1.029302f, 1.029767f, 1.030232f, 1.030697f, 1.031162f, 1.031627f, 1.032093f,
    1.032559f, 1.033025f, 1.033491f, 1.033491f, 1.033958f, 1.034424f, 1.034891f,
    1.035358f, 1.035826f, 1.036293f, 1.036761f, 1.037229f, 1.037697f, 1.038166f,
    1.038634f, 1.039103f, 1.039572f, 1.040041f, 1.040511f, 1.040980f, 1.041450f,
    1.041920f, 1.042390f, 1.042861f, 1.043332f, 1.043803f, 1.044274f, 1.044745f,
    1.045217f, 1.045688f, 1.046160f, 1.046633f, 1.047105f, 1.047578f, 1.048051f,
    1.048524f, 1.048997f, 1.049470f, 1.049944f, 1.050418f, 1.050892f, 1.051366f,
    1.051841f, 1.052316f, 1.052791f, 1.053266f, 1.053741f, 1.054217f, 1.054693f,
    1.055169f, 1.055645f, 1.056122f, 1.056598f, 1.057075f, 1.057552f, 1.058030f,
    1.058507f, 1.058985f};

static inline float get_transpose_factor(int8_t transpose) {
  int idx = transpose + 60;
  if (idx < 0)
    idx = 0;
  if (idx > 120)
    idx = 120;
  return transpose_factor_lut[idx];
}

static inline float mix_volume_gain(uint8_t v) {
  return mix_volume_lut[v & 127];
}

static float noise_b0 = 1.0f, noise_b1 = 0.0f, noise_b2 = 0.0f;
static float noise_a1 = 0.0f, noise_a2 = 0.0f;
static float noise_z1 = 0.0f, noise_z2 = 0.0f;
static uint8_t noise_prev_color = 0xFF;
static uint8_t noise_mode = 0;
static uint32_t noise_rng = 1u;

static inline void noise_update_coeffs(uint8_t color, float sampleRate) {
  (void)sampleRate;
  if (color == noise_prev_color)
    return;
  noise_prev_color = color;

  noise_mode = g_noise_mode_lut[color];
  if (noise_mode == 0) {
    return;
  }

  noise_b0 = g_noise_filter_lut[color][0];
  noise_b1 = g_noise_filter_lut[color][1];
  noise_b2 = g_noise_filter_lut[color][2];
  noise_a1 = g_noise_filter_lut[color][3];
  noise_a2 = g_noise_filter_lut[color][4];
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

static inline float noise_rand(void) {
  noise_rng ^= noise_rng << 13;
  noise_rng ^= noise_rng >> 17;
  noise_rng ^= noise_rng << 5;
  return ((noise_rng & 0x7FFFFFFF) / 1073741824.0f) - 1.0f;
}

static inline uint32_t freq_to_inc(float freq) {
  return (uint32_t)(freq * (4294967296.0f / SAMPLE_RATE));
}

static inline int get_mipmap_level(float freq) {
  // Optimized mipmap selection
  // 20Hz -> 0
  // 40Hz -> 1
  // ...
  // 2560Hz -> 7
  if (freq < 20.0f)
    return 0;
  if (freq >= 2560.0f)
    return 7;

  // Fast approximation or just simple ifs
  if (freq < 40.0f)
    return 0;
  if (freq < 80.0f)
    return 1;
  if (freq < 160.0f)
    return 2;
  if (freq < 320.0f)
    return 3;
  if (freq < 640.0f)
    return 4;
  if (freq < 1280.0f)
    return 5;
  return 6;
}

static float wt_tri[4096];

static inline float table_interp(const float *table, uint32_t phase) {
  uint32_t idx = phase >> (32 - 12);
  uint32_t next_idx = (idx + 1) & 4095;
  // Use bit shift and multiply for faster frac calculation
  float frac = (float)(phase & 0x000FFFFF) * (1.0f / 1048576.0f);
  float a = table[idx];
  return a + (table[next_idx] - a) * frac;
}

static inline float wave_saw(uint32_t phase, int level) {
  return table_interp(wt_saw[level], phase);
}

static inline float wave_sin(uint32_t phase) {
  return table_interp(wt_sin, phase);
}

static inline float wave_tri(uint32_t phase) {
  return table_interp(wt_tri, phase);
}

static inline float wave_pulse(uint32_t phase, int level, float pw) {
  uint32_t offset = (uint32_t)(pw * 4294967296.0f);
  float val1 = wave_saw(phase, level);
  float val2 = wave_saw(phase + offset, level);
  // wt_saw goes from 0 down to -1, wraps to +1, goes down to 0
  // Diff amplitude is roughly 2.0. Subtract the midpoint to center at 0.
  return (val1 - val2) - (1.0f - 2.0f * pw);
}

static inline float wave_pam(uint32_t phase, uint8_t pattern_idx) {
  uint32_t segment = phase >> (32 - 4);
  return pam4_patterns[pattern_idx][segment];
}

static inline float apply_multi_fold_fast(float x, uint8_t shape) {
  if (shape < 16)
    return x;
  float gain = 1.0f + ((float)(shape - 16) * (4.0f / 111.0f));
  float y = x * gain;
  float x1 = (y + 1.0f) * 0.25f;
  float f = x1 - (int)x1;
  if (f < 0)
    f += 1.0f;
  return fabsf(f * 4.0f - 2.0f) - 1.0f;
}

static inline uint32_t apply_bit_fold(uint32_t phase, uint8_t shape) {
  int factor = (int)(shape / 8) + 1;
  if (factor > 16)
    factor = 16;
  return phase * (uint32_t)factor;
}

static inline float pwm_from_t(float t) {
  if (t < 0.0f)
    t = 0.0f;
  if (t > 1.0f)
    t = 1.0f;
  return 0.5f - t * 0.49f;
}

static inline uint8_t pam_pattern_from_t(float t) {
  if (t < 0.0f)
    t = 0.0f;
  if (t > 1.0f)
    t = 1.0f;
  int idx = (int)(t * 5.0f);
  if (idx > 4)
    idx = 4;
  return (uint8_t)idx;
}

static inline float wave_pulse_fast(uint32_t phase, int level, uint32_t offset,
                                    float p_const) {
  float val1 = wave_saw(phase, level);
  float val2 = wave_saw(phase + offset, level);
  return (val1 - val2) - p_const;
}

static inline float
__not_in_flash_func(sample_wave_raw)(uint32_t phase, int level, WaveType type,
                                     float pwm, uint8_t pam_pattern) {
  if (type == W_SIN)
    return wave_sin(phase);
  if (type == W_TRI)
    return wave_tri(phase);
  if (type == W_SAW)
    return wave_saw(phase, level);
  if (type == W_RSAW)
    return -wave_saw(phase, level);
  if (type == W_SQR || type == W_PULSE)
    return wave_pulse(phase, level, pwm);
  return wave_pam(phase, pam_pattern);
}

static inline float __not_in_flash_func(sample_wave)(uint32_t phase, int level,
                                                     WaveType type, float pwm,
                                                     uint8_t pam_pattern,
                                                     uint8_t shape) {
  float s = sample_wave_raw(apply_bit_fold(phase, shape), level, type, pwm,
                            pam_pattern);
  s = apply_multi_fold_fast(s, shape);
  if (shape >= 8) {
    s *= (phase < 0x80000000) ? 1.0f : -1.0f;
  }
  return s;
}

static inline void
__not_in_flash_func(omsk_osc_update_strategy)(OmskOsc *vco,
                                              uint8_t wave_param) {
  // Strategy caching is no longer needed with the vco_lut_data table.
  (void)vco;
  (void)wave_param;
}

static inline float get_wave_lut(int level, uint32_t phase, WaveType w,
                                 float pwm, uint8_t pam_idx, uint8_t shape) {
  if (level < 0)
    level = 0;
  if (level > 7)
    level = 7;
  if (shape > 127)
    shape = 127;

  int p_idx0 = (phase >> 24) & 255;
  int p_idx1 = (p_idx0 + 1) & 255;
  float p_frac = (float)(phase & 0x00FFFFFF) * (1.0f / 16777216.0f);

  if (w <= W_RSAW) {
    float s_b = (float)shape * (63.0f / 127.0f);
    int sb_0 = (int)s_b;
    int sb_1 = sb_0 < 63 ? sb_0 + 1 : 63;
    float sb_frac = s_b - (float)sb_0;

    float v00 = (float)lut_basic[level][w][sb_0][p_idx0];
    float v01 = (float)lut_basic[level][w][sb_0][p_idx1];
    float v10 = (float)lut_basic[level][w][sb_1][p_idx0];
    float v11 = (float)lut_basic[level][w][sb_1][p_idx1];

    float i0 = v00 + (v01 - v00) * p_frac;
    float i1 = v10 + (v11 - v10) * p_frac;
    return (i0 + (i1 - i0) * sb_frac) * (1.0f / 32767.0f);
  } else if (w == W_PAM) {
    int idx = (pam_idx + shape) % 128;
    int p_idx = (phase >> 28) & 15;
    return lut_pam4[idx][p_idx];
  } else {
    float b_pwm = (w == W_PULSE) ? pwm : 0.5f;
    float f_idx = (0.5f - b_pwm) * (15.0f / 0.49f);
    int pw_idx = (int)f_idx;
    if (pw_idx < 0)
      pw_idx = 0;
    else if (pw_idx > 15)
      pw_idx = 15;

    float s_f = (float)shape * (15.0f / 127.0f);
    int s_idx0 = (int)s_f;
    int s_idx1 = s_idx0 < 15 ? s_idx0 + 1 : 15;
    float s_frac = s_f - (float)s_idx0;

    float v00 = (float)lut_pulse[level][pw_idx][s_idx0][p_idx0];
    float v01 = (float)lut_pulse[level][pw_idx][s_idx0][p_idx1];
    float v10 = (float)lut_pulse[level][pw_idx][s_idx1][p_idx0];
    float v11 = (float)lut_pulse[level][pw_idx][s_idx1][p_idx1];

    float interp_s0 = v00 + (v01 - v00) * p_frac;
    float interp_s1 = v10 + (v11 - v10) * p_frac;
    return (interp_s0 + (interp_s1 - interp_s0) * s_frac) * (1.0f / 32767.0f);
  }
}

static inline WaveType get_hybrid_type(int is_b, int idx) {
  static const WaveType pairs[22][2] = {
      {W_SIN, W_TRI},   {W_SIN, W_SAW},    {W_SIN, W_RSAW},  {W_SIN, W_SQR},
      {W_SIN, W_PULSE}, {W_SIN, W_PAM},    {W_TRI, W_SAW},   {W_TRI, W_RSAW},
      {W_TRI, W_SQR},   {W_TRI, W_PULSE},  {W_TRI, W_PAM},   {W_SAW, W_RSAW},
      {W_RSAW, W_SAW},  {W_SAW, W_SQR},    {W_SAW, W_PULSE}, {W_SAW, W_PAM},
      {W_RSAW, W_SQR},  {W_RSAW, W_PULSE}, {W_RSAW, W_PAM},  {W_SQR, W_PULSE},
      {W_SQR, W_PAM},   {W_PULSE, W_PAM}};
  return pairs[idx][is_b];
}

static float __not_in_flash_func(generate_wave)(uint32_t phase,
                                                uint32_t phase_inc,
                                                uint8_t wave_param,
                                                uint8_t shape, float freq) {
  (void)phase_inc;
  int level = get_mipmap_level(freq);
  float final_val = 0.0f;

  if (wave_param < 32) {
    int seg = wave_param / 8;
    float t = (float)(wave_param % 8) / 7.0f;
    if (seg == 2) {
      float s_saw = get_wave_lut(level, phase, W_SAW, 0.5f, 0, shape);
      float s_rsaw = get_wave_lut(level, phase, W_RSAW, 0.5f, 0, shape);
      float s_tri = get_wave_lut(level, phase, W_TRI, 0.5f, 0, shape);
      float curr_t = t * 2.0f;
      if (t < 0.5f) {
        final_val = (1.0f - curr_t) * s_saw + curr_t * s_tri;
      } else {
        curr_t -= 1.0f;
        final_val = (1.0f - curr_t) * s_tri + curr_t * s_rsaw;
      }
    } else {
      WaveType types[4][2] = {
          {W_SIN, W_TRI}, {W_TRI, W_SAW}, {W_SAW, W_RSAW}, {W_RSAW, W_PULSE}};
      WaveType a = types[seg][0];
      WaveType b = types[seg][1];
      float s1 = get_wave_lut(level, phase, a, 0.5f, 0, shape);
      float s2 = get_wave_lut(level, phase, b, 0.5f, 0, shape);
      final_val = (1.0f - t) * s1 + t * s2;
    }
  } else if (wave_param < 64) {
    float pw = 0.5f - ((float)(wave_param - 32) / 31.0f) * 0.49f;
    final_val = get_wave_lut(level, phase, W_PULSE, pw, 0, shape);
  } else if (wave_param < 80) {
    uint8_t base_idx = (uint8_t)((wave_param - 64) * (127.0f / 15.0f));
    final_val = get_wave_lut(level, phase, W_PAM, 0.5f, base_idx, shape);
  } else {
    float pos = (wave_param - 80) / 47.0f;
    float segf = pos * 21.0f;
    int idx = (int)segf;
    if (idx > 21)
      idx = 21;
    float t = segf - (float)idx;

    WaveType a = get_hybrid_type(0, idx);
    WaveType b = get_hybrid_type(1, idx);

    float pwm_val = pwm_from_t(t);
    uint8_t pam_val = (uint8_t)(t * 127.0f);

    uint32_t phase_a = phase;
    if (a == W_PULSE || a == W_SQR)
      phase_a += 0x40000000;
    // Invert phase for waves 85-93
    if (wave_param >= 85 && wave_param <= 93 && a == W_SIN)
      phase_a += 0x80000000;
    uint32_t phase_b = phase + 0x80000000;

    float s_a = get_wave_lut(level, phase_a, a, pwm_val, pam_val, shape);
    float s_b = get_wave_lut(level, phase_b, b, pwm_val, pam_val, shape);

    final_val = (phase < 0x80000000) ? s_a : s_b;
  }

  return final_val;
}

static float __not_in_flash_func(generate_wave_fast)(OmskOsc *vco, int level,
                                                     uint8_t shape_param,
                                                     uint32_t phase) {
  return generate_wave(phase, 0, vco->wave, shape_param, vco->freq);
}

// 2. Oscillator Baking: Asynchronous incremental morphing
static inline void __not_in_flash_func(omsk_bake_osc_start)(OmskOsc *vco) {
  if (vco->bake_progress < 256)
    return; // already baking
  vco->bake_progress = 0;
  vco->bake_w = vco->wave;
  vco->bake_s = vco->shape;
  vco->bake_lvl = get_mipmap_level(vco->freq);
  vco->bake_freq = vco->freq;
}

static inline void __not_in_flash_func(omsk_bake_osc_step)(OmskOsc *vco) {
  if (vco->bake_progress >= 256)
    return;

  uint8_t wp = vco->bake_w;
  uint8_t shape = vco->bake_s;
  int level = vco->bake_lvl;
  float inv_32767 = 1.0f / 32767.0f;
  uint8_t inactive_bank = vco->active_bank ^ 1;

  for (int iter = 0; iter < 4; iter++) {
    if (vco->bake_progress >= 256)
      break;
    int i = vco->bake_progress;

    if (wp < 32) {
      int seg = wp / 8;
      float t = (float)(wp % 8) / 7.0f;
      uint8_t sh_idx = (uint8_t)((float)shape * (63.0f / 127.0f));

      if (seg == 2) {
        float curr_t = t * 2.0f;
        if (t < 0.5f) {
          const int16_t *lut_a = lut_basic[level][W_SAW][sh_idx];
          const int16_t *lut_b = lut_basic[level][W_TRI][sh_idx];
          float v_a = (float)lut_a[i] * inv_32767;
          float v_b = (float)lut_b[i] * inv_32767;
          float val = (1.0f - curr_t) * v_a + curr_t * v_b;
          vco->baked_wave[inactive_bank][i] = (int16_t)(val * 32767.0f);
        } else {
          curr_t -= 1.0f;
          const int16_t *lut_a = lut_basic[level][W_TRI][sh_idx];
          const int16_t *lut_b = lut_basic[level][W_RSAW][sh_idx];
          float v_a = (float)lut_a[i] * inv_32767;
          float v_b = (float)lut_b[i] * inv_32767;
          float val = (1.0f - curr_t) * v_a + curr_t * v_b;
          vco->baked_wave[inactive_bank][i] = (int16_t)(val * 32767.0f);
        }
      } else {
        WaveType type_a = (seg == 0)   ? W_SIN
                          : (seg == 1) ? W_TRI
                          : (seg == 2) ? W_SAW
                                       : W_RSAW;
        WaveType type_b = (seg == 0)   ? W_TRI
                          : (seg == 1) ? W_SAW
                          : (seg == 2) ? W_RSAW
                                       : W_PULSE;

        const int16_t *lut_a = (type_a <= W_RSAW)
                                   ? lut_basic[level][type_a][sh_idx]
                                   : lut_pulse[level][0][shape >> 3];
        const int16_t *lut_b = (type_b <= W_RSAW)
                                   ? lut_basic[level][type_b][sh_idx]
                                   : lut_pulse[level][0][shape >> 3];

        float v_a = (float)lut_a[i] * inv_32767;
        float v_b = (float)lut_b[i] * inv_32767;
        float val = (1.0f - t) * v_a + t * v_b;
        vco->baked_wave[inactive_bank][i] = (int16_t)(val * 32767.0f);
      }
    } else {
      float val =
          generate_wave((uint32_t)i << 24, 0, wp, shape, vco->bake_freq);
      vco->baked_wave[inactive_bank][i] = (int16_t)(val * 32767.0f);
    }

    vco->bake_progress++;
  }

  if (vco->bake_progress >= 256) {
    vco->active_bank = inactive_bank;
  }
}

// Mutex disabled for now to debug freeze
// #include "pico/multicore.h"
// #include "hardware/sync.h"

// static mutex_t engine_mutex;
// static bool mutex_initialized = false;

void omsk_core_init(void) {
  // Precompute triangle table (too large for LUT or fits better here)
  for (int i = 0; i < 4096; i++) {
    float p = (float)i / 4096.0f;
    wt_tri[i] = 2.0f * fabsf(2.0f * p - 1.0f) - 1.0f;
  }

  // Pre-calculated LUTs are now constant externs from omsk_lut_data.c
  for (int i = -60; i <= 60; i++) {
    transpose_factor_lut[i + 60] = powf(2.0f, (float)i / 12.0f);
  }

  init_soft_clip_lut();

  memset(&engine, 0, sizeof(engine));

  // Defaults
  engine.vco1_wave = 0;
  engine.vco1_shape = 0;
  engine.vco1_detune = 64; // Center
  engine.vco1_transpose = 0;
  engine.vco1_transpose_raw = 5;

  engine.vco2_wave = 0;
  engine.vco2_shape = 0;
  engine.vco2_detune = 64; // Center
  engine.vco2_transpose = 0;
  engine.vco2_transpose_raw = 5;

  for (int i = 0; i < MAX_VOICES; i++) {
    engine.voices[i].vcf1.set_instance_id(1);
    engine.voices[i].vcf2.set_instance_id(2);

    engine.voices[i].vco1.bake_progress = 256;
    engine.voices[i].vco2.bake_progress = 256;
    omsk_bake_osc_start(&engine.voices[i].vco1);
    while (engine.voices[i].vco1.bake_progress < 256) {
      omsk_bake_osc_step(&engine.voices[i].vco1);
    }
    omsk_bake_osc_start(&engine.voices[i].vco2);
    while (engine.voices[i].vco2.bake_progress < 256) {
      omsk_bake_osc_step(&engine.voices[i].vco2);
    }
  }

  engine.mix_vco1 = 127;
  engine.mix_vco2 = 127;
  engine.mix_noise = 0;
  engine.mix_phase2 = 0;
  engine.noise_color = 64;
  engine.route_vco1 = ROUTE_VCF1;
  engine.route_vco2 = ROUTE_VCF2;
  engine.route_noise = ROUTE_VCF2;
  engine.lfo1_rate = 10; // ~1Hz
  engine.target_pitch_bend = 1.0f;
  engine.current_pitch_bend = 1.0f;
  engine.pitch_bend_range = CFG_PITCH_BEND_RANGE_SEMITONES;
  engine.pan = 64; // center
  engine.pan_l = 0.8f;
  engine.pan_r = 0.8f;

  engine.eg1_attack = 0;
  engine.eg1_decay = 0;
  engine.eg1_sustain = 127; // VCA Sustain 100%
  engine.eg1_release = 0;

  engine.eg2_attack = 0;
  engine.eg2_decay = 0;
  engine.eg2_sustain = 127; // EG2 sustain 100%
  engine.eg2_release = 0;

  engine.sample_counter = 0;
  engine.current_gain = 1.0f;
  engine.target_gain = 1.0f;
  engine.master_gain_base = 38.0f; // 30% default
  engine.master_gain = 0.8f;
  engine.chord_mode = 0;

  // VCF Defaults (Sync with params)
  engine.vcf1_cutoff = 112;
  engine.vcf1_res = 64;
  engine.vcf1_mix = 127;
  engine.vcf2_cutoff = 127;
  engine.vcf2_res = 64;
  engine.vcf2_mix = 127;

  // FX Defaults
  engine.fx1_time = 64;
  engine.fx1_feedback = 64;
  engine.fx1_spread = 64;
  engine.fx1_mix = 0;
  engine.fx2_time = 64;
  engine.fx2_feedback = 64;
  engine.fx2_tone = 127;
  engine.fx2_time = 64;
  engine.fx2_feedback = 64;
  engine.fx2_tone = 127;
  engine.fx2_mix = 0;
  engine.glide_poly = 0;
  engine.glide_time = 0;
  engine.glide_slope = 64;
  engine.glide_mode = 0;
  engine.active_voices_count = 0;

  for (int i = 0; i < MAX_VOICES; i++) {
    engine.voices[i].freq_start = 440.0f;
    engine.voices[i].freq_target = 440.0f;
    engine.voices[i].glide_elapsed = 1.0f;
    engine.voices[i].unison_detune = 0.0f;
    engine.voices[i].unison_detune_factor = 1.0f;
    engine.voices[i].vco1.transpose = 0;
    engine.voices[i].vco1.detune_factor = 1.0f;
    engine.voices[i].vco2.transpose = 0;
    engine.voices[i].vco2.detune_factor = 1.0f;
    update_vco_pitch(&engine.voices[i].vco1);
    update_vco_pitch(&engine.voices[i].vco2);
    update_voice_frequencies(i, 440.0f, true);
    engine.voices[i].vcf1.set_cutoff(112);
    engine.voices[i].vcf1.set_resonance(64);
    engine.voices[i].vcf1.set_mix(127);
    engine.voices[i].vcf2.set_cutoff(127);
    engine.voices[i].vcf2.set_resonance(64);
    engine.voices[i].vcf2.set_mix(127);
    engine.voices[i].cached_velocity_gain = 1.0f;
  }
}

static inline void update_vco_pitch(OmskOsc *vco) {
  // Recalculate base pitch multiplier (without dynamic pitch bend)
  vco->pitch_mult_base =
      get_transpose_factor(vco->transpose) * vco->detune_factor;
}

static inline void update_voice_frequencies(int i, float target_f,
                                            bool immediate) {
  OmskVoice *v = &engine.voices[i];
  if (immediate) {
    v->freq_start = target_f;
    v->freq_target = target_f;
    v->glide_elapsed = 1.0f;
    v->vco1.freq = target_f;
    v->vco2.freq = target_f;
    v->vco1.current_pitch_mult = v->vco1.pitch_mult_base;
    v->vco2.current_pitch_mult = v->vco2.pitch_mult_base;
  } else {
    // Current frequency is the start of the glide
    float current_f =
        v->freq_start + (v->freq_target - v->freq_start) * v->glide_elapsed;
    v->freq_start = current_f;
    v->freq_target = target_f;
    v->glide_elapsed = 0.0f;
    v->vco1.current_pitch_mult = v->vco1.pitch_mult_base;
    v->vco2.current_pitch_mult = v->vco2.pitch_mult_base;
  }
}

void omsk_core_note_on(uint8_t note, uint8_t velocity) {
  float freq = midi_to_freq(note);
  bool unison = (engine.glide_poly > 0);
  bool was_any_active = false;
  for (int i = 0; i < MAX_VOICES; i++) {
    if (engine.voices[i].active && !engine.voices[i].is_stealing)
      was_any_active = true;
  }

  uint8_t gm = engine.glide_mode;
  bool should_glide = (gm > 85) || (gm > 42 && was_any_active);
  if (engine.glide_time == 0)
    should_glide = false;

  if (unison) {
    // UNISON MODE: All voices play the same note
    engine.active_voices_count = 1; // logical count
    float spread = (float)engine.glide_poly / 127.0f;
    float detunes[4] = {-0.375f * spread, -0.125f * spread, 0.125f * spread,
                        0.375f * spread};

    for (int i = 0; i < MAX_VOICES; i++) {
      OmskVoice *v = &engine.voices[i];
      v->note = note;
      v->velocity = velocity;
      v->cached_velocity_gain = mix_volume_lut[velocity & 127];
      v->active = true;
      v->gate = true;
      v->sustain_hold = false;
      v->age = ++engine.global_age;
      v->is_stealing = false;
      v->unison_detune = detunes[i];
      v->unison_detune_factor = powf(2.0f, v->unison_detune / 12.0f);

      if (gm > 42 && gm <= 85 && was_any_active) {
        // Legato Mode: Don't reset EG level if already active
      } else {
        v->eg1.level = 0.0f;
        v->eg2.level = 0.0f;
      }

      v->eg1.state = OMSK_EG_ATTACK;
      v->eg1.sustain_level = (float)engine.eg1_sustain / 127.0f;
      v->eg1.attack_inc = omsk_eg_calc_inc(engine.eg1_attack);
      v->eg1.decay_inc = omsk_eg_calc_inc(engine.eg1_decay);
      v->eg1.release_inc = omsk_eg_calc_inc(engine.eg1_release);
      v->eg1.velocity = velocity;

      v->eg2.state = OMSK_EG_ATTACK;
      v->eg2.sustain_level = (float)engine.eg2_sustain / 127.0f;
      v->eg2.attack_inc = omsk_eg_calc_inc(engine.eg2_attack);
      v->eg2.decay_inc = omsk_eg_calc_inc(engine.eg2_decay);
      v->eg2.release_inc = omsk_eg_calc_inc(engine.eg2_release);
      v->eg2.velocity = velocity;

      v->vco1.transpose = engine.vco1_transpose;
      v->vco1.detune_factor = get_detune_factor(engine.vco1_detune);
      v->vco2.transpose = engine.vco2_transpose;
      v->vco2.detune_factor = get_detune_factor(engine.vco2_detune);
      update_vco_pitch(&v->vco1);
      update_vco_pitch(&v->vco2);

      update_voice_frequencies(i, freq, !should_glide);
      omsk_osc_update_strategy(&v->vco1, engine.vco1_wave);
      omsk_osc_update_strategy(&v->vco2, engine.vco2_wave);
    }
    return;
  }

  // POLYPHONIC MODE (Normal)
  for (int i = 0; i < MAX_VOICES; i++) {
    if (engine.voices[i].active && engine.voices[i].note == note &&
        !engine.voices[i].is_stealing) {
      OmskVoice *v = &engine.voices[i];
      v->velocity = velocity;
      v->gate = true;
      v->sustain_hold = false;
      v->age = ++engine.global_age;
      v->eg1.state = OMSK_EG_ATTACK;
      v->eg1.level = 0.0f;
      v->eg2.state = OMSK_EG_ATTACK;
      v->eg2.level = 0.0f;
      update_voice_frequencies(i, freq, !should_glide);
      return;
    }
  }

  int voice_idx = -1;
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!engine.voices[i].active) {
      voice_idx = i;
      break;
    }
  }
  if (voice_idx == -1) {
    uint32_t min_age = 0xFFFFFFFF;
    for (int i = 0; i < MAX_VOICES; i++) {
      if (engine.voices[i].age < min_age) {
        min_age = engine.voices[i].age;
        voice_idx = i;
      }
    }
  }
  if (voice_idx == -1)
    voice_idx = 0;

  OmskVoice *v = &engine.voices[voice_idx];
  if (v->active && (v->eg2.level > 0.01f || v->eg1.level > 0.01f)) {
    v->is_stealing = true;
    v->target_note = note;
    v->target_velocity = velocity;
    return;
  }

  v->is_stealing = false;
  v->note = note;
  v->velocity = velocity;
  v->cached_velocity_gain = mix_volume_lut[velocity & 127];
  v->active = true;
  v->gate = true;
  v->sustain_hold = false;
  v->age = ++engine.global_age;
  v->unison_detune = 0.0f;

  v->unison_detune_factor = 1.0f;

  v->eg1.state = OMSK_EG_ATTACK;
  v->eg1.level = 0.0f;
  v->eg1.sustain_level = (float)engine.eg1_sustain / 127.0f;
  v->eg1.attack_inc = omsk_eg_calc_inc(engine.eg1_attack);
  v->eg1.decay_inc = omsk_eg_calc_inc(engine.eg1_decay);
  v->eg1.release_inc = omsk_eg_calc_inc(engine.eg1_release);
  v->eg1.velocity = velocity;

  v->eg2.state = OMSK_EG_ATTACK;
  v->eg2.level = 0.0f;
  v->eg2.sustain_level = (float)engine.eg2_sustain / 127.0f;
  v->eg2.attack_inc = omsk_eg_calc_inc(engine.eg2_attack);
  v->eg2.decay_inc = omsk_eg_calc_inc(engine.eg2_decay);
  v->eg2.release_inc = omsk_eg_calc_inc(engine.eg2_release);
  v->eg2.velocity = velocity;

  v->vco1.detune_factor = get_detune_factor(engine.vco1_detune);
  v->vco2.detune_factor = get_detune_factor(engine.vco2_detune);
  update_vco_pitch(&v->vco1);
  update_vco_pitch(&v->vco2);
  update_voice_frequencies(voice_idx, freq, !should_glide);

  omsk_osc_update_strategy(&v->vco1, engine.vco1_wave);
  omsk_osc_update_strategy(&v->vco2, engine.vco2_wave);

  // Reset phase, filter state and stale output to avoid click/silence at voice
  // start
  v->vco1.phase = 0;
  v->vco2.phase = 0;
  v->cached_s1_out = 0.0f;
  v->cached_s2_out = 0.0f;
  v->vcf1.reset_state();
  v->vcf2.reset_state();

  // Pre-initialize gain caches so first sample is correct (not zero)
  v->cached_g1 = mix_volume_lut[engine.mix_vco1 & 127];
  v->cached_g2 = mix_volume_lut[engine.mix_vco2 & 127];
  v->cached_g3 = mix_volume_lut[engine.mix_noise & 127];
  v->cached_level1 = get_mipmap_level(midi_to_freq(note));
  v->cached_level2 = v->cached_level1;

  // DO NOT call omsk_bake_osc here - it blocks the audio ISR for ~100µs
  // causing a gap in all active voices. Defer bake to first process cycle.
  v->steal_fade = -1.0f; // sentinel: needs initial bake
}

void omsk_core_note_off(uint8_t note) {
  bool unison = (engine.glide_poly > 0);
  for (int i = 0; i < MAX_VOICES; i++) {
    if (engine.voices[i].active && (engine.voices[i].note == note || unison)) {
      engine.voices[i].gate = false;
      if (engine.sustain_pedal) {
        engine.voices[i].sustain_hold = true;
      } else {
        engine.voices[i].eg1.state = OMSK_EG_RELEASE;
        engine.voices[i].eg2.state = OMSK_EG_RELEASE;
      }
    }
  }
}

void omsk_core_set_param(uint8_t param_id, uint16_t value) {
  switch (param_id) {
  case PARAM_VCO1_WAVE:
    engine.vco1_wave = value;
    break;
  case PARAM_VCO1_SHAPE:
    engine.vco1_shape = value;
    break;
  case PARAM_VCO1_TRANSPOSE:
    engine.vco1_transpose_raw = (uint8_t)value;
    engine.vco1_transpose = ((int8_t)value - 5) * 12;
    for (int i = 0; i < MAX_VOICES; i++) {
      engine.voices[i].vco1.transpose = engine.vco1_transpose;
      update_vco_pitch(&engine.voices[i].vco1);
    }
    break;
  case PARAM_VCO1_DETUNE:
    engine.vco1_detune = value;
    for (int i = 0; i < MAX_VOICES; i++) {
      engine.voices[i].vco1.detune_factor = get_detune_factor(value);
      update_vco_pitch(&engine.voices[i].vco1);
    }
    break;
  case PARAM_VCO2_WAVE:
    engine.vco2_wave = value;
    break;
  case PARAM_VCO2_SHAPE:
    engine.vco2_shape = value;
    break;
  case PARAM_VCO2_TRANSPOSE:
    engine.vco2_transpose_raw = (uint8_t)value;
    engine.vco2_transpose = ((int8_t)value - 5) * 12;
    for (int i = 0; i < MAX_VOICES; i++) {
      engine.voices[i].vco2.transpose = engine.vco2_transpose;
      update_vco_pitch(&engine.voices[i].vco2);
    }
    break;
  case PARAM_VCO2_DETUNE:
    engine.vco2_detune = value;
    for (int i = 0; i < MAX_VOICES; i++) {
      engine.voices[i].vco2.detune_factor = get_detune_factor(value);
      update_vco_pitch(&engine.voices[i].vco2);
    }
    break;
  case PARAM_MIX_VCO1_VOL:
    engine.mix_vco1 = value;
    break;
  case PARAM_MIX_VCO2_VOL:
    engine.mix_vco2 = value;
    break;
  case PARAM_MIX_NOISE_VOL:
    engine.mix_noise = value;
    break;
  case PARAM_MIX_PHASE2:
    engine.mix_phase2 = value;
    break;
  case PARAM_NOISE_COLOR:
    engine.noise_color = value;
    noise_update_coeffs(value, SAMPLE_RATE); // Move to control rate (set_param)
    break;
  case PARAM_VCF1_CUTOFF:
    engine.vcf1_cutoff = value;
    break;
  case PARAM_VCF1_RES:
    engine.vcf1_res = value;
    break;
  case PARAM_VCF1_DRIVE:
    engine.vcf1_drive = value;
    break;
  case PARAM_VCF1_MIX:
    engine.vcf1_mix = value;
    break;
  case PARAM_VCF2_CUTOFF:
    engine.vcf2_cutoff = value;
    break;
  case PARAM_VCF2_RES:
    engine.vcf2_res = value;
    break;
  case PARAM_VCF2_DRIVE:
    engine.vcf2_drive = value;
    for (int i = 0; i < MAX_VOICES; i++)
      engine.voices[i].vcf2.set_filter_mode(value < 64 ? 0 : 127);
    break;
  case PARAM_VCF2_MIX:
    engine.vcf2_mix = value;
    break;
  case PARAM_VCF_KEY_TRACK:
    engine.vcf_key_track = value;
    break;
  case PARAM_LFO1_RATE:
    engine.lfo1_rate = value;
    break;
  case PARAM_LFO1_WAVE:
    engine.lfo1_wave = value;
    break;
  case PARAM_LFO1_SHAPE:
    engine.lfo1_shape = value;
    break;
  case PARAM_LFO1_SMOOTH:
    engine.lfo1_smooth = value;
    break;
  case PARAM_LFO2_RATE:
    engine.lfo2_rate = value;
    break;
  case PARAM_LFO2_WAVE:
    engine.lfo2_wave = value;
    break;
  case PARAM_LFO2_SHAPE:
    engine.lfo2_shape = value;
    break;
  case PARAM_LFO2_SMOOTH:
    engine.lfo2_smooth = value;
    break;
  case PARAM_EG1_ATTACK:
    engine.eg1_attack = value;
    {
      float inc = omsk_eg_calc_inc(value);
      for (int i = 0; i < MAX_VOICES; i++)
        engine.voices[i].eg1.attack_inc = inc;
    }
    break;
  case PARAM_PITCH_BEND_RANGE:
    if (value > 24)
      value = 24;
    engine.pitch_bend_range = value;
    break;
  case PARAM_PAN: {
    engine.pan = value;
    float pan_f = (float)value / 127.0f;
    engine.pan_l = sqrtf(1.0f - pan_f);
    engine.pan_r = sqrtf(pan_f);
    break;
  }
  case PARAM_EG1_DECAY:
    engine.eg1_decay = value;
    {
      float inc = omsk_eg_calc_inc(value);
      for (int i = 0; i < MAX_VOICES; i++)
        engine.voices[i].eg1.decay_inc = inc;
    }
    break;
  case PARAM_EG1_SUSTAIN:
    engine.eg1_sustain = value;
    {
      float sus = (float)value / 127.0f;
      for (int i = 0; i < MAX_VOICES; i++)
        engine.voices[i].eg1.sustain_level = sus;
    }
    break;
  case PARAM_EG1_RELEASE:
    engine.eg1_release = value;
    {
      float inc = omsk_eg_calc_inc(value);
      for (int i = 0; i < MAX_VOICES; i++)
        engine.voices[i].eg1.release_inc = inc;
    }
    break;
  case PARAM_EG2_ATTACK:
    engine.eg2_attack = value;
    {
      float inc = omsk_eg_calc_inc(value);
      for (int i = 0; i < MAX_VOICES; i++)
        engine.voices[i].eg2.attack_inc = inc;
    }
    break;
  case PARAM_EG2_DECAY:
    engine.eg2_decay = value;
    {
      float inc = omsk_eg_calc_inc(value);
      for (int i = 0; i < MAX_VOICES; i++)
        engine.voices[i].eg2.decay_inc = inc;
    }
    break;
  case PARAM_EG2_SUSTAIN:
    engine.eg2_sustain = value;
    {
      float sus = (float)value / 127.0f;
      for (int i = 0; i < MAX_VOICES; i++)
        engine.voices[i].eg2.sustain_level = sus;
    }
    break;
  case PARAM_EG2_RELEASE:
    engine.eg2_release = value;
    {
      float inc = omsk_eg_calc_inc(value);
      for (int i = 0; i < MAX_VOICES; i++)
        engine.voices[i].eg2.release_inc = inc;
    }
    break;
  case PARAM_FX1_TIME:
    engine.fx1_time = value;
    engine.chorus.set_chorus_delay_time(value);
    break;
  case PARAM_FX1_FEEDBACK:
    engine.fx1_feedback = value;
    engine.chorus.set_chorus_feedback(value);
    break;
  case PARAM_FX1_SPREAD:
    engine.fx1_spread = value;
    engine.chorus.set_chorus_depth(value);
    break;
  case PARAM_FX1_MIX:
    engine.fx1_mix = value;
    engine.chorus.set_chorus_level(value);
    break;
  case PARAM_FX2_TIME:
    engine.fx2_time = value;
    engine.delay.set_delay_time(value);
    break;
  case PARAM_FX2_FEEDBACK:
    engine.fx2_feedback = value;
    engine.delay.set_delay_feedback(value);
    break;
  case PARAM_FX2_TONE:
    engine.fx2_tone = value;
    engine.delay.set_delay_mode(value); // Reuse mode for now or update class
    break;
  case PARAM_FX2_MIX:
    engine.fx2_mix = value;
    engine.delay.set_delay_level(value);
    break;
  case PARAM_AMP_GAIN:
    engine.master_gain_base = (float)value;
    break;
  case PARAM_CHORD_MODE:
    engine.chord_mode = value;
    break;
  case PARAM_GLIDE_POLY:
    engine.glide_poly = value;
    break;
  case PARAM_GLIDE_TIME:
    engine.glide_time = value;
    break;
  case PARAM_GLIDE_SLOPE:
    engine.glide_slope = value;
    break;
  case PARAM_GLIDE_MODE:
    engine.glide_mode = value;
    break;
  case PARAM_MOD_ROUTING1:
    engine.mod_routing1 = value / 16;
    break;
  case PARAM_MOD_DEPTH1:
    engine.mod_depth1_f = (float)value / 127.0f;
    break;
  case PARAM_MOD_ROUTING2:
    engine.mod_routing2 = value / 16;
    break;
  case PARAM_MOD_DEPTH2:
    engine.mod_depth2_f = (float)value / 127.0f;
    break;
  }

  // Recalculate engine gains AFTER all parameter updates (moved from hanging
  // block)
  float g1 = mix_volume_gain(engine.mix_vco1);
  float g2 = mix_volume_gain(engine.mix_vco2);
  float g3 = mix_volume_gain(engine.mix_noise);
  float total_mix = g1 + g2 + g3 + 1e-9f;
  engine.gain_vco1 = g1 / total_mix;
  engine.gain_vco2 = g2 / total_mix;
  engine.gain_noise = g3 / total_mix;
}

static inline float omsk_soft_clip(float x) {
  // Simple soft-saturation to prevent harsh digital clipping.
  // This curve is linear near zero and gradually compresses as it
  // approaches 1.0.
  if (x > 1.25f)
    return 0.99f;
  if (x < -1.25f)
    return -0.99f;

  // Cubic soft-clipper: f(x) = x - x^3/3 (normalized for 1.0)
  // We use a slightly adjusted version to provide a bit of headroom
  float x_scaled =
      x * 0.85f; // -1.4dB pad to prevent DAC "true-peak" distortion
  if (x_scaled > 1.0f)
    return 0.99f;
  if (x_scaled < -1.0f)
    return -0.99f;

  return x_scaled * (1.0f - (x_scaled * x_scaled) * 0.15f);
}

void omsk_core_update_control(void) {
  // 1. Update modulation masks
  for (int p = 0; p < PARAM_COUNT; p++) {
    uint8_t mask = 0;
    for (int s = 0; s < SRC_COUNT; s++) {
      uint8_t depth = params.mod_matrix[p][s];
      if (depth != 64) {
        mask |= (1 << s);
        engine.mod_depth_float[p][s] =
            ((float)depth - 64.0f) * (127.0f / 64.0f);
      }
    }
    engine.mod_active_mask[p] = mask;
  }

  // 2. LFO & Envelope Internal Logic (Core 0)
  // Use LUTs to avoid powf
  float lfo1_freq = lfo_freq_lut[engine.lfo1_rate & 127];
  engine.lfo1.phase_inc = freq_to_inc(lfo1_freq);
  engine.lfo1.rate = lfo1_freq;
  engine.lfo1.smooth_alpha = smooth_alpha_lut[engine.lfo1_smooth & 127];

  float lfo2_freq = lfo_freq_lut[engine.lfo2_rate & 127];
  engine.lfo2.phase_inc = freq_to_inc(lfo2_freq);
  engine.lfo2.rate = lfo2_freq;
  engine.lfo2.smooth_alpha = smooth_alpha_lut[engine.lfo2_smooth & 127];

  // 3. Update Pan from LUT
  engine.pan_l = pan_l_lut[engine.pan & 127];
  engine.pan_r = pan_r_lut[engine.pan & 127];

  // 4. Pre-calculate Phase Offset (Control Rate)
  engine.mix_phase_offset =
      (uint32_t)((engine.mix_phase2 / 127.0f) * 4294967295.0f);

  // 5. Update Chorus Rate from BPM (Control Rate)
  if (params.chorus_sync) {
    engine.chorus.set_chorus_rate_bpm((float)seq_get_bpm());
  }
}

void __not_in_flash_func(omsk_core_process)(float *out_l, float *out_r) {
  engine.sample_counter++;

  // 1. LFOs (Global)
  engine.lfo1.phase += engine.lfo1.phase_inc;
  float lfo1_raw =
      generate_wave(engine.lfo1.phase, engine.lfo1.phase_inc, engine.lfo1_wave,
                    engine.lfo1_shape, engine.lfo1.rate);
  float lfo1_target = (lfo1_raw + 1.0f) * 0.5f;
  engine.lfo1.current +=
      (lfo1_target - engine.lfo1.current) * engine.lfo1.smooth_alpha;
  float lfo1_out = engine.lfo1.current;

  engine.lfo2.phase += engine.lfo2.phase_inc;
  float lfo2_raw =
      generate_wave(engine.lfo2.phase, engine.lfo2.phase_inc, engine.lfo2_wave,
                    engine.lfo2_shape, engine.lfo2.rate);
  float lfo2_target = (lfo2_raw + 1.0f) * 0.5f;
  engine.lfo2.current +=
      (lfo2_target - engine.lfo2.current) * engine.lfo2.smooth_alpha;
  float lfo2_out = engine.lfo2.current;

  // 2. Global Modulation Sources (Cached for this sample)
  float mw = engine.modwheel;
  float at = engine.aftertouch;
  float br = engine.breath;
  float pitch_bend = engine.current_pitch_bend +=
      (engine.target_pitch_bend - engine.current_pitch_bend) * 0.001f;

  float pl = engine.pan_l;
  float pr = engine.pan_r;

  uint8_t cycle = engine.sample_counter & 15;

  // Global Parameter Modulation (Distributed: Sample 0)
  if (cycle == 0) {
    float ge1 = engine.voices[0].active ? engine.voices[0].eg1.level : 0.0f;
    float ge2 = engine.voices[0].active ? engine.voices[0].eg2.level : 0.0f;

    float ph2_val =
        omsk_apply_mod_fast(PARAM_MIX_PHASE2, (float)engine.mix_phase2, ge1,
                            ge2, lfo1_out, lfo2_out, mw, at, br);
    engine.mix_phase_offset = (uint32_t)((ph2_val / 127.0f) * 4294967295.0f);

    float sub_val =
        omsk_apply_mod_fast(PARAM_SUB_OSC, (float)params.sub_osc, ge1, ge2,
                            lfo1_out, lfo2_out, mw, at, br);

    float nc_val =
        omsk_apply_mod_fast(PARAM_NOISE_COLOR, (float)engine.noise_color, ge1,
                            ge2, lfo1_out, lfo2_out, mw, at, br);
    static uint8_t last_nc = 0xFF;
    uint8_t target_nc = (uint8_t)(nc_val + 0.5f) & 127;
    if (target_nc != last_nc) {
      noise_update_coeffs(target_nc, SAMPLE_RATE);
      last_nc = target_nc;
    }

    float gt_val =
        omsk_apply_mod_fast(PARAM_GLIDE_TIME, (float)engine.glide_time, ge1,
                            ge2, lfo1_out, lfo2_out, mw, at, br);

    float f1t = omsk_apply_mod_fast(PARAM_FX1_TIME, (float)engine.fx1_time, ge1,
                                    ge2, lfo1_out, lfo2_out, mw, at, br);
    float f1f =
        omsk_apply_mod_fast(PARAM_FX1_FEEDBACK, (float)engine.fx1_feedback, ge1,
                            ge2, lfo1_out, lfo2_out, mw, at, br);
    float f1s = omsk_apply_mod_fast(PARAM_FX1_SPREAD, (float)engine.fx1_spread,
                                    ge1, ge2, lfo1_out, lfo2_out, mw, at, br);
    float f1m = omsk_apply_mod_fast(PARAM_FX1_MIX, (float)engine.fx1_mix, ge1,
                                    ge2, lfo1_out, lfo2_out, mw, at, br);
    engine.chorus.set_chorus_delay_time((uint8_t)(f1t + 0.5f));
    engine.chorus.set_chorus_feedback((uint8_t)(f1f + 0.5f));
    engine.chorus.set_chorus_depth((uint8_t)(f1s + 0.5f));
    engine.chorus.set_chorus_level((uint8_t)(f1m + 0.5f));

    float f2t = omsk_apply_mod_fast(PARAM_FX2_TIME, (float)engine.fx2_time, ge1,
                                    ge2, lfo1_out, lfo2_out, mw, at, br);
    float f2f =
        omsk_apply_mod_fast(PARAM_FX2_FEEDBACK, (float)engine.fx2_feedback, ge1,
                            ge2, lfo1_out, lfo2_out, mw, at, br);
    float f2x = omsk_apply_mod_fast(PARAM_FX2_MIX, (float)engine.fx2_mix, ge1,
                                    ge2, lfo1_out, lfo2_out, mw, at, br);
    engine.delay.set_delay_time((uint8_t)(f2t + 0.5f));
    engine.delay.set_delay_feedback((uint8_t)(f2f + 0.5f));
    engine.delay.set_delay_level((uint8_t)(f2x + 0.5f));

    float mg_val =
        omsk_apply_mod_fast(PARAM_AMP_GAIN, engine.master_gain_base, ge1, ge2,
                            lfo1_out, lfo2_out, mw, at, br);
    engine.master_gain = mg_val * (1.0f / 127.0f);
    if (engine.master_gain < 0.0f)
      engine.master_gain = 0.0f;
  }

  // 3. Fixed per-voice scaling (no dynamic AGC to avoid gain dips on new voice)
  // Each voice outputs at 1/MAX_VOICES scale so summing all 4 stays ≤ 1.0
  float master_v = engine.master_gain * (1.0f / MAX_VOICES);

  // // 3. Gain & Mix Normalization
  //   if ((engine.sample_counter & 31) == 0) {
  //     float total_potential = 0.0f;
  //     for (int i = 0; i < MAX_VOICES; i++) {
  //       if (engine.voices[i].active) {
  //         total_potential +=
  //             engine.voices[i].cached_velocity_gain *
  //             engine.voices[i].eg1.level;
  //       }
  //     }
  //     float agc = (total_potential > 0.5f) ? (2.0f / total_potential) : 1.0f;
  //     if (agc > 4.0f)
  //       agc = 4.0f;
  //     engine.target_gain = agc * engine.master_gain;
  //   }
  //   engine.current_gain += (engine.target_gain - engine.current_gain) *
  //   0.01f; float master_v = engine.current_gain;

  float noise_raw = noise_process_sample(noise_rand());
  uint32_t phase_offset = engine.mix_phase_offset;

  float filtered_sum_l = 0.0f;
  float filtered_sum_r = 0.0f;

  // 4. Voice Loop
  for (int i = 0; i < MAX_VOICES; i++) {
    OmskVoice *v = &engine.voices[i];
    if (!v->active)
      continue;

    // Envelopes
    omsk_eg_process(&v->eg1);
    omsk_eg_process(&v->eg2);
    float e1 = v->eg1.level;
    float e2 = v->eg2.level;

    bool just_stolen = false;
    // Voice termination / stealing
    if (v->is_stealing) {
      v->is_stealing = false;
      just_stolen = true;
      v->note = v->target_note;
      v->velocity = v->target_velocity;
      v->cached_velocity_gain = mix_volume_lut[v->velocity & 127];
      v->gate = true;
      v->sustain_hold = false;
      v->age = ++engine.global_age;
      v->eg1.state = OMSK_EG_ATTACK;
      v->eg1.level = 0.0f;
      v->eg1.sustain_level = (float)engine.eg1_sustain / 127.0f;
      v->eg1.attack_inc = omsk_eg_calc_inc(engine.eg1_attack);
      v->eg1.decay_inc = omsk_eg_calc_inc(engine.eg1_decay);
      v->eg1.release_inc = omsk_eg_calc_inc(engine.eg1_release);
      v->eg2.state = OMSK_EG_ATTACK;
      v->eg2.level = 0.0f;
      v->eg2.sustain_level = (float)engine.eg2_sustain / 127.0f;
      v->eg2.attack_inc = omsk_eg_calc_inc(engine.eg2_attack);
      v->eg2.decay_inc = omsk_eg_calc_inc(engine.eg2_decay);
      v->eg2.release_inc = omsk_eg_calc_inc(engine.eg2_release);
      v->vco1.freq = v->vco2.freq = midi_to_freq(v->note);
      update_vco_pitch(&v->vco1);
      update_vco_pitch(&v->vco2);
      v->vco1.current_pitch_mult = v->vco1.pitch_mult_base;
      v->vco2.current_pitch_mult = v->vco2.pitch_mult_base;
      // Reset phase, filter state and outputs to prevent click on stolen voice
      v->vco1.phase = 0;
      v->vco2.phase = 0;
      v->cached_s1_out = 0.0f;
      v->cached_s2_out = 0.0f;
      v->vcf1.reset_state();
      v->vcf2.reset_state();
      v->cached_g1 = mix_volume_lut[engine.mix_vco1 & 127];
      v->cached_g2 = mix_volume_lut[engine.mix_vco2 & 127];
      v->cached_g3 = mix_volume_lut[engine.mix_noise & 127];
    } else if (!v->gate && !v->sustain_hold && e1 <= 0.0001f && e2 <= 0.0001f) {
      v->active = false;
      continue;
    }

    // Mixer & Osc Modulation (Distributed updates)
    // force_update: on just_stolen transition OR on first process call
    // (steal_fade<0 sentinel)
    bool force_update = just_stolen || (v->steal_fade < 0.0f);
    if (force_update)
      v->steal_fade = 1.0f; // clear deferred-bake sentinel
    if (cycle == (uint8_t)(i + 1) || force_update) {
      uint8_t old_w1 = v->vco1.wave;
      uint8_t old_s1 = v->vco1.shape;
      int old_lvl1 = v->cached_level1;
      uint8_t old_w2 = v->vco2.wave;
      uint8_t old_s2 = v->vco2.shape;
      int old_lvl2 = v->cached_level2;

      float mv1 =
          omsk_apply_mod_fast(PARAM_MIX_VCO1_VOL, (float)engine.mix_vco1, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);
      float mv2 =
          omsk_apply_mod_fast(PARAM_MIX_VCO2_VOL, (float)engine.mix_vco2, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);
      float mns =
          omsk_apply_mod_fast(PARAM_MIX_NOISE_VOL, (float)engine.mix_noise, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);

      v->cached_g1 = mix_volume_lut[(uint8_t)(mv1 + 0.5f) & 127];
      v->cached_g2 = mix_volume_lut[(uint8_t)(mv2 + 0.5f) & 127];
      v->cached_g3 = mix_volume_lut[(uint8_t)(mns + 0.5f) & 127];

      float d1_val =
          omsk_apply_mod_fast(PARAM_VCO1_DETUNE, (float)engine.vco1_detune, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);
      float d2_val =
          omsk_apply_mod_fast(PARAM_VCO2_DETUNE, (float)engine.vco2_detune, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);
      float d1_f = detune_factor_lut[(uint8_t)(d1_val + 0.5f) & 127];
      float d2_f = detune_factor_lut[(uint8_t)(d2_val + 0.5f) & 127];

      float s1_val =
          omsk_apply_mod_fast(PARAM_VCO1_SHAPE, (float)engine.vco1_shape, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);
      float s2_val =
          omsk_apply_mod_fast(PARAM_VCO2_SHAPE, (float)engine.vco2_shape, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);
      v->cached_s1 = (uint8_t)(s1_val + 0.5f) & 127;
      v->cached_s2 = (uint8_t)(s2_val + 0.5f) & 127;
      v->vco1.shape = v->cached_s1;
      v->vco2.shape = v->cached_s2;

      float w1_val =
          omsk_apply_mod_fast(PARAM_VCO1_WAVE, (float)engine.vco1_wave, e1, e2,
                              lfo1_out, lfo2_out, mw, at, br);
      float w2_val =
          omsk_apply_mod_fast(PARAM_VCO2_WAVE, (float)engine.vco2_wave, e1, e2,
                              lfo1_out, lfo2_out, mw, at, br);
      v->vco1.wave = (uint8_t)(w1_val + 0.5f) & 127;
      v->vco2.wave = (uint8_t)(w2_val + 0.5f) & 127;

      float t1_val = omsk_apply_mod_fast(PARAM_VCO1_TRANSPOSE,
                                         (float)engine.vco1_transpose_raw, e1,
                                         e2, lfo1_out, lfo2_out, mw, at, br);
      float t2_val = omsk_apply_mod_fast(PARAM_VCO2_TRANSPOSE,
                                         (float)engine.vco2_transpose_raw, e1,
                                         e2, lfo1_out, lfo2_out, mw, at, br);
      v->vco1.transpose = ((int8_t)(t1_val + 0.5f) - 5) * 12;
      v->vco2.transpose = ((int8_t)(t2_val + 0.5f) - 5) * 12;
      update_vco_pitch(&v->vco1);
      update_vco_pitch(&v->vco2);

      // Update the active multiplier so pitch changes apply instantly
      v->vco1.current_pitch_mult = v->vco1.pitch_mult_base;
      v->vco2.current_pitch_mult = v->vco2.pitch_mult_base;

      // EG Parameter Modulation
      float eg1_a =
          omsk_apply_mod_fast(PARAM_EG1_ATTACK, (float)engine.eg1_attack, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);
      float eg1_d =
          omsk_apply_mod_fast(PARAM_EG1_DECAY, (float)engine.eg1_decay, e1, e2,
                              lfo1_out, lfo2_out, mw, at, br);
      float eg1_s =
          omsk_apply_mod_fast(PARAM_EG1_SUSTAIN, (float)engine.eg1_sustain, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);
      float eg1_r =
          omsk_apply_mod_fast(PARAM_EG1_RELEASE, (float)engine.eg1_release, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);

      v->eg1.attack_inc = omsk_eg_calc_inc((uint8_t)(eg1_a + 0.5f));
      v->eg1.decay_inc = omsk_eg_calc_inc((uint8_t)(eg1_d + 0.5f));
      v->eg1.sustain_level = ((uint8_t)(eg1_s + 0.5f) & 127) * (1.0f / 127.0f);
      v->eg1.release_inc = omsk_eg_calc_inc((uint8_t)(eg1_r + 0.5f));

      float eg2_a =
          omsk_apply_mod_fast(PARAM_EG2_ATTACK, (float)engine.eg2_attack, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);
      float eg2_d =
          omsk_apply_mod_fast(PARAM_EG2_DECAY, (float)engine.eg2_decay, e1, e2,
                              lfo1_out, lfo2_out, mw, at, br);
      float eg2_s =
          omsk_apply_mod_fast(PARAM_EG2_SUSTAIN, (float)engine.eg2_sustain, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);
      float eg2_r =
          omsk_apply_mod_fast(PARAM_EG2_RELEASE, (float)engine.eg2_release, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);

      v->eg2.attack_inc = omsk_eg_calc_inc((uint8_t)(eg2_a + 0.5f));
      v->eg2.decay_inc = omsk_eg_calc_inc((uint8_t)(eg2_d + 0.5f));
      v->eg2.sustain_level = ((uint8_t)(eg2_s + 0.5f) & 127) * (1.0f / 127.0f);
      v->eg2.release_inc = omsk_eg_calc_inc((uint8_t)(eg2_r + 0.5f));

      float base1 =
          v->vco1.freq * (v->vco1.current_pitch_mult / v->vco1.detune_factor);
      float base2 =
          v->vco2.freq * (v->vco2.current_pitch_mult / v->vco2.detune_factor);
      v->cached_f1 = base1 * d1_f * pitch_bend;
      v->cached_f2 = base2 * d2_f * pitch_bend;
      v->vco1.phase_inc = freq_to_inc(v->cached_f1);
      v->vco2.phase_inc = freq_to_inc(v->cached_f2);
      v->cached_level1 = get_mipmap_level(v->cached_f1);
      v->cached_level2 = get_mipmap_level(v->cached_f2);

      omsk_osc_update_strategy(&v->vco1, v->vco1.wave);
      omsk_osc_update_strategy(&v->vco2, v->vco2.wave);

      bool bake1 = (old_w1 != v->vco1.wave) || (old_s1 != v->vco1.shape) ||
                   (old_lvl1 != v->cached_level1) || just_stolen ||
                   force_update;
      bool bake2 = (old_w2 != v->vco2.wave) || (old_s2 != v->vco2.shape) ||
                   (old_lvl2 != v->cached_level2) || just_stolen ||
                   force_update;

      // Bake to RAM
      if (bake1)
        omsk_bake_osc_start(&v->vco1);
      if (bake2)
        omsk_bake_osc_start(&v->vco2);

      float kt =
          ((float)engine.vcf_key_track / 127.0f) * (float)((int)v->note - 60);
      float vcf1_val =
          omsk_apply_mod_fast(PARAM_VCF1_CUTOFF, (float)engine.vcf1_cutoff, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);
      vcf1_val += kt;
      if (vcf1_val < 0.0f)
        vcf1_val = 0.0f;
      if (vcf1_val > 127.0f)
        vcf1_val = 127.0f;
      float vcf1_res_val =
          omsk_apply_mod_fast(PARAM_VCF1_RES, (float)engine.vcf1_res, e1, e2,
                              lfo1_out, lfo2_out, mw, at, br);
      v->vcf1.set_cutoff((uint8_t)(vcf1_val + 0.5f));
      v->vcf1.set_resonance((uint8_t)(vcf1_res_val + 0.5f) & 127);
      v->vcf1.set_filter_mode(params.vcf1_type);
      v->vcf1.process_at_low_rate(0, 0, 0, 0);

      float vcf2_val =
          omsk_apply_mod_fast(PARAM_VCF2_CUTOFF, (float)engine.vcf2_cutoff, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);
      vcf2_val += kt;
      if (vcf2_val < 0.0f)
        vcf2_val = 0.0f;
      if (vcf2_val > 127.0f)
        vcf2_val = 127.0f;
      float vcf2_res_val =
          omsk_apply_mod_fast(PARAM_VCF2_RES, (float)engine.vcf2_res, e1, e2,
                              lfo1_out, lfo2_out, mw, at, br);
      v->vcf2.set_cutoff((uint8_t)(vcf2_val + 0.5f));
      v->vcf2.set_resonance((uint8_t)(vcf2_res_val + 0.5f) & 127);
      v->vcf2.set_filter_mode(params.vcf2_type);
      v->vcf2.process_at_low_rate(0, 0, 0, 0);

      float vcf1_m_val =
          omsk_apply_mod_fast(PARAM_VCF1_MIX, (float)engine.vcf1_mix, e1, e2,
                              lfo1_out, lfo2_out, mw, at, br);
      float vcf2_m_val =
          omsk_apply_mod_fast(PARAM_VCF2_MIX, (float)engine.vcf2_mix, e1, e2,
                              lfo1_out, lfo2_out, mw, at, br);
      v->cached_vcf1_mix = (uint8_t)(vcf1_m_val + 0.5f) * (1.0f / 127.0f);
      v->cached_vcf2_mix = (uint8_t)(vcf2_m_val + 0.5f) * (1.0f / 127.0f);

      float vcf1_d_val =
          omsk_apply_mod_fast(PARAM_VCF1_DRIVE, (float)engine.vcf1_drive, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);
      float vcf2_d_val =
          omsk_apply_mod_fast(PARAM_VCF2_DRIVE, (float)engine.vcf2_drive, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);
      v->cached_vcf1_drive = (uint8_t)(vcf1_d_val + 0.5f) * (1.0f / 127.0f);
      v->cached_vcf2_drive = (uint8_t)(vcf2_d_val + 0.5f) * (1.0f / 127.0f);

      if (v->glide_elapsed < 1.0f) {
        float gt_m =
            omsk_apply_mod_fast(PARAM_GLIDE_TIME, (float)engine.glide_time, e1,
                                e2, lfo1_out, lfo2_out, mw, at, br);
        float gs_m =
            omsk_apply_mod_fast(PARAM_GLIDE_SLOPE, (float)engine.glide_slope,
                                e1, e2, lfo1_out, lfo2_out, mw, at, br);
        // Cached values for the sample loop
        v->cached_f1 =
            gt_m; // Reusing fields as temp if needed, but better use local
        v->cached_f2 = gs_m;
      }
    }

    // Audio-Rate Modulation
    uint32_t p1 = v->vco1.phase;
    uint32_t p2 = v->vco2.phase;

    float s1_mod = v->cached_s1_out;
    float s2_mod = v->cached_s2_out;

    // Apply FM
    float fm_depth1 = engine.mod_depth1_f;
    float fm_depth2 = engine.mod_depth2_f;

    if (engine.mod_routing1 == 5)
      p2 += (uint32_t)(s1_mod * fm_depth1 * 429496729.0f);
    if (engine.mod_routing2 == 5)
      p1 += (uint32_t)(s2_mod * fm_depth2 * 429496729.0f);

    // Apply Sync
    bool vco1_sync = (v->vco1.phase < v->vco1.phase_inc);
    bool vco2_sync = (v->vco2.phase < v->vco2.phase_inc);
    if (engine.mod_routing1 == 1 && vco1_sync)
      p2 = p1;
    if (engine.mod_routing2 == 1 && vco2_sync)
      p1 = p2;

    bool need_vco1 = (v->cached_g1 > 1e-4f) || (engine.mod_routing1 != 0);
    bool need_vco2 = (v->cached_g2 > 1e-4f) || (engine.mod_routing2 != 0);
    bool need_noise = (v->cached_g3 > 1e-4f);

    // Generate waves: Fast RAM lookup with linear interpolation
    float s1 = 0.0f;
    if (need_vco1) {
      uint8_t i1 = (uint8_t)(p1 >> 24);
      int16_t v1_0 = v->vco1.baked_wave[v->vco1.active_bank][i1];
      int16_t v1_1 = v->vco1.baked_wave[v->vco1.active_bank][(i1 + 1) & 0xFF];
      float frac1 = (float)(p1 & 0x00FFFFFF) * (1.0f / 16777216.0f);
      s1 = (float)v1_0 + ((float)v1_1 - (float)v1_0) * frac1;
      s1 *= (1.0f / 32767.0f);
    }

    float s2 = 0.0f;
    if (need_vco2) {
      uint8_t i2 = (uint8_t)(p2 >> 24);
      int16_t v2_0 = v->vco2.baked_wave[v->vco2.active_bank][i2];
      int16_t v2_1 = v->vco2.baked_wave[v->vco2.active_bank][(i2 + 1) & 0xFF];
      float frac2 = (float)(p2 & 0x00FFFFFF) * (1.0f / 16777216.0f);
      s2 = (float)v2_0 + ((float)v2_1 - (float)v2_0) * frac2;
      s2 *= (1.0f / 32767.0f);
    }

    v->cached_s1_out = s1;
    v->cached_s2_out = s2;

    // Apply AM and RM
    if (engine.mod_routing1 == 3)
      s2 *= (1.0f + s1 * fm_depth1);
    if (engine.mod_routing1 == 7)
      s2 *= (s1 * fm_depth1);
    if (engine.mod_routing2 == 3)
      s1 *= (1.0f + s2 * fm_depth2);
    if (engine.mod_routing2 == 7)
      s1 *= (s2 * fm_depth2);

    // Apply to Noise
    float out_n = 0.0f;
    if (need_noise) {
      out_n = noise_raw;
      if (engine.mod_routing1 == 4)
        out_n *= (1.0f + s1 * fm_depth1);
      if (engine.mod_routing1 == 8)
        out_n *= (s1 * fm_depth1);
      if (engine.mod_routing2 == 4)
        out_n *= (1.0f + s2 * fm_depth2);
      if (engine.mod_routing2 == 8)
        out_n *= (s2 * fm_depth2);
    }

    v->vco1.phase += v->vco1.phase_inc;
    v->vco2.phase += v->vco2.phase_inc;

    float vcf1_in = 0.0f, vcf2_in = 0.0f, dry_in = 0.0f;
    float v_g1 = s1 * v->cached_g1;
    float v_g2 = s2 * v->cached_g2;
    float v_g3 = out_n * v->cached_g3;

    // Routing
    if (params.route_vco1 == ROUTE_VCF1 || params.route_vco1 == ROUTE_NONE)
      vcf1_in += v_g1;
    else if (params.route_vco1 == ROUTE_VCF2)
      vcf2_in += v_g1;
    else
      dry_in += v_g1;

    if (params.route_vco2 == ROUTE_VCF1)
      vcf1_in += v_g2;
    else if (params.route_vco2 == ROUTE_VCF2 || params.route_vco2 == ROUTE_NONE)
      vcf2_in += v_g2;
    else
      dry_in += v_g2;

    if (params.route_noise == ROUTE_VCF1)
      vcf1_in += v_g3;
    else if (params.route_noise == ROUTE_VCF2)
      vcf2_in += v_g3;
    else
      dry_in += v_g3;

    float out_vcf1 = vcf1_in;
    if (v->cached_vcf1_mix > 1e-4f) {
      float drive_gain = 1.0f + v->cached_vcf1_drive * 15.0f;
      int32_t in_fixed = (int32_t)(vcf1_in * drive_gain * 4194304.0f);
      float processed = (float)v->vcf1.process(in_fixed) * (1.0f / (drive_gain * 4194304.0f));
      out_vcf1 = vcf1_in + (processed - vcf1_in) * v->cached_vcf1_mix;
    }

    float out_vcf2 = vcf2_in;
    if (v->cached_vcf2_mix > 1e-4f) {
      float drive_gain = 1.0f + v->cached_vcf2_drive * 15.0f;
      int32_t in_fixed = (int32_t)(vcf2_in * drive_gain * 4194304.0f);
      float processed = (float)v->vcf2.process(in_fixed) * (1.0f / (drive_gain * 4194304.0f));
      out_vcf2 = vcf2_in + (processed - vcf2_in) * v->cached_vcf2_mix;
    }

    float voice_out =
        (dry_in + out_vcf1 + out_vcf2) * v->cached_velocity_gain * e1;

    // Glide interpolation
    if (v->glide_elapsed < 1.0f) {
      float gt_m =
          omsk_apply_mod_fast(PARAM_GLIDE_TIME, (float)engine.glide_time, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);
      float glide_rate = glide_time_lut[(uint8_t)(gt_m + 0.5f) & 127];
      v->glide_elapsed += glide_rate;
      if (v->glide_elapsed > 1.0f)
        v->glide_elapsed = 1.0f;

      float t = v->glide_elapsed;
      float gs_m =
          omsk_apply_mod_fast(PARAM_GLIDE_SLOPE, (float)engine.glide_slope, e1,
                              e2, lfo1_out, lfo2_out, mw, at, br);
      uint8_t slope = (uint8_t)(gs_m + 0.5f) & 127;
      if (slope != 64) {
        int idx = (int)(t * 255.0f);
        if (idx < 0)
          idx = 0;
        if (idx > 255)
          idx = 255;
        if (slope < 64) {
          float s_val = g_glide_slope_lut[idx];
          float amount = (float)(64 - slope) / 64.0f;
          t = t * (1.0f - amount) + s_val * amount;
        } else {
          float s_val = 1.0f - g_glide_slope_lut[255 - idx];
          float amount = (float)(slope - 64) / 63.0f;
          t = t * (1.0f - amount) + s_val * amount;
        }
      }
      float current_freq = v->freq_start + (v->freq_target - v->freq_start) * t;
      current_freq *= v->unison_detune_factor;
      v->vco1.freq = current_freq;
      v->vco2.freq = current_freq;
    } else {
      float current_freq = v->freq_target;
      current_freq *= v->unison_detune_factor;
      v->vco1.freq = v->vco2.freq = current_freq;
    }

    filtered_sum_l += voice_out * pl;
    filtered_sum_r += voice_out * pr;
  }

  // FX low rate processing
  if ((engine.sample_counter & 7) == 0) {
    if (params.fx2_mix > 0) engine.delay.process_at_low_rate(engine.sample_counter >> 3);
    if (params.fx1_mix > 0) engine.chorus.process_at_low_rate(engine.sample_counter >> 3);
  }

  // Asynchronous incremental bake processing for exactly ONE oscillator per
  // frame
  for (int i = 0; i < MAX_VOICES; i++) {
    if (engine.voices[i].vco1.bake_progress < 256) {
      omsk_bake_osc_step(&engine.voices[i].vco1);
      break;
    }
    if (engine.voices[i].vco2.bake_progress < 256) {
      omsk_bake_osc_step(&engine.voices[i].vco2);
      break;
    }
  }

  // FX: Delay (Mono)
  float mono_sum = (filtered_sum_l + filtered_sum_r) * 0.5f;
  // Scale down input to FX (2nd gain stage) to prevent internal 24 -> 16 bit
  // overflow
  int32_t fx_in = (int32_t)(mono_sum * 1048576.0f);
  int32_t delay_out = fx_in;
  if (params.fx2_mix > 0) {
    delay_out = engine.delay.process(fx_in);
  }

  // FX: Chorus (Stereo)
  int32_t chorus_out_r = delay_out;
  int32_t chorus_out_l = delay_out;
  if (params.fx1_mix > 0) {
    chorus_out_l = engine.chorus.process(delay_out, delay_out, chorus_out_r);
  }

  // Scale back up after effect (conversion from 24-bit fixed point)
  float final_l = (float)chorus_out_l * (1.0f / 1048576.0f);
  float final_r = (float)chorus_out_r * (1.0f / 1048576.0f);

  *out_l = omsk_soft_clip_fast(final_l * master_v);
  *out_r = omsk_soft_clip_fast(final_r * master_v);
}

void omsk_core_all_notes_off(void) {
  // if (!mutex_initialized) return;
  // mutex_enter_blocking(&engine_mutex);
  for (int i = 0; i < MAX_VOICES; i++) {
    engine.voices[i].active = false;
    engine.voices[i].gate = false;
  }
  // mutex_exit(&engine_mutex);
}

void omsk_core_pitch_bend(uint8_t lsb, uint8_t msb) {
  float semitones = midi_pitch_bend_to_semitones(lsb, msb, (float)engine.pitch_bend_range);

  // Smooth pitch bend using LUTs (avoid powf)
  float abs_semi = semitones < 0 ? -semitones : semitones;
  int8_t semi_idx = (int8_t)floorf(abs_semi);
  float frac = abs_semi - (float)semi_idx;
  int8_t frac_idx = (int8_t)(frac * 127.0f);

  float factor = get_transpose_factor(semi_idx) * pitch_fine_lut[frac_idx];
  if (semitones < 0) {
    engine.target_pitch_bend = 1.0f / (factor + 1e-9f);
  } else {
    engine.target_pitch_bend = factor;
  }

  // Pitch base is constant, audio loop applies target_pitch_bend smoothly
}

void omsk_core_set_pitch_bend_range(uint8_t semitones) {
  engine.pitch_bend_range = semitones;
}

void omsk_core_set_sustain(bool on) {
  engine.sustain_pedal = on;
  if (!on) {
    for (int i = 0; i < MAX_VOICES; i++) {
      if (engine.voices[i].sustain_hold) {
        engine.voices[i].sustain_hold = false;
        // If gate is also false (key up), then it was held by pedal
        if (!engine.voices[i].gate) {
          // Voice and env will naturally release now that gate is false and
          // sustain_hold is false (The process function uses !gate for release)
        }
      }
    }
  }
}
void omsk_core_set_modwheel(uint8_t value) {
  engine.modwheel = (float)value / 127.0f;
}

void omsk_core_set_aftertouch(uint8_t value) {
  engine.aftertouch = (float)value / 127.0f;
}

void omsk_core_set_breath(uint8_t value) {
  engine.breath = (float)value / 127.0f;
}

