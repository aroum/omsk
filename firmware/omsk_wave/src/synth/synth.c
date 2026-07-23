#include "synth.h"
#include "../sw_config.h"
#include "../midi/midi_map.h"
#include "../tables/omsk_wavetables.h"
#include "../ui/ui_state.h"
#include "audio.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pra_synth.h"
#include "synth_defs.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

static uint8_t preset_write_buffer[FLASH_SECTOR_SIZE] __attribute__((aligned(4)));


SynthParams params;

enum { PRESET_SLOT_COUNT = 16 };

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t checksum;
  SynthParams data;
} PresetSlot;

static const uint32_t PRESET_MAGIC = 0x54534552u;
static const uint16_t PRESET_VERSION = 1;

static uint32_t preset_checksum(const SynthParams *p) {
  const uint8_t *b = (const uint8_t *)p;
  uint32_t sum = 0;
  for (size_t i = 0; i < sizeof(SynthParams); i++) {
    sum += b[i];
  }
  return sum;
}

static uint32_t preset_flash_offset(uint8_t slot) {
  uint32_t base =
      PICO_FLASH_SIZE_BYTES - (PRESET_SLOT_COUNT * FLASH_SECTOR_SIZE);
  return base + ((uint32_t)slot * FLASH_SECTOR_SIZE);
}

static uint8_t get_param_value(ParamID param) {
  switch (param) {
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
  case PARAM_CHORD_MODE:
    return params.chord_mode;
  case PARAM_ARP_RATE:
    return params.arp_rate;
  case PARAM_ARP_MODE:
    return params.arp_mode;
  case PARAM_ARP_SWING:
    return params.arp_swing;
  case PARAM_ARP_OCT:
    return params.arp_oct;
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
  case PARAM_FX2_TONE:
    return params.fx2_tone;
  case PARAM_FX2_MIX:
    return params.fx2_mix;
  case PARAM_VCF_KEY_TRACK:
    return params.vcf_key_track;
  case PARAM_VCF_EG_AMT:
    return params.vcf_eg_amt;
  case PARAM_VCF_MODE:
    return params.vcf_mode;
  case PARAM_LFO_DEPTH:
    return params.lfo_depth;
  case PARAM_LFO_FILTER_AMT:
    return params.lfo_filter_amt;
  case PARAM_LFO_OSC_AMT:
    return params.lfo_osc_amt;
  case PARAM_LFO_OSC_DST:
    return params.lfo_osc_dst;
  case PARAM_EG_OSC_AMT:
    return params.eg_osc_amt;
  case PARAM_EG_OSC_DST:
    return params.eg_osc_dst;
  case PARAM_SUB_OSC:
    return params.sub_osc;
  case PARAM_AMP_GAIN:
    return params.amp_gain;
  case PARAM_EG_AMP_MOD:
    return params.eg_amp_mod;
  case PARAM_PITCH_BEND_RANGE:
    return params.pitch_bend_range;
  case PARAM_VOICE_MODE:
    return params.voice_mode;
  case PARAM_VOICE_ASSIGN_MODE:
    return params.voice_assign_mode;
  case PARAM_PAN:
    return params.pan;
  case PARAM_EG_VEL_SENS:
    return params.eg_vel_sens;
  case PARAM_ADV_TEMPO:
    return params.adv_tempo;
  case PARAM_ADV_SCALE:
    return params.adv_scale;
  case PARAM_ADV_MIDI_CH:
    return params.midi_channel;
  case PARAM_ADV_SYNC_MODE:
    return params.adv_sync_mode;
  case PARAM_ADV_SCALE_KEY:
    return params.adv_scale_key;
  default:
    return 0;
  }
}

static inline uint8_t clamp_u8(uint8_t v, uint8_t lo, uint8_t hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static void synth_sanitize_params(void) {
  // Keep UI, engine, and persistence compatible when ranges change.
  // - VCO transpose is an enum-like 0..10 (maps to -5..+5 oct in UI)
  // - ARP rate/mode/oct are also narrow ranges
  params.vco1_transpose = clamp_u8(params.vco1_transpose, 0, 10);
  params.vco2_transpose = clamp_u8(params.vco2_transpose, 0, 10);

  params.arp_rate = clamp_u8(params.arp_rate, 0, 17);
  params.arp_mode = clamp_u8(params.arp_mode, 0, 5);
  params.arp_oct = clamp_u8(params.arp_oct, 0, 6);
}

void synth_init(void) {
  memset(&params, 0, sizeof(SynthParams));
  // Initialize Mod Matrix to center (no modulation)
  for (int p = 0; p < PARAM_COUNT; p++) {
    for (int s = 0; s < SRC_COUNT; s++) {
      params.mod_matrix[p][s] = 64;
    }
  }
  for (int p = 0; p < PARAM_COUNT; p++) {
    params.mod_source_assigned[p] = 0xFF; // None
  }

  params.vco1_transpose = 64;
  params.vco1_detune = 64;
  params.vco1_wave = 127;
  params.vco1_shape = 0;
  params.vco2_transpose = 64;
  params.vco2_detune = 72;
  params.vco2_wave = 0;
  params.vcf1_cutoff = 112;
  params.vcf1_mix = 127;
  params.vcf1_res = 64;
  params.vcf2_cutoff = 127;
  params.vcf2_mix = 127;
  params.mix_vco_balance = 127;
  params.mix_osc_noise = 127;
  params.mix_master = 0;
  params.noise_color = 64;
  params.eg1_attack = 96;
  params.eg1_decay = 96;
  params.eg1_sustain = 127;
  params.eg1_release = 32;
  params.eg2_attack = 32;
  params.eg2_decay = 32;
  params.eg2_sustain = 127;
  params.eg2_release = 32;
  params.vcf_key_track = 96;
  params.vcf_eg_amt = 40;
  params.vcf_mode = 0;
  params.lfo_depth = 127; // Keep 127 for range, use individual amts
  params.lfo1_rate = 80;
  params.lfo1_wave = 0;
  params.lfo1_smooth = 0;
  params.lfo_filter_amt = 76;
  params.lfo_osc_amt = 64;
  params.lfo_osc_dst = 0;
  params.eg_osc_amt = 64;
  params.eg_osc_dst = 0;
  params.sub_osc = 64; // Legacy: Sub Level (64 for PRA32-like)
  params.amp_gain = 100;
  params.eg_amp_mod = 0;
  params.pitch_bend_range = CFG_PITCH_BEND_RANGE_SEMITONES;
  params.voice_mode = 0;
  params.voice_assign_mode = 0;
  params.pan = 64;
  params.eg_vel_sens = 0;
  params.amp_vel_sens = 0;
  params.route_vco1 = ROUTE_VCF1;
  params.route_vco2 = ROUTE_VCF2;
  params.route_noise = ROUTE_VCF2;
  params.chord_mode = CHORD_OFF;
  params.midi_channel = 0; // Channel 1
  params.adv_tempo = ADV_TEMPO_OFF;
  params.adv_scale = ADV_SCALE_OFF;
  params.adv_sync_mode = 8; // 1/4
  params.adv_scale_key = 0; // C

  // FX
  params.fx1_mix = 0;
  params.fx1_time = 64;
  params.fx1_feedback = 64;
  params.fx1_spread = 64;
  params.fx2_mix = 0;
  params.fx2_time = 64;
  params.fx2_feedback = 64;
  params.fx2_tone = 127;

  // Load persistence from slot 0, then apply all
  if (!synth_preset_load(0)) {
    synth_sanitize_params();
    synth_apply_all_params();
  }
}

void synth_note_on(uint8_t note, uint8_t velocity) {
  // Route internal notes (matrix, sequencer, etc.) through the same MIDI pipeline
  // but call the engine directly now to avoid circular dependency if we use midi_map_process
  pra_synth_midi_note_on(params.midi_channel, note, velocity);
}

void synth_note_off(uint8_t note) {
  pra_synth_midi_note_off(params.midi_channel, note, 0);
}

int16_t synth_get_sample(void) { return pra_synth_get_sample(); }

void synth_set_lpf_cutoff(uint8_t val) { params.vcf1_cutoff = val; }
void synth_set_resonance(uint8_t val) { params.vcf1_res = val; }
void synth_set_lfo_freq(uint8_t val) { params.lfo1_rate = val; }
void synth_set_param(int module, int param_idx, uint8_t value) {
  (void)module;
  (void)param_idx;
  (void)value;
}

void synth_apply_all_params(void) {
  for (int p = 0; p < PARAM_COUNT; p++) {
    pra_synth_param_change((ParamID)p, get_param_value((ParamID)p));
  }
}

bool synth_preset_save(uint8_t slot) {
  if (slot >= PRESET_SLOT_COUNT)
    return false;
  PresetSlot ps;
  ps.magic = PRESET_MAGIC;
  ps.version = PRESET_VERSION;
  ps.reserved = 0;
  ps.data = params;
  ps.checksum = preset_checksum(&ps.data);

  memset(preset_write_buffer, 0xFF, FLASH_SECTOR_SIZE);
  memcpy(preset_write_buffer, &ps, sizeof(ps));

  uint32_t offset = preset_flash_offset(slot);
  
  multicore_lockout_start_blocking();
  uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(offset, FLASH_SECTOR_SIZE);
  flash_range_program(offset, preset_write_buffer, FLASH_SECTOR_SIZE);
  restore_interrupts(ints);
  multicore_lockout_end_blocking();
  
  return true;
}

bool synth_preset_load(uint8_t slot) {
  if (slot >= PRESET_SLOT_COUNT)
    return false;
  uint32_t offset = preset_flash_offset(slot);
  const PresetSlot *ps = (const PresetSlot *)(XIP_BASE + offset);
  if (ps->magic != PRESET_MAGIC)
    return false;
  if (ps->version != PRESET_VERSION)
    return false;
  uint32_t sum = preset_checksum(&ps->data);
  if (sum != ps->checksum)
    return false;
  params = ps->data;
  synth_sanitize_params();
  synth_apply_all_params();
  return true;
}
