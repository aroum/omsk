#include "ui_logic.h"
#include "../sw_config.h"
#include "../midi/midi_handler.h"
#include "../midi/midi_map.h"
#include "../sequencer/sequencer.h"
#include "../synth/pra_synth.h"
#include "../synth/synth.h"
#include "../synth/synth_defs.h"
#include "../synth/fm_synth.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "ui_oled.h"
#include "ui_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../shared/hardware/colors.h"
static const char* LFO_NAMES[] = {"TRI ", "SAW+", "SAW-", "SQR ", "SIN ", "S&H "};

UIMode ui_mode = UI_MODE_PIANO;
UIMode last_ui_mode = UI_MODE_PIANO;
ModuleID selected_module = MOD_FREQ;
ModuleID target_module = MOD_FREQ;
ModuleID last_active_module = MOD_FREQ;

volatile bool g_oled_dirty = true;
uint8_t g_sys_oled_brightness = OLED_BRIGHTNESS_PERCENT;
volatile uint32_t g_oled_draw_count = 0;

bool set_mode_active = false;
bool fn_mode_active = false;
bool fn_button_held = false;
bool ui_oled_view_graph = false;
ModuleID set_context_module = MOD_NONE;
ModuleID last_mod_source = MOD_NONE; 
int set_context_src_override = -1;
bool preset_mode_active = false;
uint32_t preset_hold_start[16];
bool preset_hold_used[16];

bool set_button_held = false;
bool assignment_mode = false;
uint16_t midi_pad_state = 0;
uint32_t set_press_time = 0;

uint8_t active_op = 0;
uint8_t sub_page = 0;
static uint32_t op_click_times[6] = {0};

char ui_status_msg[32] = "";
uint32_t ui_status_msg_timeout_ms = 0;

void ui_set_status(const char *msg, uint32_t ms) {
    strncpy(ui_status_msg, msg, sizeof(ui_status_msg) - 1);
    ui_status_msg_timeout_ms = to_ms_since_boot(get_absolute_time()) + ms;
    g_oled_dirty = true;
}

int octave = 0;
uint16_t held_keys = 0;
uint16_t combo_used_keys = 0;
uint16_t latched_keys = 0;
static bool preset_combo_held = false;

static uint8_t mem_mode = 0; // 0 = Load, 1 = Save
static uint8_t mem_cartridge = 0; // 0..31
static uint8_t mem_slot = 0; // 0..31
static uint8_t mem_prog = 0; // 0..31 (RAM Program)

int active_notes[16];
bool midi_held_notes[128];
uint16_t midi_note_mask = 0;

uint32_t last_arp_step = 0;
int arp_note_index = 0;
int arp_notes[16];
int arp_note_count = 0;
int last_arp_note = -1;
bool arp_was_on = false;
static int arp_direction = 1;
static bool arp_note_is_on = false;
static int arp_step_parity = 0;

static uint8_t seq_edit_step = 0;

const int base_notes[12] = {
    36, 39, 42, 45,
    37, 40, 43, 46,
    38, 41, 44, 47
};

const char *get_piano_key_label_from_index(int idx) {
  if (idx < 0 || idx >= 16) return "";
  return CFG_PIANO_LAYOUT[idx / 4][idx % 4];
}

const ModuleID btn_to_mod[16] = {
    MOD_FREQ,     MOD_LVL_MOD, MOD_LFO,     MOD_EG,
    MOD_KBDSCALE, MOD_FILT,    MOD_ALGO_FB, MOD_PITCH_EG,
    MOD_OP1,      MOD_OP2,     MOD_OP3,     MOD_MEM,
    MOD_OP4,      MOD_OP5,     MOD_OP6,     MOD_SYS
};

bool is_modulator(ModuleID m) {
  return (m == MOD_LFO || m == MOD_EG || m == MOD_PITCH_EG);
}

bool is_filter(ModuleID m) { return (m == MOD_FILT); }
bool is_source(ModuleID m) { return (m == MOD_FREQ || m == MOD_LVL_MOD); }

int get_mod_source_idx(ModuleID m) {
  if (m == MOD_LFO) return 0;
  if (m == MOD_EG) return 1;
  return -1;
}

const char *module_name(ModuleID m) {
  switch (m) {
  case MOD_FREQ: return "FREQ";
  case MOD_LVL_MOD: return "LEVEL";
  case MOD_LFO: return "LFO";
  case MOD_EG: return "EG";
  case MOD_KBDSCALE: return "SCALE";
  case MOD_FILT: return "FILT";
  case MOD_ALGO_FB: return "ALGO";
  case MOD_PITCH_EG: return "P_EG";
  case MOD_OP1: return "OP1";
  case MOD_OP2: return "OP2";
  case MOD_OP3: return "OP3";
  case MOD_OP4: return "OP4";
  case MOD_OP5: return "OP5";
  case MOD_OP6: return "OP6";
  case MOD_MEM: return "MEM";
  case MOD_SYS: return "SYS";
  default: return "OTHER";
  }
}

void handle_module_press(ModuleID m) {
  if (m >= MOD_OP1 && m <= MOD_OP6) {
    uint8_t op_idx = m - MOD_OP1;
    
    active_op = op_idx;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - op_click_times[op_idx] < 300) {
      g_active_patch.op[op_idx].active = !g_active_patch.op[op_idx].active;
      ui_set_status(g_active_patch.op[op_idx].active ? "OP ACTIVE" : "OP BYPASS", 1000);
    } else {
      char buf[16];
      snprintf(buf, sizeof(buf), "OP%d SELECTED", op_idx + 1);
      ui_set_status(buf, 1000);
    }
    op_click_times[op_idx] = now;
    if (selected_module != MOD_FREQ && selected_module != MOD_LVL_MOD && selected_module != MOD_LFO && selected_module != MOD_EG && selected_module != MOD_KBDSCALE) {
      selected_module = MOD_FREQ;
    }
    g_oled_dirty = true;
    return;
  }

  if (m == selected_module) {
    if (m == MOD_EG || m == MOD_LFO || m == MOD_KBDSCALE || m == MOD_PITCH_EG || m == MOD_FILT) {
      sub_page = 1 - sub_page;
    } else if (m == MOD_MEM) {
      char buf[32];
      if (mem_mode == 0) {
        // Load patch from cartridge/slot
        if (fm_library_load(mem_cartridge, mem_slot, &g_active_patch)) {
          snprintf(buf, sizeof(buf), "LOADED C%d S%d", mem_cartridge + 1, mem_slot + 1);
          ui_set_status(buf, 2000);
        } else {
          ui_set_status("SLOT EMPTY", 2000);
        }
      } else {
        // Save patch to cartridge/slot
        if (fm_library_save(mem_cartridge, mem_slot, &g_active_patch)) {
          snprintf(buf, sizeof(buf), "SAVED C%d S%d", mem_cartridge + 1, mem_slot + 1);
          ui_set_status(buf, 2000);
        } else {
          ui_set_status("SAVE FAILED", 2000);
        }
      }
    }
  } else {
    sub_page = 0;
    selected_module = m;
  }
  g_oled_dirty = true;
}

void ui_handle_mode_switch(void) {
  if (ui_mode == UI_MODE_PIANO) {
    last_ui_mode = UI_MODE_PIANO;
    ui_mode = UI_MODE_PARAMS;
    ui_set_status("PARAMS MODE", 1000);
  } else {
    last_ui_mode = UI_MODE_PARAMS;
    ui_mode = UI_MODE_PIANO;
    ui_set_status("PIANO MODE", 1000);
  }
  for (int i = 0; i < 16; i++) {
    if (active_notes[i] != -1) {
      synth_note_off(active_notes[i]);
      active_notes[i] = -1;
    }
  }
  g_oled_dirty = true;
}

void update_arp_notes(void) {
  arp_note_count = 0;
  for (int i = 0; i < 12; i++) {
    if ((held_keys | latched_keys) & (1 << i)) {
      int note = base_notes[i] + (octave * 12);
      arp_notes[arp_note_count++] = note;
    }
  }
}

void process_arp(uint32_t now) {
  if (params.arp_mode == 0 || arp_note_count == 0) {
    if (last_arp_note != -1) {
      synth_note_off(last_arp_note);
      last_arp_note = -1;
    }
    return;
  }
  float tempo = 120.0f;
  if (params.adv_tempo >= ADV_TEMPO_BPM_MIN) {
    tempo = (float)(30 + params.adv_tempo - ADV_TEMPO_BPM_MIN);
  }
  float step_ms = adv_sync_mode_to_ms(params.arp_rate, tempo);
  uint32_t step_us = (uint32_t)(step_ms * 1000.0f);

  if (time_us_32() - last_arp_step >= step_us) {
    last_arp_step = time_us_32();
    if (last_arp_note != -1) {
      synth_note_off(last_arp_note);
    }
    if (arp_note_index >= arp_note_count) arp_note_index = 0;
    int base_note = arp_notes[arp_note_index];
    int note = base_note + (params.arp_oct - 3) * 12;
    synth_note_on(note, 127);
    last_arp_note = note;
    arp_note_index++;
  }
}

void ui_handle_pad_pressed(uint8_t pad_index) {
  if (pad_index >= 16) return;
  held_keys |= (1 << pad_index);

  if (ui_mode == UI_MODE_PIANO) {
    if ((held_keys & (1 << 12)) && (held_keys & (1 << 13))) {
      ui_handle_mode_switch();
      combo_used_keys |= ((1<<12) | (1<<13));
      return;
    }
    // Combo: OCT- (12) + ADV (15) -> cycle Scale Key
    if ((held_keys & (1 << 12)) && pad_index == 15) {
      params.adv_scale_key = (params.adv_scale_key + 1) % 12;
      static const char *key_names[12] = {
          "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
      };
      char buf[20];
      snprintf(buf, sizeof(buf), "KEY: %s", key_names[params.adv_scale_key]);
      ui_set_status(buf, 2000);
      combo_used_keys |= ((1<<12) | (1<<15));
      return;
    }
    // Combo: ARP (14) + ADV (15) -> Toggle hold/seq/latch or seq mode
    if ((held_keys & (1 << 14)) && pad_index == 15) {
      if (ui_mode != UI_MODE_SEQ) {
          last_ui_mode = ui_mode;
          ui_mode = UI_MODE_SEQ;
          ui_set_status("SEQ MODE", 2000);
      } else {
          ui_mode = last_ui_mode;
          ui_set_status("SEQ OFF", 2000);
      }
      combo_used_keys |= ((1<<14) | (1<<15));
      return;
    }
    if (pad_index < 12) {
      int note = base_notes[pad_index] + (octave * 12);
      if (pra_synth_is_hold_mode()) {
        latched_keys |= (1 << pad_index);
      }
      if (params.arp_mode > 0) {
        update_arp_notes();
        return;
      }
      if (active_notes[pad_index] != -1) synth_note_off(active_notes[pad_index]);
      active_notes[pad_index] = note;
      synth_note_on(note, 127);
      return;
    } else if (pad_index == 12) {
      if (octave > -4) octave--;
      return;
    } else if (pad_index == 13) {
      if (octave < 4) octave++;
      return;
    }
  } else if (ui_mode == UI_MODE_PARAMS) {
    if ((held_keys & (1 << 12)) && (held_keys & (1 << 13))) {
      ui_handle_mode_switch();
      combo_used_keys |= ((1<<12) | (1<<13));
      return;
    }
    // Combo MEM (11) + SYS (15) -> toggle graph view
    if (((held_keys & (1 << 11)) && pad_index == 15) || ((held_keys & (1 << 15)) && pad_index == 11)) {
      ui_oled_view_graph = !ui_oled_view_graph;
      ui_set_status(ui_oled_view_graph ? "GRAPH VIEW" : "KNOBS VIEW", 1500);
      combo_used_keys |= ((1 << 11) | (1 << 15));
      return;
    }
    // Combo LFO + EG (Pads 2+3) -> PRESET Mode
    if ((held_keys & (1 << 2)) && (held_keys & (1 << 3))) {
      preset_mode_active = true;
      preset_combo_held = true;
      ui_set_status("Select 1 of 16 slots", 5000);
      combo_used_keys |= ((1 << 2) | (1 << 3));
      return;
    }
    ModuleID m = btn_to_mod[pad_index];
    handle_module_press(m);
  }
  g_oled_dirty = true;
}

void ui_handle_pad_released(uint8_t pad_index) {
  if (pad_index >= 16) return;
  held_keys &= ~(1 << pad_index);

  bool was_in_combo = (combo_used_keys & (1 << pad_index)) != 0;
  if (was_in_combo) {
    combo_used_keys &= ~(1 << pad_index);
    return;
  }

  if (ui_mode == UI_MODE_PIANO) {
    if (pad_index < 12) {
      if (params.arp_mode > 0) {
        update_arp_notes();
        return;
      }
      if (active_notes[pad_index] != -1) {
        synth_note_off(active_notes[pad_index]);
        active_notes[pad_index] = -1;
      }
    } else if (pad_index == 14) {
      handle_module_press(MOD_ARP);
    } else if (pad_index == 15) {
      handle_module_press(MOD_ADV);
    }
  } else if (ui_mode == UI_MODE_PARAMS) {
    // If we released MOD_SYS (pad 15) when on MOD_ADV page, go back to PIANO
    if (selected_module == MOD_ADV && pad_index == 15) {
      ui_mode = UI_MODE_PIANO;
      selected_module = last_active_module;
      ui_set_status("PIANO MODE", 1000);
    }
  }
  g_oled_dirty = true;
}

void ui_init(void) {
  for (int i = 0; i < 16; i++) active_notes[i] = -1;
}

void update_param(uint8_t *param, int delta, uint8_t max_val) {
  int val = (int)*param + delta;
  if (val < 0) val = 0;
  if (val > max_val) val = max_val;
  if (*param != (uint8_t)val) {
    *param = (uint8_t)val;
    g_oled_dirty = true;
  }
}

void update_param_with_id(ParamID param_id, uint8_t *param, int delta) {
  update_param(param, delta, 127);
  pra_synth_param_change(param_id, *param);
}

void update_param_choice_with_id(ParamID param_id, uint8_t *param, int delta, uint8_t max_val) {
  update_param(param, delta, max_val);
  pra_synth_param_change(param_id, *param);
}

void update_param16_with_id(ParamID param_id, uint16_t *param, int delta, uint16_t max_val) {
  int val = (int)*param + delta;
  if (val < 0) val = 0;
  if (val > max_val) val = max_val;
  if (*param != (uint16_t)val) {
    *param = (uint16_t)val;
    pra_synth_param_change(param_id, (uint8_t)val);
    g_oled_dirty = true;
  }
}

void handle_params_encoders(int d1, int d2, int d3, int d4) {
  if (ui_mode != UI_MODE_PARAMS) return;

  switch (selected_module) {
    case MOD_FREQ:
      if (d1) {
        FmOperatorPatch &op = g_active_patch.op[active_op];
        int val = op.fixed_freq ? 1 : 0;
        val = 1 - val;
        op.fixed_freq = val ? true : false;
        pra_synth_param_change(PARAM_VCO1_TRANSPOSE, val);
      }
      if (d2) {
        FmOperatorPatch &op = g_active_patch.op[active_op];
        update_param(&op.freq_coarse, d2, 31);
        pra_synth_param_change(PARAM_VCO1_DETUNE, op.freq_coarse);
      }
      if (d3) {
        FmOperatorPatch &op = g_active_patch.op[active_op];
        update_param(&op.freq_fine, d3, 99);
        pra_synth_param_change(PARAM_VCO1_WAVE, op.freq_fine);
      }
      if (d4) {
        FmOperatorPatch &op = g_active_patch.op[active_op];
        int val = (int)op.detune + d4;
        if (val < -7) val = -7;
        if (val > 7) val = 7;
        op.detune = val;
        pra_synth_param_change(PARAM_VCO1_SHAPE, (uint8_t)(val + 7));
      }
      break;

    case MOD_LVL_MOD:
      if (d1) {
        FmOperatorPatch &op = g_active_patch.op[active_op];
        update_param(&op.output_level, d1, 99);
        pra_synth_param_change(PARAM_VCO2_TRANSPOSE, op.output_level);
      }
      if (d2) {
        FmOperatorPatch &op = g_active_patch.op[active_op];
        update_param(&op.key_velocity_sensitivity, d2, 7);
        pra_synth_param_change(PARAM_VCO2_DETUNE, op.key_velocity_sensitivity);
      }
      if (d3) {
        FmOperatorPatch &op = g_active_patch.op[active_op];
        update_param(&op.rate_scaling, d3, 7);
        pra_synth_param_change(PARAM_VCO2_WAVE, op.rate_scaling);
      }
      break;

    case MOD_LFO:
      if (sub_page == 0) {
        if (d1) update_param_choice_with_id(PARAM_LFO1_RATE, &g_active_patch.lfo_waveform, d1, 5);
        if (d2) update_param_choice_with_id(PARAM_LFO1_SMOOTH, &g_active_patch.lfo_speed, d2, 99);
        if (d3) update_param_choice_with_id(PARAM_LFO1_WAVE, &g_active_patch.lfo_delay, d3, 99);
        if (d4) update_param_choice_with_id(PARAM_LFO1_SHAPE, &g_active_patch.pitch_mod_sensitivity, d4, 7);
      } else {
        if (d1) update_param_choice_with_id(PARAM_LFO2_RATE, &g_active_patch.lfo_pitch_mod_depth, d1, 99);
        if (d2) update_param_choice_with_id(PARAM_LFO2_SMOOTH, &g_active_patch.lfo_amp_mod_depth, d2, 99);
        if (d3) {
          g_active_patch.lfo_sync = g_active_patch.lfo_sync ? false : true;
          pra_synth_param_change(PARAM_LFO2_WAVE, g_active_patch.lfo_sync ? 1 : 0);
        }
      }
      break;

    case MOD_EG: {
      FmOperatorPatch &op = g_active_patch.op[active_op];
      if (sub_page == 0) {
        if (d1) { update_param(&op.levels[0], d1, 99); pra_synth_param_change(PARAM_EG1_ATTACK, op.levels[0]); }
        if (d2) { update_param(&op.levels[1], d2, 99); pra_synth_param_change(PARAM_EG1_DECAY, op.levels[1]); }
        if (d3) { update_param(&op.levels[2], d3, 99); pra_synth_param_change(PARAM_EG1_SUSTAIN, op.levels[2]); }
        if (d4) { update_param(&op.levels[3], d4, 99); pra_synth_param_change(PARAM_EG1_RELEASE, op.levels[3]); }
      } else {
        if (d1) { update_param(&op.rates[0], d1, 99); pra_synth_param_change(PARAM_EG2_ATTACK, op.rates[0]); }
        if (d2) { update_param(&op.rates[1], d2, 99); pra_synth_param_change(PARAM_EG2_DECAY, op.rates[1]); }
        if (d3) { update_param(&op.rates[2], d3, 99); pra_synth_param_change(PARAM_EG2_SUSTAIN, op.rates[2]); }
        if (d4) { update_param(&op.rates[3], d4, 99); pra_synth_param_change(PARAM_EG2_RELEASE, op.rates[3]); }
      }
      break;
    }

    case MOD_KBDSCALE: {
      FmOperatorPatch &op = g_active_patch.op[active_op];
      if (sub_page == 0) {
        if (d1) { update_param(&op.left_curve, d1, 3); pra_synth_param_change(PARAM_GLIDE_TIME, op.left_curve); }
        if (d2) { update_param(&op.left_depth, d2, 99); pra_synth_param_change(PARAM_GLIDE_SLOPE, op.left_depth); }
        if (d3) { update_param(&op.break_point, d3, 99); pra_synth_param_change(PARAM_GLIDE_MODE, op.break_point); }
      } else {
        if (d1) { update_param(&op.right_curve, d1, 3); pra_synth_param_change(PARAM_FX1_TIME, op.right_curve); }
        if (d2) { update_param(&op.right_depth, d2, 99); pra_synth_param_change(PARAM_FX1_FEEDBACK, op.right_depth); }
      }
      break;
    }

    case MOD_ALGO_FB:
      if (d1) update_param_choice_with_id(PARAM_MIX_VCO2_VOL, &g_active_patch.feedback, d1, 7);
      if (d2) update_param_choice_with_id(PARAM_MIX_VCO1_VOL, &g_active_patch.algorithm, d2, 31);
      break;

    case MOD_PITCH_EG:
      if (sub_page == 0) {
        if (d1) { update_param(&g_active_patch.pitch_eg_levels[0], d1, 99); pra_synth_param_change(PARAM_FX2_TIME, g_active_patch.pitch_eg_levels[0]); }
        if (d2) { update_param(&g_active_patch.pitch_eg_levels[1], d2, 99); pra_synth_param_change(PARAM_FX2_FEEDBACK, g_active_patch.pitch_eg_levels[1]); }
        if (d3) { update_param(&g_active_patch.pitch_eg_levels[2], d3, 99); pra_synth_param_change(PARAM_FX2_TONE, g_active_patch.pitch_eg_levels[2]); }
        if (d4) { update_param(&g_active_patch.pitch_eg_levels[3], d4, 99); pra_synth_param_change(PARAM_FX2_MIX, g_active_patch.pitch_eg_levels[3]); }
      } else {
        if (d1) { update_param(&g_active_patch.pitch_eg_rates[0], d1, 99); pra_synth_param_change(PARAM_MOD_ROUTING1, g_active_patch.pitch_eg_rates[0]); }
        if (d2) { update_param(&g_active_patch.pitch_eg_rates[1], d2, 99); pra_synth_param_change(PARAM_MOD_DEPTH1, g_active_patch.pitch_eg_rates[1]); }
        if (d3) { update_param(&g_active_patch.pitch_eg_rates[2], d3, 99); pra_synth_param_change(PARAM_MOD_ROUTING2, g_active_patch.pitch_eg_rates[2]); }
        if (d4) { update_param(&g_active_patch.pitch_eg_rates[3], d4, 99); pra_synth_param_change(PARAM_MOD_DEPTH2, g_active_patch.pitch_eg_rates[3]); }
      }
      break;

    case MOD_FILT:
      if (d1) update_param_choice_with_id(PARAM_VCF1_CUTOFF, &params.vcf1_cutoff, d1, 127);
      if (d2) update_param_choice_with_id(PARAM_VCF1_RES, &params.vcf1_res, d2, 127);
      break;

    case MOD_SYS:
      if (d1) {
        int ch = (int)params.midi_channel + d1;
        if (ch < 0) ch = 0;
        if (ch > 15) ch = 15;
        if (params.midi_channel != (uint8_t)ch) {
          params.midi_channel = (uint8_t)ch;
          pra_synth_param_change(PARAM_ADV_MIDI_CH, params.midi_channel);
          g_oled_dirty = true;
        }
      }
      if (d2) {
        int val = (int)g_sys_oled_brightness + d2 * 5;
        if (val < 5) val = 5;
        if (val > 100) val = 100;
        if (g_sys_oled_brightness != (uint8_t)val) {
          g_sys_oled_brightness = (uint8_t)val;
          ui_oled_set_brightness(g_sys_oled_brightness);
          g_oled_dirty = true;
        }
      }
      break;

    case MOD_ARP:
      if (d1) update_param_choice_with_id(PARAM_ARP_RATE, &params.arp_rate, d1, 17);
      if (d2) {
        uint8_t prev_mode = params.arp_mode;
        int val = (int)params.arp_mode + d2;
        if (val < 0) val = 0;
        if (val > 5) val = 5;
        params.arp_mode = (uint8_t)val;
        if (params.arp_mode != prev_mode) {
          pra_synth_param_change(PARAM_ARP_MODE, params.arp_mode);
          if (params.arp_mode > 0) {
            update_arp_notes();
          } else {
            if (last_arp_note != -1) {
              synth_note_off(last_arp_note);
              last_arp_note = -1;
            }
          }
        }
      }
      if (d3) update_param_with_id(PARAM_ARP_SWING, &params.arp_swing, d3);
      if (d4) update_param_choice_with_id(PARAM_ARP_OCT, &params.arp_oct, d4, 6);
      break;

    case MOD_ADV:
      if (d1) update_param16_with_id(PARAM_ADV_TEMPO, &params.adv_tempo, d1, 272);
      if (d2) update_param_choice_with_id(PARAM_ADV_SCALE, &params.adv_scale, d2, ADV_SCALE_COUNT - 1);
      if (d3) update_param_choice_with_id(PARAM_CHORD_MODE, &params.chord_mode, d3, CHORD_MODE_COUNT - 1);
      break;

    case MOD_MEM:
      if (d1) {
        int val = (int)mem_mode + d1;
        if (val < 0) val = 0;
        if (val > 1) val = 1;
        mem_mode = (uint8_t)val;
      }
      if (d2) {
        int val = (int)mem_cartridge + d2;
        if (val < 0) val = 0;
        if (val > 31) val = 31;
        mem_cartridge = (uint8_t)val;
      }
      if (d3) {
        int val = (int)mem_slot + d3;
        if (val < 0) val = 0;
        if (val > 31) val = 31;
        mem_slot = (uint8_t)val;
      }
      if (d4) {
        int val = (int)mem_prog + d4;
        if (val < 0) val = 0;
        if (val > 31) val = 31;
        if (val != mem_prog) {
          mem_prog = (uint8_t)val;
          fm_synth_set_patch(mem_prog);
          char buf[16];
          snprintf(buf, sizeof(buf), "PROG %d LOADED", mem_prog + 1);
          ui_set_status(buf, 1000);
        }
      }
      break;

    default:
      break;
  }
  if (d1 || d2 || d3 || d4) {
    g_oled_dirty = true;
  }
}

const char *chord_mode_name(uint8_t mode) {
  static const char *chord_names[] = {
    "Off", "Oct", "Sus2", "Sus4", "Min", "Maj", "Dom7", "Min7",
    "Maj7", "Dim7", "Aug", "7#5"
  };
  if (mode < 12) return chord_names[mode];
  return "Off";
}

void build_seq_oled_page(OledPage *out) {
    out->knobs[0].title = "Spd";
    out->knobs[0].value = (uint8_t)current_seq.speed * 20;
    out->knobs[0].mod_label = "";
    out->knobs[0].mod_amount = 64;
    static const char *spd_names[] = {"1/16", "1/8", "1/4", "1/2", "1X", "2X", "4X"};
    if (current_seq.speed <= SEQ_SPEED_4X) snprintf(out->knobs[0].value_str, 16, "%s", spd_names[current_seq.speed]);

    out->knobs[1].title = "Swng";
    out->knobs[1].value = current_seq.swing;
    out->knobs[1].mod_label = "";
    out->knobs[1].mod_amount = 64;
    snprintf(out->knobs[1].value_str, 16, "%d%%", current_seq.swing);

    out->knobs[2].title = "Mode";
    out->knobs[2].value = (uint8_t)current_seq.play_mode * 20;
    out->knobs[2].mod_label = "";
    out->knobs[2].mod_amount = 64;
    static const char *mode_names[] = {"FWD", "BWD", "PING", "SNK", "RND", "DRNK"};
    if (current_seq.play_mode < SEQ_MODE_COUNT) snprintf(out->knobs[2].value_str, 16, "%s", mode_names[current_seq.play_mode]);

    out->knobs[3].title = "Len";
    out->knobs[3].value = 50; 
    out->knobs[3].mod_label = "";
    out->knobs[3].mod_amount = 64;
    snprintf(out->knobs[3].value_str, 16, "50%%"); 

    out->layout_id = 3; // Custom SEQ layout
}

void build_seq_edit_oled_page(OledPage *out) {
    SeqStep *s = &current_seq.steps[seq_edit_step];
    
    out->knobs[0].title = "Vel";
    out->knobs[0].value = s->notes[0].velocity;
    out->knobs[0].mod_label = "";
    out->knobs[0].mod_amount = 64;
    snprintf(out->knobs[0].value_str, 16, "%d", s->notes[0].velocity);

    out->knobs[1].title = "Chrd";
    out->knobs[1].value = s->chord_mode;
    out->knobs[1].mod_label = "";
    out->knobs[1].mod_amount = 64;
    if (s->chord_mode == 0) snprintf(out->knobs[1].value_str, 16, "OFF");
    else snprintf(out->knobs[1].value_str, 16, "%s", chord_mode_name(s->chord_mode));

    out->knobs[2].title = "Evry";
    out->knobs[2].value = s->loop_every * 15;
    out->knobs[2].mod_label = "";
    out->knobs[2].mod_amount = 64;
    snprintf(out->knobs[2].value_str, 16, "%dx", s->loop_every);

    out->knobs[3].title = "Prob";
    out->knobs[3].value = s->notes[0].probability;
    out->knobs[3].mod_label = "";
    out->knobs[3].mod_amount = 64;
    snprintf(out->knobs[3].value_str, 16, "%d%%", s->notes[0].probability);

    out->layout_id = 1;
}

void build_oled_page(OledPage *out) {
  if (ui_mode == UI_MODE_SEQ) {
    build_seq_oled_page(out);
    return;
  }
  if (ui_mode == UI_MODE_SEQ_EDIT) {
    build_seq_edit_oled_page(out);
    return;
  }
  FmOperatorPatch &op = g_active_patch.op[active_op];
  out->layout_id = 0;
  
  // Clear all
  for (int i = 0; i < 4; i++) {
    out->knobs[i].title = NULL;
    out->knobs[i].value = 0;
    out->knobs[i].mod_label = NULL;
    out->knobs[i].mod_amount = 64;
    out->knobs[i].value_str[0] = '\0';
  }

  switch (selected_module) {
    case MOD_FREQ:
      out->knobs[0].title = "Mode";
      out->knobs[0].value = op.fixed_freq ? 127 : 0;
      strcpy(out->knobs[0].value_str, op.fixed_freq ? "FIX" : "RAT");

      out->knobs[1].title = "Coar";
      out->knobs[1].value = op.freq_coarse * 127 / 31;
      snprintf(out->knobs[1].value_str, 16, "%d", op.freq_coarse);

      out->knobs[2].title = "Fine";
      out->knobs[2].value = op.freq_fine * 127 / 99;
      snprintf(out->knobs[2].value_str, 16, "%d", op.freq_fine);

      out->knobs[3].title = "Detn";
      out->knobs[3].value = (op.detune + 7) * 127 / 14;
      snprintf(out->knobs[3].value_str, 16, "%+d", op.detune);
      break;

    case MOD_LVL_MOD:
      out->knobs[0].title = "Lvl";
      out->knobs[0].value = op.output_level * 127 / 99;
      snprintf(out->knobs[0].value_str, 16, "%d", op.output_level);

      out->knobs[1].title = "Vel";
      out->knobs[1].value = op.key_velocity_sensitivity * 127 / 7;
      snprintf(out->knobs[1].value_str, 16, "%d", op.key_velocity_sensitivity);

      out->knobs[2].title = "Scal";
      out->knobs[2].value = op.rate_scaling * 127 / 7;
      snprintf(out->knobs[2].value_str, 16, "%d", op.rate_scaling);
      break;

    case MOD_LFO:
      if (sub_page == 0) {
        out->knobs[0].title = "Wave";
        out->knobs[0].value = g_active_patch.lfo_waveform * 127 / 5;
        strcpy(out->knobs[0].value_str, LFO_NAMES[g_active_patch.lfo_waveform % 6]);

        out->knobs[1].title = "Sped";
        out->knobs[1].value = g_active_patch.lfo_speed * 127 / 99;
        snprintf(out->knobs[1].value_str, 16, "%d", g_active_patch.lfo_speed);

        out->knobs[2].title = "Dlay";
        out->knobs[2].value = g_active_patch.lfo_delay * 127 / 99;
        snprintf(out->knobs[2].value_str, 16, "%d", g_active_patch.lfo_delay);

        out->knobs[3].title = "PMS";
        out->knobs[3].value = g_active_patch.pitch_mod_sensitivity * 127 / 7;
        snprintf(out->knobs[3].value_str, 16, "%d", g_active_patch.pitch_mod_sensitivity);
      } else {
        out->knobs[0].title = "PMD";
        out->knobs[0].value = g_active_patch.lfo_pitch_mod_depth * 127 / 99;
        snprintf(out->knobs[0].value_str, 16, "%d", g_active_patch.lfo_pitch_mod_depth);

        out->knobs[1].title = "AMD";
        out->knobs[1].value = g_active_patch.lfo_amp_mod_depth * 127 / 99;
        snprintf(out->knobs[1].value_str, 16, "%d", g_active_patch.lfo_amp_mod_depth);

        out->knobs[2].title = "Sync";
        out->knobs[2].value = g_active_patch.lfo_sync ? 127 : 0;
        strcpy(out->knobs[2].value_str, g_active_patch.lfo_sync ? "ON" : "OFF");
      }
      break;

    case MOD_EG:
      if (sub_page == 0) {
        for (int i = 0; i < 4; i++) {
          out->knobs[i].title = (i == 0) ? "L1" : (i == 1) ? "L2" : (i == 2) ? "L3" : "L4";
          out->knobs[i].value = op.levels[i] * 127 / 99;
          snprintf(out->knobs[i].value_str, 16, "%d", op.levels[i]);
        }
      } else {
        for (int i = 0; i < 4; i++) {
          out->knobs[i].title = (i == 0) ? "R1" : (i == 1) ? "R2" : (i == 2) ? "R3" : "R4";
          out->knobs[i].value = op.rates[i] * 127 / 99;
          snprintf(out->knobs[i].value_str, 16, "%d", op.rates[i]);
        }
      }
      break;

    case MOD_KBDSCALE:
      if (sub_page == 0) {
        out->knobs[0].title = "LCrv";
        out->knobs[0].value = op.left_curve * 127 / 3;
        snprintf(out->knobs[0].value_str, 16, "%d", op.left_curve);

        out->knobs[1].title = "LDep";
        out->knobs[1].value = op.left_depth * 127 / 99;
        snprintf(out->knobs[1].value_str, 16, "%d", op.left_depth);

        out->knobs[2].title = "BP";
        out->knobs[2].value = op.break_point * 127 / 99;
        snprintf(out->knobs[2].value_str, 16, "%d", op.break_point);
      } else {
        out->knobs[0].title = "RCrv";
        out->knobs[0].value = op.right_curve * 127 / 3;
        snprintf(out->knobs[0].value_str, 16, "%d", op.right_curve);

        out->knobs[1].title = "RDep";
        out->knobs[1].value = op.right_depth * 127 / 99;
        snprintf(out->knobs[1].value_str, 16, "%d", op.right_depth);
      }
      break;

    case MOD_FILT:
      out->knobs[0].title = "Cut";
      out->knobs[0].value = params.vcf1_cutoff;
      snprintf(out->knobs[0].value_str, 16, "%d", params.vcf1_cutoff);

      out->knobs[1].title = "Res";
      out->knobs[1].value = params.vcf1_res;
      snprintf(out->knobs[1].value_str, 16, "%d", params.vcf1_res);
      break;

    case MOD_ALGO_FB:
      out->knobs[0].title = "Feed";
      out->knobs[0].value = g_active_patch.feedback * 127 / 7;
      snprintf(out->knobs[0].value_str, 16, "%d", g_active_patch.feedback);

      out->knobs[1].title = "Algo";
      out->knobs[1].value = g_active_patch.algorithm * 127 / 31;
      snprintf(out->knobs[1].value_str, 16, "%d", g_active_patch.algorithm + 1);

      out->knobs[2].title = NULL;
      out->knobs[3].title = NULL;
      break;

    case MOD_PITCH_EG:
      if (sub_page == 0) {
        for (int i = 0; i < 4; i++) {
          out->knobs[i].title = (i == 0) ? "PL1" : (i == 1) ? "PL2" : (i == 2) ? "PL3" : "PL4";
          out->knobs[i].value = g_active_patch.pitch_eg_levels[i] * 127 / 99;
          snprintf(out->knobs[i].value_str, 16, "%d", g_active_patch.pitch_eg_levels[i]);
        }
      } else {
        for (int i = 0; i < 4; i++) {
          out->knobs[i].title = (i == 0) ? "PR1" : (i == 1) ? "PR2" : (i == 2) ? "PR3" : "PR4";
          out->knobs[i].value = g_active_patch.pitch_eg_rates[i] * 127 / 99;
          snprintf(out->knobs[i].value_str, 16, "%d", g_active_patch.pitch_eg_rates[i]);
        }
      }
      break;

    case MOD_SYS:
      out->knobs[0].title = "MIDI";
      out->knobs[0].value = params.midi_channel * 127 / 15;
      snprintf(out->knobs[0].value_str, 16, "Ch %d", params.midi_channel + 1);

      out->knobs[1].title = "OLED";
      out->knobs[1].value = g_sys_oled_brightness * 127 / 100;
      snprintf(out->knobs[1].value_str, 16, "%d%%", g_sys_oled_brightness);
      break;

    case MOD_ARP:
      out->knobs[0].title = "Rate";
      out->knobs[0].value = params.arp_rate * 127 / 17;
      {
        static const char *sync_names[] = {
            "8/1", "8/1t", "4/1", "4/1t", "1/1", "1/1t", "1/2", "1/2t",
            "1/4", "1/4t", "1/8", "1/8t", "1/16", "1/16t", "1/32", "1/32t",
            "1/64", "1/64t"
        };
        if (params.arp_rate < 18) strcpy(out->knobs[0].value_str, sync_names[params.arp_rate]);
      }
      out->knobs[1].title = "Mode";
      out->knobs[1].value = params.arp_mode * 127 / 5;
      {
        static const char *modes[] = {"OFF", "UP", "DOWN", "UP-DN", "RND", "DRNK"};
        if (params.arp_mode < 6) strcpy(out->knobs[1].value_str, modes[params.arp_mode]);
      }
      out->knobs[2].title = "Swng";
      out->knobs[2].value = params.arp_swing;
      if (params.arp_swing == 0) {
        strcpy(out->knobs[2].value_str, "Off");
      } else {
        float pct = 50.0f + ((float)params.arp_swing / 127.0f) * 25.0f;
        snprintf(out->knobs[2].value_str, 16, "%.1f%%", pct);
      }
      out->knobs[3].title = "Oct";
      out->knobs[3].value = params.arp_oct * 127 / 6;
      snprintf(out->knobs[3].value_str, 16, "%+d oct", (int)params.arp_oct - 3);
      break;

    case MOD_ADV:
      out->knobs[0].title = "Temp";
      out->knobs[0].value = params.adv_tempo;
      if (params.adv_tempo == ADV_TEMPO_OFF) strcpy(out->knobs[0].value_str, "Off");
      else if (params.adv_tempo == ADV_TEMPO_EXT) strcpy(out->knobs[0].value_str, "Ext");
      else snprintf(out->knobs[0].value_str, 16, "%d", 30 + (int)(params.adv_tempo - ADV_TEMPO_BPM_MIN));

      out->knobs[1].title = "Scal";
      out->knobs[1].value = params.adv_scale * 127 / (ADV_SCALE_COUNT - 1);
      {
        static const char *scale_names[] = {
          "Off","Major","Minor","HMin","MMin","Dorian",
          "Locr","Lydian","Blues","MajP","MinP","Aug"
        };
        if (params.adv_scale < ADV_SCALE_COUNT) strcpy(out->knobs[1].value_str, scale_names[params.adv_scale]);
      }

      out->knobs[2].title = "Chrd";
      out->knobs[2].value = params.chord_mode * 127 / (CHORD_MODE_COUNT - 1);
      {
        static const char *chord_names[] = {
          "Off", "Oct", "Sus2", "Sus4", "Min", "Maj", "Dom7", "Min7",
          "Maj7", "Dim7", "Aug", "7#5"
        };
        if (params.chord_mode < CHORD_MODE_COUNT) strcpy(out->knobs[2].value_str, chord_names[params.chord_mode]);
      }
      break;

    case MOD_MEM:
      out->knobs[0].title = "Mode";
      out->knobs[0].value = mem_mode ? 127 : 0;
      strcpy(out->knobs[0].value_str, mem_mode ? "SAVE" : "LOAD");

      out->knobs[1].title = "Cart";
      out->knobs[1].value = mem_cartridge * 127 / 31;
      snprintf(out->knobs[1].value_str, 16, "%d", mem_cartridge + 1);

      out->knobs[2].title = "Slot";
      out->knobs[2].value = mem_slot * 127 / 31;
      snprintf(out->knobs[2].value_str, 16, "%d", mem_slot + 1);

      out->knobs[3].title = "Prog";
      out->knobs[3].value = mem_prog * 127 / 31;
      snprintf(out->knobs[3].value_str, 16, "%d", mem_prog + 1);
      break;

    default:
      break;
  }
}

void get_encoder_led(const OledPage *page, int enc_idx, uint8_t *r, uint8_t *g, uint8_t *b) {
  if (enc_idx < 0 || enc_idx >= 4 || !page) {
    *r = 0; *g = 0; *b = 0;
    return;
  }
  
  if (!page->knobs[enc_idx].title) {
    *r = 0; *g = 0; *b = 0;
    return;
  }
  
  uint8_t val = page->knobs[enc_idx].value;
  if (val > 127) val = 127;
  get_cold_hot_color((float)val / 127.0f, r, g, b);
}

void handle_params_encoders_lower_row(int d1, int d2, int d3, int d4) {
  (void)d1; (void)d2; (void)d3; (void)d4;
}

void ui_process_preset_longpress(uint32_t now) {
  (void)now;
}

void ui_midi_note_on(uint8_t note, uint8_t velocity) {
  synth_note_on(note, velocity);
}

void ui_midi_note_off(uint8_t note) {
  synth_note_off(note);
}
