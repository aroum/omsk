#include "midi_map.h"
#include "../sw_config.h"
#include "../synth/pra_synth.h"
#include "../synth/synth.h"

#include "../ui/ui_state.h"
#include "../ui/ui_logic.h"
#if CFG_ENABLE_SEQUENCER
#include "../sequencer/sequencer.h"
#endif
#include <string.h>
#include <stdio.h>

// =============================================================================
// Precomputed LUT tables for debug output — replaces powf() calls.
// Safe: these are ROM constants, never computed at runtime.
// =============================================================================

// dbg_cutoff_hz_lut[v] = round(50 * 160^(v/127)), clamped to 8000
static const uint16_t dbg_cutoff_hz_lut[128] = {
  50, 52, 54, 56, 59, 61, 64, 66, 69, 72, 75, 78, 81,
  84, 87, 91, 95, 99, 103, 107, 111, 116, 120, 125, 130, 136, 141, 147, 153,
  159, 166, 173, 180, 187, 195, 202, 211, 219, 228, 238, 247, 257, 268, 279,
  290, 302, 314, 327, 340, 354, 369, 384, 399, 416, 433, 450, 469, 488, 508,
  528, 550, 572, 596, 620, 645, 672, 699, 727, 757, 788, 820, 853, 888, 924,
  962, 1001, 1042, 1085, 1129, 1175, 1223, 1273, 1325, 1379, 1435, 1493, 1554,
  1618, 1684, 1752, 1824, 1898, 1975, 2056, 2140, 2227, 2318, 2412, 2511,
  2613, 2720, 2830, 2946, 3066, 3191, 3321, 3456, 3597, 3744, 3897, 4056,
  4221, 4393, 4572, 4759, 4953, 5154, 5365, 5583, 5811, 6048, 6294, 6551,
  6818, 7096, 7385, 7687, 8000
};

// dbg_eg_ms_lut[v] = round(0.001 * 10^(v/127*4) * 1000) ms
static const uint16_t dbg_eg_ms_lut[128] = {
  1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,
  3, 4, 4, 4, 5, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 11,
  12, 13, 14, 15, 16, 17, 18, 20, 21, 23, 24, 26, 28, 30, 32, 35, 38, 40,
  43, 47, 50, 54, 58, 62, 67, 72, 78, 83, 90, 96, 104, 111, 120, 129, 139,
  149, 160, 172, 185, 199, 214, 230, 248, 266, 286, 308, 331, 356, 383, 411,
  442, 476, 511, 550, 591, 636, 683, 735, 790, 849, 913, 982, 1056, 1135,
  1221, 1313, 1411, 1517, 1632, 1754, 1886, 2028, 2181, 2345, 2521, 2711,
  2915, 3134, 3369, 3623, 3895, 4188, 4503, 4842, 5206, 5598, 6019, 6472,
  6959, 7482, 8045, 8650, 9300, 10000
};

// dbg_fx_ms_lut[v] = round(5 * 2^(v/127*7.64)) ms
static const uint16_t dbg_fx_ms_lut[128] = {
  5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10,
  10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 17, 17, 18, 19, 20, 21,
  22, 22, 23, 24, 25, 27, 28, 29, 30, 31, 33, 34, 35, 37, 39, 40, 42, 44,
  46, 48, 50, 52, 54, 56, 59, 61, 64, 66, 69, 72, 75, 78, 82, 85, 89, 93,
  97, 101, 105, 109, 114, 119, 124, 129, 135, 141, 146, 153, 159, 166, 173,
  180, 188, 196, 204, 213, 222, 232, 242, 252, 263, 274, 285, 298, 310, 324,
  337, 352, 367, 382, 399, 415, 433, 452, 471, 491, 512, 534, 556, 580, 605,
  630, 657, 685, 714, 745, 777, 810, 844, 880, 918, 957, 997
};

static uint8_t note_channel = 0;
static int8_t cc_to_param[128];
static uint8_t cc_channel[128];
static uint8_t *param_slots[PARAM_COUNT];
static uint8_t last_pad_cc;
static uint8_t last_pad_val;
static int8_t ext_enc_delta[8];
static uint8_t ext_enc_last_val[8];
static uint8_t param_to_cc[PARAM_COUNT];
static uint8_t modwheel_value;
static bool modwheel_changed;
static uint8_t aftertouch_value;
static bool aftertouch_changed;
static uint8_t breath_value;
static bool breath_changed;

static inline uint8_t v7_to_v8(uint8_t v) { return v; }

static inline uint8_t clamp_u8(uint8_t v, uint8_t lo, uint8_t hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static inline uint8_t clamp_param_value(ParamID pid, uint8_t v) {
  // Keep CC input consistent with UI ranges for enum-like parameters.
  switch (pid) {
  case PARAM_VCO1_TRANSPOSE:
  case PARAM_VCO2_TRANSPOSE:
    return clamp_u8(v, 0, 10);
  case PARAM_ARP_RATE:
    return clamp_u8(v, 0, 17);
  case PARAM_ARP_MODE:
    return clamp_u8(v, 0, 5);
  case PARAM_ARP_OCT:
    return clamp_u8(v, 0, 6);
  default:
    return v;
  }
}
// MIDI CC for 4x4 matrix
// 40 41 42 43
// 44 45 46 47
// 48 49 50 51
// 52 53 54 55
static const char *pid_module(ParamID p) {
  if (p <= PARAM_VCO1_SHAPE)
    return "VCO1";
  if (p >= PARAM_VCO2_TRANSPOSE && p <= PARAM_VCO2_SHAPE)
    return "VCO2";
  if (p >= PARAM_VCF1_CUTOFF && p <= PARAM_VCF1_MIX)
    return "VCF1";
  if (p >= PARAM_VCF2_CUTOFF && p <= PARAM_VCF2_MIX)
    return "VCF2";
  if (p >= PARAM_LFO1_RATE && p <= PARAM_LFO1_SHAPE)
    return "LFO1";
  if (p >= PARAM_LFO2_RATE && p <= PARAM_LFO2_SHAPE)
    return "LFO2";
  if (p >= PARAM_EG1_ATTACK && p <= PARAM_EG1_RELEASE)
    return "EG1";
  if (p >= PARAM_EG2_ATTACK && p <= PARAM_EG2_RELEASE)
    return "EG2";
  if (p >= PARAM_NOISE_COLOR && p <= PARAM_CHORD_MODE)
    return "NOISE";
  if (p >= PARAM_MIX_VCO1_VOL && p <= PARAM_MIX_NOISE_VOL)
    return "MIXER";
  if (p >= PARAM_ARP_RATE && p <= PARAM_ARP_OCT)
    return "ARP";
  if (p >= PARAM_GLIDE_POLY && p <= PARAM_GLIDE_MODE)
    return "GLIDE";
  if (p >= PARAM_FX1_TIME && p <= PARAM_FX1_MIX)
    return "FX1";
  if (p >= PARAM_FX2_TIME && p <= PARAM_FX2_MIX)
    return "FX2";
  if (p >= PARAM_LFO_DEPTH && p <= PARAM_AMP_VEL_SENS)
    return "SET";
  return "";
}

static const char *pid_label(ParamID p) {
  switch (p) {
  case PARAM_VCO1_TRANSPOSE:
  case PARAM_VCO2_TRANSPOSE:
    return "Trns";
  case PARAM_VCO1_DETUNE:
  case PARAM_VCO2_DETUNE:
    return "Detn";
  case PARAM_VCO1_WAVE:
  case PARAM_VCO2_WAVE:
    return "Wave";
  case PARAM_VCO1_SHAPE:
  case PARAM_VCO2_SHAPE:
    return "Shap";
  case PARAM_VCF1_CUTOFF:
  case PARAM_VCF2_CUTOFF:
    return "Cut";
  case PARAM_VCF1_RES:
  case PARAM_VCF2_RES:
    return "Res";
  case PARAM_VCF1_DRIVE:
  case PARAM_VCF2_DRIVE:
    return "Drv";
  case PARAM_VCF1_MIX:
  case PARAM_VCF2_MIX:
    return "Mix";
  case PARAM_LFO1_RATE:
  case PARAM_LFO2_RATE:
    return "Rate";
  case PARAM_LFO1_SMOOTH:
  case PARAM_LFO2_SMOOTH:
    return "Smth";
  case PARAM_LFO1_WAVE:
  case PARAM_LFO2_WAVE:
    return "Wave";
  case PARAM_LFO1_SHAPE:
  case PARAM_LFO2_SHAPE:
    return "Shap";
  case PARAM_EG1_ATTACK:
  case PARAM_EG2_ATTACK:
    return "Attk";
  case PARAM_EG1_DECAY:
  case PARAM_EG2_DECAY:
    return "Dcy";
  case PARAM_EG1_SUSTAIN:
  case PARAM_EG2_SUSTAIN:
    return "Sust";
  case PARAM_EG1_RELEASE:
  case PARAM_EG2_RELEASE:
    return "Rel";
  case PARAM_NOISE_COLOR:
    return "Color";
  case PARAM_CHORD_MODE:
    return "Chord";
  case PARAM_MIX_VCO1_VOL:
    return "VCO1";
  case PARAM_MIX_VCO2_VOL:
    return "VCO2";
  case PARAM_MIX_PHASE2:
    return "Phase2";
  case PARAM_MIX_NOISE_VOL:
    return "Noise";
  case PARAM_ARP_RATE:
    return "Rate";
  case PARAM_ARP_MODE:
    return "Mode";
  case PARAM_ARP_SWING:
    return "Swng";
  case PARAM_ARP_OCT:
    return "Oct";
  case PARAM_GLIDE_POLY:
    return "Poly";
  case PARAM_GLIDE_TIME:
    return "Time";
  case PARAM_GLIDE_SLOPE:
    return "Slope";
  case PARAM_GLIDE_MODE:
    return "Mode";
  case PARAM_FX1_TIME:
  case PARAM_FX2_TIME:
    return "Time";
  case PARAM_FX1_FEEDBACK:
  case PARAM_FX2_FEEDBACK:
    return "Feed";
  case PARAM_FX1_SPREAD:
    return "Sprd";
  case PARAM_FX1_MIX:
  case PARAM_FX2_MIX:
    return "Mix";
  case PARAM_FX2_TONE:
    return "Tone";
  case PARAM_VCF_KEY_TRACK:
  case PARAM_LFO_DEPTH:
    return "LfoD";
  case PARAM_LFO_FILTER_AMT:
    return "LfoF";
  case PARAM_LFO_OSC_AMT:
    return "LfoO";
  case PARAM_LFO_OSC_DST:
    return "LfoD";
  case PARAM_EG_OSC_AMT:
    return "EgO";
  case PARAM_EG_OSC_DST:
    return "EgD";
  case PARAM_SUB_OSC:
    return "Sub";
  case PARAM_AMP_GAIN:
    return "Gain";
  case PARAM_EG_AMP_MOD:
    return "EgA";
  case PARAM_PITCH_BEND_RANGE:
    return "PbR";
  case PARAM_VOICE_MODE:
    return "VMode";
  case PARAM_VOICE_ASSIGN_MODE:
    return "VAsn";
  case PARAM_PAN:
    return "Pan";
  case PARAM_EG_VEL_SENS:
    return "EgVel";
  case PARAM_AMP_VEL_SENS:
    return "AmVel";
  default:
    return "";
  }
}

static void dbg_print_param_midi(ParamID p, uint8_t v, uint8_t cc, uint8_t ch) {
#if CFG_ENABLE_DEBUG
  // NOTE: No powf() calls here — Core 0 shares FPU with Core 1 (audio).
  // All conversions use precomputed LUT tables instead.
  const char *mod = pid_module(p);
  const char *key = pid_label(p);

  if (p == PARAM_VCF1_CUTOFF || p == PARAM_VCF2_CUTOFF) {
    DBG_PRINTF("MIDI %s %s=%u (~%u Hz) [CC %u ch %u]\n", mod, key,
               (unsigned)v, (unsigned)dbg_cutoff_hz_lut[v],
               (unsigned)cc, (unsigned)ch);
    return;
  }
  if (p == PARAM_VCF1_RES || p == PARAM_VCF2_RES) {
    // Q = 0.5 + (v/127) * 11.5 — linear, safe
    unsigned q_int = (unsigned)(5 + (unsigned)v * 115 / 127);
    DBG_PRINTF("MIDI %s %s=%u (Q=%u.%u) [CC %u ch %u]\n", mod, key,
               (unsigned)v, q_int / 10, q_int % 10,
               (unsigned)cc, (unsigned)ch);
    return;
  }
  if (p == PARAM_VCF1_DRIVE || p == PARAM_VCF2_DRIVE) {
    // gain = 1 + (v/127)*9, *10 for one decimal
    unsigned g10 = 10 + (unsigned)v * 90 / 127;
    DBG_PRINTF("MIDI %s %s=%u (x%u.%u) [CC %u ch %u]\n", mod, key,
               (unsigned)v, g10 / 10, g10 % 10,
               (unsigned)cc, (unsigned)ch);
    return;
  }
  if (p == PARAM_LFO1_RATE || p == PARAM_LFO2_RATE) {
    // Hz = v/127 * 20, *100 for two decimals
    unsigned hz100 = (unsigned)v * 2000 / 127;
    DBG_PRINTF("MIDI %s %s=%u (%u.%02u Hz) [CC %u ch %u]\n", mod, key,
               (unsigned)v, hz100 / 100, hz100 % 100,
               (unsigned)cc, (unsigned)ch);
    return;
  }
  if (p == PARAM_EG1_ATTACK || p == PARAM_EG1_DECAY || p == PARAM_EG1_RELEASE ||
      p == PARAM_EG2_ATTACK || p == PARAM_EG2_DECAY || p == PARAM_EG2_RELEASE) {
    DBG_PRINTF("MIDI %s %s=%u (~%u ms) [CC %u ch %u]\n", mod, key,
               (unsigned)v, (unsigned)dbg_eg_ms_lut[v],
               (unsigned)cc, (unsigned)ch);
    return;
  }
  if (p == PARAM_MIX_PHASE2) {
    unsigned deg = (unsigned)v * 360 / 127;
    DBG_PRINTF("MIDI %s %s=%u (%u deg) [CC %u ch %u]\n", mod, key,
               (unsigned)v, deg, (unsigned)cc, (unsigned)ch);
    return;
  }
  if (p == PARAM_EG1_SUSTAIN || p == PARAM_EG2_SUSTAIN || p == PARAM_VCF1_MIX ||
      p == PARAM_VCF2_MIX || p == PARAM_MIX_VCO1_VOL ||
      p == PARAM_MIX_VCO2_VOL || p == PARAM_MIX_NOISE_VOL ||
      p == PARAM_FX1_MIX || p == PARAM_FX2_MIX || p == PARAM_LFO1_SMOOTH ||
      p == PARAM_LFO2_SMOOTH) {
    // 0..100%
    unsigned p10 = (unsigned)v * 1000 / 127;
    DBG_PRINTF("MIDI %s %s=%u (%u.%u%%) [CC %u ch %u]\n", mod, key, (unsigned)v,
               p10 / 10, p10 % 10, (unsigned)cc, (unsigned)ch);
    return;
  }
  if (p == PARAM_FX1_TIME) {
    // 0..127 -> 0..20.0ms
    unsigned t10 = (unsigned)v * 200 / 127;
    DBG_PRINTF("MIDI %s %s=%u (%u.%u ms) [CC %u ch %u]\n", mod, key, (unsigned)v,
               t10 / 10, t10 % 10, (unsigned)cc, (unsigned)ch);
    return;
  }
  if (p == PARAM_FX2_TIME) {
    // 0..127 -> 0..1000ms
    unsigned ms = (unsigned)v * 1000 / 127;
    DBG_PRINTF("MIDI %s %s=%u (%u ms) [CC %u ch %u]\n", mod, key, (unsigned)v, ms,
               (unsigned)cc, (unsigned)ch);
    return;
  }
  if (p == PARAM_ARP_RATE) {
    // period_us = 100000 + (127-v)*2000, hz*100 = 100000000/period_us
    unsigned period_us = 100000 + (unsigned)(127 - v) * 2000;
    unsigned hz100 = 100000000u / period_us;
    DBG_PRINTF("MIDI %s %s=%u (%u.%02u Hz) [CC %u ch %u]\n", mod, key,
               (unsigned)v, hz100 / 100, hz100 % 100,
               (unsigned)cc, (unsigned)ch);
    return;
  }
  if (p == PARAM_VCO1_TRANSPOSE || p == PARAM_VCO2_TRANSPOSE) {
    // oct = round(((v-64)/64)*5.0) — integer math
    int oct = (int)v - 64;
    // Scale: oct_rounded = round(oct/64 * 5)
    int oct5 = (oct * 5 + (oct >= 0 ? 32 : -32)) / 64;
    DBG_PRINTF("MIDI %s %s=%u (%+d oct) [CC %u ch %u]\n", mod, key,
               (unsigned)v, oct5, (unsigned)cc, (unsigned)ch);
    return;
  }
  if (p == PARAM_VCO1_DETUNE || p == PARAM_VCO2_DETUNE) {
    // Show raw value — detune_table is in src/tables but type is int16 cents
    int det = (int)v - 64;
    DBG_PRINTF("MIDI %s %s=%u (%+d raw) [CC %u ch %u]\n", mod, key,
               (unsigned)v, det, (unsigned)cc, (unsigned)ch);
    return;
  }
  DBG_PRINTF("MIDI %s %s=%u [CC %u ch %u]\n", mod, key, (unsigned)v,
             (unsigned)cc, (unsigned)ch);
#endif
}
static void bind_param_slots(void) {
  param_slots[PARAM_VCO1_TRANSPOSE] = &params.vco1_transpose;
  param_slots[PARAM_VCO1_DETUNE] = &params.vco1_detune;
  param_slots[PARAM_VCO1_WAVE] = &params.vco1_wave;
  param_slots[PARAM_VCO1_SHAPE] = &params.vco1_shape;

  param_slots[PARAM_VCO2_TRANSPOSE] = &params.vco2_transpose;
  param_slots[PARAM_VCO2_DETUNE] = &params.vco2_detune;
  param_slots[PARAM_VCO2_WAVE] = &params.vco2_wave;
  param_slots[PARAM_VCO2_SHAPE] = &params.vco2_shape;

  param_slots[PARAM_VCF1_CUTOFF] = &params.vcf1_cutoff;
  param_slots[PARAM_VCF1_RES] = &params.vcf1_res;
  param_slots[PARAM_VCF1_DRIVE] = &params.vcf1_drive;
  param_slots[PARAM_VCF1_MIX] = &params.vcf1_mix;

  param_slots[PARAM_VCF2_CUTOFF] = &params.vcf2_cutoff;
  param_slots[PARAM_VCF2_RES] = &params.vcf2_res;
  param_slots[PARAM_VCF2_DRIVE] = &params.vcf2_drive;
  param_slots[PARAM_VCF2_MIX] = &params.vcf2_mix;

  param_slots[PARAM_LFO1_RATE] = &params.lfo1_rate;
  param_slots[PARAM_LFO1_SMOOTH] = &params.lfo1_smooth;
  param_slots[PARAM_LFO1_WAVE] = &params.lfo1_wave;
  param_slots[PARAM_LFO1_SHAPE] = &params.lfo1_shape;

  param_slots[PARAM_LFO2_RATE] = &params.lfo2_rate;
  param_slots[PARAM_LFO2_SMOOTH] = &params.lfo2_smooth;
  param_slots[PARAM_LFO2_WAVE] = &params.lfo2_wave;
  param_slots[PARAM_LFO2_SHAPE] = &params.lfo2_shape;

  param_slots[PARAM_EG1_ATTACK] = &params.eg1_attack;
  param_slots[PARAM_EG1_DECAY] = &params.eg1_decay;
  param_slots[PARAM_EG1_SUSTAIN] = &params.eg1_sustain;
  param_slots[PARAM_EG1_RELEASE] = &params.eg1_release;

  param_slots[PARAM_EG2_ATTACK] = &params.eg2_attack;
  param_slots[PARAM_EG2_DECAY] = &params.eg2_decay;
  param_slots[PARAM_EG2_SUSTAIN] = &params.eg2_sustain;
  param_slots[PARAM_EG2_RELEASE] = &params.eg2_release;

  param_slots[PARAM_MIX_VCO1_VOL] = &params.mix_vco_balance;
  param_slots[PARAM_MIX_VCO2_VOL] = &params.mix_osc_noise;
  param_slots[PARAM_MIX_PHASE2] = &params.mix_phase2;
  param_slots[PARAM_MIX_NOISE_VOL] = &params.mix_master;

  param_slots[PARAM_NOISE_COLOR] = &params.noise_color;
  param_slots[PARAM_CHORD_MODE] = &params.chord_mode;

  param_slots[PARAM_ARP_RATE] = &params.arp_rate;
  param_slots[PARAM_ARP_MODE] = &params.arp_mode;
  param_slots[PARAM_ARP_SWING] = &params.arp_swing;
  param_slots[PARAM_ARP_OCT] = &params.arp_oct;

  param_slots[PARAM_GLIDE_POLY] = &params.glide_poly;
  param_slots[PARAM_GLIDE_TIME] = &params.glide_time;
  param_slots[PARAM_GLIDE_SLOPE] = &params.glide_slope;
  param_slots[PARAM_GLIDE_MODE] = &params.glide_mode;

  param_slots[PARAM_FX1_TIME] = &params.fx1_time;
  param_slots[PARAM_FX1_FEEDBACK] = &params.fx1_feedback;
  param_slots[PARAM_FX1_SPREAD] = &params.fx1_spread;
  param_slots[PARAM_FX1_MIX] = &params.fx1_mix;
  param_slots[PARAM_FX2_TIME] = &params.fx2_time;
  param_slots[PARAM_FX2_FEEDBACK] = &params.fx2_feedback;
  param_slots[PARAM_FX2_TONE] = &params.fx2_tone;
  param_slots[PARAM_FX2_MIX] = &params.fx2_mix;
  param_slots[PARAM_LFO_DEPTH] = &params.lfo_depth;
  param_slots[PARAM_LFO_FILTER_AMT] = &params.lfo_filter_amt;
  param_slots[PARAM_LFO_OSC_AMT] = &params.lfo_osc_amt;
  param_slots[PARAM_LFO_OSC_DST] = &params.lfo_osc_dst;
  param_slots[PARAM_EG_OSC_AMT] = &params.eg_osc_amt;
  param_slots[PARAM_EG_OSC_DST] = &params.eg_osc_dst;
  param_slots[PARAM_SUB_OSC] = &params.sub_osc;
  param_slots[PARAM_AMP_GAIN] = &params.amp_gain;
  param_slots[PARAM_EG_AMP_MOD] = &params.eg_amp_mod;
  param_slots[PARAM_PITCH_BEND_RANGE] = &params.pitch_bend_range;
  param_slots[PARAM_VOICE_MODE] = &params.voice_mode;
  param_slots[PARAM_VOICE_ASSIGN_MODE] = &params.voice_assign_mode;
  param_slots[PARAM_PAN] = &params.pan;
  param_slots[PARAM_EG_VEL_SENS] = &params.eg_vel_sens;
  param_slots[PARAM_AMP_VEL_SENS] = &params.amp_vel_sens;
}

void midi_map_init(void) {
  bind_param_slots();
  for (int i = 0; i < 128; i++) {
    cc_to_param[i] = -1;
    cc_channel[i] = 0xFF;
  }
  for (int i = 0; i < PARAM_COUNT; i++) {
    param_to_cc[i] = 0xFF;
  }
  last_pad_cc = 0;
  last_pad_val = 0;
  modwheel_value = 0;
  modwheel_changed = false;
  aftertouch_value = 0;
  aftertouch_changed = false;
  breath_value = 0;
  breath_changed = false;
  for (int i = 0; i < 8; i++) {
    ext_enc_delta[i] = 0;
    ext_enc_last_val[i] = 0xFF;
  }
  midi_map_set_note_channel(params.midi_channel);

  // VCO1
  midi_map_set_cc_mapping(20, 0xFF, PARAM_VCO1_TRANSPOSE);
  midi_map_set_cc_mapping(21, 0xFF, PARAM_VCO1_DETUNE);
  midi_map_set_cc_mapping(70, 0xFF, PARAM_VCO1_WAVE);
  midi_map_set_cc_mapping(22, 0xFF, PARAM_VCO1_SHAPE);

  // VCO2
  midi_map_set_cc_mapping(23, 0xFF, PARAM_VCO2_TRANSPOSE);
  midi_map_set_cc_mapping(24, 0xFF, PARAM_VCO2_DETUNE);
  midi_map_set_cc_mapping(25, 0xFF, PARAM_VCO2_WAVE);
  midi_map_set_cc_mapping(26, 0xFF, PARAM_VCO2_SHAPE);

  // VCF1
  midi_map_set_cc_mapping(74, 0xFF, PARAM_VCF1_CUTOFF);
  midi_map_set_cc_mapping(71, 0xFF, PARAM_VCF1_RES);
  midi_map_set_cc_mapping(75, 0xFF, PARAM_VCF_KEY_TRACK); // Updated to correct ParamID
  midi_map_set_cc_mapping(76, 0xFF, PARAM_VCF_EG_AMT);   // Updated to correct ParamID
  midi_map_set_cc_mapping(106, 0xFF, PARAM_VCF1_MIX);    // Added new CC for VCF1 Mix
  midi_map_set_cc_mapping(107, 0xFF, PARAM_VCF_MODE);    // Added new CC for VCF Mode

  // VCF2
  midi_map_set_cc_mapping(77, 0xFF, PARAM_VCF2_CUTOFF);
  midi_map_set_cc_mapping(78, 0xFF, PARAM_VCF2_RES);
  midi_map_set_cc_mapping(79, 0xFF, PARAM_VCF_KEY_TRACK); // Shared with VCF1 or separate? 
  midi_map_set_cc_mapping(27, 0xFF, PARAM_VCF2_MIX);

  // LFO1
  midi_map_set_cc_mapping(14, 0xFF, PARAM_LFO1_RATE);
  midi_map_set_cc_mapping(2, 0xFF, PARAM_LFO1_SMOOTH);
  midi_map_set_cc_mapping(28, 0xFF, PARAM_LFO1_WAVE);
  midi_map_set_cc_mapping(29, 0xFF, PARAM_LFO1_SHAPE);

  // LFO2
  midi_map_set_cc_mapping(30, 0xFF, PARAM_LFO2_RATE);
  midi_map_set_cc_mapping(31, 0xFF, PARAM_LFO2_SMOOTH);
  midi_map_set_cc_mapping(16, 0xFF, PARAM_LFO2_WAVE);
  midi_map_set_cc_mapping(17, 0xFF, PARAM_LFO2_SHAPE);

  // EG1
  midi_map_set_cc_mapping(73, 0xFF, PARAM_EG1_ATTACK);
  midi_map_set_cc_mapping(18, 0xFF, PARAM_EG1_DECAY);
  midi_map_set_cc_mapping(19, 0xFF, PARAM_EG1_SUSTAIN);
  midi_map_set_cc_mapping(72, 0xFF, PARAM_EG1_RELEASE);

  // EG2
  midi_map_set_cc_mapping(85, 0xFF, PARAM_EG2_ATTACK);
  midi_map_set_cc_mapping(86, 0xFF, PARAM_EG2_DECAY);
  midi_map_set_cc_mapping(87, 0xFF, PARAM_EG2_SUSTAIN);
  midi_map_set_cc_mapping(89, 0xFF, PARAM_EG2_RELEASE);

  // Mixer
  midi_map_set_cc_mapping(106, 0xFF, PARAM_MIX_VCO1_VOL);
  midi_map_set_cc_mapping(88, 0xFF, PARAM_MIX_PHASE2);
  midi_map_set_cc_mapping(90, 0xFF, PARAM_MIX_VCO2_VOL);
  midi_map_set_cc_mapping(11, 0xFF, PARAM_MIX_NOISE_VOL);

  // Noise / Global
  midi_map_set_cc_mapping(80, 0xFF, PARAM_NOISE_COLOR);
  midi_map_set_cc_mapping(101, 0xFF, PARAM_CHORD_MODE);

  // Arp
  midi_map_set_cc_mapping(81, 0xFF, PARAM_ARP_RATE);
  midi_map_set_cc_mapping(82, 0xFF, PARAM_ARP_MODE);
  midi_map_set_cc_mapping(83, 0xFF, PARAM_ARP_SWING);
  midi_map_set_cc_mapping(102, 0xFF, PARAM_ARP_OCT);

  // Glide
  midi_map_set_cc_mapping(103, 0xFF, PARAM_GLIDE_POLY);
  midi_map_set_cc_mapping(5, 0xFF, PARAM_GLIDE_TIME);
  midi_map_set_cc_mapping(104, 0xFF, PARAM_GLIDE_SLOPE);
  midi_map_set_cc_mapping(105, 0xFF, PARAM_GLIDE_MODE);

  // FX
  midi_map_set_cc_mapping(92, 0xFF, PARAM_FX2_TIME);
  midi_map_set_cc_mapping(93, 0xFF, PARAM_FX2_FEEDBACK);
  midi_map_set_cc_mapping(91, 0xFF, PARAM_FX2_MIX);
  midi_map_set_cc_mapping(94, 0xFF, PARAM_FX2_TONE);

  // Global / Modulation
  midi_map_set_cc_mapping(3, 0xFF, PARAM_LFO_DEPTH);
  midi_map_set_cc_mapping(12, 0xFF, PARAM_LFO_FILTER_AMT);
  midi_map_set_cc_mapping(13, 0xFF, PARAM_LFO_OSC_AMT);
  midi_map_set_cc_mapping(15, 0xFF, PARAM_EG_OSC_AMT);
  midi_map_set_cc_mapping(7, 0xFF, PARAM_AMP_GAIN);
  midi_map_set_cc_mapping(109, 0xFF, PARAM_VOICE_MODE);
  midi_map_set_cc_mapping(118, 0xFF, PARAM_PAN);

  // Interface Emulation (Pads & Encoders)
  // These map specific CCs to generic interface control targets
  midi_map_set_cc_mapping(40, 0xFF, PARAM_PAD_0);
  midi_map_set_cc_mapping(41, 0xFF, PARAM_PAD_1);
  midi_map_set_cc_mapping(42, 0xFF, PARAM_PAD_2);
  midi_map_set_cc_mapping(43, 0xFF, PARAM_PAD_3);
  midi_map_set_cc_mapping(44, 0xFF, PARAM_PAD_4);
  midi_map_set_cc_mapping(45, 0xFF, PARAM_PAD_5);
  midi_map_set_cc_mapping(46, 0xFF, PARAM_PAD_6);
  midi_map_set_cc_mapping(47, 0xFF, PARAM_PAD_7);
  midi_map_set_cc_mapping(48, 0xFF, PARAM_PAD_8);
  midi_map_set_cc_mapping(49, 0xFF, PARAM_PAD_9);
  midi_map_set_cc_mapping(50, 0xFF, PARAM_PAD_10);
  midi_map_set_cc_mapping(51, 0xFF, PARAM_PAD_11);
  midi_map_set_cc_mapping(52, 0xFF, PARAM_PAD_12);
  midi_map_set_cc_mapping(53, 0xFF, PARAM_PAD_13);
  midi_map_set_cc_mapping(54, 0xFF, PARAM_PAD_14);
  midi_map_set_cc_mapping(55, 0xFF, PARAM_PAD_15);

  midi_map_set_cc_mapping(110, 0xFF, PARAM_ENC_0);
  midi_map_set_cc_mapping(111, 0xFF, PARAM_ENC_1);
  midi_map_set_cc_mapping(112, 0xFF, PARAM_ENC_2);

  // Remaining Synth parameters
  midi_map_set_cc_mapping(9, 0xFF, PARAM_VCF1_DRIVE);
  midi_map_set_cc_mapping(56, 0xFF, PARAM_VCF2_DRIVE);
  midi_map_set_cc_mapping(57, 0xFF, PARAM_VCF1_MIX);

  // FX1 (Chorus)
  midi_map_set_cc_mapping(58, 0xFF, PARAM_FX1_TIME);
  midi_map_set_cc_mapping(59, 0xFF, PARAM_FX1_FEEDBACK);
  midi_map_set_cc_mapping(60, 0xFF, PARAM_FX1_SPREAD);
  midi_map_set_cc_mapping(61, 0xFF, PARAM_FX1_MIX);

  // Modulation Slots
  midi_map_set_cc_mapping(62, 0xFF, PARAM_MOD_ROUTING1);
  midi_map_set_cc_mapping(63, 0xFF, PARAM_MOD_DEPTH1);
  midi_map_set_cc_mapping(65, 0xFF, PARAM_MOD_ROUTING2);
  midi_map_set_cc_mapping(67, 0xFF, PARAM_MOD_DEPTH2);

  // Sub & Dest
  midi_map_set_cc_mapping(68, 0xFF, PARAM_SUB_OSC);
  midi_map_set_cc_mapping(69, 0xFF, PARAM_LFO_OSC_DST);
  midi_map_set_cc_mapping(79, 0xFF, PARAM_EG_OSC_DST);
  midi_map_set_cc_mapping(84, 0xFF, PARAM_EG_AMP_MOD);

  // Vel Sens
  midi_map_set_cc_mapping(95, 0xFF, PARAM_EG_VEL_SENS);
  midi_map_set_cc_mapping(96, 0xFF, PARAM_AMP_VEL_SENS);

  // EG Curves
  midi_map_set_cc_mapping(97, 0xFF, PARAM_EG1_ATTACK_CURVE);
  midi_map_set_cc_mapping(98, 0xFF, PARAM_EG1_DECAY_CURVE);
  midi_map_set_cc_mapping(99, 0xFF, PARAM_EG1_RELEASE_CURVE);
  midi_map_set_cc_mapping(100, 0xFF, PARAM_EG2_ATTACK_CURVE);
  midi_map_set_cc_mapping(104, 0xFF, PARAM_EG2_DECAY_CURVE);
  midi_map_set_cc_mapping(108, 0xFF, PARAM_EG2_RELEASE_CURVE);

  // ADV Sync & Key
  midi_map_set_cc_mapping(4, 0xFF, PARAM_ADV_SYNC_MODE);
  midi_map_set_cc_mapping(6, 0xFF, PARAM_ADV_SCALE_KEY);
  midi_map_set_cc_mapping(113, 0xFF, PARAM_ENC_3);
}

void midi_map_set_note_channel(uint8_t ch) { 
  note_channel = ch & 0x0F; 
  params.midi_channel = note_channel;
  pra_synth_set_midi_channel(note_channel);
}

void midi_map_set_cc_mapping(uint8_t cc, uint8_t channel, ParamID param) {
  if (cc < 128 && param < PARAM_COUNT) {
    cc_to_param[cc] = (int8_t)param;
    cc_channel[cc] = (channel <= 0x0F) ? channel : 0xFF;
    param_to_cc[param] = cc;
  }
}

void midi_map_clear_cc(uint8_t cc) {
  if (cc < 128) {
    cc_to_param[cc] = -1;
    cc_channel[cc] = 0xFF;
  }
}

uint8_t midi_map_get_cc_for_param(ParamID param) {
  if (param >= PARAM_COUNT)
    return 0xFF;
  return param_to_cc[param];
}

uint8_t midi_map_get_value_for_param(ParamID param) {
  if (param >= PARAM_COUNT)
    return 0;
  uint8_t *slot = param_slots[param];
  return slot ? *slot : 0;
}

uint8_t midi_map_get_note_channel(void) { return note_channel; }

void midi_map_consume_pad_event(uint8_t *cc, uint8_t *val) {
  if (cc) {
    *cc = last_pad_cc;
  }
  if (val) {
    *val = last_pad_val;
  }
  last_pad_cc = 0;
  last_pad_val = 0;
}

void midi_map_consume_encoder_deltas(int8_t out[8]) {
  if (!out) {
    for (int i = 0; i < 8; i++) {
      ext_enc_delta[i] = 0;
    }
    return;
  }
  for (int i = 0; i < 8; i++) {
    out[i] = ext_enc_delta[i];
    ext_enc_delta[i] = 0;
  }
}

bool midi_map_consume_modwheel(uint8_t *value) {
  if (!modwheel_changed) {
    return false;
  }
  modwheel_changed = false;
  if (value) {
    *value = modwheel_value;
  }
  return true;
}

bool midi_map_consume_aftertouch(uint8_t *value) {
  if (!aftertouch_changed) {
    return false;
  }
  aftertouch_changed = false;
  if (value) {
    *value = aftertouch_value;
  }
  return true;
}

bool midi_map_consume_breath(uint8_t *value) {
  if (!breath_changed) {
    return false;
  }
  breath_changed = false;
  if (value) {
    *value = breath_value;
  }
  return true;
}

void midi_map_process(uint8_t status, uint8_t d1, uint8_t d2) {
  uint8_t type = status & 0xF0;
  uint8_t ch = status & 0x0F;
  if (type == 0xE0) {
    if (ch == note_channel) {
      pra_synth_midi_pitch_bend(ch, d1 & 0x7F, d2 & 0x7F);
    }
    return;
  }
  if (type == 0xD0) {
    if (ch == note_channel) {
      aftertouch_value = d1 & 0x7F;
      aftertouch_changed = true;
      pra_synth_midi_aftertouch(ch, aftertouch_value);
    }
    return;
  }
  if (type == 0x90) {
    if (ch == note_channel) {
      if (d2) {
        ui_midi_note_on(d1, d2);
      } else {
        ui_midi_note_off(d1);
      }
    }
    return;
  }
  if (type == 0x80) {
    if (ch == note_channel) {
      ui_midi_note_off(d1);
    }
    return;
  }
  if (type == 0xB0) {
    if (ch != note_channel)
      return;
    uint8_t cc = d1 & 0x7F;
    uint8_t val7 = d2 & 0x7F;
    if (cc == 32) { // Preset Load
      if (val7 <= 15) {
        synth_preset_load(val7);
        char buf[32];
        snprintf(buf, sizeof(buf), "Preset loaded: Slot %d", val7 + 1);
        ui_set_status(buf, 2000);
      }
      return;
    }
    if (cc == 33) { // Preset Save
      if (val7 <= 15) {
        synth_preset_save(val7);
        char buf[32];
        snprintf(buf, sizeof(buf), "Preset saved: Slot %d", val7 + 1);
        ui_set_status(buf, 2000);
      }
      return;
    }
#if CFG_ENABLE_SEQUENCER
    if (cc == 34) { // Step Enable
      if (val7 <= 63) {
        current_seq.steps[val7].notes[0].enabled = true;
        if (current_seq.steps[val7].notes[0].note == 0) {
          current_seq.steps[val7].notes[0].note = 60; // Default C4
          current_seq.steps[val7].notes[0].velocity = 100;
          current_seq.steps[val7].notes[0].probability = 100;
        }
      }
      return;
    }
    if (cc == 35) { // Step Disable
      if (val7 <= 63) {
        current_seq.steps[val7].notes[0].enabled = false;
      }
      return;
    }
    if (cc == 36) { // Step Stop Set
      if (val7 <= 63) {
        current_seq.steps[val7].stop_flag = true;
      }
      return;
    }
    if (cc == 37) { // Step Stop Clear
      if (val7 <= 63) {
        current_seq.steps[val7].stop_flag = false;
      }
      return;
    }
    if (cc == 38) { // Sequence Save
      if (val7 <= 15) {
        seq_save_current(val7);
        char buf[32];
        snprintf(buf, sizeof(buf), "Seq saved: Slot %d", val7 + 1);
        ui_set_status(buf, 2000);
      }
      return;
    }
    if (cc == 39) { // Sequence Load
      if (val7 <= 15) {
        seq_load_sequence(val7);
        char buf[32];
        snprintf(buf, sizeof(buf), "Seq loaded: Slot %d", val7 + 1);
        ui_set_status(buf, 2000);
      }
      return;
    }
#endif

    if (cc == 66) { // Hold Mode
      pra_synth_set_hold_mode(val7 >= 64);
      if (val7 >= 64) ui_set_status("HOLD ON", 2000);
      else {
        ui_set_status("HOLD OFF", 2000);
        latched_keys = 0;
      }
      return;
    }
#if CFG_ENABLE_SEQUENCER
    if (cc == 114) { // Sequencer Start/Stop
      if (val7 >= 64) seq_start();
      else seq_stop();
      return;
    }
    if (cc == 115) { // Sequencer BPM
      uint8_t bpm = 30 + (val7 * 270) / 127;
      seq_set_bpm(bpm);
      return;
    }
    if (cc == 116) { // Sequencer Speed
      uint8_t speed = (val7 * 6) / 127;
      seq_set_speed((SeqSpeed)speed);
      return;
    }
    if (cc == 117) { // Sequencer Retrigger
      seq_set_retrigger(val7 >= 64);
      return;
    }
    if (cc == 119) { // Sequencer Play Mode
      uint8_t mode = (val7 * 5) / 127;
      current_seq.play_mode = (SeqMode)mode;
      return;
    }
#endif

    if (cc == 1 && ch == note_channel) {
      modwheel_value = d2 & 0x7F;
      modwheel_changed = true;
    }
    if (cc == 2 && ch == note_channel) {
      breath_value = d2 & 0x7F;
      breath_changed = true;
    }
    int8_t pid = cc_to_param[cc];

    // Handle Interface Emulation (Encoders)
    if (pid >= PARAM_ENC_0 && pid <= PARAM_ENC_3) {
      uint8_t idx = (uint8_t)(pid - PARAM_ENC_0);
      uint8_t val7 = d2 & 0x7F;
      int8_t step = 0;
      if (val7 == 63 || val7 == 127) {
        step = -1;
      } else if (val7 == 62 || val7 == 126) {
        step = -3;
      } else if (val7 == 65 || val7 == 1) {
        step = 1;
      } else if (val7 == 66 || val7 == 2) {
        step = 3;
      } else if (val7 == 0) {
        step = 0;
      } else {
        uint8_t last = ext_enc_last_val[idx];
        if (last != 0xFF) {
          int8_t delta = (int8_t)val7 - (int8_t)last;
          if (delta > 64)
            delta -= 128;
          if (delta < -64)
            delta += 128;
          if (delta > 3)
            delta = 3;
          if (delta < -3)
            delta = -3;
          step = delta;
        } else {
          step = 0;
        }
      }
      ext_enc_last_val[idx] = val7;
      ext_enc_delta[idx] += step;
      if (ext_enc_delta[idx] > 3)
        ext_enc_delta[idx] = 3;
      if (ext_enc_delta[idx] < -3)
        ext_enc_delta[idx] = -3;
      return;
    }

    // Handle Interface Emulation (Pads)
    if (pid >= PARAM_PAD_0 && pid <= PARAM_PAD_15) {
      uint8_t midx = (uint8_t)(pid - PARAM_PAD_0);
      if (d2 >= 64) {
          midi_pad_state |= (1 << midx);
      } else {
          midi_pad_state &= ~(1 << midx);
      }
      return;
    }

    if (ch == note_channel) {
      if (cc == 120 || cc == 123) {
        pra_synth_all_notes_off();
      } else {
        uint8_t val7 = d2 & 0x7F;
        uint8_t pra_cc = cc;
        if (cc == 106)
          pra_cc = 21; // MIXER_OSC_MIX: VCO1/VCO2 баланс
        if (cc == 88)
          pra_cc = 13; // LFO_OSC_AMT: фазовое/движение осцилляторов
        if (cc == 90)
          pra_cc = 104; // OSC_2_WAVE: от чистого осц до шума
        pra_synth_midi_control_change(ch, pra_cc, val7);
      }
    }

    if (pid >= 0 && pid < PARAM_PAD_0) { // Only handle standard parameters here
      uint8_t mch = cc_channel[cc];
      if (mch == 0xFF || mch == ch) {
        uint8_t v = clamp_param_value((ParamID)pid, v7_to_v8(d2 & 0x7F));

        if (set_button_held) {
          // MIDI SET MODE: Change Modulation Depth
          int src_idx = (set_context_src_override >= 0)
                            ? set_context_src_override
                            : get_mod_source_idx(set_context_module);

          // Allow selecting target module via MIDI CC while SET is held
          // If the CC corresponds to a parameter in a different module, we
          // should switch selected_module But here we are just receiving a
          // value change for a parameter. If the user wants to assign LFO1 to
          // VCO1.Trns:
          // 1. Hold SET (LFO1 is context)
          // 2. Select VCO1 (via pad or maybe just by touching param?)
          // 3. Move VCO1.Trns encoder/CC

          // When using physical encoders, 'selected_module' is updated by
          // button presses. When using MIDI CC, we know exactly which parameter
          // is being changed (pid). So we can assign modulation to 'pid'
          // regardless of 'selected_module'.

          if (src_idx >= 0) {
            params.mod_matrix[pid][src_idx] = v;

            // Log change
            int8_t depth = (int8_t)v - 64;
            float pct = (depth * 100.0f) / 64.0f;
            DBG_PRINTF("MOD_ASSIGN (MIDI) %s -> %s = %.0f%%\n",
                       module_name(set_context_module), pid_label((ParamID)pid),
                       pct);
          }
          return;
        }

        uint8_t *slot = param_slots[pid];
        if (slot) {
          uint8_t before = *slot;
          if (before != v) {
            *slot = v;
            pra_synth_param_change((ParamID)pid, v);
#if CFG_ENABLE_DEBUG
            dbg_print_param_midi((ParamID)pid, v, cc, ch);
#endif
          }
        }
      }
    }
    return;
  }
}
uint8_t *midi_map_get_param_ptr(ParamID pid) {
  if (pid >= PARAM_COUNT)
    return NULL;
  return param_slots[pid];
}
