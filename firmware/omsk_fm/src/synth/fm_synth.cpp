#include "fm_synth.h"
#include "dx7_tables.h"
#include "../sw_config.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
  int32_t level_q24;
  int32_t target_level_q24;
  uint32_t increment_q24;
  int32_t outlevel;
  uint8_t rate_scaling;
  uint8_t stage_index;
  bool rising;
  bool key_down;
  int32_t static_count;
} FmDx7EnvelopeState;

typedef struct {
  int32_t level_q24;
  int32_t target_level_q24;
  int32_t increment_q24;
  uint8_t stage_index;
  bool rising;
  bool key_down;
} FmPitchEnvelopeState;

typedef struct {
  uint32_t phase; // Q32
  uint32_t delta;
  uint8_t waveform;
  uint8_t randstate;
  bool sync;

  uint32_t delaystate;
  uint32_t delayinc;
  uint32_t delayinc2;
} FmLfoState;

typedef struct {
  uint32_t phase;
  uint32_t phase_step;
  int32_t gain_q24;
  int32_t target_pitch;
  int32_t porta_curpitch;
  FmDx7EnvelopeState env;
} FmOperatorState;

typedef struct {
  bool active;
  uint8_t note;
  uint8_t transposed_note;
  uint8_t velocity;
  uint32_t age;
  int32_t feedback_buffer[2];
  int32_t modulation_15;
  int32_t memory_15;
  FmPitchEnvelopeState pitch_env;
  FmOperatorState op[FM_SYNTH_OPERATOR_COUNT];
} FmVoice;

__attribute__((always_inline)) inline static int32_t fm_dx7_sin_lookup(uint32_t phase);
__attribute__((always_inline)) inline static int32_t fm_dx7_exp2_lookup(int32_t x);



static FmVoice g_voices[FM_SYNTH_VOICE_COUNT];
static uint32_t g_voice_age_counter = 0;
static uint32_t g_voice_steal_count = 0;
static uint32_t g_same_note_retrigger_count = 0;
static uint32_t g_output_clip_count = 0;

static uint8_t g_master_level = 90;
static uint8_t g_voice_level = 90;

FmPatch g_active_patch;
FmPatch g_presets[32];

bool g_portamento_enable = false;
uint8_t g_portamento_time = 0;
uint8_t g_mod_wheel_val = 0;

static FmLfoState g_lfo;



enum {
  FM_DX7_OUT_BUS_ONE = 1 << 0,
  FM_DX7_OUT_BUS_TWO = 1 << 1,
  FM_DX7_OUT_BUS_ADD = 1 << 2,
  FM_DX7_IN_BUS_ONE = 1 << 4,
  FM_DX7_IN_BUS_TWO = 1 << 5,
  FM_DX7_FB_IN = 1 << 6,
  FM_DX7_FB_OUT = 1 << 7,
};

typedef struct {
  uint8_t ops[FM_SYNTH_OPERATOR_COUNT];
} FmDx7Algorithm;

static const FmDx7Algorithm k_dx7_algorithms[32] = {
    {{0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}}, {{0x01, 0x11, 0x11, 0x14, 0xc1, 0x14}},
    {{0xc1, 0x11, 0x14, 0x01, 0x11, 0x14}}, {{0x41, 0x11, 0x94, 0x01, 0x11, 0x14}},
    {{0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}}, {{0x41, 0x94, 0x01, 0x14, 0x01, 0x14}},
    {{0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}}, {{0x01, 0x11, 0xc5, 0x14, 0x01, 0x14}},
    {{0x01, 0x11, 0x05, 0x14, 0xc1, 0x14}}, {{0x01, 0x05, 0x14, 0xc1, 0x11, 0x14}},
    {{0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}}, {{0x01, 0x05, 0x05, 0x14, 0xc1, 0x14}},
    {{0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}}, {{0xc1, 0x05, 0x11, 0x14, 0x01, 0x14}},
    {{0x01, 0x05, 0x11, 0x14, 0xc1, 0x14}}, {{0xc1, 0x11, 0x02, 0x25, 0x05, 0x14}},
    {{0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}}, {{0x01, 0x11, 0x11, 0xc5, 0x05, 0x14}},
    {{0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}}, {{0x01, 0x05, 0x14, 0xc1, 0x14, 0x14}},
    {{0x01, 0x14, 0x14, 0xc1, 0x14, 0x14}}, {{0xc1, 0x14, 0x14, 0x14, 0x01, 0x14}},
    {{0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}}, {{0xc1, 0x14, 0x14, 0x14, 0x04, 0x04}},
    {{0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}}, {{0xc1, 0x05, 0x14, 0x01, 0x14, 0x04}},
    {{0x01, 0x05, 0x14, 0xc1, 0x14, 0x04}}, {{0x04, 0xc1, 0x11, 0x14, 0x01, 0x14}},
    {{0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}}, {{0x04, 0xc1, 0x11, 0x14, 0x04, 0x04}},
    {{0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}}, {{0xc4, 0x04, 0x04, 0x04, 0x04, 0x04}},
};

static int fm_dx7_scale_output_level(int outlevel) {
  if (outlevel >= 20) {
    return 28 + outlevel;
  }
  return k_dx7_level_lut[outlevel];
}

static int fm_dx7_scale_velocity(int velocity, int sensitivity) {
  int clamped_velocity = velocity;
  if (clamped_velocity < 0) clamped_velocity = 0;
  if (clamped_velocity > 127) clamped_velocity = 127;
  int velocity_value = k_dx7_velocity_lut[clamped_velocity >> 1] - 239;
  return ((sensitivity * velocity_value + 7) >> 3) << 4;
}

static uint8_t fm_dx7_scale_rate(uint8_t note, uint8_t sensitivity) {
  int x = (int)note / 3 - 7;
  if (x < 0) x = 0;
  if (x > 31) x = 31;
  return (uint8_t)((sensitivity * x) >> 3);
}

static int fm_dx7_scale_curve(int group, int depth, int curve) {
  int scale;
  if (curve == 0 || curve == 3) {
    scale = (group * depth * 329) >> 12;
  } else {
    const int raw_exp = k_dx7_exp_scale_lut[group < 33 ? group : 32];
    scale = (raw_exp * depth * 329) >> 15;
  }
  if (curve < 2) scale = -scale;
  return scale;
}

static int fm_dx7_scale_level(uint8_t note, const FmOperatorPatch *patch) {
  const int offset = (int)note - (int)patch->break_point - 17;
  if (offset >= 0) {
    return fm_dx7_scale_curve((offset + 1) / 3, patch->right_depth, patch->right_curve);
  }
  return fm_dx7_scale_curve(-(offset - 1) / 3, patch->left_depth, patch->left_curve);
}

static int32_t fm_dx7_logfreq_round2semi(int32_t freq) {
  const int32_t base = 50857777;
  const int32_t step = 1398101;
  int32_t rem = (freq - base) % step;
  return freq - rem;
}

static void fm_dx7_pitchenv_advance(FmPitchEnvelopeState *env, const FmPatch *patch, uint8_t new_stage_index) {
  env->stage_index = new_stage_index;
  if (new_stage_index < 4u) {
    int newlevel = patch->pitch_eg_levels[new_stage_index];
    if (newlevel > 99) newlevel = 99;
    env->target_level_q24 = (int32_t)k_pitchenv_tab[newlevel] << 19;
    env->rising = env->target_level_q24 > env->level_q24;

    int rate = patch->pitch_eg_rates[new_stage_index];
    if (rate > 99) rate = 99;
    env->increment_q24 = (int32_t)k_pitchenv_rate[rate] * 525;
  }
}

static void fm_dx7_pitchenv_init(FmPitchEnvelopeState *env, const FmPatch *patch) {
  int start_level = patch->pitch_eg_levels[3];
  if (start_level > 99) start_level = 99;
  env->level_q24 = (int32_t)k_pitchenv_tab[start_level] << 19;
  env->key_down = true;
  fm_dx7_pitchenv_advance(env, patch, 0u);
}

static void fm_dx7_pitchenv_keydown(FmPitchEnvelopeState *env, const FmPatch *patch, bool key_down) {
  if (env->key_down == key_down) return;
  env->key_down = key_down;
  fm_dx7_pitchenv_advance(env, patch, key_down ? 0u : 3u);
}

static int32_t fm_dx7_pitchenv_get_sample(FmPitchEnvelopeState *env, const FmPatch *patch, size_t samples) {
  if (env->stage_index < 3u || (env->stage_index < 4u && !env->key_down)) {
    int32_t inc = (int32_t)(((int64_t)env->increment_q24 * (int32_t)samples) / 32);
    if (env->rising) {
      env->level_q24 += inc;
      if (env->level_q24 >= env->target_level_q24) {
        env->level_q24 = env->target_level_q24;
        fm_dx7_pitchenv_advance(env, patch, (uint8_t)(env->stage_index + 1u));
      }
    } else {
      env->level_q24 -= inc;
      if (env->level_q24 <= env->target_level_q24) {
        env->level_q24 = env->target_level_q24;
        fm_dx7_pitchenv_advance(env, patch, (uint8_t)(env->stage_index + 1u));
      }
    }
  }
  return env->level_q24;
}

static void fm_dx7_lfo_reset(FmLfoState *lfo, const FmPatch *patch) {
  int rate = patch->lfo_speed;
  if (rate > 99) rate = 99;

  double lfo_source = k_lfo_source_lut[rate];
  lfo->delta = (uint32_t)(lfo_source * 2958333.33);

  int a = 99 - patch->lfo_delay;
  if (a >= 99) {
    lfo->delayinc = ~0u;
    lfo->delayinc2 = ~0u;
  } else {
    if (a < 0) a = 0;
    a = (16 + (a & 15)) << (1 + (a >> 4));
    lfo->delayinc = 16794 * a;
    a &= 0xff80;
    if (a < 0x80) a = 0x80;
    lfo->delayinc2 = 16794 * a;
  }
  lfo->waveform = patch->lfo_waveform;
  lfo->sync = patch->lfo_sync != 0;
}

static void fm_dx7_lfo_keydown(FmLfoState *lfo) {
  if (lfo->sync) {
    lfo->phase = (1U << 31) - 1;
  }
  lfo->delaystate = 0;
}

static int32_t fm_dx7_lfo_get_sample(FmLfoState *lfo, size_t samples) {
  lfo->phase += (uint32_t)(((uint64_t)lfo->delta * samples) / 32);
  int32_t x;
  switch (lfo->waveform) {
    case 0:  // triangle
      x = (int32_t)(lfo->phase >> 7);
      x ^= -((int32_t)(lfo->phase >> 31));
      x &= (1 << 24) - 1;
      return x;
    case 1:  // sawtooth down
      return (int32_t)((~lfo->phase ^ (1U << 31)) >> 8);
    case 2:  // sawtooth up
      return (int32_t)((lfo->phase ^ (1U << 31)) >> 8);
    case 3:  // square
      return (int32_t)(((~lfo->phase) >> 7) & (1 << 24));
    case 4:  // sine
      return (1 << 23) + (fm_dx7_sin_lookup(lfo->phase >> 8) >> 1);
    case 5:  // s&h
      if (lfo->phase < lfo->delta) {
        lfo->randstate = (uint8_t)((lfo->randstate * 179 + 17) & 0xff);
      }
      x = lfo->randstate ^ 0x80;
      return (x + 1) << 16;
  }
  return 1 << 23;
}

static int32_t fm_dx7_lfo_get_delay(FmLfoState *lfo, size_t samples) {
  uint32_t delta = lfo->delaystate < (1U << 31) ? lfo->delayinc : lfo->delayinc2;
  uint32_t scaled_delta = (uint32_t)(((uint64_t)delta * samples) / 32);
  uint64_t d = ((uint64_t)lfo->delaystate) + scaled_delta;
  if (d > ~0u) {
    return 1 << 24;
  }
  lfo->delaystate = (uint32_t)d;
  if (d < (1U << 31)) {
    return 0;
  } else {
    return (int32_t)((d >> 7) & ((1 << 24) - 1));
  }
}

static int32_t fm_dx7_freqlut_lookup(int32_t logfreq) {
  int ix = (logfreq & 0xffffff) >> 14;
  int32_t y0 = k_dx7_freqlut[ix];
  int32_t y1 = k_dx7_freqlut[ix + 1];
  int lowbits = logfreq & 0x3fff;
  int32_t y = y0 + ((((int64_t)(y1 - y0) * (int32_t)lowbits)) >> 14);
  int hibits = logfreq >> 24;
  if (hibits >= 20) return y;
  int shift = 20 - hibits;
  if (shift >= 32) return 0;
  if (shift < 0) return y << (-shift);
  return y >> shift;
}

static void fm_dx7_env_advance(FmDx7EnvelopeState *env, const FmOperatorPatch *patch, uint8_t new_stage_index) {
  env->stage_index = new_stage_index;
  if (new_stage_index < 4u) {
    int actual_level = fm_dx7_scale_output_level(patch->levels[new_stage_index]) >> 1;
    actual_level = (actual_level << 6) + env->outlevel - 4256;
    if (actual_level < 16) actual_level = 16;

    env->target_level_q24 = actual_level << 16;
    env->rising = env->target_level_q24 > env->level_q24;

    int qrate = ((int)patch->rates[new_stage_index] * 41) >> 6;
    qrate += env->rate_scaling;
    if (qrate > 63) qrate = 63;

    uint32_t inc_for_64 = (uint32_t)((4 + (qrate & 3)) << (2 + (qrate >> 2) + 6));
    env->increment_q24 = (uint32_t)(((uint64_t)inc_for_64 * 30106) >> 16);

    if (env->target_level_q24 == env->level_q24 || (new_stage_index == 0u && patch->levels[0] == 0u)) {
      int static_rate = patch->rates[new_stage_index];
      static_rate += env->rate_scaling;
      if (static_rate > 99) static_rate = 99;
      int32_t count = static_rate < 77 ? k_dx7_statics_lut[static_rate] : 20 * (99 - static_rate);
      if (static_rate < 77 && (new_stage_index == 0u && patch->levels[0] == 0u)) {
        count /= 20;
      }
      env->static_count = (int32_t)(((int64_t)count * 48000) / 44100);
    } else {
      env->static_count = 0;
    }
  }
}

static void fm_dx7_env_init(FmDx7EnvelopeState *env, const FmOperatorPatch *patch, uint8_t note, uint8_t velocity) {
  int outlevel = fm_dx7_scale_output_level(patch->output_level);
  outlevel += fm_dx7_scale_level(note, patch);
  if (outlevel > 127) outlevel = 127;
  outlevel <<= 5;
  outlevel += fm_dx7_scale_velocity(velocity, patch->key_velocity_sensitivity);
  if (outlevel < 0) outlevel = 0;

  memset(env, 0, sizeof(*env));
  env->outlevel = outlevel;
  env->rate_scaling = fm_dx7_scale_rate(note, patch->rate_scaling);
  env->key_down = true;
  fm_dx7_env_advance(env, patch, 0u);
}

static void fm_dx7_env_keydown(FmDx7EnvelopeState *env, const FmOperatorPatch *patch, bool key_down) {
  if (env->key_down == key_down) return;
  env->key_down = key_down;
  fm_dx7_env_advance(env, patch, key_down ? 0u : 3u);
}

__attribute__((always_inline)) inline static int32_t fm_dx7_env_get_sample(FmDx7EnvelopeState *env, const FmOperatorPatch *patch, size_t samples) {
  if (env->static_count > 0) {
    env->static_count -= (int32_t)samples;
    if (env->static_count <= 0) {
      env->static_count = 0;
      fm_dx7_env_advance(env, patch, (uint8_t)(env->stage_index + 1u));
    }
  }

  if (env->stage_index < 3u || (env->stage_index < 4u && !env->key_down)) {
    if (env->static_count > 0) {
      // Do nothing, level remains constant during static delay
    } else if (env->rising) {
      const int32_t jump_target = 1716 << 16;
      if (env->level_q24 < jump_target) env->level_q24 = jump_target;
      env->level_q24 += (((17 << 24) - env->level_q24) >> 24) * (int32_t)env->increment_q24;
      if (env->level_q24 >= env->target_level_q24) {
        env->level_q24 = env->target_level_q24;
        fm_dx7_env_advance(env, patch, (uint8_t)(env->stage_index + 1u));
      }
    } else {
      env->level_q24 -= (int32_t)env->increment_q24;
      if (env->level_q24 <= env->target_level_q24) {
        env->level_q24 = env->target_level_q24;
        fm_dx7_env_advance(env, patch, (uint8_t)(env->stage_index + 1u));
      }
    }
  }
  return env->level_q24;
}

__attribute__((always_inline)) inline static bool fm_dx7_env_is_finished(const FmDx7EnvelopeState *env) {
  return env->stage_index >= 4u;
}

__attribute__((always_inline)) inline static int32_t fm_dx7_sin_lookup(uint32_t phase) {
  uint32_t index = (phase >> 14) & (FM_DX7_SIN_N_SAMPLES - 1);
  uint32_t lowbits = phase & 0x3FFF;
  int32_t dy = g_dx7_sin_lut[index << 1];
  int32_t y0 = g_dx7_sin_lut[(index << 1) + 1];
  return y0 + ((dy * (int32_t)lowbits) >> 14);
}

__attribute__((always_inline)) inline static int32_t fm_dx7_exp2_lookup(int32_t x) {
  int32_t x_shifted = x >> 24;
  int shift = 6 - x_shifted;
  if (shift >= 31) return 0;
  if (shift < 0) shift = 0;
  
  uint32_t index = ((uint32_t)x >> 14) & 1023;
  uint32_t lowbits = (uint32_t)x & 0x3FFF;
  int32_t dy = g_dx7_exp2_lut[index << 1];
  int32_t y0 = g_dx7_exp2_lut[(index << 1) + 1];
  int32_t y = y0 + ((dy * (int32_t)lowbits) >> 14);
  return y >> shift;
}

static void fm_synth_set_ratio_from_dx7(uint8_t coarse, uint8_t fine, uint8_t *ratio_num, uint8_t *ratio_den) {
  double ratio = coarse == 0u ? 0.5 : (double)coarse;
  ratio *= 1.0 + ((double)fine / 100.0);
  uint8_t denom = ratio < 1.0 ? 255u : (uint8_t)floor(255.0 / ratio);
  if (denom == 0u) denom = 1u;
  uint32_t numer = (uint32_t)lround(ratio * (double)denom);
  if (numer == 0u) numer = 1u;
  else if (numer > 255u) numer = 255u;
  *ratio_num = (uint8_t)numer;
  *ratio_den = denom;
}

void dx7_unpack_voice(const uint8_t *dx7_data, FmPatch *fm_patch) {
  fm_patch->algorithm = dx7_data[110] & 0x1Fu;
  fm_patch->feedback = dx7_data[111] & 0x07u;
  fm_patch->transpose = (int8_t)dx7_data[117] - 24;
  fm_patch->lfo_speed = dx7_data[112];
  fm_patch->lfo_delay = dx7_data[113];
  fm_patch->lfo_pitch_mod_depth = dx7_data[114];
  fm_patch->lfo_amp_mod_depth = dx7_data[115];
  fm_patch->lfo_sync = (dx7_data[116] >> 7) & 1;
  fm_patch->lfo_waveform = (dx7_data[116] >> 4) & 7;
  fm_patch->pitch_mod_sensitivity = dx7_data[116] & 7;

  for (int i = 0; i < 4; i++) {
    fm_patch->pitch_eg_rates[i] = dx7_data[102 + i] > 99u ? 99u : dx7_data[102 + i];
    fm_patch->pitch_eg_levels[i] = dx7_data[106 + i] > 99u ? 99u : dx7_data[106 + i];
  }

  memcpy(fm_patch->name, (const char*)&dx7_data[118], 10);
  fm_patch->name[10] = '\0';

  for (int op = 0; op < FM_SYNTH_OPERATOR_COUNT; op++) {
    const int base_offset = (5 - op) * 17; // OP6 is first in memory layout
    const uint8_t *op_data = &dx7_data[base_offset];
    const uint8_t curves = op_data[11];
    const uint8_t detune_rate = op_data[12];
    const uint8_t touch_mod = op_data[13];
    const uint8_t freq_mode = op_data[15];
    const uint8_t freq_coarse = (freq_mode >> 1) & 0x1Fu;
    const uint8_t freq_fine = op_data[16] <= 99u ? op_data[16] : 99u;
    uint8_t ratio_num = 1u;
    uint8_t ratio_den = 1u;

    fm_synth_set_ratio_from_dx7(freq_coarse, freq_fine, &ratio_num, &ratio_den);
    double ratio = freq_coarse == 0u ? 0.5 : (double)freq_coarse;
    ratio *= 1.0 + ((double)freq_fine / 100.0);

    fm_patch->op[op] = (FmOperatorPatch){
      .rates = {
        (uint8_t)(op_data[0] > 99u ? 99u : op_data[0]),
        (uint8_t)(op_data[1] > 99u ? 99u : op_data[1]),
        (uint8_t)(op_data[2] > 99u ? 99u : op_data[2]),
        (uint8_t)(op_data[3] > 99u ? 99u : op_data[3])
      },
      .levels = {
        (uint8_t)(op_data[4] > 99u ? 99u : op_data[4]),
        (uint8_t)(op_data[5] > 99u ? 99u : op_data[5]),
        (uint8_t)(op_data[6] > 99u ? 99u : op_data[6]),
        (uint8_t)(op_data[7] > 99u ? 99u : op_data[7])
      },
      .break_point = op_data[8],
      .left_depth = op_data[9],
      .right_depth = op_data[10],
      .left_curve = (uint8_t)(curves & 0x03u),
      .right_curve = (uint8_t)((curves >> 2) & 0x03u),
      .rate_scaling = (uint8_t)(detune_rate & 0x07u),
      .key_velocity_sensitivity = (uint8_t)((touch_mod >> 2) & 0x07u),
      .amp_mod_sens = (uint8_t)(touch_mod & 0x03u),
      .output_level = (uint8_t)(op_data[14] & 0x7F),
      .ratio_num = ratio_num,
      .ratio_den = ratio_den,
      .ratio = (float)ratio,
      .freq_coarse = freq_coarse,
      .freq_fine = freq_fine,
      .detune = (int8_t)((int8_t)((detune_rate >> 3) & 0x0Fu) - 7),
      .fixed_freq = (freq_mode & 0x01u) != 0u,
      .carrier = false,
      .active = true
    };
  }
  fm_dx7_lfo_reset(&g_lfo, fm_patch);
}

void dx7_unpack_unpacked_voice(const uint8_t *patch, FmPatch *fm_patch) {
  fm_patch->algorithm = patch[134] & 0x1Fu;
  fm_patch->feedback = patch[135] & 0x07u;
  fm_patch->transpose = (int8_t)patch[144] - 24;
  fm_patch->lfo_speed = patch[137];
  fm_patch->lfo_delay = patch[138];
  fm_patch->lfo_pitch_mod_depth = patch[139];
  fm_patch->lfo_amp_mod_depth = patch[140];
  fm_patch->lfo_sync = patch[141] & 1;
  fm_patch->lfo_waveform = patch[142] & 7;
  fm_patch->pitch_mod_sensitivity = patch[143] & 7;

  for (int i = 0; i < 4; i++) {
    fm_patch->pitch_eg_rates[i] = patch[126 + i] > 99u ? 99u : patch[126 + i];
    fm_patch->pitch_eg_levels[i] = patch[130 + i] > 99u ? 99u : patch[130 + i];
  }

  memcpy(fm_patch->name, (const char*)&patch[145], 10);
  fm_patch->name[10] = '\0';

  for (int op = 0; op < FM_SYNTH_OPERATOR_COUNT; op++) {
    const int off = (5 - op) * 21;
    const uint8_t *op_data = &patch[off];
    
    uint8_t ratio_num = 1u;
    uint8_t ratio_den = 1u;
    fm_synth_set_ratio_from_dx7(op_data[18], op_data[19], &ratio_num, &ratio_den);
    double ratio = op_data[18] == 0u ? 0.5 : (double)op_data[18];
    ratio *= 1.0 + ((double)op_data[19] / 100.0);

    fm_patch->op[op] = (FmOperatorPatch){
      .rates = {
        (uint8_t)(op_data[0] > 99u ? 99u : op_data[0]),
        (uint8_t)(op_data[1] > 99u ? 99u : op_data[1]),
        (uint8_t)(op_data[2] > 99u ? 99u : op_data[2]),
        (uint8_t)(op_data[3] > 99u ? 99u : op_data[3])
      },
      .levels = {
        (uint8_t)(op_data[4] > 99u ? 99u : op_data[4]),
        (uint8_t)(op_data[5] > 99u ? 99u : op_data[5]),
        (uint8_t)(op_data[6] > 99u ? 99u : op_data[6]),
        (uint8_t)(op_data[7] > 99u ? 99u : op_data[7])
      },
      .break_point = op_data[8],
      .left_depth = op_data[9],
      .right_depth = op_data[10],
      .left_curve = (uint8_t)(op_data[11] & 0x03u),
      .right_curve = (uint8_t)(op_data[12] & 0x03u),
      .rate_scaling = (uint8_t)(op_data[13] & 0x07u),
      .key_velocity_sensitivity = (uint8_t)(op_data[15] & 0x07u),
      .amp_mod_sens = (uint8_t)(op_data[14] & 0x03u),
      .output_level = (uint8_t)(op_data[16] & 0x7F),
      .ratio_num = ratio_num,
      .ratio_den = ratio_den,
      .ratio = (float)ratio,
      .freq_coarse = op_data[18],
      .freq_fine = op_data[19],
      .detune = (int8_t)((int8_t)(op_data[20] & 0x0Fu) - 7),
      .fixed_freq = op_data[17] != 0u,
      .carrier = false,
      .active = true
    };
  }
  fm_dx7_lfo_reset(&g_lfo, fm_patch);
}

void dx7_pack_voice(const FmPatch *patch, uint8_t *dx7_data) {
  memset(dx7_data, 0, 128);
  dx7_data[110] = patch->algorithm & 0x1F;
  dx7_data[111] = patch->feedback & 0x07;
  dx7_data[117] = (uint8_t)(patch->transpose + 24);
  dx7_data[112] = patch->lfo_speed;
  dx7_data[113] = patch->lfo_delay;
  dx7_data[114] = patch->lfo_pitch_mod_depth;
  dx7_data[115] = patch->lfo_amp_mod_depth;
  dx7_data[116] = (uint8_t)(((patch->lfo_sync & 1) << 7) | ((patch->lfo_waveform & 7) << 4) | (patch->pitch_mod_sensitivity & 7));

  for (int i = 0; i < 4; i++) {
    dx7_data[102 + i] = patch->pitch_eg_rates[i];
    dx7_data[106 + i] = patch->pitch_eg_levels[i];
  }
  memcpy(&dx7_data[118], patch->name, 10);

  for (int op = 0; op < FM_SYNTH_OPERATOR_COUNT; op++) {
    const int base_offset = (5 - op) * 17;
    uint8_t *op_data = &dx7_data[base_offset];
    const FmOperatorPatch *op_patch = &patch->op[op];

    op_data[0] = op_patch->rates[0];
    op_data[1] = op_patch->rates[1];
    op_data[2] = op_patch->rates[2];
    op_data[3] = op_patch->rates[3];

    op_data[4] = op_patch->levels[0];
    op_data[5] = op_patch->levels[1];
    op_data[6] = op_patch->levels[2];
    op_data[7] = op_patch->levels[3];

    op_data[8] = op_patch->break_point;
    op_data[9] = op_patch->left_depth;
    op_data[10] = op_patch->right_depth;
    op_data[11] = (uint8_t)((op_patch->left_curve & 3) | ((op_patch->right_curve & 3) << 2));
    op_data[12] = (uint8_t)((op_patch->rate_scaling & 7) | (((op_patch->detune + 7) & 0x0F) << 3));
    op_data[13] = (uint8_t)(((op_patch->key_velocity_sensitivity & 7) << 2) | (op_patch->amp_mod_sens & 3));
    op_data[14] = op_patch->output_level;
    op_data[15] = (uint8_t)((op_patch->fixed_freq ? 1 : 0) | ((op_patch->freq_coarse & 0x1F) << 1));
    op_data[16] = op_patch->freq_fine;
  }
}

static uint16_t g_pitch_bend = 8192;

static int32_t fm_synth_operator_target_pitch(uint8_t transposed_note, const FmOperatorPatch *patch) {
  int32_t target_pitch;
  if (patch->fixed_freq) {
    target_pitch = (4458616 * ((patch->freq_coarse & 3) * 100 + patch->freq_fine)) >> 3;
    target_pitch += patch->detune > 0 ? 13457 * patch->detune : 0;
  } else {
    target_pitch = 50857777 + transposed_note * 1398101;
    double logfreq_d = (double)target_pitch;
    double detuneRatio = 0.0209 * exp(-0.396 * (logfreq_d / 16777216.0)) / 7.0;
    int32_t delta_logfreq = (int32_t)(detuneRatio * logfreq_d * (double)patch->detune);
    target_pitch += delta_logfreq;
    target_pitch += k_coarsemul_lut[patch->freq_coarse & 31];
    if (patch->freq_fine != 0) {
      target_pitch += (int32_t)floor(24204406.323123 * log(1 + 0.01 * patch->freq_fine) + 0.5);
    }
  }
  return target_pitch;
}

void fm_synth_set_pitch_bend(uint16_t pb) {
  g_pitch_bend = pb;
}

void fm_synth_init(void) {
  dx7_tables_init(48000.0);

  // Set default patch: simple Init voice (BASIC PLUCK)
  memset(&g_active_patch, 0, sizeof(g_active_patch));
  g_active_patch.algorithm = 4; // Alg 5: Carriers at OP1, OP3, OP5
  g_active_patch.feedback = 6;
  g_active_patch.transpose = 24;
  strcpy(g_active_patch.name, "BASIC PLUCK");
  for (int i = 0; i < 4; i++) {
    g_active_patch.pitch_eg_rates[i] = 99;
    g_active_patch.pitch_eg_levels[i] = 50;
  }
  for (int op = 0; op < FM_SYNTH_OPERATOR_COUNT; op++) {
    g_active_patch.op[op].rates[0] = 99; g_active_patch.op[op].rates[1] = 60; g_active_patch.op[op].rates[2] = 0; g_active_patch.op[op].rates[3] = (op % 2 == 0) ? 20 : 60; // slower release for carriers
    g_active_patch.op[op].levels[0] = 99;
    g_active_patch.op[op].levels[1] = 75;
    g_active_patch.op[op].levels[2] = (op % 2 == 0) ? 99 : 75; // higher sustain for carriers
    g_active_patch.op[op].levels[3] = 0;
    g_active_patch.op[op].output_level = (op % 2 == 0) ? 99 : 0; // carriers at even indices
    g_active_patch.op[op].ratio_num = 1; g_active_patch.op[op].ratio_den = 1;
    g_active_patch.op[op].ratio = 1.0f;
    g_active_patch.op[op].freq_coarse = 1;
    g_active_patch.op[op].active = true;
    g_active_patch.op[op].key_velocity_sensitivity = 3;
  }
  // Setup carriers and modulators
  g_active_patch.op[0].output_level = 99; // OP1 (Carrier)
  g_active_patch.op[2].output_level = 99; // OP3 (Carrier)
  g_active_patch.op[4].output_level = 99; // OP5 (Carrier)
  g_active_patch.op[1].output_level = 80; // OP2 (Modulator)
  g_active_patch.op[1].freq_coarse = 1;

  g_active_patch.op[2].output_level = 99; // OP3 (Carrier)
  g_active_patch.op[3].output_level = 80; // OP4 (Modulator)
  g_active_patch.op[3].freq_coarse = 2;

  g_active_patch.op[4].output_level = 99; // OP5 (Carrier)
  g_active_patch.op[5].output_level = 80; // OP6 (Modulator with feedback)
  g_active_patch.op[5].freq_coarse = 4;

  // Presets initialization
  for (int i = 0; i < 32; i++) {
    g_presets[i] = g_active_patch;
  }

  memset(g_voices, 0, sizeof(g_voices));
}

void fm_synth_set_master_level(uint8_t level) {
  g_master_level = level;
}

uint8_t fm_synth_master_level(void) {
  return g_master_level;
}

void fm_synth_set_voice_level(uint8_t level) {
  g_voice_level = level;
}

uint8_t fm_synth_voice_level(void) {
  return g_voice_level;
}

void fm_synth_set_patch(uint8_t patch_id) {
  if (patch_id < 32) {
    g_active_patch = g_presets[patch_id];
    fm_dx7_lfo_reset(&g_lfo, &g_active_patch);
  }
}

void fm_synth_note_on(uint8_t note, uint8_t velocity) {
  if (note >= FM_NOTE_COUNT) return;

  int transposed_note = (int)note + (int)g_active_patch.transpose;
  if (transposed_note < 0) transposed_note = 0;
  if (transposed_note >= (int)FM_NOTE_COUNT) transposed_note = FM_NOTE_COUNT - 1u;

  // Retrigger if already exists
  for (int i = 0; i < FM_SYNTH_VOICE_COUNT; ++i) {
    if (g_voices[i].active && g_voices[i].note == note) {
      g_same_note_retrigger_count++;
      g_voices[i].velocity = velocity;
      fm_dx7_pitchenv_init(&g_voices[i].pitch_env, &g_active_patch);
      for (int op = 0; op < FM_SYNTH_OPERATOR_COUNT; ++op) {
        int32_t target = fm_synth_operator_target_pitch((uint8_t)transposed_note, &g_active_patch.op[op]);
        g_voices[i].op[op].target_pitch = target;
        g_voices[i].op[op].porta_curpitch = target;
        fm_dx7_env_init(&g_voices[i].op[op].env, &g_active_patch.op[op], (uint8_t)transposed_note, velocity);
      }
      return;
    }
  }

  // Allocate new voice
  FmVoice *v = NULL;
  for (int i = 0; i < FM_SYNTH_VOICE_COUNT; ++i) {
    if (!g_voices[i].active) {
      v = &g_voices[i];
      break;
    }
  }
  if (!v) {
    // Steal oldest active
    v = &g_voices[0];
    for (int i = 1; i < FM_SYNTH_VOICE_COUNT; ++i) {
      if (g_voices[i].age < v->age) v = &g_voices[i];
    }
    g_voice_steal_count++;
  }

  // Find the most recently triggered voice to copy portamento from
  int last_voice_idx = -1;
  uint32_t max_age = 0;
  for (int i = 0; i < FM_SYNTH_VOICE_COUNT; ++i) {
    if (g_voices[i].active && g_voices[i].age > max_age && &g_voices[i] != v) {
      max_age = g_voices[i].age;
      last_voice_idx = i;
    }
  }

  memset(v, 0, sizeof(*v));
  v->active = true;
  v->note = note;
  v->transposed_note = (uint8_t)transposed_note;
  v->velocity = velocity;
  v->age = ++g_voice_age_counter;

  fm_dx7_lfo_keydown(&g_lfo);
  fm_dx7_pitchenv_init(&v->pitch_env, &g_active_patch);

  for (int op = 0; op < FM_SYNTH_OPERATOR_COUNT; ++op) {
    int32_t target = fm_synth_operator_target_pitch((uint8_t)transposed_note, &g_active_patch.op[op]);
    v->op[op].target_pitch = target;
    if (g_portamento_enable && g_portamento_time > 0 && last_voice_idx != -1) {
      v->op[op].porta_curpitch = g_voices[last_voice_idx].op[op].porta_curpitch;
    } else {
      v->op[op].porta_curpitch = target;
    }
    fm_dx7_env_init(&v->op[op].env, &g_active_patch.op[op], (uint8_t)transposed_note, velocity);
  }
}

void fm_synth_note_off(uint8_t note) {
  for (int i = 0; i < FM_SYNTH_VOICE_COUNT; ++i) {
    if (g_voices[i].active && g_voices[i].note == note) {
      fm_dx7_pitchenv_keydown(&g_voices[i].pitch_env, &g_active_patch, false);
      for (int op = 0; op < FM_SYNTH_OPERATOR_COUNT; ++op) {
        fm_dx7_env_keydown(&g_voices[i].op[op].env, &g_active_patch.op[op], false);
      }
    }
  }
}

void fm_synth_all_notes_off(void) {
  for (int i = 0; i < FM_SYNTH_VOICE_COUNT; ++i) {
    if (g_voices[i].active) {
      fm_dx7_pitchenv_keydown(&g_voices[i].pitch_env, &g_active_patch, false);
      for (int op = 0; op < FM_SYNTH_OPERATOR_COUNT; ++op) {
        fm_dx7_env_keydown(&g_voices[i].op[op].env, &g_active_patch.op[op], false);
      }
    }
  }
}

void fm_synth_panic(void) {
  for (int i = 0; i < FM_SYNTH_VOICE_COUNT; ++i) {
    g_voices[i].active = false;
  }
}

// Emulate DX7 algorithmic operator configurations
static void fm_dx7_render_voice(FmVoice *v, int32_t *mix_buffer, size_t samples, int32_t lfo_val, int32_t lfo_delay) {
  if (!v->active) return;

  // ==== LFO AND PITCH MODULATION ====
  uint32_t pmd = (uint32_t)g_active_patch.lfo_pitch_mod_depth * lfo_delay; // Q32
  int32_t senslfo = (int32_t)g_active_patch.pitch_mod_sensitivity * (lfo_val - (1 << 23));
  int32_t pmod_1 = (int32_t)((((int64_t)pmd) * (int64_t)senslfo) >> 39);
  if (pmod_1 < 0) pmod_1 = -pmod_1;

  int32_t pmod_2 = (int32_t)((((int64_t)(g_mod_wheel_val << 7) * (int64_t)senslfo)) >> 14);
  if (pmod_2 < 0) pmod_2 = -pmod_2;

  int32_t pitch_mod_depth = pmod_1 > pmod_2 ? pmod_1 : pmod_2;
  int32_t pitch_eg_val = fm_dx7_pitchenv_get_sample(&v->pitch_env, &g_active_patch, samples);
  int32_t pitch_mod = pitch_eg_val + (pitch_mod_depth * (senslfo < 0 ? -1 : 1));

  int32_t pb = ((int32_t)g_pitch_bend - 8192);
  int32_t pitch_base = (pb * 1398101 * 2) / 8192;
  pitch_mod += pitch_base;

  // LFO Amplitude Modulation
  int32_t lfo_val_inv = (1 << 24) - lfo_val;
  uint32_t amod_1 = (uint32_t)((((int64_t)g_active_patch.lfo_amp_mod_depth * (int64_t)lfo_delay)) >> 8); // Q24
  amod_1 = (uint32_t)((((int64_t)amod_1 * (int64_t)lfo_val_inv)) >> 24);
  uint32_t amd_mod = amod_1;

  // Render envelopes and operators
  int32_t env_out[FM_SYNTH_OPERATOR_COUNT];
  for (int op = 0; op < FM_SYNTH_OPERATOR_COUNT; op++) {
    if (!g_active_patch.op[op].active) {
      env_out[op] = 0;
    } else {
      env_out[op] = fm_dx7_env_get_sample(&v->op[op].env, &g_active_patch.op[op], samples);
    }
  }

  // Active check
  bool finished = true;
  for (int op = 0; op < FM_SYNTH_OPERATOR_COUNT; op++) {
    if (g_active_patch.op[op].active && !fm_dx7_env_is_finished(&v->op[op].env)) {
      finished = false;
      break;
    }
  }
  if (finished) {
    v->active = false;
    return;
  }

  // Compute Portamento & final phase step for operators
  for (int op = 0; op < FM_SYNTH_OPERATOR_COUNT; op++) {
    if (!g_active_patch.op[op].active) {
      v->op[op].phase_step = 0;
      continue;
    }

    int32_t basepitch = v->op[op].porta_curpitch;
    int32_t targetpitch = v->op[op].target_pitch;

    if (!g_active_patch.op[op].fixed_freq) {
      if (v->op[op].porta_curpitch != targetpitch) {
        int32_t porta_rate;
        if (g_portamento_enable) {
          porta_rate = g_portamento_time > 0 ? g_porta_rates[g_portamento_time] : g_porta_rates[0];
        } else {
          porta_rate = g_porta_rates[0];
        }

        int32_t cur = v->op[op].porta_curpitch;
        bool going_up = cur < targetpitch;
        int32_t newpitch = cur + (going_up ? +porta_rate : -porta_rate);

        if ((going_up && newpitch > targetpitch) || (!going_up && newpitch < targetpitch)) {
          newpitch = targetpitch;
        }
        v->op[op].porta_curpitch = newpitch;
        basepitch = newpitch;
      }
    }

    int32_t final_pitch;
    if (g_active_patch.op[op].fixed_freq) {
      final_pitch = basepitch + pitch_base;
    } else {
      final_pitch = basepitch + pitch_mod;
    }
    v->op[op].phase_step = fm_dx7_freqlut_lookup(final_pitch);
  }

  const FmDx7Algorithm &alg = k_dx7_algorithms[g_active_patch.algorithm < 32 ? g_active_patch.algorithm : 0];

  int32_t target_gain[FM_SYNTH_OPERATOR_COUNT];
  int32_t dgain[FM_SYNTH_OPERATOR_COUNT];
  int32_t current_gain[FM_SYNTH_OPERATOR_COUNT];
  
  for (int op = 0; op < FM_SYNTH_OPERATOR_COUNT; op++) {
    if (!g_active_patch.op[op].active) {
      target_gain[op] = 0;
    } else {
      int32_t env_val = env_out[op];
      if (g_active_patch.op[op].amp_mod_sens != 0) {
        int32_t sensamp = (int32_t)(((uint64_t)amd_mod * g_active_patch.op[op].amp_mod_sens) >> 1); // Q24, slightly scaled
        env_val -= sensamp;
        if (env_val < 0) env_val = 0;
      }
      target_gain[op] = fm_dx7_exp2_lookup(env_val - (14 * (1 << 24)));
    }
    current_gain[op] = v->op[op].gain_q24;
    dgain[op] = (target_gain[op] - current_gain[op]) / (int32_t)samples;
  }

#if CFG_FM_YM21280_DECODE
  // Exact emulation of YM21280 OPS using the decoded ROM settings
  // Credit to Ken Shirriff for YM21280 decapping and John D. Haughton (picoX7)
  int32_t fdbk = (7 - g_active_patch.feedback) + 2;

  const uint8_t alg_idx = g_active_patch.algorithm < 32 ? g_active_patch.algorithm : 0;
  const OpStep *steps = k_ym21280_steps[alg_idx];

  int32_t modulation_15 = v->modulation_15;
  int32_t memory_15 = v->memory_15;
  int32_t feedback1_15 = v->feedback_buffer[0] >> 10; // Translate Q24 -> 15-bit (max 16384)
  int32_t feedback2_15 = v->feedback_buffer[1] >> 10;

  for (size_t s = 0; s < samples; s++) {
    // Interpolate gains
    for (int op = 0; op < FM_SYNTH_OPERATOR_COUNT; op++) {
      current_gain[op] += dgain[op];
    }

    for (int op_idx = 0; op_idx < FM_SYNTH_OPERATOR_COUNT; op_idx++) {
      // Hardware computation order is OP6..OP1 (op_idx 0 to 5)
      const int op = FM_SYNTH_OPERATOR_COUNT - 1 - op_idx;
      const OpStep &step = steps[op_idx];

      if (!g_active_patch.op[op].active) {
        v->op[op].phase += v->op[op].phase_step;
        if (step.a) {
          feedback2_15 = feedback1_15;
          feedback1_15 = 0;
        }
        continue;
      }

      // Step phase & lookup sine
      uint32_t phase_32 = v->op[op].phase;
      // Add modulation (translate 15-bit modulation input to phase offset, shifted left by 10 to match Q24 phase scale where 2^24 is 1 cycle)
      uint32_t phase_shifted = phase_32 + (modulation_15 << 10);
      
      int32_t raw_sin = fm_dx7_sin_lookup(phase_shifted); // Q24 sine output

      // Apply EG attenuation and Alg Level compensation
      // In picoX7: log_wave_14 += LOG2_COM << 7; 
      // This represents adding attenuation (quieter).
      // Shifting right by log2_com / 8 and interpolating fractional bits:
      int32_t op_gain = current_gain[op];
      if (step.log2_com > 0) {
        uint8_t int_shift = step.log2_com >> 3;
        uint8_t frac = step.log2_com & 7;
        op_gain >>= int_shift;
        if (frac > 0) {
          // Linear interpolation for fractional bit shift (1.0 to 0.5)
          // 0.5 ^ (frac/8) approximated:
          op_gain = (int32_t)(((int64_t)op_gain * k_frac_mul[frac]) >> 14);
        }
      }

      // Max product of raw_sin (Q24) and op_gain (Q24) is Q48.
      // We shift right by 34 to scale it to max 16384 (15-bit signed magnitude).
      int32_t output_15 = (int32_t)(((int64_t)raw_sin * op_gain) >> 34);

      // Mixing and routing
      int32_t sum_15 = 0;
      if (step.c) sum_15 = memory_15;
      if (step.d) sum_15 += output_15;

      switch(step.sel) {
        case 0: modulation_15 = 0; break;
        case 1: modulation_15 = output_15; break;
        case 2: modulation_15 = sum_15; break;
        case 3: modulation_15 = memory_15; break;
        case 4: modulation_15 = feedback1_15; break;
        case 5: modulation_15 = (feedback1_15 + feedback2_15) >> fdbk; break;
      }

      if (step.a) {
        feedback2_15 = feedback1_15;
        feedback1_15 = output_15;
      }

      if (step.c || step.d) {
        memory_15 = sum_15;
      }

      v->op[op].phase += v->op[op].phase_step;
    }

    // Output sample from final mixer step (sum_15 converted back to Q24)
    mix_buffer[s] += memory_15 << 10;
  }

  // Save persistent variables back to voice state
  v->modulation_15 = modulation_15;
  v->memory_15 = memory_15;
  v->feedback_buffer[0] = feedback1_15 << 10;
  v->feedback_buffer[1] = feedback2_15 << 10;
#else
  for (size_t s = 0; s < samples; s++) {
    int32_t buses[3] = {0};
    bool has_contents[3] = {false, false, false};
    int32_t feedback_shift = 8 - g_active_patch.feedback;
    if (g_active_patch.feedback == 0) feedback_shift = 16; // Feedback off

    for (int op = FM_SYNTH_OPERATOR_COUNT - 1; op >= 0; op--) {
      uint8_t flags = alg.ops[5 - op];
      int32_t modulation = 0;

      if (!g_active_patch.op[op].active) {
        v->op[op].phase += v->op[op].phase_step;
        int out_bus = flags & 3;
        bool add = (flags & FM_DX7_OUT_BUS_ADD) != 0;
        if (!add) {
          has_contents[out_bus] = false;
        }
        if (flags & FM_DX7_FB_OUT) {
          v->feedback_buffer[0] = 0;
          v->feedback_buffer[1] = 0;
        }
        continue;
      }

      int in_bus = (flags >> 4) & 3;
      if (in_bus > 0 && has_contents[in_bus]) {
        modulation = buses[in_bus];
      }
      if (flags & FM_DX7_FB_IN) {
        modulation += (v->feedback_buffer[0] + v->feedback_buffer[1]) >> (feedback_shift + 1);
      }

      uint32_t phase = v->op[op].phase + (uint32_t)modulation;
      int32_t raw_sin = fm_dx7_sin_lookup(phase);
      
      // Interpolate gain sample-by-sample
      current_gain[op] += dgain[op];
      int32_t gain = current_gain[op];
      int32_t sample = (int32_t)(((int64_t)raw_sin * gain) >> 24);

      int out_bus = flags & 3;
      bool add = (flags & FM_DX7_OUT_BUS_ADD) != 0;
      if (!has_contents[out_bus]) {
        add = false;
      }

      if (add) {
        buses[out_bus] += sample;
      } else {
        buses[out_bus] = sample;
      }
      has_contents[out_bus] = true;

      if (flags & FM_DX7_FB_OUT) {
        v->feedback_buffer[1] = v->feedback_buffer[0];
        v->feedback_buffer[0] = sample;
      }

      v->op[op].phase += v->op[op].phase_step;
    }

    if (has_contents[0]) {
      mix_buffer[s] += buses[0];
    }
  }
#endif

  // Set absolute target gains for next block to prevent drift
  for (int op = 0; op < FM_SYNTH_OPERATOR_COUNT; op++) {
    v->op[op].gain_q24 = target_gain[op];
  }
}

void fm_synth_render_block(int16_t *buffer, size_t samples) {
  int32_t mix_buf[256];
  if (samples > 256) samples = 256;
  memset(mix_buf, 0, sizeof(mix_buf));

  int32_t lfo_val = fm_dx7_lfo_get_sample(&g_lfo, samples);
  int32_t lfo_delay = fm_dx7_lfo_get_delay(&g_lfo, samples);

  for (int i = 0; i < FM_SYNTH_VOICE_COUNT; i++) {
    if (g_voices[i].active) {
      fm_dx7_render_voice(&g_voices[i], mix_buf, samples, lfo_val, lfo_delay);
    }
  }

  // Apply master level and voice levels with headroom scaling
  int32_t total_gain = (int32_t)g_master_level * (int32_t)g_voice_level;
  for (size_t s = 0; s < samples; s++) {
    int32_t val = (int32_t)(((int64_t)mix_buf[s] * total_gain) >> 26);
    if (val > 32767) {
      val = 32767;
      g_output_clip_count++;
    } else if (val < -32768) {
      val = -32768;
      g_output_clip_count++;
    }
    buffer[s] = (int16_t)val;
  }
}

void fm_synth_get_stats(FmSynthStats *stats) {
  stats->voice_steal_count = g_voice_steal_count;
  stats->same_note_retrigger_count = g_same_note_retrigger_count;
  stats->output_clip_count = g_output_clip_count;
  stats->active_voice_count = 0;
  for (int i = 0; i < FM_SYNTH_VOICE_COUNT; i++) {
    if (g_voices[i].active) stats->active_voice_count++;
  }
  stats->peak_active_voice_count = FM_SYNTH_VOICE_COUNT;
}

bool fm_synth_algo_has_feedback(int algo, int op) {
  if (algo < 0 || algo >= 32 || op < 0 || op >= 6) return false;
  return (k_dx7_algorithms[algo].ops[5 - op] & FM_DX7_FB_OUT) != 0;
}
