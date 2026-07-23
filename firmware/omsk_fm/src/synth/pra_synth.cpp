#include "pra_synth.h"
#include "fm_synth.h"
#include "../ui/ui_state.h"
#include <string.h>

static bool hold_mode_active = false;

extern "C" {

void pra_synth_init(void) {
  fm_synth_init();
}

int16_t pra_synth_get_sample(void) {
  int16_t out_sample = 0;
  fm_synth_render_block(&out_sample, 1);
  return out_sample;
}

void pra_synth_get_stereo(int16_t *left, int16_t *right) {
  int16_t sample = pra_synth_get_sample();
  if (left) *left = sample;
  if (right) *right = sample;
}

void pra_synth_note_on(uint8_t note) {
  fm_synth_note_on(note, 127);
}

void pra_synth_note_off(uint8_t note) {
  fm_synth_note_off(note);
}

void pra_synth_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
  (void)channel;
  fm_synth_note_on(note, velocity);
}

void pra_synth_midi_note_off(uint8_t channel, uint8_t note, uint8_t velocity) {
  (void)channel;
  (void)velocity;
  fm_synth_note_off(note);
}

void pra_synth_midi_pitch_bend(uint8_t channel, uint8_t lsb, uint8_t msb) {
  (void)channel;
  uint16_t val = (msb << 7) | lsb;
  fm_synth_set_pitch_bend(val);
}

void pra_synth_midi_aftertouch(uint8_t channel, uint8_t pressure) {
  (void)channel;
  (void)pressure;
}

void pra_synth_midi_control_change(uint8_t channel, uint8_t control_number, uint8_t value) {
  (void)channel;
  (void)control_number;
  (void)value;
}

void pra_synth_all_notes_off(void) {
  fm_synth_all_notes_off();
}

void pra_synth_update_control(void) {
  // Envelopes and LFO stepping
}

void pra_synth_param_change(ParamID param, uint16_t value) {
  FmOperatorPatch &op = g_active_patch.op[active_op];
  switch (param) {
    // MOD_FREQ
    case PARAM_VCO1_TRANSPOSE:
      op.fixed_freq = (value > 0);
      break;
    case PARAM_VCO1_DETUNE:
      op.freq_coarse = value;
      break;
    case PARAM_VCO1_WAVE:
      op.freq_fine = value;
      break;
    case PARAM_VCO1_SHAPE:
      op.detune = (int)value - 7;
      break;

    // MOD_LVL_MOD
    case PARAM_VCO2_TRANSPOSE:
      op.output_level = value;
      break;
    case PARAM_VCO2_DETUNE:
      op.key_velocity_sensitivity = value;
      break;
    case PARAM_VCO2_WAVE:
      op.rate_scaling = value;
      break;

    // MOD_LFO
    case PARAM_LFO1_RATE:
      g_active_patch.lfo_waveform = value;
      break;
    case PARAM_LFO1_SMOOTH:
      g_active_patch.lfo_speed = value;
      break;
    case PARAM_LFO1_WAVE:
      g_active_patch.lfo_delay = value;
      break;
    case PARAM_LFO1_SHAPE:
      g_active_patch.pitch_mod_sensitivity = value;
      break;
    case PARAM_LFO2_RATE:
      g_active_patch.lfo_pitch_mod_depth = value;
      break;
    case PARAM_LFO2_SMOOTH:
      g_active_patch.lfo_amp_mod_depth = value;
      break;
    case PARAM_LFO2_WAVE:
      g_active_patch.lfo_sync = (value > 0);
      break;

    // MOD_EG
    case PARAM_EG1_ATTACK:
      op.levels[0] = value;
      break;
    case PARAM_EG1_DECAY:
      op.levels[1] = value;
      break;
    case PARAM_EG1_SUSTAIN:
      op.levels[2] = value;
      break;
    case PARAM_EG1_RELEASE:
      op.levels[3] = value;
      break;
    case PARAM_EG2_ATTACK:
      op.rates[0] = value;
      break;
    case PARAM_EG2_DECAY:
      op.rates[1] = value;
      break;
    case PARAM_EG2_SUSTAIN:
      op.rates[2] = value;
      break;
    case PARAM_EG2_RELEASE:
      op.rates[3] = value;
      break;

    // MOD_KBDSCALE
    case PARAM_GLIDE_TIME:
      op.left_curve = value;
      break;
    case PARAM_GLIDE_SLOPE:
      op.left_depth = value;
      break;
    case PARAM_GLIDE_MODE:
      op.break_point = value;
      break;
    case PARAM_FX1_TIME:
      op.right_curve = value;
      break;
    case PARAM_FX1_FEEDBACK:
      op.right_depth = value;
      break;

    // MOD_FILT
    case PARAM_VCF1_CUTOFF:
      break;
    case PARAM_VCF1_RES:
      break;

    // MOD_ALGO_FB
    case PARAM_MIX_VCO1_VOL:
      g_active_patch.algorithm = value;
      break;
    case PARAM_MIX_VCO2_VOL:
      g_active_patch.feedback = value;
      break;
    case PARAM_MIX_PHASE2:
      g_active_patch.transpose = (int)value - 24;
      break;

    // MOD_PITCH_EG
    case PARAM_FX2_TIME:
      g_active_patch.pitch_eg_levels[0] = value;
      break;
    case PARAM_FX2_FEEDBACK:
      g_active_patch.pitch_eg_levels[1] = value;
      break;
    case PARAM_FX2_TONE:
      g_active_patch.pitch_eg_levels[2] = value;
      break;
    case PARAM_FX2_MIX:
      g_active_patch.pitch_eg_levels[3] = value;
      break;
    case PARAM_MOD_ROUTING1:
      g_active_patch.pitch_eg_rates[0] = value;
      break;
    case PARAM_MOD_DEPTH1:
      g_active_patch.pitch_eg_rates[1] = value;
      break;
    case PARAM_MOD_ROUTING2:
      g_active_patch.pitch_eg_rates[2] = value;
      break;
    case PARAM_MOD_DEPTH2:
      g_active_patch.pitch_eg_rates[3] = value;
      break;

    default:
      break;
  }
}

uint16_t pra_synth_get_modulated_param_value(ParamID param) {
  FmOperatorPatch &op = g_active_patch.op[active_op];
  switch (param) {
    case PARAM_VCO1_TRANSPOSE: return op.fixed_freq ? 1 : 0;
    case PARAM_VCO1_DETUNE: return op.freq_coarse;
    case PARAM_VCO1_WAVE: return op.freq_fine;
    case PARAM_VCO1_SHAPE: return op.detune + 7;

    case PARAM_VCO2_TRANSPOSE: return op.output_level;
    case PARAM_VCO2_DETUNE: return op.key_velocity_sensitivity;
    case PARAM_VCO2_WAVE: return op.rate_scaling;

    case PARAM_LFO1_RATE: return g_active_patch.lfo_waveform;
    case PARAM_LFO1_SMOOTH: return g_active_patch.lfo_speed;
    case PARAM_LFO1_WAVE: return g_active_patch.lfo_delay;
    case PARAM_LFO1_SHAPE: return g_active_patch.pitch_mod_sensitivity;
    case PARAM_LFO2_RATE: return g_active_patch.lfo_pitch_mod_depth;
    case PARAM_LFO2_SMOOTH: return g_active_patch.lfo_amp_mod_depth;
    case PARAM_LFO2_WAVE: return g_active_patch.lfo_sync ? 1 : 0;

    case PARAM_EG1_ATTACK: return op.levels[0];
    case PARAM_EG1_DECAY: return op.levels[1];
    case PARAM_EG1_SUSTAIN: return op.levels[2];
    case PARAM_EG1_RELEASE: return op.levels[3];
    case PARAM_EG2_ATTACK: return op.rates[0];
    case PARAM_EG2_DECAY: return op.rates[1];
    case PARAM_EG2_SUSTAIN: return op.rates[2];
    case PARAM_EG2_RELEASE: return op.rates[3];

    case PARAM_GLIDE_TIME: return op.left_curve;
    case PARAM_GLIDE_SLOPE: return op.left_depth;
    case PARAM_GLIDE_MODE: return op.break_point;
    case PARAM_FX1_TIME: return op.right_curve;
    case PARAM_FX1_FEEDBACK: return op.right_depth;

    case PARAM_MIX_VCO1_VOL: return g_active_patch.algorithm;
    case PARAM_MIX_VCO2_VOL: return g_active_patch.feedback;
    case PARAM_MIX_PHASE2: return g_active_patch.transpose + 24;

    case PARAM_FX2_TIME: return g_active_patch.pitch_eg_levels[0];
    case PARAM_FX2_FEEDBACK: return g_active_patch.pitch_eg_levels[1];
    case PARAM_FX2_TONE: return g_active_patch.pitch_eg_levels[2];
    case PARAM_FX2_MIX: return g_active_patch.pitch_eg_levels[3];
    case PARAM_MOD_ROUTING1: return g_active_patch.pitch_eg_rates[0];
    case PARAM_MOD_DEPTH1: return g_active_patch.pitch_eg_rates[1];
    case PARAM_MOD_ROUTING2: return g_active_patch.pitch_eg_rates[2];
    case PARAM_MOD_DEPTH2: return g_active_patch.pitch_eg_rates[3];

    default:
      return 0;
  }
}

void pra_synth_set_hold_mode(bool active) {
  hold_mode_active = active;
}

bool pra_synth_is_hold_mode(void) {
  return hold_mode_active;
}

void pra_synth_set_midi_channel(uint8_t ch) {
  (void)ch;
}

} // extern "C"
