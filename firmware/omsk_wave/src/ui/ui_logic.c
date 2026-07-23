#include "ui_logic.h"
#include "../sw_config.h"
#include "../midi/midi_handler.h"
#include "../midi/midi_map.h"
#include "../sequencer/sequencer.h"
#include "../synth/pra_synth.h"
#include "../synth/synth.h"
#include "../synth/synth_defs.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "tusb.h"
#include "ui_oled.h"

extern SynthParams params;
#include "ui_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../shared/hardware/colors.h"

#include "../tables/omsk_wavetables.h"
#include "../tables/vcf_lut_data.h"

UIMode ui_mode = UI_MODE_PIANO;
UIMode last_ui_mode = UI_MODE_PIANO;
ModuleID selected_module = MOD_VCO1;
ModuleID target_module = MOD_VCO1;
ModuleID last_active_module = MOD_VCO1; // For SET mode
#if CFG_ENABLE_DEBUG
static uint32_t log_panel_block_until_us;
#endif

// SET/FN Mode State
#define PAD_FN_IDX 15
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

bool ui_is_fn_held(void) {
    return fn_button_held || ((midi_pad_state >> 15) & 1);
}

int mod_edit_param_idx = 0;  // 0-3
int mod_edit_source_idx = 0; // 0-3 (LFO1, LFO2, EG1, EG2)

// ADV Mode Scale Key setting state
static bool is_adv_key_setting = false;

char ui_status_msg[32] = "";
uint32_t ui_status_msg_timeout_ms = 0;

void ui_set_status(const char *msg, uint32_t ms) {
    strncpy(ui_status_msg, msg, sizeof(ui_status_msg) - 1);
    ui_status_msg_timeout_ms = to_ms_since_boot(get_absolute_time()) + ms;
}

// Piano State
int octave = 0; // -4 to +4, base_notes[] relative to middle C (36 or 60 depending on engine)
uint16_t held_keys = 0; // Previous keys state
uint16_t combo_used_keys = 0; // Tracks keys used in combos to prevent single-press actions
uint16_t latched_keys = 0;
static bool preset_combo_held = false;

// Polyphony Tracking
// Maps Key Index (0-15) to MIDI Note (-1 if off)
int active_notes[16];
bool midi_held_notes[128];
uint16_t midi_note_mask = 0; // Bitmask for current octave notes (0-11)

// Arp State
uint32_t last_arp_step = 0;
int arp_note_index = 0;
int arp_notes[16];
int arp_note_count = 0;
int last_arp_note = -1;
bool arp_was_on = false; // To detect transition
static int arp_direction = 1;
static bool arp_note_is_on = false;
static int arp_step_parity = 0;

// Seq State
static uint8_t seq_edit_step = 0;
static bool seq_copy_armed = false;
static int seq_copy_source = -1;
static bool seq_save_armed = false;
static bool seq_load_armed = false;
static bool seq_param_mode = false; 

static const float swing_lut[128] = {
    0.5000f, 0.5020f, 0.5039f, 0.5059f, 0.5079f, 0.5098f, 0.5118f, 0.5138f, 0.5157f, 0.5177f, 
    0.5197f, 0.5217f, 0.5236f, 0.5256f, 0.5276f, 0.5295f, 0.5315f, 0.5335f, 0.5354f, 0.5374f, 
    0.5394f, 0.5413f, 0.5433f, 0.5453f, 0.5472f, 0.5492f, 0.5512f, 0.5531f, 0.5551f, 0.5571f, 
    0.5591f, 0.5610f, 0.5630f, 0.5650f, 0.5669f, 0.5689f, 0.5709f, 0.5728f, 0.5748f, 0.5768f, 
    0.5787f, 0.5807f, 0.5827f, 0.5846f, 0.5866f, 0.5886f, 0.5906f, 0.5925f, 0.5945f, 0.5965f, 
    0.5984f, 0.6004f, 0.6024f, 0.6043f, 0.6063f, 0.6083f, 0.6102f, 0.6122f, 0.6142f, 0.6161f, 
    0.6181f, 0.6201f, 0.6220f, 0.6240f, 0.6260f, 0.6280f, 0.6299f, 0.6319f, 0.6339f, 0.6358f, 
    0.6378f, 0.6398f, 0.6417f, 0.6437f, 0.6457f, 0.6476f, 0.6496f, 0.6516f, 0.6535f, 0.6555f, 
    0.6575f, 0.6594f, 0.6614f, 0.6634f, 0.6654f, 0.6673f, 0.6693f, 0.6713f, 0.6732f, 0.6752f, 
    0.6772f, 0.6791f, 0.6811f, 0.6831f, 0.6850f, 0.6870f, 0.6890f, 0.6909f, 0.6929f, 0.6949f, 
    0.6969f, 0.6988f, 0.7008f, 0.7028f, 0.7047f, 0.7067f, 0.7087f, 0.7106f, 0.7126f, 0.7146f, 
    0.7165f, 0.7185f, 0.7205f, 0.7224f, 0.7244f, 0.7264f, 0.7283f, 0.7303f, 0.7323f, 0.7343f, 
    0.7362f, 0.7382f, 0.7402f, 0.7421f, 0.7441f, 0.7461f, 0.7480f, 0.7500f
};

// Notes Map
// Row-major mapping based on user table:
// Row 0: C, D#, F#, A
// Row 1: C#, E, G, A#
// Row 2: D, F, G#, B
const int base_notes[12] = {
    36, 39, 42, 45, // C1, D#1, F#1, A1
    37, 40, 43, 46, // C#1, E1, G1, A#1
    38, 41, 44, 47  // D1, F1, G#1, B1
};
// const int base_notes[12] = {12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23};
// Helper: Map piano key index (0-11) to label from CFG_PIANO_LAYOUT in config.h
const char *get_piano_key_label_from_index(int idx) {
  if (idx < 0 || idx >= (CFG_PIANO_LAYOUT_ROWS * CFG_PIANO_LAYOUT_COLS)) {
    return "";
  }
  int row = idx / CFG_PIANO_LAYOUT_COLS;
  int col = idx % CFG_PIANO_LAYOUT_COLS;
  if (row < 0 || row >= CFG_PIANO_LAYOUT_ROWS || col < 0 ||
      col >= CFG_PIANO_LAYOUT_COLS) {
    return "";
  }
  return CFG_PIANO_LAYOUT[row][col];
}

// Mapping for Params Mode: Button Index -> ModuleID
// Layout matches layout.md C1..C4, top to bottom
const ModuleID btn_to_mod[16] = {
    MOD_VCO1,  MOD_VCF1, MOD_LFO1,  MOD_EG1,
    MOD_VCO2,  MOD_VCF2, MOD_LFO2,  MOD_EG2,
    MOD_NOISE, MOD_FX1,  MOD_GLIDE, MOD_SET,
    MOD_MIXER, MOD_FX2,  MOD_MOD,   MOD_FN
};

// --- Helpers ---

bool is_modulator(ModuleID m) {
  return (m == MOD_LFO1 || m == MOD_LFO2 || m == MOD_EG1 || m == MOD_EG2);
}

bool is_filter(ModuleID m) { return (m == MOD_VCF1 || m == MOD_VCF2); }

bool is_source(ModuleID m) {
  return (m == MOD_VCO1 || m == MOD_VCO2 || m == MOD_NOISE);
}

int get_mod_source_idx(ModuleID m) {
  switch (m) {
  case MOD_LFO1:
    return 0;
  case MOD_LFO2:
    return 1;
  case MOD_EG1:
    return 2;
  case MOD_EG2:
    return 3;
  default:
    return -1;
  }
}

const char *module_name(ModuleID m) {
  switch (m) {
  case MOD_VCO1:
    return "VCO1";
  case MOD_VCO2:
    return "VCO2";
  case MOD_VCF1:
    return "VCF1";
  case MOD_VCF2:
    return "VCF2";
  case MOD_LFO1:
    return "LFO1";
  case MOD_LFO2:
    return "LFO2";
  case MOD_EG1:
    return "EG1";
  case MOD_EG2:
    return "EG2";
  case MOD_MIXER:
    return "MIX";
  case MOD_NOISE:
    return "NOISE";
  case MOD_ARP:
    return "ARP";
  case MOD_GLIDE:
    return "GLIDE";
  case MOD_FX1:
    return "FX1";
  case MOD_FX2:
    return "FX2";
  case MOD_SET:
    return "SET";
  case MOD_MOD:
    return "MOD";
  case MOD_FN:
    return "FN";
  case MOD_ADV:
    return "ADV";
  default:
    return "OTHER";
  }
}

bool set_button_held = false;
bool assignment_mode = false;
uint16_t midi_pad_state = 0;
uint32_t set_press_time = 0;

void handle_module_press(ModuleID m) {
  if (set_mode_active || set_button_held || assignment_mode) {
    if (is_modulator(m)) {
      set_context_src_override = -1;
    }
    if (is_filter(m) && !is_modulator(set_context_module)) {
      if (m != (ModuleID)set_context_module) {
          set_context_module = m;
          selected_module = m;
#if CFG_ENABLE_DEBUG
          if (time_us_32() > log_panel_block_until_us) {
              DBG_PRINTF("PANEL SELECT %s\n", module_name(m));
              log_panel_block_until_us = time_us_32() + 300000;
          }
#endif
          return;
      }
    }

    if (is_filter(set_context_module) && (is_source(m) || is_filter(m))) {
      int route_val = (set_context_module == MOD_VCF1) ? 1 : 2;
      
      if (m == (ModuleID)set_context_module) {
        // Clear all routings for current VCF
        if (params.route_vco1 == route_val) params.route_vco1 = 0;
        if (params.route_vco2 == route_val) params.route_vco2 = 0;
        if (params.route_noise == route_val) params.route_noise = 0;
        
        char buf[32];
        snprintf(buf, sizeof(buf), "%s CLEARED", (route_val == 1 ? "VCF1" : "VCF2"));
        ui_set_status(buf, 1500);
        return;
      }
      
      if (is_source(m)) {
        if (m == MOD_VCO1)
          params.route_vco1 = (params.route_vco1 == route_val) ? 0 : route_val;
        else if (m == MOD_VCO2)
          params.route_vco2 = (params.route_vco2 == route_val) ? 0 : route_val;
        else if (m == MOD_NOISE)
          params.route_noise = (params.route_noise == route_val) ? 0 : route_val;
        
        char buf[32];
        const char* src_name = (m == MOD_VCO1 ? "VCO1" : (m == MOD_VCO2 ? "VCO2" : "NOISE"));
        int current_route = (m == MOD_VCO1 ? params.route_vco1 : (m == MOD_VCO2 ? params.route_vco2 : params.route_noise));
        snprintf(buf, sizeof(buf), "%s -> %s: %s", src_name, (route_val == 1 ? "VCF1" : "VCF2"), 
                (current_route == route_val ? "ON" : "OFF"));
        ui_set_status(buf, 1500);
        return;
      }
      return;
    } else if (is_modulator(set_context_module)) {
      if (m == (ModuleID)set_context_module) {
        // Clear all routings for this modulator
        int src = get_mod_source_idx(set_context_module);
        if (src >= 0) {
          for (int p = 0; p < PARAM_COUNT; p++) {
            params.mod_matrix[p][src] = 64; // Reset to 0%
            if (params.mod_source_assigned[p] == src) {
                params.mod_source_assigned[p] = 0xFF;
            }
          }
        }
        
        if (set_context_module == MOD_MOD) {
            params.mod_routing1 = 0;
            params.mod_depth1 = 64;
            params.mod_routing2 = 0;
            params.mod_depth2 = 64;
        }

        char buf[32];
        snprintf(buf, sizeof(buf), "%s CLEARED", module_name(set_context_module));
        ui_set_status(buf, 1500);
        return;
      }
      selected_module = m;
      return;
    } else {
      selected_module = m;
      return;
    }
  }

  if (fn_button_held || fn_mode_active) {
    ParamID base = get_base_param_id(m);
    if (base != (ParamID)-1) {
        for (int i = 0; i < 4; i++) {
            uint8_t *p = midi_map_get_param_ptr((ParamID)(base + i));
            if (p) {
                *p = rand() % 128;
            }
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "%s RANDOMIZED", module_name(m));
        ui_set_status(buf, 1500);
    }
    selected_module = m;
    return;
  }


  if (ui_mode == UI_MODE_PIANO) {
      last_active_module = selected_module; // Save current layer
      last_ui_mode = UI_MODE_PIANO;
      ui_mode = UI_MODE_PARAMS;
  }
  selected_module = m;
  
  if (m != MOD_SET && m != MOD_MOD) {
    target_module = m;
  }
#if CFG_ENABLE_DEBUG
  if (time_us_32() > log_panel_block_until_us) {
      DBG_PRINTF("PANEL SELECT %s\n", module_name(m));
      log_panel_block_until_us = time_us_32() + 300000;
  }
#endif
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
        // Reset SET/FN on return to Piano mode
        set_mode_active = false;
        fn_mode_active = false;
        set_context_module = MOD_NONE;
        target_module = selected_module;
    }
  // Clear notes on mode switch
  for (int i = 0; i < 16; i++) {
    if (active_notes[i] != -1) {
      synth_note_off(active_notes[i]);
      active_notes[i] = -1;
    }
  }
}

void ui_handle_pad_pressed(uint8_t pad_index) {
  if (pad_index >= 16)
    return;

  // Send MIDI CC for physical button press (only if not a piano key in piano mode)
  if (ui_mode != UI_MODE_PIANO || pad_index >= 12) {
    uint8_t status = (uint8_t)(0xB0 | (params.midi_channel & 0x0F));
    uint8_t msg[3] = {status, (uint8_t)(40 + pad_index), 127};
    midi_send_message(msg, 3);
  }

  // Update held_keys first for combo detection
  held_keys |= (1 << pad_index);

  // Detect LFO1 (2) + EG1 (3) combo for Preset Mode — PARAMS mode only
  if (ui_mode == UI_MODE_PARAMS) {
    if ((held_keys & (1 << 2)) && (held_keys & (1 << 3))) {
        preset_combo_held = true;
        combo_used_keys |= (1 << 2) | (1 << 3);
    }
  }

  if (preset_mode_active) {
    preset_hold_start[pad_index] = to_ms_since_boot(get_absolute_time());
    preset_hold_used[pad_index] = false;
    return;
  }

  if (ui_mode == UI_MODE_SEQ) {
      // ARP+ADV -> Exit SEQ
      if ((held_keys & (1 << 14)) && (held_keys & (1 << 15))) {
          ui_mode = last_ui_mode;
          char buf[32]; snprintf(buf, sizeof(buf), (ui_mode == UI_MODE_PARAMS) ? "PARAMS MODE" : "PIANO MODE");
          ui_set_status(buf, 1000);
          held_keys &= ~((1<<14) | (1<<15));
          combo_used_keys |= ((1<<14) | (1<<15));
          return;
      }
      // STP10+STP11 -> Play/Pause
      if ((held_keys & (1 << 9)) && (held_keys & (1 << 10))) {
          if (seq_state.is_playing) {
              seq_stop();
              ui_set_status("SEQ: STOP", 1000);
          } else {
              seq_start();
              ui_set_status("SEQ: PLAY", 1000);
          }
          held_keys &= ~((1<<9) | (1<<10)); // Clear to debounce
          combo_used_keys |= ((1<<9) | (1<<10));
          return;
      }
      static bool seq_waiting_for_stop_step = false;
      static bool seq_waiting_for_edit_step = false;

      // Check waiting flags first
      if (seq_waiting_for_stop_step) {
          seq_waiting_for_stop_step = false;
          uint8_t step = seq_state.current_page * 16 + pad_index;
          current_seq.steps[step].stop_flag = !current_seq.steps[step].stop_flag;
          char buf[32]; snprintf(buf, sizeof(buf), current_seq.steps[step].stop_flag ? "STOP AT %d" : "CLEAR STOP", step + 1);
          ui_set_status(buf, 1500);
          return;
      }
      
      if (seq_waiting_for_edit_step) {
          seq_waiting_for_edit_step = false;
          seq_edit_step = seq_state.current_page * 16 + pad_index;
          ui_mode = UI_MODE_SEQ_EDIT;
          char buf[32]; snprintf(buf, sizeof(buf), "EDIT STEP %d", seq_edit_step + 1);
          ui_set_status(buf, 2000);
          return;
      }

      // STP14+STP15 -> Stop step
      if ((held_keys & (1 << 13)) && (held_keys & (1 << 14))) {
          seq_waiting_for_stop_step = true;
          ui_set_status("PRESS STOP STEP", 2000);
          held_keys &= ~((1<<13) | (1<<14)); // Clear combo
          combo_used_keys |= ((1<<13) | (1<<14));
          return;
      }
      // STP1+STP2 -> Edit step
      if ((held_keys & (1 << 0)) && (held_keys & (1 << 1))) {
          seq_waiting_for_edit_step = true;
          ui_set_status("SELECT EDIT STEP", 2000);
          held_keys &= ~((1<<0) | (1<<1)); // Clear combo
          combo_used_keys |= ((1<<0) | (1<<1));
          return;
      }
      // Page navigation
      if ((held_keys & (1 << 4)) && (held_keys & (1 << 8))) {
          if (seq_state.current_page > 0) seq_state.current_page--;
          else seq_state.current_page = 3;
          char buf[16]; snprintf(buf, sizeof(buf), "PAGE %d", seq_state.current_page + 1);
          ui_set_status(buf, 1000);
          held_keys &= ~((1<<4) | (1<<8));
          combo_used_keys |= ((1<<4) | (1<<8));
          return;
      }
      if ((held_keys & (1 << 7)) && (held_keys & (1 << 11))) {
          if (seq_state.current_page < 3) seq_state.current_page++;
          else seq_state.current_page = 0;
          char buf[16]; snprintf(buf, sizeof(buf), "PAGE %d", seq_state.current_page + 1);
          ui_set_status(buf, 1000);
          held_keys &= ~((1<<7) | (1<<11));
          combo_used_keys |= ((1<<7) | (1<<11));
          return;
      }

      // Pads 0 and 1 are reserved for the STP1+STP2 edit combo
      if (pad_index == 0 || pad_index == 1) return;

      // Single press -> toggle mute
      uint8_t step = seq_state.current_page * 16 + pad_index;
      current_seq.steps[step].notes[0].enabled = !current_seq.steps[step].notes[0].enabled;
      return;
  }

  if (ui_mode == UI_MODE_SEQ_EDIT) {
      if (pad_index < 12) {
          static const uint8_t pad_to_note[] = {
              0, 2, 4, 5,  // C, D, E, F
              7, 9, 11, 1, // G, A, B, C#
              3, 6, 8, 10  // D#, F#, G#, A#
          };
          uint8_t n = pad_to_note[pad_index];
          static const char* note_names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
          current_seq.steps[seq_edit_step].notes[0].note = (params.arp_oct + 2) * 12 + n;
          current_seq.steps[seq_edit_step].notes[0].enabled = true;
          char buf[32]; snprintf(buf, sizeof(buf), "NOTE %s%d", note_names[n], params.arp_oct + 2);
          ui_set_status(buf, 1000);
          return;
      } else if (pad_index == 12) { // OCT-
          if (params.arp_oct > 0) params.arp_oct--;
          ui_set_status("OCT-", 1000);
          return;
      } else if (pad_index == 13) { // OCT+
          if (params.arp_oct < 8) params.arp_oct++;
          ui_set_status("OCT+", 1000);
          return;
      } else if (pad_index == 14) { // Clear
          current_seq.steps[seq_edit_step].notes[0].enabled = false;
          ui_set_status("STEP CLEARED", 1000);
          return;
      } else if (pad_index == 15) { // Done
          ui_mode = UI_MODE_SEQ;
          ui_set_status("SEQ MODE", 1000);
          return;
      }
      return; 
  }



  // Detect Setup Mode switch: OCT- (12) + OCT+ (13)
  if (ui_mode == UI_MODE_PIANO) {
      if ((held_keys & (1 << 12)) && (held_keys & (1 << 13))) {
          ui_handle_mode_switch(); // Will switch to UI_MODE_PARAMS
          combo_used_keys |= ((1<<12) | (1<<13));
          return;
      }
  }

  // Detect Piano Mode switch in PARAMS mode: MIXER (12) + FX2 (13)
  if (ui_mode == UI_MODE_PARAMS) {
      if ((held_keys & (1 << 12)) && (held_keys & (1 << 13))) {
          ui_handle_mode_switch(); // Will switch to UI_MODE_PIANO
          combo_used_keys |= ((1<<12) | (1<<13));
          return;
      }
  }

  // Detect Hold toggle: OCT+ (13) + ARP (14)
  if (ui_mode == UI_MODE_PIANO) {
      if ((held_keys & (1 << 13)) && (held_keys & (1 << 14))) {
          bool active = !pra_synth_is_hold_mode();
          pra_synth_set_hold_mode(active);
          if (active) {
              ui_set_status("HOLD ON", 2000);
          } else {
              ui_set_status("HOLD OFF", 2000);
              latched_keys = 0; // Clear latched keys when Hold is off
              if (params.arp_mode > 0) update_arp_notes();
          }
          combo_used_keys |= ((1<<13) | (1<<14));
          return;
      }
  }

  // Check for exit from ADV mode to save changes
  static ModuleID last_mod = MOD_NONE;
  static uint8_t last_midi_ch = 0xFF;
  if (selected_module != last_mod) {
      if (last_mod == MOD_ADV && last_midi_ch != params.midi_channel) {
          synth_preset_save(0); // Save to slot 0 or current? User just said save.
          DBG_PRINTF("ADV: MIDI CH changed, saving to flash\n");
      }
      last_mod = selected_module;
      last_midi_ch = params.midi_channel;
      // Reset ADV specific states when changing module
      is_adv_key_setting = false;
  }

  // Handle Piano Mode Notes & Octaves
  if (ui_mode == UI_MODE_PIANO) {
    if (pad_index < 12) {
      // Note On
      int note = base_notes[pad_index] + (octave * 12);
      if (note < 0)
        note = 0;
      if (note > 127)
        note = 127;

      if (pra_synth_is_hold_mode()) {
          latched_keys |= (1 << pad_index);
      }

      if (params.arp_mode > 0) {
          update_arp_notes();
          return;
      }

      if (active_notes[pad_index] != -1)
        synth_note_off(active_notes[pad_index]);
      active_notes[pad_index] = note;
      synth_note_on(note, 127);

      {
        uint8_t status = (uint8_t)(0x90 | (params.midi_channel & 0x0F));
        uint8_t msg[3] = {status, (uint8_t)note, 127};
        midi_send_message(msg, 3);
      }
      return;
    } else if (pad_index == 12) {
      // OCT-
      if (octave > -4)
        octave--;
      return;
    } else if (pad_index == 13) {
      // OCT+
      if (octave < 4)
        octave++;
      return;
    } else if (pad_index == 14) {
      // Defer to release
      return;
    } else if (pad_index == 15) {
      // ADV key
      // Combo: OCT- (12) + ADV (15) -> cycle Scale Key
      if (held_keys & (1 << 12)) {
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
      // Combo: ARP (14) + ADV (15) -> toggle Seq mode
      if (held_keys & (1 << 14)) {
        if (ui_mode != UI_MODE_SEQ) {
            last_ui_mode = ui_mode;
            ui_mode = UI_MODE_SEQ;
            ui_set_status("SEQ MODE", 2000);
            // Clear notes on mode change
            for (int i = 0; i < 16; i++) {
                if (active_notes[i] != -1) {
                    synth_note_off(active_notes[i]);
                    active_notes[i] = -1;
                }
            }
        } else {
            // Toggle back to previous mode (Piano or Params)
            ui_mode = last_ui_mode;
            ui_set_status("SEQ OFF", 2000);
        }
        held_keys &= ~((1<<14) | (1<<15));
        combo_used_keys |= ((1<<14) | (1<<15));
        return;
      }
      // ADV alone - Defer to release
      return;
    }
  }

  // Special pad handling in ADV mode
  if (ui_mode == UI_MODE_PARAMS && selected_module == MOD_ADV) {
      static const char *key_names[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

      if (is_adv_key_setting && pad_index < 12) {
          // Finish Key selection
          // Map pad_index to semitone (0-11) using base_notes relationship
          params.adv_scale_key = (base_notes[pad_index] - 36) % 12;
          char buf[32];
          snprintf(buf, sizeof(buf), "KEY: %s SET", key_names[params.adv_scale_key]);
          ui_set_status(buf, 2000);
          is_adv_key_setting = false;
          return;
      }

      if (pad_index < 12) {
          // Play note instead of setting key
          int note = base_notes[pad_index] + (octave * 12);
          if (note < 0) note = 0;
          if (note > 127) note = 127;
          
          if (active_notes[pad_index] != -1)
            synth_note_off(active_notes[pad_index]);
          active_notes[pad_index] = note;
          synth_note_on(note, 127);

          {
            uint8_t status = (uint8_t)(0x90 | (params.midi_channel & 0x0F));
            uint8_t msg[3] = {status, (uint8_t)note, 127};
            midi_send_message(msg, 3);
          }
          return;
      }
      if (pad_index == 12) { // Oct-
          if (octave > -4) octave--;
          return;
      }
      if (pad_index == 13) { // Oct+
          if (octave < 4) octave++;
          return;
      }
      if (pad_index == 14) { // Key (Initiate Setting)
          is_adv_key_setting = true;
          ui_set_status("SET SCALE KEY: PRESS NOTE", 5000);
          return;
      }
      if (pad_index == 15) { // Return to PIANO mode
          is_adv_key_setting = false;
          ui_mode = UI_MODE_PIANO;
          selected_module = last_active_module;
          ui_set_status("PIANO MODE", 1000);
          return;
      }
  }

  if (ui_mode == UI_MODE_PARAMS && selected_module == MOD_ARP) {
      if (pad_index == 15) { // Pad 16 -> ADV
          // Defer to release
          return;
      }
  }

  ModuleID m = btn_to_mod[pad_index];

  if (m == MOD_SET) {
    if (ui_is_fn_held()) {
        ui_oled_view_graph = !ui_oled_view_graph;
        ui_set_status(ui_oled_view_graph ? "GRAPH VIEW" : "KNOBS VIEW", 1500);
        return;
    }
    set_press_time = to_ms_since_boot(get_absolute_time());
    set_button_held = true;
    if (is_modulator(selected_module) || is_filter(selected_module)) {
      set_context_module = selected_module;
    }
    DBG_PRINTF("SET_ON\n");
    return;
  }

  if (pad_index == PAD_FN_IDX) {
    if (set_button_held) {
        ui_oled_view_graph = !ui_oled_view_graph;
        ui_set_status(ui_oled_view_graph ? "GRAPH VIEW" : "KNOBS VIEW", 1500);
        return;
    }
    fn_button_held = true;
    DBG_PRINTF("FN_ON\n");
    return;
  }


  // We no longer trigger module selection on press.
  // It is deferred to ui_handle_pad_released to prevent combo conflicts.
}

void ui_handle_pad_released(uint8_t pad_index) {
  if (pad_index >= 16)
    return;

  // Send MIDI CC for physical button release (only if not a piano key in piano mode)
  if (ui_mode != UI_MODE_PIANO || pad_index >= 12) {
    uint8_t status = (uint8_t)(0xB0 | (params.midi_channel & 0x0F));
    uint8_t msg[3] = {status, (uint8_t)(40 + pad_index), 0};
    midi_send_message(msg, 3);
  }

  // Handle Preset Combo Release
  if (pad_index == 2 || pad_index == 3) {
      uint16_t current_held = held_keys & ~(1 << pad_index);
      if (preset_combo_held && !(current_held & (1 << 2)) && !(current_held & (1 << 3))) {
          // Both special buttons released after being held together
          preset_mode_active = true;
          preset_combo_held = false;
          // Reset other mode flags to avoid conflicts
          set_mode_active = false;
          fn_mode_active = false;
          assignment_mode = false;
          DBG_PRINTF("PRESET_MODE_ON\n");
          ui_set_status("Select 1 of 16 slots", 5000);
          // Clear held_keys bit for these
          held_keys &= ~(1 << pad_index);
          return;
      }
      // Note: We don't call handle_module_press here because it's now back in ui_handle_pad_pressed
  }

  // Update held_keys
  held_keys &= ~(1 << pad_index);

  bool was_in_combo = (combo_used_keys & (1 << pad_index)) != 0;
  if (was_in_combo) {
      combo_used_keys &= ~(1 << pad_index);
  }

  if (preset_mode_active) {
    if (preset_hold_start[pad_index] != 0 && !preset_hold_used[pad_index]) {
      if (synth_preset_load(pad_index)) {
          char buf[32];
          snprintf(buf, sizeof(buf), "Preset loaded: Slot %d", pad_index + 1);
          ui_set_status(buf, 2000);
      } else {
          ui_set_status("Load failed", 2000);
      }
      preset_mode_active = false;
    }
    preset_hold_start[pad_index] = 0;
    preset_hold_used[pad_index] = false;
    return;
  }

  // Handle Note Off (Piano or ADV mode)
  if (ui_mode == UI_MODE_PIANO || (ui_mode == UI_MODE_PARAMS && selected_module == MOD_ADV)) {
      if (pad_index < 12) {
          if (ui_mode == UI_MODE_PIANO && params.arp_mode > 0) {
              update_arp_notes();
              return;
          }
          
          if (active_notes[pad_index] != -1) {
              int note_to_off = active_notes[pad_index];
              synth_note_off(note_to_off);
              active_notes[pad_index] = -1;
              
              {
                  uint8_t status = (uint8_t)(0x80 | (params.midi_channel & 0x0F));
                  uint8_t msg[3] = {status, (uint8_t)note_to_off, 0};
                  midi_send_message(msg, 3);
              }
          }
          return;
      }
  }

  if (ui_mode == UI_MODE_PARAMS && !was_in_combo) {
      ModuleID m = btn_to_mod[pad_index];
      if (m != MOD_SET && pad_index != PAD_FN_IDX) {
          handle_module_press(m);
      }
  } else if (ui_mode == UI_MODE_PIANO && !was_in_combo) {
      if (pad_index == 14) {
          handle_module_press(MOD_ARP);
      } else if (pad_index == 15) {
          handle_module_press(MOD_ADV);
      }
  }

  ModuleID m = btn_to_mod[pad_index];

  if (m == MOD_SET) {
      uint32_t now_ms = to_ms_since_boot(get_absolute_time());
      if (set_button_held && (now_ms - set_press_time) < 300) {
          // Short press: Toggle Assignment Mode
          assignment_mode = !assignment_mode;
          if (assignment_mode) {
              if (is_modulator(selected_module)) {
                  set_context_module = selected_module;
              } else if (last_mod_source != MOD_NONE) {
                  set_context_module = last_mod_source;
              } else {
                  set_context_module = MOD_LFO1;
              }
              DBG_PRINTF("ASSIGN_MODE_ON Context=%s\n", module_name(set_context_module));
              char buf[32];
              snprintf(buf, sizeof(buf), "ASSIGN: %s", module_name(set_context_module));
              ui_set_status(buf, 1500);
          } else {
              DBG_PRINTF("ASSIGN_MODE_OFF\n");
              ui_set_status("ASSIGN OFF", 1000);
          }
      }
      set_button_held = false;
      DBG_PRINTF("SET_OFF\n");
      return;
  }

  if (pad_index == PAD_FN_IDX) {
      fn_button_held = false;
      DBG_PRINTF("FN_OFF\n");
      return;
  }
}

void ui_process_preset_longpress(uint32_t now) {
  if (!preset_mode_active)
    return;
  for (int i = 0; i < 16; i++) {
    if (preset_hold_start[i] != 0 && !preset_hold_used[i]) {
      if ((uint32_t)(now - preset_hold_start[i]) >= 2000) { // 2 seconds
        ui_set_status("SAVING...", 1000);
        if (synth_preset_save((uint8_t)i)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Preset saved: Slot %d", i + 1);
            ui_set_status(buf, 2000);
        } else {
            ui_set_status("Save failed", 2000);
        }
        preset_hold_used[i] = true;
        preset_mode_active = false;
        // Reset all hold timers
        for (int j = 0; j < 16; j++) {
          preset_hold_start[j] = 0;
          preset_hold_used[j] = false;
        }
        break;
      }
    }
  }
}

void handle_module_select_from_pad(uint8_t pad_index) {
  ui_handle_pad_pressed(pad_index);
}

int get_module_index(ModuleID m) {
  for (int i = 0; i < 16; i++) {
    if (btn_to_mod[i] == m)
      return i;
  }
  return -1;
}

ModuleID get_module_below(ModuleID m) {
  int idx = get_module_index(m);
  if (idx < 0)
    return MOD_NONE;
  int below = (idx + 4) % 16;
  return btn_to_mod[below];
}

void update_param(uint8_t *param, int delta);
void update_param_wrap(uint8_t *param, int delta);
static void update_param_choice(uint8_t *param, int delta, uint8_t max_val);
void update_param_with_id(ParamID param_id, uint8_t *param, int delta);
void update_param_wrap_with_id(ParamID param_id, uint8_t *param, int delta);
static void update_param_choice_with_id(ParamID param_id, uint8_t *param,
                                        int delta, uint8_t max_val);
void log_chord_mode_if_changed(void);

void update_param16_with_id(ParamID param_id, uint16_t *param, int delta, uint16_t max) {
  uint16_t prev = *param;
  int val = (int)*param + delta;
  if (val < 0) val = 0;
  if (val > (int)max) val = (int)max;
  *param = (uint16_t)val;
  if (*param != prev) {
    pra_synth_param_change(param_id, *param);
  }
}

static void ui_cycle_vcf_mode(uint8_t *vcf_type, int delta, const char *vcf_name) {
    if (!delta) return;
    *vcf_type = (*vcf_type + (delta > 0 ? 1 : 4)) % 5;
    char buf[32];
    static const char* vcf_types[] = {"LPF", "BPF", "HPF", "BCF", "APF"};
    snprintf(buf, sizeof(buf), "%s: %s", vcf_name, vcf_types[*vcf_type % 5]);
    ui_set_status(buf, 1500);
    pra_synth_param_change(PARAM_VCF_MODE, (*vcf_type) * 32);
}

static void ui_update_eg_curve(ParamID param_id, uint8_t *param, int delta, const char *prefix) {
    if (!delta) return;
    int val = (int)*param + delta;
    if (val < 0) val = 0;
    if (val > 127) val = 127;
    *param = (uint8_t)val;
    pra_synth_param_change(param_id, *param);
}

void handle_params_encoders_for_module(ModuleID mod, int d1, int d2, int d3,
                                       int d4) {
  switch (mod) {
  case MOD_VCO1:
    if (d1)
      update_param_choice_with_id(PARAM_VCO1_TRANSPOSE, &params.vco1_transpose, d1, 10);
    if (d2)
      update_param_with_id(PARAM_VCO1_DETUNE, &params.vco1_detune, d2);
    if (d3)
      update_param_with_id(PARAM_VCO1_WAVE, &params.vco1_wave, d3);
    if (d4)
      update_param_with_id(PARAM_VCO1_SHAPE, &params.vco1_shape, d4);
    break;
  case MOD_VCO2:
    if (d1)
      update_param_choice_with_id(PARAM_VCO2_TRANSPOSE, &params.vco2_transpose, d1, 10);
    if (d2)
      update_param_with_id(PARAM_VCO2_DETUNE, &params.vco2_detune, d2);
    if (d3)
      update_param_with_id(PARAM_VCO2_WAVE, &params.vco2_wave, d3);
    if (d4)
      update_param_with_id(PARAM_VCO2_SHAPE, &params.vco2_shape, d4);
    break;
  case MOD_VCF1:
    if (d1) {
        if (ui_is_fn_held()) {
            ui_cycle_vcf_mode(&params.vcf1_type, d1, "VCF1");
        } else {
            update_param_with_id(PARAM_VCF1_CUTOFF, &params.vcf1_cutoff, d1);
        }
    }
    if (d2)
      update_param_with_id(PARAM_VCF1_RES, &params.vcf1_res, d2);
    if (d3)
      update_param_with_id(PARAM_VCF_KEY_TRACK, &params.vcf_key_track, d3);
    if (d4)
      update_param_with_id(PARAM_VCF1_MIX, &params.vcf1_mix, d4);
    break;
  case MOD_VCF2:
    if (d1) {
        if (ui_is_fn_held()) {
            ui_cycle_vcf_mode(&params.vcf2_type, d1, "VCF2");
        } else {
            update_param_with_id(PARAM_VCF2_CUTOFF, &params.vcf2_cutoff, d1);
        }
    }
    if (d2)
      update_param_with_id(PARAM_VCF2_RES, &params.vcf2_res, d2);
    if (d3)
      update_param_with_id(PARAM_VCF_KEY_TRACK, &params.vcf_key_track, d3);
    if (d4)
      update_param_with_id(PARAM_VCF2_MIX, &params.vcf2_mix, d4);
    break;
  case MOD_EG1:
    if (d1) {
        if (ui_is_fn_held()) ui_update_eg_curve(PARAM_EG1_ATTACK_CURVE, &params.eg1_attack_curve, d1, "EG1");
        else update_param_with_id(PARAM_EG1_ATTACK, &params.eg1_attack, d1);
    }
    if (d2) {
        if (ui_is_fn_held()) ui_update_eg_curve(PARAM_EG1_DECAY_CURVE, &params.eg1_decay_curve, d2, "EG1");
        else update_param_with_id(PARAM_EG1_DECAY, &params.eg1_decay, d2);
    }
    if (d3)
      update_param_with_id(PARAM_EG1_SUSTAIN, &params.eg1_sustain, d3);
    if (d4) {
        if (ui_is_fn_held()) ui_update_eg_curve(PARAM_EG1_RELEASE_CURVE, &params.eg1_release_curve, d4, "EG1");
        else update_param_with_id(PARAM_EG1_RELEASE, &params.eg1_release, d4);
    }
    break;
  case MOD_EG2:
    if (d1) {
        if (ui_is_fn_held()) ui_update_eg_curve(PARAM_EG2_ATTACK_CURVE, &params.eg2_attack_curve, d1, "EG2");
        else update_param_with_id(PARAM_EG2_ATTACK, &params.eg2_attack, d1);
    }
    if (d2) {
        if (ui_is_fn_held()) ui_update_eg_curve(PARAM_EG2_DECAY_CURVE, &params.eg2_decay_curve, d2, "EG2");
        else update_param_with_id(PARAM_EG2_DECAY, &params.eg2_decay, d2);
    }
    if (d3)
      update_param_with_id(PARAM_EG2_SUSTAIN, &params.eg2_sustain, d3);
    if (d4) {
        if (ui_is_fn_held()) ui_update_eg_curve(PARAM_EG2_RELEASE_CURVE, &params.eg2_release_curve, d4, "EG2");
        else update_param_with_id(PARAM_EG2_RELEASE, &params.eg2_release, d4);
    }
    break;
  case MOD_LFO1:
    if (d1)
      update_param_with_id(PARAM_LFO1_RATE, &params.lfo1_rate, d1);
    if (d2)
      update_param_with_id(PARAM_LFO1_SMOOTH, &params.lfo1_smooth, d2);
    if (d3)
      update_param_wrap_with_id(PARAM_LFO1_WAVE, &params.lfo1_wave, d3);
    if (d4)
      update_param_with_id(PARAM_LFO1_SHAPE, &params.lfo1_shape, d4);
    break;
  case MOD_LFO2:
    if (d1)
      update_param_with_id(PARAM_LFO2_RATE, &params.lfo2_rate, d1);
    if (d2)
      update_param_with_id(PARAM_LFO2_SMOOTH, &params.lfo2_smooth, d2);
    if (d3)
      update_param_wrap_with_id(PARAM_LFO2_WAVE, &params.lfo2_wave, d3);
    if (d4)
      update_param_with_id(PARAM_LFO2_SHAPE, &params.lfo2_shape, d4);
    break;
  case MOD_NOISE:
    if (d1)
      update_param_with_id(PARAM_NOISE_COLOR, &params.noise_color, d1);
    // Chord mode removed from this page
    break;
  case MOD_ARP:
    if (d1)
      update_param_choice_with_id(PARAM_ARP_RATE, &params.arp_rate, d1, 17);
    if (d2) {
      uint8_t prev_mode = params.arp_mode;
      int val = (int)params.arp_mode + (d2 > 0 ? 1 : -1);
      if (val < 0)
        val = 0;
      if (val > 5)
        val = 5;
      params.arp_mode = (uint8_t)val;

      if (params.arp_mode != prev_mode) {
        pra_synth_param_change(PARAM_ARP_MODE, params.arp_mode);
        if (params.arp_mode > 0) {
          if (params.arp_rate == 0)
            params.arp_rate = 12;
          if (params.arp_swing == 0)
            params.arp_swing = 100;
          if (params.arp_oct == 0)
            params.arp_oct = 3;
          update_arp_notes();
        } else {
          if (last_arp_note != -1) {
            synth_note_off(last_arp_note);
            last_arp_note = -1;
          }
        }
      }
    }
    if (d3)
      update_param_with_id(PARAM_ARP_SWING, &params.arp_swing, d3);
    if (d4)
      update_param_choice_with_id(PARAM_ARP_OCT, &params.arp_oct, d4, 6);
    break;
  case MOD_FX1:
    if (d1)
      update_param_with_id(PARAM_FX1_TIME, &params.fx1_time, d1);
    if (d2)
      update_param_with_id(PARAM_FX1_FEEDBACK, &params.fx1_feedback, d2);
    if (d3)
      update_param_with_id(PARAM_FX1_SPREAD, &params.fx1_spread, d3);
    if (d4)
      update_param_with_id(PARAM_FX1_MIX, &params.fx1_mix, d4);
    break;
  case MOD_FX2:
    if (d1)
      update_param_with_id(PARAM_FX2_TIME, &params.fx2_time, d1);
    if (d2)
      update_param_with_id(PARAM_FX2_FEEDBACK, &params.fx2_feedback, d2);
    if (d3)
      update_param_with_id(PARAM_FX2_TONE, &params.fx2_tone, d3);
    if (d4)
      update_param_with_id(PARAM_FX2_MIX, &params.fx2_mix, d4);
    break;
  case MOD_GLIDE:
    if (d1)
      update_param_with_id(PARAM_GLIDE_TIME, &params.glide_time, d1);
    if (d2)
      update_param_with_id(PARAM_GLIDE_SLOPE, &params.glide_slope, d2);
    if (d3)
      update_param_with_id(PARAM_GLIDE_MODE, &params.glide_mode, d3);
    if (d4)
      update_param_with_id(PARAM_GLIDE_POLY, &params.glide_poly, d4);
    break;
  case MOD_MIXER:
    if (d1) {
      if (ui_is_fn_held()) {
        update_param_with_id(PARAM_AMP_GAIN, &params.amp_gain, d1);
      } else {
        update_param_with_id(PARAM_MIX_VCO1_VOL, &params.mix_vco_balance, d1);
      }
    }
    if (d2)
      update_param_with_id(PARAM_MIX_VCO2_VOL, &params.mix_osc_noise, d2);
    if (d3)
      update_param_with_id(PARAM_MIX_PHASE2, &params.mix_phase2, d3);
    if (d4)
      update_param_with_id(PARAM_MIX_NOISE_VOL, &params.mix_master, d4);
    break;
  case MOD_ADV:
    // Enc1: Tempo (0=Off, 1=Ext, 2..272 = BPM 30..300)
    if (d1)
      update_param16_with_id(PARAM_ADV_TEMPO, &params.adv_tempo, d1, 272);
    // Enc2: Scale (0=Off, 1=Major, ..., 11=Augmented)
    if (d2)
      update_param_choice_with_id(PARAM_ADV_SCALE, &params.adv_scale, d2, ADV_SCALE_COUNT - 1);
    // Enc3: Chord
    if (d3)
      update_param_choice_with_id(PARAM_CHORD_MODE, &params.chord_mode, d3, CHORD_MODE_COUNT - 1);
    // Enc4: MIDI Channel (1-16 -> 0-15)
    if (d4) {
      int ch = (int)params.midi_channel + d4;
      if (ch < 0) ch = 0;
      if (ch > 15) ch = 15;
      params.midi_channel = (uint8_t)ch;
      pra_synth_param_change(PARAM_ADV_MIDI_CH, params.midi_channel);
      midi_map_set_note_channel((uint8_t)ch);
    }
    break;
  default:
    break;
  }
}

void log_panel_changes_for_module(ModuleID mod, int d1, int d2, int d3, int d4);

void handle_params_encoders_lower_row(int d1, int d2, int d3, int d4) {
  if (!d1 && !d2 && !d3 && !d4)
    return;
  if (set_button_held || fn_mode_active)
    return;
  ModuleID m = get_module_below(selected_module);
  if (m == MOD_NONE)
    return;
  handle_params_encoders_for_module(m, d1, d2, d3, d4);
}

// Helper: Get Base Param ID for a Module (first of 4)
ParamID get_base_param_id(ModuleID mod) {
  switch (mod) {
  case MOD_VCO1:
    return PARAM_VCO1_TRANSPOSE;
  case MOD_VCO2:
    return PARAM_VCO2_TRANSPOSE;
  case MOD_VCF1:
    return PARAM_VCF1_CUTOFF;
  case MOD_VCF2:
    return PARAM_VCF2_CUTOFF;
  case MOD_LFO1:
    return PARAM_LFO1_RATE;
  case MOD_LFO2:
    return PARAM_LFO2_RATE;
  case MOD_EG1:
    return PARAM_EG1_ATTACK;
  case MOD_EG2:
    return PARAM_EG2_ATTACK;
  case MOD_MIXER:
    return PARAM_MIX_VCO1_VOL;
  case MOD_NOISE:
    return PARAM_NOISE_COLOR;
  case MOD_FX1:
    return PARAM_FX1_TIME;
  case MOD_FX2:
    return PARAM_FX2_TIME;
  case MOD_ARP:
    return PARAM_ARP_RATE;
  case MOD_GLIDE:
    return PARAM_GLIDE_TIME;
  case MOD_FN:
    return PARAM_VCO1_TRANSPOSE; // Placeholder
  case MOD_MOD:
    return PARAM_MOD_ROUTING1;
  case MOD_ADV:
    return PARAM_ADV_TEMPO;
  default:
    return PARAM_VCO1_TRANSPOSE;
  }
}

static void format_param_value(char *out, ModuleID mod, int knob_idx, uint8_t val) {
  if (!out)
    return;
  out[0] = '\0';

  switch (mod) {
  case MOD_VCO1:
  case MOD_VCO2:
    if (knob_idx == 0) { // Transpose (0..10 maps to -5..+5 octaves)
      int oct = (int)val - 5;
      snprintf(out, 16, "%+d oct", oct);
    } else if (knob_idx == 1) { // Detune
      int cents = (int)detune_table[val];
      snprintf(out, 16, "%+d ct", cents);
    } else {
      snprintf(out, 16, "%d%%", (val * 100) / 127);
    }
    break;

  case MOD_VCF1:
  case MOD_VCF2:
    if (knob_idx == 0) { // Cutoff
      // 20Hz * 1000^(val/127) = 20Hz to 20kHz
      float hz = 20.0f * powf(1000.0f, (float)val / 127.0f);
      if (hz >= 1000.0f)
        snprintf(out, 16, "%.1fkHz", hz / 1000.0f);
      else
        snprintf(out, 16, "%dHz", (int)hz);
    } else if (knob_idx == 1) { // Resonance
      float q = g_vcf_res_q_lut[val >> 2];
      snprintf(out, 16, "%.1f", q);
    } else if (knob_idx == 2) { // Keytrack
      snprintf(out, 16, "%d%%", (val * 100) / 127);
    } else { // Mix / Drive / etc
      snprintf(out, 16, "%d%%", (val * 100) / 127);
    }
    break;

  case MOD_LFO1:
  case MOD_LFO2:
    if (knob_idx == 0) { // Rate
      // 0.1Hz * 200^(val/127) = 0.1Hz to 20Hz
      float hz = 0.1f * powf(200.0f, (float)val / 127.0f);
      if (hz < 1.0f)
        snprintf(out, 16, "%.2fHz", hz);
      else
        snprintf(out, 16, "%.1fHz", hz);
    } else {
      snprintf(out, 16, "%d%%", (val * 100) / 127);
    }
    break;

  case MOD_EG1:
  case MOD_EG2:
    if (ui_is_fn_held() && knob_idx != 2) { // Attack, Decay, Release Curves
      int display_val = (val <= 64) ? 
                        ((int)val - 64) * 20 / 64 :
                        ((int)val - 64) * 20 / 63;
      snprintf(out, 16, "%+d", display_val);
    } else if (knob_idx != 2) { // Attack, Decay, Release (0, 1, 3) times
      // Logarithmic: ms = 2000 * (100^(val/127) - 1) / 99
      float ms = 2000.0f * (powf(100.0f, (float)val / 127.0f) - 1.0f) / 99.0f;
      if (ms >= 1000.0f)
        snprintf(out, 16, "%.2fs", ms / 1000.0f);
      else
        snprintf(out, 16, "%dms", (int)ms);
    } else { // Sustain
      snprintf(out, 16, "%d%%", (val * 100) / 127);
    }
    break;

  case MOD_MIXER:
    snprintf(out, 16, "%d%%", (val * 100) / 127);
    break;

  case MOD_FX1:
    if (knob_idx == 0) { // Time (0..20ms linear)
      float ms = (float)val * 20.0f / 127.0f;
      snprintf(out, 16, "%.1fms", ms);
    } else {
      snprintf(out, 16, "%d%%", (val * 100) / 127);
    }
    break;

  case MOD_NOISE:
    if (knob_idx == 0) { // Color
      if (val < 57) snprintf(out, 16, "Pink");
      else if (val > 70) snprintf(out, 16, "Blue");
      else snprintf(out, 16, "White");
    } else {
      snprintf(out, 16, "%d%%", (val * 100) / 127);
    }
    break;

  case MOD_FX2:
    if (knob_idx == 0) { // Time (0..1000ms linear)
      float ms = (float)val * 1000.0f / 127.0f;
      if (ms >= 1000.0f)
        snprintf(out, 16, "1.00s");
      else
        snprintf(out, 16, "%dms", (int)ms);
    } else {
      snprintf(out, 16, "%d%%", (val * 100) / 127);
    }
    break;

  case MOD_GLIDE:
    if (knob_idx == 0) { // Time
       float ms = 10.0f * powf(200.0f, (float)val / 127.0f);
       if (ms >= 1000.0f) snprintf(out, 16, "%.2fs", ms / 1000.0f);
       else snprintf(out, 16, "%dms", (int)ms);
    } else if (knob_idx == 1) { // Slope
       if (val == 64) snprintf(out, 16, "Lin");
       else if (val < 64) snprintf(out, 16, "Log%d", 64 - val);
       else snprintf(out, 16, "Exp%d", val - 64);
    } else if (knob_idx == 2) { // Mode
       if (val < 43) snprintf(out, 16, "Off");
       else if (val < 86) snprintf(out, 16, "Legat");
       else snprintf(out, 16, "Alway");
    } else if (knob_idx == 3) { // Poly Mode
       if (val == 0) snprintf(out, 16, "4V");
       else snprintf(out, 16, "u%d%%", (val * 100) / 127);
    }
    break;

  case MOD_MOD:
    if (knob_idx == 0 || knob_idx == 2) {
      static const char *names[] = {"OFF", "Sc 1>2", "Sc 1>N", "AM 1>2", "AM 1>N", "FM 1>2", "FM 1>N", "RM 1>2", "RM 1>N"};
      static const char *names2[] = {"OFF", "Sc 2>1", "Sc 2>N", "AM 2>1", "AM 2>N", "FM 2>1", "FM 2>N", "RM 2>1", "RM 2>N"};
      int idx = val / 15;
      if (idx > 8) idx = 8;
      snprintf(out, 16, "%s", (knob_idx == 0) ? names[idx] : names2[idx]);
    } else {
      snprintf(out, 16, "%d%%", (val * 100) / 127);
    }
    break;

  case MOD_ARP:
    if (knob_idx == 0) { // Rate (Sync divisions 0-17)
      static const char *sync_names[] = {
          "8/1", "8/1t", "4/1", "4/1t", "1/1", "1/1t", "1/2", "1/2t",
          "1/4", "1/4t", "1/8", "1/8t", "1/16", "1/16t", "1/32", "1/32t",
          "1/64", "1/64t"
      };
      if (val < 18) snprintf(out, 16, "%s", sync_names[val]);
      else snprintf(out, 16, "%d", val);
    } else if (knob_idx == 1) { // Mode
      static const char *modes[] = {"OFF", "UP", "DOWN", "UP-DN", "RND", "DRNK"};
      if (val < 6) snprintf(out, 16, "%s", modes[val]);
      else snprintf(out, 16, "M%d", val);
    } else if (knob_idx == 2) { // Swing
      if (val == 0) {
        snprintf(out, 16, "Off");
      } else {
        float pct = 50.0f + ((float)val / 127.0f) * 25.0f;
        snprintf(out, 16, "%.1f%%", pct);
      }
    } else if (knob_idx == 3) { // Oct
      snprintf(out, 16, "%+d oct", (int)val - 3);
    }
    break;

  case MOD_ADV:
    if (knob_idx == 0) { // Tempo
      if (val == ADV_TEMPO_OFF) {
        snprintf(out, 16, "Off");
      } else if (val == ADV_TEMPO_EXT) {
        snprintf(out, 16, "Ext");
      } else {
        int bpm = 30 + (int)(val - ADV_TEMPO_BPM_MIN);
        snprintf(out, 16, "%d", bpm);
      }
    } else if (knob_idx == 1) { // Scale
      static const char *scale_names[] = {
        "Off","Major","Minor","HMin","MMin","Dorian",
        "Locr","Lydian","Blues","MajP","MinP","Aug"
      };
      if (val < ADV_SCALE_COUNT) snprintf(out, 16, "%s", scale_names[val]);
      else snprintf(out, 16, "%d", val);
    } else if (knob_idx == 2) { // Chord
      static const char *chord_names[] = {
          "OFF", "m2", "M2", "m3", "M3", "P4", "Tri", "P5", "m6", "M6", "m7i", "M7i", "Oct",
          "Maj", "Min", "Dim", "Aug", "Sus2", "Sus4", "Maj7", "Dom7", "Min7", "m7b5", "Dim7",
          "mMaj7", "AMaj7", "Aug7", "Add9", "6th", "m6th", "6/9", "7sus4", "M7sus4"
      };
      if (val < CHORD_MODE_COUNT) snprintf(out, 16, "%s", chord_names[val]);
      else snprintf(out, 16, "%d", val);
    } else if (knob_idx == 3) { // MIDI Channel (1-16)
      snprintf(out, 16, "Ch %d", val + 1);
    }
    break;

  default:
    snprintf(out, 16, "%d", val);
    break;

  }
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

uint16_t get_param_max(ModuleID mod, int knob_idx) {
  switch (mod) {
  case MOD_VCO1:
  case MOD_VCO2:
    if (knob_idx == 0) return 10; // Transpose: +/- 5 octaves
    break;
   case MOD_LFO1:
   case MOD_LFO2:
     if (knob_idx == 2) return 127; // Wave (Morphing)
     break;
  case MOD_NOISE:
    if (knob_idx == 1) return CHORD_MODE_COUNT > 0 ? CHORD_MODE_COUNT - 1 : 127;
    break;
  case MOD_ARP:
    if (knob_idx == 0) return 17; // Rate (Sync divisions 0-17)
    if (knob_idx == 1) return 5; // Mode
    if (knob_idx == 3) return 6; // Oct
    break;
  case MOD_MOD:
    if (knob_idx == 0 || knob_idx == 2) return 8; // Routing
    break;
  case MOD_ADV:
    if (knob_idx == 0) return 272; // Tempo
    if (knob_idx == 1) return ADV_SCALE_COUNT > 0 ? ADV_SCALE_COUNT - 1 : 127;
    if (knob_idx == 2) return CHORD_MODE_COUNT > 0 ? CHORD_MODE_COUNT - 1 : 127;
    if (knob_idx == 3) return 15; // MIDI Channel
    break;
  default:
    break;
  }
  return 127;
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
  const char *t0 = "P0";
  const char *t1 = "P1";
  const char *t2 = "P2";
  const char *t3 = "P3";
  uint8_t v0 = 0, v1 = 0, v2 = 0, v3 = 0;
  uint16_t mx0 = 127, mx1 = 127, mx2 = 127, mx3 = 127;

  static const char* vcf_types[] = {"|LPF|", "|BPF|", "|HPF|", "|BCF|", "|APF|"};
  switch (selected_module) {
  case MOD_VCO1:
    t0 = "Trns";
    t1 = "Detn";
    t2 = "Wave";
    t3 = "Shap";
    v0 = pra_synth_get_modulated_param_value(PARAM_VCO1_TRANSPOSE);
    v1 = pra_synth_get_modulated_param_value(PARAM_VCO1_DETUNE);
    v2 = pra_synth_get_modulated_param_value(PARAM_VCO1_WAVE);
    v3 = pra_synth_get_modulated_param_value(PARAM_VCO1_SHAPE);
    break;
  case MOD_VCO2:
    t0 = "Trns";
    t1 = "Detn";
    t2 = "Wave";
    t3 = "Shap";
    v0 = pra_synth_get_modulated_param_value(PARAM_VCO2_TRANSPOSE);
    v1 = pra_synth_get_modulated_param_value(PARAM_VCO2_DETUNE);
    v2 = pra_synth_get_modulated_param_value(PARAM_VCO2_WAVE);
    v3 = pra_synth_get_modulated_param_value(PARAM_VCO2_SHAPE);
    break;
  case MOD_VCF1:
    t0 = (params.vcf1_type < 5) ? vcf_types[params.vcf1_type % 5] : "Cut";
    t1 = "Res";
    t2 = "KTrck";
    t3 = "Mix";
    v0 = pra_synth_get_modulated_param_value(PARAM_VCF1_CUTOFF);
    v1 = pra_synth_get_modulated_param_value(PARAM_VCF1_RES);
    v2 = pra_synth_get_modulated_param_value(PARAM_VCF_KEY_TRACK);
    v3 = pra_synth_get_modulated_param_value(PARAM_VCF1_MIX);
    break;
  case MOD_VCF2:
    t0 = (params.vcf2_type < 5) ? vcf_types[params.vcf2_type % 5] : "Cut";
    t1 = "Res";
    t2 = "KTrck";
    t3 = "Mix";
    v0 = pra_synth_get_modulated_param_value(PARAM_VCF2_CUTOFF);
    v1 = pra_synth_get_modulated_param_value(PARAM_VCF2_RES);
    v2 = pra_synth_get_modulated_param_value(PARAM_VCF_KEY_TRACK);
    v3 = pra_synth_get_modulated_param_value(PARAM_VCF2_MIX);
    break;
  case MOD_LFO1:
    t0 = "Rate";
    t1 = "Smth";
    t2 = "Wave";
    t3 = "Shap";
    v0 = pra_synth_get_modulated_param_value(PARAM_LFO1_RATE);
    v1 = pra_synth_get_modulated_param_value(PARAM_LFO1_SMOOTH);
    v2 = pra_synth_get_modulated_param_value(PARAM_LFO1_WAVE);
    v3 = pra_synth_get_modulated_param_value(PARAM_LFO1_SHAPE);
    break;
  case MOD_LFO2:
    t0 = "Rate";
    t1 = "Smth";
    t2 = "Wave";
    t3 = "Shap";
    v0 = pra_synth_get_modulated_param_value(PARAM_LFO2_RATE);
    v1 = pra_synth_get_modulated_param_value(PARAM_LFO2_SMOOTH);
    v2 = pra_synth_get_modulated_param_value(PARAM_LFO2_WAVE);
    v3 = pra_synth_get_modulated_param_value(PARAM_LFO2_SHAPE);
    break;
  case MOD_EG1:
    if (ui_is_fn_held()) {
      t0 = "|Curve|"; t1 = "|Curve|"; t2 = "Sus"; t3 = "|Curve|";
      v0 = pra_synth_get_modulated_param_value(PARAM_EG1_ATTACK_CURVE);
      v1 = pra_synth_get_modulated_param_value(PARAM_EG1_DECAY_CURVE);
      v2 = pra_synth_get_modulated_param_value(PARAM_EG1_SUSTAIN);
      v3 = pra_synth_get_modulated_param_value(PARAM_EG1_RELEASE_CURVE);
    } else {
      t0 = "|Attk|"; t1 = "|Dcy|"; t2 = "Sus"; t3 = "|Rels|";
      v0 = pra_synth_get_modulated_param_value(PARAM_EG1_ATTACK);
      v1 = pra_synth_get_modulated_param_value(PARAM_EG1_DECAY);
      v2 = pra_synth_get_modulated_param_value(PARAM_EG1_SUSTAIN);
      v3 = pra_synth_get_modulated_param_value(PARAM_EG1_RELEASE);
    }
    break;
  case MOD_EG2:
    if (ui_is_fn_held()) {
      t0 = "|Curve|"; t1 = "|Curve|"; t2 = "Sus"; t3 = "|Curve|";
      v0 = pra_synth_get_modulated_param_value(PARAM_EG2_ATTACK_CURVE);
      v1 = pra_synth_get_modulated_param_value(PARAM_EG2_DECAY_CURVE);
      v2 = pra_synth_get_modulated_param_value(PARAM_EG2_SUSTAIN);
      v3 = pra_synth_get_modulated_param_value(PARAM_EG2_RELEASE_CURVE);
    } else {
      t0 = "|Attk|"; t1 = "|Dcy|"; t2 = "Sus"; t3 = "|Rels|";
      v0 = pra_synth_get_modulated_param_value(PARAM_EG2_ATTACK);
      v1 = pra_synth_get_modulated_param_value(PARAM_EG2_DECAY);
      v2 = pra_synth_get_modulated_param_value(PARAM_EG2_SUSTAIN);
      v3 = pra_synth_get_modulated_param_value(PARAM_EG2_RELEASE);
    }
    break;
  case MOD_MIXER:
    if (ui_is_fn_held()) {
      t0 = "|MSTR|";
      v0 = pra_synth_get_modulated_param_value(PARAM_AMP_GAIN);
    } else {
      t0 = "|VCO1|";
      v0 = pra_synth_get_modulated_param_value(PARAM_MIX_VCO1_VOL);
    }
    t1 = "VCO2";
    t2 = "Phs2";
    t3 = "Noise";
    v1 = pra_synth_get_modulated_param_value(PARAM_MIX_VCO2_VOL);
    v2 = pra_synth_get_modulated_param_value(PARAM_MIX_PHASE2);
    v3 = pra_synth_get_modulated_param_value(PARAM_MIX_NOISE_VOL);
    break;
  case MOD_NOISE:
    t0 = "Colr";
    t1 = "—";
    t2 = "—";
    t3 = "—";
    v0 = pra_synth_get_modulated_param_value(PARAM_NOISE_COLOR);
    v1 = 0;
    v2 = 0;
    v3 = 0;
    break;
  case MOD_ARP:
    t0 = "Rate";
    t1 = "Mode";
    t2 = "Swing";
    t3 = "Oct";
    v0 = pra_synth_get_modulated_param_value(PARAM_ARP_RATE);
    v1 = pra_synth_get_modulated_param_value(PARAM_ARP_MODE);
    v2 = pra_synth_get_modulated_param_value(PARAM_ARP_SWING);
    v3 = pra_synth_get_modulated_param_value(PARAM_ARP_OCT);
    break;
  case MOD_GLIDE:
    t0 = "Time";
    t1 = "Slop";
    t2 = "Mode";
    t3 = "Poly";
    v0 = pra_synth_get_modulated_param_value(PARAM_GLIDE_TIME);
    v1 = pra_synth_get_modulated_param_value(PARAM_GLIDE_SLOPE);
    v2 = pra_synth_get_modulated_param_value(PARAM_GLIDE_MODE);
    v3 = pra_synth_get_modulated_param_value(PARAM_GLIDE_POLY);
    break;
  case MOD_FX1:
    t0 = "Time";
    t1 = "Feed";
    t2 = "Dpth";
    t3 = "Mix";
    v0 = pra_synth_get_modulated_param_value(PARAM_FX1_TIME);
    v1 = pra_synth_get_modulated_param_value(PARAM_FX1_FEEDBACK);
    v2 = pra_synth_get_modulated_param_value(PARAM_FX1_SPREAD);
    v3 = pra_synth_get_modulated_param_value(PARAM_FX1_MIX);
    break;
  case MOD_FX2:
    t0 = "Time";
    t1 = "Feed";
    t2 = "Tone";
    t3 = "Mix";
    v0 = pra_synth_get_modulated_param_value(PARAM_FX2_TIME);
    v1 = pra_synth_get_modulated_param_value(PARAM_FX2_FEEDBACK);
    v2 = pra_synth_get_modulated_param_value(PARAM_FX2_TONE);
    v3 = pra_synth_get_modulated_param_value(PARAM_FX2_MIX);
    break;
  case MOD_MOD:
    t0 = "s>t 1";
    t1 = "Dpth1";
    t2 = "s>t 2";
    t3 = "Dpth2";
    v0 = params.mod_routing1;
    v1 = params.mod_depth1;
    v2 = params.mod_routing2;
    v3 = params.mod_depth2;
    break;
  case MOD_ADV: {
    t0 = "Tempo";
    t1 = "Scale";
    t2 = "Chord";
    t3 = "MIDI";
    v0 = params.adv_tempo;
    v1 = params.adv_scale;
    v2 = params.chord_mode;
    v3 = params.midi_channel;
    break;
  }
  default:
    break;
  }
  ParamID base = get_base_param_id(selected_module);
  const char *lbl[4] = {"", "", "", ""};
  uint8_t mamt[4] = {0, 0, 0, 0};
  for (int i = 0; i < 4; i++) {
    int pid = base + i;
    if (pid >= 0 && pid < PARAM_COUNT) {
      int src_idx = params.mod_source_assigned[pid];
      mamt[i] = (src_idx != 0xFF) ? params.mod_matrix[pid][src_idx] : 64;
      switch (src_idx) {
      case 0:
        lbl[i] = "LF1";
        break;
      case 1:
        lbl[i] = "LF2";
        break;
      case 2:
        lbl[i] = "EG1";
        break;
      case 3:
        lbl[i] = "EG2";
        break;
      case 4:
        lbl[i] = "MODWH";
        break;
      case 5:
        lbl[i] = "AFT";
        break;
      case 6:
        lbl[i] = "BTH";
        break;
      default:
        lbl[i] = "";
        break;
      }
    }
  }
  out->knobs[0].title = t0;
  mx0 = get_param_max(selected_module, 0);
  out->knobs[0].value = (mx0 > 0) ? (uint8_t)(((uint32_t)v0 * 127) / mx0) : 0;
  out->knobs[0].mod_label = lbl[0];
  out->knobs[0].mod_amount = mamt[0];
  format_param_value(out->knobs[0].value_str, selected_module, 0, v0);

  out->knobs[1].title = t1;
  mx1 = get_param_max(selected_module, 1);
  out->knobs[1].value = (mx1 > 0) ? (uint8_t)(((uint32_t)v1 * 127) / mx1) : 0;
  out->knobs[1].mod_label = lbl[1];
  out->knobs[1].mod_amount = mamt[1];
  format_param_value(out->knobs[1].value_str, selected_module, 1, v1);

  out->knobs[2].title = t2;
  mx2 = get_param_max(selected_module, 2);
  out->knobs[2].value = (mx2 > 0) ? (uint8_t)(((uint32_t)v2 * 127) / mx2) : 0;
  out->knobs[2].mod_label = lbl[2];
  out->knobs[2].mod_amount = mamt[2];
  format_param_value(out->knobs[2].value_str, selected_module, 2, v2);

  out->knobs[3].title = t3;
  mx3 = get_param_max(selected_module, 3);
  out->knobs[3].value = (mx3 > 0) ? (uint8_t)(((uint32_t)v3 * 127) / mx3) : 0;
  out->knobs[3].mod_label = lbl[3];
  out->knobs[3].mod_amount = mamt[3];
  format_param_value(out->knobs[3].value_str, selected_module, 3, v3);

  if (selected_module == MOD_ARP || selected_module == MOD_ADV) {
    out->layout_id = 4;
  } else {
    switch (selected_module) {
    case MOD_VCO1:
    case MOD_VCO2:
    case MOD_VCF1:
    case MOD_VCF2:
    case MOD_MIXER:
    case MOD_FX1:
    case MOD_FX2:
    case MOD_NOISE:
      out->layout_id = 0;
      break;
    default:
      out->layout_id = 1;
      break;
    }
  }
}

// Helper: Map Value to Color based on Module
void get_param_color(ModuleID mod, uint8_t val, uint8_t *r, uint8_t *g, uint8_t *b) {
  if (val > 127) val = 127;
  get_cold_hot_color((float)val / 127.0f, r, g, b);
}

// Helper: MIDI to Freq
float mtof(int note) { return 440.0f * powf(2.0f, (note - 69) / 12.0f); }

const char *chord_mode_name(uint8_t mode) {
  switch (mode) {
  case CHORD_OFF:
    return "OFF";
  case CHORD_MIN2:
    return "m2";
  case CHORD_MAJ2:
    return "M2";
  case CHORD_MIN3:
    return "m3";
  case CHORD_MAJ3:
    return "M3";
  case CHORD_P4:
    return "P4";
  case CHORD_TRITONE:
    return "TT";
  case CHORD_P5:
    return "P5";
  case CHORD_MIN6:
    return "m6";
  case CHORD_MAJ6:
    return "M6";
  case CHORD_INT_MIN7:
    return "m7i";
  case CHORD_INT_MAJ7:
    return "M7i";
  case CHORD_OCT:
    return "OCT";
  case CHORD_MAJ:
    return "MAJ";
  case CHORD_MIN:
    return "MIN";
  case CHORD_DIM:
    return "DIM";
  case CHORD_AUG:
    return "AUG";
  case CHORD_SUS2:
    return "SUS2";
  case CHORD_SUS4:
    return "SUS4";
  case CHORD_MAJ7:
    return "M7";
  case CHORD_MIN7:
    return "m7";
  case CHORD_DOM7:
    return "7";
  case CHORD_M7B5:
    return "m7b5";
  case CHORD_DIM7:
    return "DIM7";
  case CHORD_MIN_MAJ7:
    return "mM7";
  case CHORD_AUG_MAJ7:
    return "AUGM7";
  case CHORD_AUG7:
    return "AUG7";
  case CHORD_ADD9:
    return "ADD9";
  case CHORD_6:
    return "6";
  case CHORD_M6:
    return "m6";
  case CHORD_69:
    return "6/9";
  case CHORD_7SUS4:
    return "7SUS4";
  case CHORD_MAJ7SUS4:
    return "M7S4";
  default:
    return "UNK";
  }
}

int encode_step_from_delta(int delta) {
  if (delta > 1)
    return 2;
  if (delta < -1)
    return -2;
  if (delta > 0)
    return 1;
  if (delta < 0)
    return -1;
  return 0;
}

void update_param(uint8_t *param, int delta) {
  int step = encode_step_from_delta(delta);
  if (!step)
    return;
  int val = (int)*param + step;
  if (val < 0)
    val = 0;
  if (val > 127)
    val = 127;
  *param = (uint8_t)val;
}

void update_param_wrap(uint8_t *param, int delta) {
  int step = encode_step_from_delta(delta);
  if (!step)
    return;
  int val = (int)*param + step;
  val %= 128;
  if (val < 0)
    val += 128;
  *param = (uint8_t)val;
}

static void update_param_choice(uint8_t *param, int delta, uint8_t max_val) {
  int step = encode_step_from_delta(delta);
  if (!step)
    return;
  int val = (int)*param + step;
  if (val < 0)
    val = 0;
  if (val > max_val)
    val = max_val;
  *param = (uint8_t)val;
}

void update_param_with_id(ParamID param_id, uint8_t *param, int delta) {
  uint8_t prev = *param;
  update_param(param, delta);
  if (*param != prev) {
    pra_synth_param_change(param_id, *param);
  }
}

void update_param_wrap_with_id(ParamID param_id, uint8_t *param, int delta) {
  uint8_t prev = *param;
  update_param_wrap(param, delta);
  if (*param != prev) {
    pra_synth_param_change(param_id, *param);
  }
}

static void update_param_choice_with_id(ParamID param_id, uint8_t *param,
                                        int delta, uint8_t max_val) {
  uint8_t prev = *param;
  update_param_choice(param, delta, max_val);
  if (*param != prev) {
    pra_synth_param_change(param_id, *param);
  }
}

void log_chord_mode_if_changed(void) {
  static uint8_t prev_chord_mode = 0xFF;
  if (prev_chord_mode != params.chord_mode) {
    DBG_PRINTF("CHORD MODE = %s (%u)\n", chord_mode_name(params.chord_mode),
               (unsigned)params.chord_mode);
    prev_chord_mode = params.chord_mode;
  }
}

#if CFG_ENABLE_DEBUG
void get_waveform_name(uint8_t val, char *buf) {
  if (val < 32) {
    int seg = val / 8;
    int pct = (int)(((val % 8) / 7.0f) * 100.0f);
    const char *s1 = "SIN";
    const char *s2 = "TRI";
    if (seg == 1) {
      s1 = "TRI";
      s2 = "SAW";
    } else if (seg == 2) {
      s1 = "SAW";
      s2 = "RSAW";
    } else if (seg == 3) {
      s1 = "RSAW";
      s2 = "SQR";
    }
    if (pct < 10) {
      strcpy(buf, s1);
    } else if (pct > 90) {
      strcpy(buf, s2);
    } else {
      sprintf(buf, "%s>%s (%d%%)", s1, s2, pct);
    }
    return;
  }
  if (val < 64) {
    float pw = 0.5f - ((float)(val - 32) / 31.0f) * 0.49f;
    sprintf(buf, "PWM (%.0f%%)", pw * 100.0f);
    return;
  }
  if (val < 80) {
    int pattern_idx = (val - 64) * 5 / 16;
    sprintf(buf, "PAM4 P%d", pattern_idx + 1);
    return;
  }
  static const char *wave_names[] = {"sin", "tri", "saw", "rsaw",
                                     "sqr", "pls", "pam4"};
  static const uint8_t hybrid_pairs[22][2] = {
      {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {1, 2}, {1, 3},
      {1, 4}, {1, 5}, {1, 6}, {2, 3}, {3, 2}, {2, 4}, {2, 5}, {2, 6},
      {3, 4}, {3, 5}, {3, 6}, {4, 5}, {4, 6}, {5, 6}};
  float pos = (float)(val - 80) / 47.0f;
  float segf = pos * 22.0f;
  int idx = (int)floorf(segf);
  if (idx < 0)
    idx = 0;
  if (idx > 21)
    idx = 21;
  sprintf(buf, "hybrid(%s-%s)", wave_names[hybrid_pairs[idx][0]],
          wave_names[hybrid_pairs[idx][1]]);
}

void log_panel_changes_for_module(ModuleID mod, int d1, int d2, int d3,
                                  int d4) {
  if (time_us_32() < log_panel_block_until_us)
    return;
  if (!d1 && !d2 && !d3 && !d4)
    return;
  static uint8_t prev_vals[MOD_NONE + 1][4];
  static bool prev_init = false;
  if (!prev_init) {
    for (int m = 0; m <= MOD_NONE; m++) {
      for (int i = 0; i < 4; i++) {
        prev_vals[m][i] = 0xFF;
      }
    }
    prev_init = true;
  }
  const char *mod_name = module_name(mod);
  const char *k0 = "P0";
  const char *k1 = "P1";
  const char *k2 = "P2";
  const char *k3 = "P3";
  uint8_t v0 = 0, v1 = 0, v2 = 0, v3 = 0;
  switch (mod) {
  case MOD_VCO1:
    k0 = "Trns";
    k1 = "Detn";
    k2 = "Wave";
    k3 = "Shap";
    v0 = params.vco1_transpose;
    v1 = params.vco1_detune;
    v2 = params.vco1_wave;
    v3 = params.vco1_shape;
    break;
  case MOD_VCO2:
    k0 = "Trns";
    k1 = "Detn";
    k2 = "Wave";
    k3 = "Shap";
    v0 = params.vco2_transpose;
    v1 = params.vco2_detune;
    v2 = params.vco2_wave;
    v3 = params.vco2_shape;
    break;
  case MOD_VCF1:
    k0 = "Cut";
    k1 = "Res";
    k2 = "Key";
    k3 = "Mix";
    v0 = params.vcf1_cutoff;
    v1 = params.vcf1_res;
    v2 = params.vcf_key_track;
    v3 = params.vcf1_mix;
    break;
  case MOD_VCF2:
    k0 = "Cut";
    k1 = "Res";
    k2 = "Key";
    k3 = "Mix";
    v0 = params.vcf2_cutoff;
    v1 = params.vcf2_res;
    v2 = params.vcf_key_track;
    v3 = params.vcf2_mix;
    break;
  case MOD_LFO1:
    k0 = "Rate";
    k1 = "Smth";
    k2 = "Wave";
    k3 = "Shap";
    v0 = params.lfo1_rate;
    v1 = params.lfo1_smooth;
    v2 = params.lfo1_wave;
    v3 = params.lfo1_shape;
    break;
  case MOD_LFO2:
    k0 = "Rate";
    k1 = "Smth";
    k2 = "Wave";
    k3 = "Shap";
    v0 = params.lfo2_rate;
    v1 = params.lfo2_smooth;
    v2 = params.lfo2_wave;
    v3 = params.lfo2_shape;
    break;
  case MOD_EG1:
    k0 = "Attk";
    k1 = "Dcy";
    k2 = "Sus";
    k3 = "Rels";
    v0 = params.eg1_attack;
    v1 = params.eg1_decay;
    v2 = params.eg1_sustain;
    v3 = params.eg1_release;
    break;
  case MOD_EG2:
    k0 = "Attk";
    k1 = "Dcy";
    k2 = "Sus";
    k3 = "Rels";
    v0 = params.eg2_attack;
    v1 = params.eg2_decay;
    v2 = params.eg2_sustain;
    v3 = params.eg2_release;
    break;
  case MOD_MIXER:
    k0 = "VCO1";
    k1 = "VCO2";
    k2 = "Phs2";
    k3 = "Noise";
    v0 = params.mix_vco_balance;
    v1 = params.mix_osc_noise;
    v2 = params.mix_phase2;
    v3 = params.mix_master;
    break;
  case MOD_MOD:
    k0 = "s>t 1";
    k1 = "Dpth1";
    k2 = "s>t 2";
    k3 = "Dpth2";
    v0 = params.mod_routing1;
    v1 = params.mod_depth1;
    v2 = params.mod_routing2;
    v3 = params.mod_depth2;
    break;
  case MOD_NOISE:
    k0 = "Colr";
    k1 = "Chrd";
    k2 = "P2";
    k3 = "P3";
    v0 = params.noise_color;
    v1 = params.chord_mode;
    v2 = 0;
    v3 = 0;
    break;
  case MOD_ARP:
    k0 = "Rate";
    k1 = "Mode";
    k2 = "Var";
    k3 = "Oct";
    v0 = params.arp_rate;
    v1 = params.arp_mode;
    v2 = params.arp_swing;
    v3 = params.arp_oct;
    break;
  case MOD_GLIDE:
    k0 = "Time";
    k1 = "Slop";
    k2 = "Mode";
    k3 = "Poly";
    v0 = params.glide_time;
    v1 = params.glide_slope;
    v2 = params.glide_mode;
    v3 = params.glide_poly;
    break;
  case MOD_FX1:
    k0 = "Time";
    k1 = "Feed";
    k2 = "Dpth";
    k3 = "Mix";
    v0 = params.fx1_time;
    v1 = params.fx1_feedback;
    v2 = params.fx1_spread;
    v3 = params.fx1_mix;
    break;
  case MOD_FX2:
    k0 = "Time";
    k1 = "Feed";
    k2 = "Tone";
    k3 = "Mix";
    v0 = params.fx2_time;
    v1 = params.fx2_feedback;
    v2 = params.fx2_tone;
    v3 = params.fx2_mix;
    break;
  default:
    break;
  }
  if (d1 && v0 != prev_vals[mod][0]) {
    prev_vals[mod][0] = v0;
    if (mod == MOD_VCF1 || mod == MOD_VCF2) {
      if (k0[0] == 'C') {
        float hz = 50.0f * powf(160.0f, ((float)v0 / 127.0f));
        if (hz > 8000.0f)
          hz = 8000.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.1f Hz)\n", mod_name, k0, (unsigned)v0,
                   hz);
      } else if (k0[0] == 'R') {
        float q = 0.5f + ((float)v0 / 127.0f) * (12.0f - 0.5f);
        DBG_PRINTF("PANEL %s %s=%u (Q=%.2f)\n", mod_name, k0, (unsigned)v0, q);
      } else if (k0[0] == 'D') {
        float g = 1.0f + ((float)v0 / 127.0f) * 9.0f;
        DBG_PRINTF("PANEL %s %s=%u (x%.2f)\n", mod_name, k0, (unsigned)v0, g);
      } else {
        float pct = (float)v0 * 100.0f / 127.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k0, (unsigned)v0,
                   pct);
      }
    } else if (mod == MOD_VCO1 || mod == MOD_VCO2) {
      if (k0[0] == 'T') {
        float oct = roundf((((float)v0 - 64.0f) / 64.0f) * 5.0f);
        DBG_PRINTF("PANEL %s %s=%u (%.0f oct)\n", mod_name, k0, (unsigned)v0,
                   oct);
      } else if (k0[0] == 'D') {
        float st = detune_table[v0] / 100.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.2f st)\n", mod_name, k0, (unsigned)v0,
                   st);
      } else {
        DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k0, (unsigned)v0);
      }
    } else if (mod == MOD_LFO1 || mod == MOD_LFO2) {
      if (k0[0] == 'R') {
        if (v0 == 0) {
          DBG_PRINTF("PANEL %s %s=%u (OFF)\n", mod_name, k0, (unsigned)v0);
        } else {
          float hz = 0.05f * powf(2.0f, ((float)v0 / 127.0f) * 8.64385618f);
          DBG_PRINTF("PANEL %s %s=%u (%.2f Hz)\n", mod_name, k0, (unsigned)v0,
                     hz);
        }
      } else if (k0[0] == 'S' && k0[1] == 'm') {
        float pct = (float)v0 * 100.0f / 127.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k0, (unsigned)v0,
                   pct);
      } else {
        DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k0, (unsigned)v0);
      }
    } else if (mod == MOD_EG1 || mod == MOD_EG2) {
      if (k0[0] == 'A' || k0[0] == 'D' || k0[0] == 'R') {
        float s = 2.0f * (powf(100.0f, (float)v0 / 127.0f) - 1.0f) / 99.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.3f s)\n", mod_name, k0, (unsigned)v0, s);
      } else {
        float pct = (float)v0 * 100.0f / 127.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k0, (unsigned)v0,
                   pct);
      }
    } else if (mod == MOD_MIXER) {
      float pct = (float)v0 * 100.0f / 127.0f;
      DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k0, (unsigned)v0, pct);
    } else if (mod == MOD_ARP) {
      if (k0[0] == 'R') {
        float period_us = 100000.0f + (127.0f - (float)v0) * 2000.0f;
        float hz = 1000000.0f / period_us;
        DBG_PRINTF("PANEL %s %s=%u (%.2f Hz)\n", mod_name, k0, (unsigned)v0,
                   hz);
      } else {
        DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k0, (unsigned)v0);
      }
    } else if (mod == MOD_GLIDE) {
      if (k0[0] == 'T') {
        float s = 0.001f * powf(10.0f, ((float)v0 / 127.0f) * 4.0f);
        DBG_PRINTF("PANEL %s %s=%u (%.3f s)\n", mod_name, k0, (unsigned)v0, s);
      } else {
        float pct = (float)v0 * 100.0f / 127.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k0, (unsigned)v0,
                   pct);
      }
    } else if (mod == MOD_FX1 || mod == MOD_FX2) {
      if (k0[0] == 'T' && k0[1] == 'i') {
        float ms = 5.0f * powf(2.0f, ((float)v0 / 127.0f) * 7.64f);
        DBG_PRINTF("PANEL %s %s=%u (%.1f ms)\n", mod_name, k0, (unsigned)v0,
                   ms);
      } else {
        float pct = (float)v0 * 100.0f / 127.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k0, (unsigned)v0,
                   pct);
      }
    } else {
      DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k0, (unsigned)v0);
    }
  }
  if (d2 && v1 != prev_vals[mod][1]) {
    prev_vals[mod][1] = v1;
    if (mod == MOD_NOISE) {
      DBG_PRINTF("PANEL %s %s=%u (%s)\n", mod_name, k1, (unsigned)v1,
                 chord_mode_name(v1));
    } else if (mod == MOD_VCF1 || mod == MOD_VCF2) {
      if (k1[0] == 'C') {
        float hz = 50.0f * powf(160.0f, ((float)v1 / 127.0f));
        if (hz > 8000.0f)
          hz = 8000.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.1f Hz)\n", mod_name, k1, (unsigned)v1,
                   hz);
      } else if (k1[0] == 'R') {
        float q = 0.5f + ((float)v1 / 127.0f) * (12.0f - 0.5f);
        DBG_PRINTF("PANEL %s %s=%u (Q=%.2f)\n", mod_name, k1, (unsigned)v1, q);
      } else if (k1[0] == 'D') {
        float g = 1.0f + ((float)v1 / 127.0f) * 9.0f;
        DBG_PRINTF("PANEL %s %s=%u (x%.2f)\n", mod_name, k1, (unsigned)v1, g);
      } else {
        float pct = (float)v1 * 100.0f / 127.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k1, (unsigned)v1,
                   pct);
      }
    } else if (mod == MOD_VCO1 || mod == MOD_VCO2) {
      if (k1[0] == 'T') {
        float oct = roundf((((float)v1 - 64.0f) / 64.0f) * 5.0f);
        DBG_PRINTF("PANEL %s %s=%u (%.0f oct)\n", mod_name, k1, (unsigned)v1,
                   oct);
      } else if (k1[0] == 'D') {
        float st = detune_table[v1] / 100.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.2f st)\n", mod_name, k1, (unsigned)v1,
                   st);
      } else {
        DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k1, (unsigned)v1);
      }
    } else if (mod == MOD_LFO1 || mod == MOD_LFO2) {
      if (k1[0] == 'R') {
        if (v1 == 0) {
          DBG_PRINTF("PANEL %s %s=%u (OFF)\n", mod_name, k1, (unsigned)v1);
        } else {
          float hz = 0.05f * powf(2.0f, ((float)v1 / 127.0f) * 8.64385618f);
          DBG_PRINTF("PANEL %s %s=%u (%.2f Hz)\n", mod_name, k1, (unsigned)v1,
                     hz);
        }
      } else if (k1[0] == 'S' && k1[1] == 'm') {
        float pct = (float)v1 * 100.0f / 127.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k1, (unsigned)v1,
                   pct);
      } else {
        DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k1, (unsigned)v1);
      }
    } else if (mod == MOD_EG1 || mod == MOD_EG2) {
      if (k1[0] == 'A' || k1[0] == 'D' || k1[0] == 'R') {
        float s = 2.0f * (powf(100.0f, (float)v1 / 127.0f) - 1.0f) / 99.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.3f s)\n", mod_name, k1, (unsigned)v1, s);
      } else {
        float pct = (float)v1 * 100.0f / 127.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k1, (unsigned)v1,
                   pct);
      }
    } else if (mod == MOD_FX1 || mod == MOD_FX2) {
      if (k1[0] == 'F') {
        float pct = (float)v1 * 100.0f / 127.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k1, (unsigned)v1,
                   pct);
      } else {
        float pct = (float)v1 * 100.0f / 127.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k1, (unsigned)v1,
                   pct);
      }
    } else if (mod == MOD_GLIDE) {
      if (k1[0] == 'T') {
        float s = 0.001f * powf(10.0f, ((float)v1 / 127.0f) * 4.0f);
        DBG_PRINTF("PANEL %s %s=%u (%.3f s)\n", mod_name, k1, (unsigned)v1, s);
      } else {
        float pct = (float)v1 * 100.0f / 127.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k1, (unsigned)v1,
                   pct);
      }
    } else {
      DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k1, (unsigned)v1);
    }
  }
  if (d3 && v2 != prev_vals[mod][2]) {
    prev_vals[mod][2] = v2;
    if (mod == MOD_VCF1 || mod == MOD_VCF2) {
      if (k2[0] == 'C') {
        float hz = 50.0f * powf(160.0f, ((float)v2 / 127.0f));
        if (hz > 8000.0f)
          hz = 8000.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.1f Hz)\n", mod_name, k2, (unsigned)v2,
                   hz);
      } else if (k2[0] == 'R') {
        float q = 0.5f + ((float)v2 / 127.0f) * (12.0f - 0.5f);
        DBG_PRINTF("PANEL %s %s=%u (Q=%.2f)\n", mod_name, k2, (unsigned)v2, q);
      } else if (k2[0] == 'K' && (mod == MOD_VCF1 || mod == MOD_VCF2)) {
        float t = ((float)v2 / 127.0f) * 2.0f;
        const char *nm = (t < 0.25f)   ? "LPF"
                         : (t < 0.75f) ? "L/B"
                         : (t < 1.25f) ? "BPF"
                         : (t < 1.75f) ? "B/H"
                                       : "HPF";
        DBG_PRINTF("PANEL %s %s=%u (%.2f %s)\n", mod_name, k2, (unsigned)v2, t,
                   nm);
      } else {
        float pct = (float)v2 * 100.0f / 127.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k2, (unsigned)v2,
                   pct);
      }
    } else if (mod == MOD_VCO1 || mod == MOD_VCO2 || mod == MOD_LFO1 ||
               mod == MOD_LFO2) {
      char wbuf[32];
      get_waveform_name(v2, wbuf);
      DBG_PRINTF("PANEL %s %s=%u (%s)\n", mod_name, k2, (unsigned)v2, wbuf);
    } else if (mod == MOD_EG1 || mod == MOD_EG2) {
      if (k2[0] == 'S') {
        float pct = (float)v2 * 100.0f / 127.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k2, (unsigned)v2,
                   pct);
      } else {
        float s = 2.0f * (powf(100.0f, (float)v2 / 127.0f) - 1.0f) / 99.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.3f s)\n", mod_name, k2, (unsigned)v2, s);
      }
    } else {
      float pct = (float)v2 * 100.0f / 127.0f;
      DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k2, (unsigned)v2, pct);
    }
  }
  if (d4 && v3 != prev_vals[mod][3]) {
    prev_vals[mod][3] = v3;
    if (mod == MOD_VCF1 || mod == MOD_VCF2) {
      if (k3[0] == 'C') {
        float hz = 50.0f * powf(160.0f, ((float)v3 / 127.0f));
        if (hz > 8000.0f)
          hz = 8000.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.1f Hz)\n", mod_name, k3, (unsigned)v3,
                   hz);
      } else if (k3[0] == 'R') {
        float q = 0.5f + ((float)v3 / 127.0f) * (12.0f - 0.5f);
        DBG_PRINTF("PANEL %s %s=%u (Q=%.2f)\n", mod_name, k3, (unsigned)v3, q);
      } else if (k3[0] == 'D') {
        float g = 1.0f + ((float)v3 / 127.0f) * 9.0f;
        DBG_PRINTF("PANEL %s %s=%u (x%.2f)\n", mod_name, k3, (unsigned)v3, g);
      } else {
        float pct = (float)v3 * 100.0f / 127.0f;
        DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k3, (unsigned)v3,
                   pct);
      }
    } else if (mod == MOD_EG1 || mod == MOD_EG2) {
      float s = 2.0f * (powf(100.0f, (float)v3 / 127.0f) - 1.0f) / 99.0f;
      DBG_PRINTF("PANEL %s %s=%u (%.3f s)\n", mod_name, k3, (unsigned)v3, s);
    } else if (mod == MOD_ARP) {
      float pct = (float)v3 * 100.0f / 127.0f;
      DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k3, (unsigned)v3, pct);
    } else {
      DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k3, (unsigned)v3);
    }
  }
}
#endif

void handle_seq_encoders(int d1, int d2, int d3, int d4) {
    if (d1) {
        // Speed
        int v = (int)current_seq.speed + d1;
        if (v < 0) v = 0;
        if (v > SEQ_SPEED_4X) v = SEQ_SPEED_4X;
        seq_set_speed((SeqSpeed)v);
    }
    if (d2) {
        // Swing
        int v = (int)current_seq.swing + d2;
        if (v < 0) v = 0;
        if (v > 75) v = 75;
        current_seq.swing = (uint8_t)v;
    }
    if (d3) {
        // Mode
        int v = (int)current_seq.play_mode + d3;
        if (v < 0) v = 0;
        if (v >= SEQ_MODE_COUNT) v = SEQ_MODE_COUNT - 1;
        current_seq.play_mode = (SeqMode)v;
    }
    if (d4) {
        // Note Length
        // (0, 10, 25, 40, 50, 75, 85, 100)
        static const uint8_t lengths[] = {0, 10, 25, 40, 50, 75, 85, 100};
        int current_idx = 0;
        for (int i = 0; i < 8; i++) {
             // Find closest or just track index if we had it.
             // Simplest: find current step's gate and use as proxy for global?
             // Actually note length in SEQ is global in this UI context?
             // Specs say "note length" is a knob in Play steps.
             // I'll assume it sets gate_length for ALL steps or just a global offset.
             // User says "gate length" ranges. I'll map to index in lengths[].
        }
        // Let's just use 0-127 for internal and map UI to these 8 values.
    }
}

void handle_seq_edit_encoders(int d1, int d2, int d3, int d4) {
    SeqStep *s = &current_seq.steps[seq_edit_step];
    if (d1) {
        // VEL (0-127)
        for (int i = 0; i < SEQ_MAX_NOTES_PER_STEP; i++) {
            int v = (int)s->notes[i].velocity + d1;
            if (v < 0) v = 0;
            if (v > 127) v = 127;
            s->notes[i].velocity = (uint8_t)v;
        }
    }
    if (d2) {
        // CHRD
        int v = (int)s->chord_mode + d2;
        if (v < 0) v = 0;
        if (v >= CHORD_MODE_COUNT) v = CHORD_MODE_COUNT - 1;
        s->chord_mode = (uint8_t)v;
    }
    if (d3) {
        // Every (1-8)
        int v = (int)s->loop_every + d3;
        if (v < 1) v = 1;
        if (v > 8) v = 8;
        s->loop_every = (uint8_t)v;
    }
    if (d4) {
        // Prob (0, 10, 25, 40, 50, 75, 85, 100)
        const uint8_t steps[] = {0, 10, 25, 40, 50, 75, 85, 100};
        for (int i = 0; i < SEQ_MAX_NOTES_PER_STEP; i++) {
            uint8_t curr = s->notes[i].probability;
            int idx = 0;
            for (int k=0; k<7; k++) if (curr >= steps[k+1]) idx = k+1;
            
            if (d4 > 0 && idx < 7) {
                s->notes[i].probability = steps[idx + 1];
            } else if (d4 < 0 && idx > 0) {
                s->notes[i].probability = steps[idx - 1];
            }
        }
    }
}

void handle_params_encoders(int d1, int d2, int d3, int d4) {
  if (ui_mode == UI_MODE_SEQ) {
    handle_seq_encoders(d1, d2, d3, d4);
    return;
  }
  if (ui_mode == UI_MODE_SEQ_EDIT) {
    handle_seq_edit_encoders(d1, d2, d3, d4);
    return;
  }
  if (ui_mode == UI_MODE_PIANO) {
    handle_params_encoders_for_module(selected_module, d1, d2, d3, d4);
    return;
  }
  if (set_button_held) {
    // Adjust depth of the Current Context Modulator for the touched parameter
    int src_idx = (set_context_src_override >= 0)
                      ? set_context_src_override
                      : get_mod_source_idx(set_context_module);

    if (src_idx >= 0) {
      ParamID base = get_base_param_id(selected_module);

      // Apply updates with replacement logic
      if (d1) {
        int pid = base;
        if (params.mod_source_assigned[pid] == src_idx && params.mod_matrix[pid][src_idx] == 64) {
            // Toggle off
            params.mod_source_assigned[pid] = 0xFF;
            ui_set_status("MOD UNASSIGNED", 1000);
        } else {
            for (int s = 0; s < SRC_COUNT; s++) if (s != src_idx) params.mod_matrix[pid][s] = 64;
            params.mod_source_assigned[pid] = src_idx;
            int val = params.mod_matrix[pid][src_idx];
            val += d1;
            if (val < 0) val = 0;
            if (val > 127) val = 127;
            params.mod_matrix[pid][src_idx] = (uint8_t)val;
        }
      }
      if (d2) {
        int pid = base + 1;
        if (params.mod_source_assigned[pid] == src_idx && params.mod_matrix[pid][src_idx] == 64) {
            params.mod_source_assigned[pid] = 0xFF;
            ui_set_status("MOD UNASSIGNED", 1000);
        } else {
            for (int s = 0; s < SRC_COUNT; s++) if (s != src_idx) params.mod_matrix[pid][s] = 64;
            params.mod_source_assigned[pid] = src_idx;
            int val = params.mod_matrix[pid][src_idx];
            val += d2;
            if (val < 0) val = 0;
            if (val > 127) val = 127;
            params.mod_matrix[pid][src_idx] = (uint8_t)val;
        }
      }
      if (d3) {
        int pid = base + 2;
        if (params.mod_source_assigned[pid] == src_idx && params.mod_matrix[pid][src_idx] == 64) {
            params.mod_source_assigned[pid] = 0xFF;
            ui_set_status("MOD UNASSIGNED", 1000);
        } else {
            for (int s = 0; s < SRC_COUNT; s++) if (s != src_idx) params.mod_matrix[pid][s] = 64;
            params.mod_source_assigned[pid] = src_idx;
            int val = params.mod_matrix[pid][src_idx];
            val += d3;
            if (val < 0) val = 0;
            if (val > 127) val = 127;
            params.mod_matrix[pid][src_idx] = (uint8_t)val;
        }
      }
      if (d4) {
        int pid = base + 3;
        if (params.mod_source_assigned[pid] == src_idx && params.mod_matrix[pid][src_idx] == 64) {
            params.mod_source_assigned[pid] = 0xFF;
            ui_set_status("MOD UNASSIGNED", 1000);
        } else {
            for (int s = 0; s < SRC_COUNT; s++) if (s != src_idx) params.mod_matrix[pid][s] = 64;
            params.mod_source_assigned[pid] = src_idx;
            int val = params.mod_matrix[pid][src_idx];
            val += d4;
            if (val < 0) val = 0;
            if (val > 127) val = 127;
            params.mod_matrix[pid][src_idx] = (uint8_t)val;
        }
      }

      // Log changes
      int delta = 0;
      int idx = -1;
      if (d1) {
        delta = d1;
        idx = 0;
      } else if (d2) {
        delta = d2;
        idx = 1;
      } else if (d3) {
        delta = d3;
        idx = 2;
      } else if (d4) {
        delta = d4;
        idx = 3;
      }

      if (delta != 0 && idx >= 0) {
        int8_t depth = (int8_t)params.mod_matrix[base + idx][src_idx] - 64;
        float pct = (depth * 100.0f) / 64.0f;
        DBG_PRINTF("MOD_ASSIGN %s -> %s P%d = %.0f%%\n",
                   module_name(set_context_module),
                   module_name(selected_module), idx, pct);
      }
    }
    return;
  }

  if (fn_mode_active) {
    // FN Mode Logic (Removal)
    ParamID base = get_base_param_id(selected_module);
    // Set all to 64 (0%) instead of 0 (-100%)
    if (d1)
      for (int s = 0; s < SRC_COUNT; s++)
        params.mod_matrix[base + 0][s] = 64;
    if (d2)
      for (int s = 0; s < SRC_COUNT; s++)
        params.mod_matrix[base + 1][s] = 64;
    if (d3)
      for (int s = 0; s < SRC_COUNT; s++)
        params.mod_matrix[base + 2][s] = 64;
    if (d4)
      for (int s = 0; s < SRC_COUNT; s++)
        params.mod_matrix[base + 3][s] = 64;
    return;
  }

  // BASE VALUE EDIT (Normal)
  switch (selected_module) {
  case MOD_VCF1:
    if (d1) {
        if (ui_is_fn_held()) {
            ui_cycle_vcf_mode(&params.vcf1_type, d1, "VCF1");
        } else {
            update_param_with_id(PARAM_VCF1_CUTOFF, &params.vcf1_cutoff, d1);
        }
    }
    if (d2)
      update_param_with_id(PARAM_VCF1_RES, &params.vcf1_res, d2);
    if (d3)
      update_param_with_id(PARAM_VCF_KEY_TRACK, &params.vcf_key_track, d3);
    if (d4)
      update_param_with_id(PARAM_VCF1_MIX, &params.vcf1_mix, d4);
    break;
  case MOD_VCF2:
    if (d1) {
        if (ui_is_fn_held()) {
            ui_cycle_vcf_mode(&params.vcf2_type, d1, "VCF2");
        } else {
            update_param_with_id(PARAM_VCF2_CUTOFF, &params.vcf2_cutoff, d1);
        }
    }
    if (d2)
      update_param_with_id(PARAM_VCF2_RES, &params.vcf2_res, d2);
    if (d3)
      update_param_with_id(PARAM_VCF_KEY_TRACK, &params.vcf_key_track, d3);
    if (d4)
      update_param_with_id(PARAM_VCF2_MIX, &params.vcf2_mix, d4);
    break;
  default:
    handle_params_encoders_for_module(selected_module, d1, d2, d3, d4);
    break;
  }

  {
    uint8_t ch = midi_map_get_note_channel();
    ParamID base = get_base_param_id(selected_module);
    if (d1) {
      ParamID pid = base + 0;
      uint8_t cc = midi_map_get_cc_for_param(pid);
      if (cc != 0xFF) {
        uint8_t val = midi_map_get_value_for_param(pid);
        uint8_t msg[3] = {(uint8_t)(0xB0 | ch), cc, val};
        midi_send_message(msg, 3);
      }
    }
    if (d2) {
      ParamID pid = base + 1;
      uint8_t cc = midi_map_get_cc_for_param(pid);
      if (cc != 0xFF) {
        uint8_t val = midi_map_get_value_for_param(pid);
        uint8_t msg[3] = {(uint8_t)(0xB0 | ch), cc, val};
        midi_send_message(msg, 3);
      }
    }
    if (d3) {
      ParamID pid = base + 2;
      uint8_t cc = midi_map_get_cc_for_param(pid);
      if (cc != 0xFF) {
        uint8_t val = midi_map_get_value_for_param(pid);
        uint8_t msg[3] = {(uint8_t)(0xB0 | ch), cc, val};
        midi_send_message(msg, 3);
      }
    }
    if (d4) {
      ParamID pid = base + 3;
      uint8_t cc = midi_map_get_cc_for_param(pid);
      if (cc != 0xFF) {
        uint8_t val = midi_map_get_value_for_param(pid);
        uint8_t msg[3] = {(uint8_t)(0xB0 | ch), cc, val};
        midi_send_message(msg, 3);
      }
    }
  }

#if CFG_ENABLE_DEBUG
#if 0
    if (d1 || d2 || d3 || d4) {
        static uint8_t prev_vals[MOD_NONE + 1][4];
        static bool prev_init = false;
        if (!prev_init) {
            for (int m = 0; m <= MOD_NONE; m++) {
                for (int i = 0; i < 4; i++) {
                    prev_vals[m][i] = 0xFF;
                }
            }
            prev_init = true;
        }
        const char* mod_name = "";
        switch (selected_module) {
            case MOD_VCO1:  mod_name = "VCO1"; break;
            case MOD_VCO2:  mod_name = "VCO2"; break;
            case MOD_VCF1:  mod_name = "VCF1"; break;
            case MOD_VCF2:  mod_name = "VCF2"; break;
            case MOD_LFO1:  mod_name = "LFO1"; break;
            case MOD_LFO2:  mod_name = "LFO2"; break;
            case MOD_EG1:   mod_name = "EG1"; break;
            case MOD_EG2:   mod_name = "EG2"; break;
            case MOD_MIXER: mod_name = "MIX"; break;
            case MOD_NOISE: mod_name = "NOISE"; break;
            case MOD_ARP:   mod_name = "ARP"; break;
            case MOD_GLIDE: mod_name = "GLIDE"; break;
            case MOD_FX1:   mod_name = "FX1"; break;
            case MOD_FX2:   mod_name = "FX2"; break;
            default:        mod_name = "OTHER"; break;
        }
        const char* k0 = "P0";
        const char* k1 = "P1";
        const char* k2 = "P2";
        const char* k3 = "P3";
        uint8_t v0 = 0, v1 = 0, v2 = 0, v3 = 0;
        switch (selected_module) {
            case MOD_VCO1:
                k0="Trns"; k1="Detn"; k2="Wave"; k3="Shap";
                v0=params.vco1_transpose; v1=params.vco1_detune; v2=params.vco1_wave; v3=params.vco1_shape;
                break;
            case MOD_VCO2:
                k0="Trns"; k1="Detn"; k2="Wave"; k3="Shap";
                v0=params.vco2_transpose; v1=params.vco2_detune; v2=params.vco2_wave; v3=params.vco2_shape;
                break;
            case MOD_VCF1: {
                static const char* vcf_lbls[] = {"|LPF|", "|BPF|", "|HPF|", "|BCF|", "|APF|"};
                k0=vcf_lbls[params.vcf1_type % 5]; k1="Res"; k2="Type"; k3="Mix";
                v0=params.vcf1_cutoff; v1=params.vcf1_res; v2=params.vcf1_drive; v3=params.vcf1_mix;
                break;
            }
            case MOD_VCF2: {
                static const char* vcf_lbls[] = {"|LPF|", "|BPF|", "|HPF|", "|BCF|", "|APF|"};
                k0=vcf_lbls[params.vcf2_type % 5]; k1="Res"; k2="Type"; k3="Mix";
                v0=params.vcf2_cutoff; v1=params.vcf2_res; v2=params.vcf2_drive; v3=params.vcf2_mix;
                break;
            }
            case MOD_LFO1:
                k0="Rate"; k1="Smth"; k2="Wave"; k3="Shap";
                v0=params.lfo1_rate; v1=params.lfo1_smooth; v2=params.lfo1_wave; v3=params.lfo1_shape;
                break;
            case MOD_LFO2:
                k0="Rate"; k1="Smth"; k2="Wave"; k3="Shap";
                v0=params.lfo2_rate; v1=params.lfo2_smooth; v2=params.lfo2_wave; v3=params.lfo2_shape;
                break;
            case MOD_EG1:
                k0="|Attk|"; k1="|Dcy|"; k2="Sus"; k3="|Rels|";
                v0=params.eg1_attack; v1=params.eg1_decay; v2=params.eg1_sustain; v3=params.eg1_release;
                break;
            case MOD_EG2:
                k0="|Attk|"; k1="|Dcy|"; k2="Sus"; k3="|Rels|";
                v0=params.eg2_attack; v1=params.eg2_decay; v2=params.eg2_sustain; v3=params.eg2_release;
                break;
            case MOD_MIXER:
                if (ui_is_fn_held()) {
                    k0="|MSTR|"; v0=params.amp_gain;
                } else {
                    k0="|VCO1|"; v0=params.mix_vco_balance;
                }
                k1="VCO2"; k2="Phs2"; k3="Noise";
                v1=params.mix_osc_noise; v2=params.mix_phase2; v3=params.mix_master;
                break;
            case MOD_NOISE:
                k0="Colr"; k1="—"; k2="—"; k3="—";
                v0=params.noise_color; v1=0; v2=0; v3=0;
                break;
            case MOD_ARP:
                k0="Rate"; k1="Mode"; k2="Swing"; k3="Oct";
                v0=params.arp_rate; v1=params.arp_mode; v2=params.arp_swing; v3=params.arp_oct;
                break;
            case MOD_GLIDE:
                k0="Time"; k1="Slop"; k2="Mode"; k3="Poly";
                v0=params.glide_time; v1=params.glide_slope; v2=params.glide_mode; v3=params.glide_poly;
                break;
            case MOD_FX1:
                k0="Time"; k1="Feed"; k2="Dpth"; k3="Mix";
                v0=params.fx1_time; v1=params.fx1_feedback; v2=params.fx1_spread; v3=params.fx1_mix;
                break;
            case MOD_FX2:
                k0="Time"; k1="Feed"; k2="Tone"; k3="Mix";
                v0=params.fx2_time; v1=params.fx2_feedback; v2=params.fx2_tone; v3=params.fx2_mix;
                break;
            default:
                break;
        }
        if (d1 && v0 != prev_vals[selected_module][0]) {
            prev_vals[selected_module][0] = v0;
            if (selected_module == MOD_VCF1 || selected_module == MOD_VCF2) {
                if (k0[0]=='C') { float hz = 50.0f * powf(160.0f, ((float)v0/127.0f)); if (hz>8000.0f) hz=8000.0f; DBG_PRINTF("PANEL %s %s=%u (%.1f Hz)\n", mod_name, k0, (unsigned)v0, hz); }
                else if (k0[0]=='R') { float q=0.5f+((float)v0/127.0f)*(12.0f-0.5f); DBG_PRINTF("PANEL %s %s=%u (Q=%.2f)\n", mod_name, k0, (unsigned)v0, q); }
                else if (k0[0]=='D') { float g=1.0f+((float)v0/127.0f)*9.0f; DBG_PRINTF("PANEL %s %s=%u (x%.2f)\n", mod_name, k0, (unsigned)v0, g); }
                else { float pct=(float)v0*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k0, (unsigned)v0, pct); }
            } else if (selected_module == MOD_VCO1 || selected_module == MOD_VCO2) {
                if (k0[0]=='T') { float oct=roundf((((float)v0-64.0f)/64.0f)*5.0f); DBG_PRINTF("PANEL %s %s=%u (%.0f oct)\n", mod_name, k0, (unsigned)v0, oct); }
                else if (k0[0]=='D') {
                    float st = detune_table[v0] / 100.0f;
                    DBG_PRINTF("PANEL %s %s=%u (%.2f st)\n", mod_name, k0, (unsigned)v0, st);
                }
                else { DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k0, (unsigned)v0); }
            } else if (selected_module == MOD_LFO1 || selected_module == MOD_LFO2) {
                if (k0[0]=='R') { float hz=0.05f*powf(2.0f, ((float)v0/127.0f)*8.64f); DBG_PRINTF("PANEL %s %s=%u (%.2f Hz)\n", mod_name, k0, (unsigned)v0, hz); }
                else if (k0[0]=='S' && k0[1]=='m') { float pct=(float)v0*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k0, (unsigned)v0, pct); }
                else { DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k0, (unsigned)v0); }
            } else if (selected_module == MOD_EG1 || selected_module == MOD_EG2) {
                if (k0[0]=='A'||k0[0]=='D'||k0[0]=='R') { float s=2.0f*(powf(100.0f,((float)v0/127.0f))-1.0f)/99.0f; DBG_PRINTF("PANEL %s %s=%u (%.3f s)\n", mod_name, k0, (unsigned)v0, s); }
                else { float pct=(float)v0*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k0, (unsigned)v0, pct); }
            } else if (selected_module == MOD_MIXER) {
                float pct=(float)v0*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k0, (unsigned)v0, pct);
            } else if (selected_module == MOD_ARP) {
                if (k0[0]=='R') { float period_us=100000.0f + (127.0f - (float)v0)*2000.0f; float hz=1000000.0f/period_us; DBG_PRINTF("PANEL %s %s=%u (%.2f Hz)\n", mod_name, k0, (unsigned)v0, hz); }
                else { DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k0, (unsigned)v0); }
            } else if (selected_module == MOD_GLIDE) {
                if (k0[0]=='T') { float s=0.001f*powf(10.0f, ((float)v0/127.0f)*4.0f); DBG_PRINTF("PANEL %s %s=%u (%.3f s)\n", mod_name, k0, (unsigned)v0, s); }
                else { float pct=(float)v0*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k0, (unsigned)v0, pct); }
            } else if (selected_module == MOD_FX1 || selected_module == MOD_FX2) {
                if (k0[0]=='T' && k0[1]=='i') { float ms=5.0f*powf(2.0f, ((float)v0/127.0f)*7.64f); DBG_PRINTF("PANEL %s %s=%u (%.1f ms)\n", mod_name, k0, (unsigned)v0, ms); }
                else { float pct=(float)v0*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k0, (unsigned)v0, pct); }
            } else {
                DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k0, (unsigned)v0);
            }
        }
        if (d2 && v1 != prev_vals[selected_module][1]) {
            prev_vals[selected_module][1] = v1;
            if (selected_module == MOD_NOISE) {
                DBG_PRINTF("PANEL %s %s=%u (%s)\n", mod_name, k1, (unsigned)v1, chord_mode_name(v1));
            } else if (selected_module == MOD_VCF1 || selected_module == MOD_VCF2) {
                if (k1[0]=='C') { float hz = 50.0f * powf(160.0f, ((float)v1/127.0f)); if (hz>8000.0f) hz=8000.0f; DBG_PRINTF("PANEL %s %s=%u (%.1f Hz)\n", mod_name, k1, (unsigned)v1, hz); }
                else if (k1[0]=='R') { float q=0.5f+((float)v1/127.0f)*(12.0f-0.5f); DBG_PRINTF("PANEL %s %s=%u (Q=%.2f)\n", mod_name, k1, (unsigned)v1, q); }
                else if (k1[0]=='D') { float g=1.0f+((float)v1/127.0f)*9.0f; DBG_PRINTF("PANEL %s %s=%u (x%.2f)\n", mod_name, k1, (unsigned)v1, g); }
                else { float pct=(float)v1*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k1, (unsigned)v1, pct); }
            } else if (selected_module == MOD_VCO1 || selected_module == MOD_VCO2) {
                if (k1[0]=='T') { float oct=roundf((((float)v1-64.0f)/64.0f)*5.0f); DBG_PRINTF("PANEL %s %s=%u (%.0f oct)\n", mod_name, k1, (unsigned)v1, oct); }
                else if (k1[0]=='D') {
                    float st = detune_table[v1] / 100.0f;
                    DBG_PRINTF("PANEL %s %s=%u (%.2f st)\n", mod_name, k1, (unsigned)v1, st);
                }
                else { DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k1, (unsigned)v1); }
            } else if (selected_module == MOD_LFO1 || selected_module == MOD_LFO2) {
                if (k1[0]=='R') { float hz=0.05f*powf(2.0f, ((float)v1/127.0f)*8.64f); DBG_PRINTF("PANEL %s %s=%u (%.2f Hz)\n", mod_name, k1, (unsigned)v1, hz); }
                else if (k1[0]=='S' && k1[1]=='m') { float pct=(float)v1*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k1, (unsigned)v1, pct); }
                else { DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k1, (unsigned)v1); }
            } else if (selected_module == MOD_EG1 || selected_module == MOD_EG2) {
                if (k1[0]=='A'||k1[0]=='D'||k1[0]=='R') { float s=0.001f*powf(10.0f, ((float)v1/127.0f)*4.0f); DBG_PRINTF("PANEL %s %s=%u (%.3f s)\n", mod_name, k1, (unsigned)v1, s); }
                else { float pct=(float)v1*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k1, (unsigned)v1, pct); }
            } else if (selected_module == MOD_MIXER || selected_module == MOD_GLIDE || selected_module == MOD_FX1 || selected_module == MOD_FX2) {
                if ((selected_module == MOD_FX1 || selected_module == MOD_FX2) && k1[0]=='F') { float pct=(float)v1*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k1, (unsigned)v1, pct); }
                else if (selected_module == MOD_GLIDE && k1[0]=='T') { float s=0.001f*powf(10.0f, ((float)v1/127.0f)*4.0f); DBG_PRINTF("PANEL %s %s=%u (%.3f s)\n", mod_name, k1, (unsigned)v1, s); }
                else { float pct=(float)v1*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k1, (unsigned)v1, pct); }
            } else if (selected_module == MOD_ARP) {
                DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k1, (unsigned)v1);
            } else {
                DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k1, (unsigned)v1);
            }
        }
        if (d3 && v2 != prev_vals[selected_module][2]) {
            prev_vals[selected_module][2] = v2;
            if (selected_module == MOD_VCF1 || selected_module == MOD_VCF2) {
                if (k2[0]=='C') { float hz = 50.0f * powf(160.0f, ((float)v2/127.0f)); if (hz>8000.0f) hz=8000.0f; DBG_PRINTF("PANEL %s %s=%u (%.1f Hz)\n", mod_name, k2, (unsigned)v2, hz); }
                else if (k2[0]=='R') { float q=0.5f+((float)v2/127.0f)*(12.0f-0.5f); DBG_PRINTF("PANEL %s %s=%u (Q=%.2f)\n", mod_name, k2, (unsigned)v2, q); }
                else if (k2[0]=='T' && (selected_module==MOD_VCF1 || selected_module==MOD_VCF2)) { float t=((float)v2/127.0f)*2.0f; const char* nm=(t<0.25f)?"LPF":(t<0.75f)?"L/B":(t<1.25f)?"BPF":(t<1.75f)?"B/H":"HPF"; DBG_PRINTF("PANEL %s %s=%u (%.2f %s)\n", mod_name, k2, (unsigned)v2, t, nm); }
                else { float pct=(float)v2*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k2, (unsigned)v2, pct); }
            } else if (selected_module == MOD_LFO1 || selected_module == MOD_LFO2 || selected_module == MOD_MIXER || selected_module == MOD_FX1 || selected_module == MOD_FX2) {
                float pct=(float)v2*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k2, (unsigned)v2, pct);
            } else if (selected_module == MOD_EG1 || selected_module == MOD_EG2) {
                if (k2[0]=='S') { float pct=(float)v2*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k2, (unsigned)v2, pct); }
                else { float s=0.001f*powf(10.0f, ((float)v2/127.0f)*4.0f); DBG_PRINTF("PANEL %s %s=%u (%.3f s)\n", mod_name, k2, (unsigned)v2, s); }
            } else if (selected_module == MOD_ARP) {
                DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k2, (unsigned)v2);
            } else if (selected_module == MOD_GLIDE) {
                float pct=(float)v2*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k2, (unsigned)v2, pct);
            } else if (selected_module == MOD_VCO1 || selected_module == MOD_VCO2) {
                DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k2, (unsigned)v2);
            } else {
                DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k2, (unsigned)v2);
            }
        }
        if (d4 && v3 != prev_vals[selected_module][3]) {
            prev_vals[selected_module][3] = v3;
            if (selected_module == MOD_VCF1 || selected_module == MOD_VCF2) {
                if (k3[0]=='C') { float hz = 50.0f * powf(160.0f, ((float)v3/127.0f)); if (hz>8000.0f) hz=8000.0f; DBG_PRINTF("PANEL %s %s=%u (%.1f Hz)\n", mod_name, k3, (unsigned)v3, hz); }
                else if (k3[0]=='R') { float q=0.5f+((float)v3/127.0f)*(12.0f-0.5f); DBG_PRINTF("PANEL %s %s=%u (Q=%.2f)\n", mod_name, k3, (unsigned)v3, q); }
                else if (k3[0]=='D') { float g=1.0f+((float)v3/127.0f)*9.0f; DBG_PRINTF("PANEL %s %s=%u (x%.2f)\n", mod_name, k3, (unsigned)v3, g); }
                else { float pct=(float)v3*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k3, (unsigned)v3, pct); }
            } else if (selected_module == MOD_VCO1 || selected_module == MOD_VCO2) {
                DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k3, (unsigned)v3);
            } else if (selected_module == MOD_LFO1 || selected_module == MOD_LFO2) {
                DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k3, (unsigned)v3);
            } else if (selected_module == MOD_EG1 || selected_module == MOD_EG2) {
                float s=0.001f*powf(10.0f, ((float)v3/127.0f)*4.0f); DBG_PRINTF("PANEL %s %s=%u (%.3f s)\n", mod_name, k3, (unsigned)v3, s);
            } else if (selected_module == MOD_MIXER) {
                float pct=(float)v3*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k3, (unsigned)v3, pct);
            } else if (selected_module == MOD_ARP) {
                DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k3, (unsigned)v3);
            } else if (selected_module == MOD_GLIDE) {
                DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k3, (unsigned)v3);
            } else if (selected_module == MOD_FX1 || selected_module == MOD_FX2) {
                float pct=(float)v3*100.0f/127.0f; DBG_PRINTF("PANEL %s %s=%u (%.0f%%)\n", mod_name, k3, (unsigned)v3, pct);
            } else {
                DBG_PRINTF("PANEL %s %s=%u\n", mod_name, k3, (unsigned)v3);
            }
        }
    }
#endif
#endif
}

// Get Color for Encoder LED based on param value
void get_encoder_led(int enc_idx, uint8_t *r, uint8_t *g, uint8_t *b) {
  uint8_t val = 0;

  if (set_mode_active) {
    // SET Mode LEDs
    int src_idx = (set_context_src_override >= 0)
                      ? set_context_src_override
                      : get_mod_source_idx(set_context_module);
    if (src_idx >= 0) {
      // Show Mod Depth
      ParamID base = get_base_param_id(selected_module);
      if (base + enc_idx < PARAM_COUNT) {
        val = params.mod_matrix[base + enc_idx][src_idx];
      }
      // Pink/Purple for Mod Depth
      *r = val;
      *g = 0;
      *b = val;
      return;
    }
    // Routing Mode (Encoders inactive)
    *r = 0;
    *g = 0;
    *b = 0;
    return;
  } else if (fn_mode_active) {
    // FN Mode (Red)
    *r = 50;
    *g = 0;
    *b = 0;
    return;
  } else {
    // Normal Mode
    // Map same as handle_params_encoders to get value
    switch (selected_module) {
    case MOD_VCO1:
      if (enc_idx == 0) val = pra_synth_get_modulated_param_value(PARAM_VCO1_TRANSPOSE);
      if (enc_idx == 1) val = pra_synth_get_modulated_param_value(PARAM_VCO1_DETUNE);
      if (enc_idx == 2) val = pra_synth_get_modulated_param_value(PARAM_VCO1_WAVE);
      if (enc_idx == 3) val = pra_synth_get_modulated_param_value(PARAM_VCO1_SHAPE);
      break;
    case MOD_VCO2:
      if (enc_idx == 0) val = pra_synth_get_modulated_param_value(PARAM_VCO2_TRANSPOSE);
      if (enc_idx == 1) val = pra_synth_get_modulated_param_value(PARAM_VCO2_DETUNE);
      if (enc_idx == 2) val = pra_synth_get_modulated_param_value(PARAM_VCO2_WAVE);
      if (enc_idx == 3) val = pra_synth_get_modulated_param_value(PARAM_VCO2_SHAPE);
      break;
    case MOD_VCF1:
      if (enc_idx == 0) val = pra_synth_get_modulated_param_value(PARAM_VCF1_CUTOFF);
      if (enc_idx == 1) val = pra_synth_get_modulated_param_value(PARAM_VCF1_RES);
      if (enc_idx == 2) val = pra_synth_get_modulated_param_value(PARAM_VCF_KEY_TRACK);
      if (enc_idx == 3) val = pra_synth_get_modulated_param_value(PARAM_VCF1_MIX);
      break;
    case MOD_VCF2:
      if (enc_idx == 0) val = pra_synth_get_modulated_param_value(PARAM_VCF2_CUTOFF);
      if (enc_idx == 1) val = pra_synth_get_modulated_param_value(PARAM_VCF2_RES);
      if (enc_idx == 2) val = pra_synth_get_modulated_param_value(PARAM_VCF_KEY_TRACK);
      if (enc_idx == 3) val = pra_synth_get_modulated_param_value(PARAM_VCF2_MIX);
      break;
    case MOD_EG1:
      if (enc_idx == 0) val = pra_synth_get_modulated_param_value(PARAM_EG1_ATTACK);
      if (enc_idx == 1) val = pra_synth_get_modulated_param_value(PARAM_EG1_DECAY);
      if (enc_idx == 2) val = pra_synth_get_modulated_param_value(PARAM_EG1_SUSTAIN);
      if (enc_idx == 3) val = pra_synth_get_modulated_param_value(PARAM_EG1_RELEASE);
      break;
    case MOD_EG2:
      if (enc_idx == 0) val = pra_synth_get_modulated_param_value(PARAM_EG2_ATTACK);
      if (enc_idx == 1) val = pra_synth_get_modulated_param_value(PARAM_EG2_DECAY);
      if (enc_idx == 2) val = pra_synth_get_modulated_param_value(PARAM_EG2_SUSTAIN);
      if (enc_idx == 3) val = pra_synth_get_modulated_param_value(PARAM_EG2_RELEASE);
      break;
    case MOD_LFO1:
      if (enc_idx == 0) val = pra_synth_get_modulated_param_value(PARAM_LFO1_RATE);
      if (enc_idx == 1) val = pra_synth_get_modulated_param_value(PARAM_LFO1_SMOOTH);
      if (enc_idx == 2) val = pra_synth_get_modulated_param_value(PARAM_LFO1_WAVE);
      if (enc_idx == 3) val = pra_synth_get_modulated_param_value(PARAM_LFO1_SHAPE);
      break;
    case MOD_LFO2:
      if (enc_idx == 0) val = pra_synth_get_modulated_param_value(PARAM_LFO2_RATE);
      if (enc_idx == 1) val = pra_synth_get_modulated_param_value(PARAM_LFO2_SMOOTH);
      if (enc_idx == 2) val = pra_synth_get_modulated_param_value(PARAM_LFO2_WAVE);
      if (enc_idx == 3) val = pra_synth_get_modulated_param_value(PARAM_LFO2_SHAPE);
      break;
    case MOD_NOISE:
      if (enc_idx == 0) val = pra_synth_get_modulated_param_value(PARAM_NOISE_COLOR);
      break;
    case MOD_ARP:
      if (enc_idx == 0) val = pra_synth_get_modulated_param_value(PARAM_ARP_RATE);
      if (enc_idx == 1) val = pra_synth_get_modulated_param_value(PARAM_ARP_MODE);
      if (enc_idx == 2) val = pra_synth_get_modulated_param_value(PARAM_ARP_SWING);
      if (enc_idx == 3) val = pra_synth_get_modulated_param_value(PARAM_ARP_OCT);
      break;
    case MOD_FX1:
      if (enc_idx == 0) val = pra_synth_get_modulated_param_value(PARAM_FX1_TIME);
      if (enc_idx == 1) val = pra_synth_get_modulated_param_value(PARAM_FX1_FEEDBACK);
      if (enc_idx == 2) val = pra_synth_get_modulated_param_value(PARAM_FX1_SPREAD);
      if (enc_idx == 3) val = pra_synth_get_modulated_param_value(PARAM_FX1_MIX);
      break;
    case MOD_FX2:
      if (enc_idx == 0) val = pra_synth_get_modulated_param_value(PARAM_FX2_TIME);
      if (enc_idx == 1) val = pra_synth_get_modulated_param_value(PARAM_FX2_FEEDBACK);
      if (enc_idx == 2) val = pra_synth_get_modulated_param_value(PARAM_FX2_TONE);
      if (enc_idx == 3) val = pra_synth_get_modulated_param_value(PARAM_FX2_MIX);
      break;
    case MOD_GLIDE:
      if (enc_idx == 0) val = pra_synth_get_modulated_param_value(PARAM_GLIDE_POLY);
      if (enc_idx == 1) val = pra_synth_get_modulated_param_value(PARAM_GLIDE_TIME);
      if (enc_idx == 2) val = pra_synth_get_modulated_param_value(PARAM_GLIDE_SLOPE);
      if (enc_idx == 3) val = pra_synth_get_modulated_param_value(PARAM_GLIDE_MODE);
      break;
    case MOD_MIXER:
      if (enc_idx == 0) {
        if (ui_is_fn_held()) val = pra_synth_get_modulated_param_value(PARAM_AMP_GAIN);
        else val = pra_synth_get_modulated_param_value(PARAM_MIX_VCO1_VOL);
      }
      if (enc_idx == 1) val = pra_synth_get_modulated_param_value(PARAM_MIX_VCO2_VOL);
      if (enc_idx == 2) val = pra_synth_get_modulated_param_value(PARAM_MIX_PHASE2);
      if (enc_idx == 3) val = pra_synth_get_modulated_param_value(PARAM_MIX_NOISE_VOL);
      break;
    default:
      val = 0;
    }
  }
  uint16_t max_val = get_param_max(selected_module, enc_idx);
  uint8_t norm_val = 0;
  if (max_val > 0) {
    norm_val = (uint8_t)(((uint32_t)val * 127) / max_val);
  }
  get_param_color(selected_module, norm_val, r, g, b);
}

// Helper: Update Arp Notes based on held keys
void ui_midi_note_on(uint8_t note, uint8_t velocity) {
  if (note >= 128) return;
  midi_held_notes[note] = true;

  // Map to current octave pads for lighting
  int base_note_in_octave = 36 + (octave * 12); // Assuming 36 is middle C base for the engine
  // Wait, let's use the actual base_notes[0] + octave*12
  int current_octave_start = base_notes[0] + (octave * 12);
  
  if (note >= current_octave_start && note < current_octave_start + 12) {
      // It's in the current octave. Find which pad.
      // We need to find which i has base_notes[i] + octave*12 == note
      for (int i = 0; i < 12; i++) {
          if (base_notes[i] + (octave * 12) == note) {
              active_notes[i] = note;
              midi_note_mask |= (1 << i);
              break;
          }
      }
  }

  if (params.arp_mode > 0) {
    update_arp_notes();
  } else {
    synth_note_on(note, velocity);
  }
}

void ui_midi_note_off(uint8_t note) {
  if (note >= 128) return;
  midi_held_notes[note] = false;

  // Update lighting mask
  for (int i = 0; i < 12; i++) {
      if (active_notes[i] == note) {
          active_notes[i] = -1;
          midi_note_mask &= ~(1 << i);
      }
  }

  if (params.arp_mode > 0) {
    update_arp_notes();
  } else {
    synth_note_off(note);
  }
}

void update_arp_notes() {
  int prev_count = arp_note_count;
  arp_note_count = 0;
  
  // Collect notes from pads
  uint16_t pad_active = (held_keys | latched_keys) & 0x0FFF;
  for (int i = 0; i < 12; i++) {
    if (pad_active & (1 << i)) {
      int note = base_notes[i] + (octave * 12);
      if (note < 0) note = 0;
      if (note > 127) note = 127;
      
      // Add if not already present
      bool exists = false;
      for (int j = 0; j < arp_note_count; j++) {
          if (arp_notes[j] == note) { exists = true; break; }
      }
      if (!exists && arp_note_count < 16) {
          arp_notes[arp_note_count++] = note;
      }
    }
  }

  // Collect notes from MIDI
  for (int i = 0; i < 128; i++) {
      if (midi_held_notes[i]) {
          bool exists = false;
          for (int j = 0; j < arp_note_count; j++) {
              if (arp_notes[j] == i) { exists = true; break; }
          }
          if (!exists && arp_note_count < 16) {
              arp_notes[arp_note_count++] = i;
          }
      }
  }
  
  // Sort notes for consistent ARP behavior (UP/DOWN/etc.)
  for (int i = 0; i < arp_note_count - 1; i++) {
      for (int j = i + 1; j < arp_note_count; j++) {
          if (arp_notes[i] > arp_notes[j]) {
              int tmp = arp_notes[i];
              arp_notes[i] = arp_notes[j];
              arp_notes[j] = tmp;
          }
      }
  }
  
  if (prev_count == 0 && arp_note_count > 0) {
      arp_note_index = 0;
      arp_direction = 1;
      arp_step_parity = 0;
      last_arp_step = time_us_32() - 2000000; // Trigger immediately
      DBG_PRINTF("ARP Pool Start: %d notes\n", arp_note_count);
  }
}

// Helper: Process Arp
void process_arp(uint32_t now) {
  if (params.arp_mode == 0) {
    if (arp_was_on) {
      if (last_arp_note != -1) {
        synth_note_off(last_arp_note);
        last_arp_note = -1;
      }
      arp_was_on = false;
      arp_note_is_on = false;
    }
    return;
  }

  arp_was_on = true;

  // Get Tempo & Sync Division
  float bpm = g_midi_bpm;
  if (params.adv_tempo == ADV_TEMPO_OFF) bpm = 120.0f;
  else if (params.adv_tempo >= ADV_TEMPO_BPM_MIN) bpm = 30.0f + (int)(params.adv_tempo - ADV_TEMPO_BPM_MIN);

  uint32_t base_period_us = (uint32_t)(adv_sync_mode_to_ms(params.arp_rate, bpm) * 1000.0f);
  
  // Apply Swing
  // Every two steps (quarter note if 8th notes), we adjust the intervals
  float swing_factor = swing_lut[params.arp_swing];
  uint32_t this_step_period_us;
  if (arp_step_parity == 0) {
      // First step (odd): take base_period * 2 * swing_factor
      this_step_period_us = (uint32_t)(base_period_us * 2.0f * swing_factor);
  } else {
      // Second step (even): take remaining time
      this_step_period_us = (uint32_t)(base_period_us * 2.0f * (1.0f - swing_factor));
  }

  uint32_t gate_us = (uint32_t)(base_period_us * 0.8f); // Fixed 80% gate length
  
  // Ensure a small gap for re-triggering
  if (gate_us >= this_step_period_us && this_step_period_us > 2000) gate_us = this_step_period_us - 1000;
  else if (gate_us >= this_step_period_us) gate_us = (this_step_period_us * 9) / 10;

  // Handle Note Off (Gate)
  if (arp_note_is_on && (now - last_arp_step >= gate_us)) {
      if (last_arp_note != -1) {
          synth_note_off(last_arp_note);
          last_arp_note = -1;
      }
      arp_note_is_on = false;
  }

  // Check Clock for next step
  if (now - last_arp_step < this_step_period_us)
    return;

  if (arp_note_count == 0) {
    last_arp_step = now; // keep it sliding
    return;
  }
  
  // Advance Step
  int range = arp_note_count;
  int oct_offset = (int)params.arp_oct - 3; // -3..+3
  int oct_count = abs(oct_offset) + 1;
  int total_steps = range * oct_count;

  // Mode Logic
  // 1: UP, 2: DOWN, 3: UPDOWN, 4: RND, 5: DRUNK
  int mode = params.arp_mode;

  if (mode == 4) { // RND
    arp_note_index = rand() % total_steps;
  } else if (mode == 5) { // DRUNK
    arp_note_index += (rand() % 3 - 1);
    if (arp_note_index < 0) arp_note_index = 0;
    if (arp_note_index >= total_steps) arp_note_index = total_steps - 1;
  } else if (mode == 2) { // DOWN
    arp_note_index--;
    if (arp_note_index < 0)
      arp_note_index = total_steps - 1;
  } else if (mode == 3) { // UP-DOWN
    arp_note_index += arp_direction;
    if (arp_note_index >= total_steps) {
        if (total_steps > 1) {
            arp_direction = -1;
            arp_note_index = total_steps - 2;
        } else {
            arp_note_index = 0;
        }
    } else if (arp_note_index < 0) {
        if (total_steps > 1) {
            arp_direction = 1;
            arp_note_index = 1;
        } else {
            arp_note_index = 0;
        }
    }
  } else { // UP (Default)
    arp_note_index++;
    if (arp_note_index >= total_steps)
      arp_note_index = 0;
  }

  // Map Index to Note + Octave
  int note_idx = arp_note_index % range;
  int oct_idx = arp_note_index / range;
  
  // Handle octave direction
  int current_oct_shift = 0;
  if (oct_offset > 0) current_oct_shift = oct_idx;
  else if (oct_offset < 0) current_oct_shift = -oct_idx;

  int note = arp_notes[note_idx] + (current_oct_shift * 12);
  if (note < 0) note = 0;
  if (note > 127) note = 127;

  DBG_PRINTF("ARP Trig: Note %d (idx %d, oct %d)\n", note, note_idx, current_oct_shift);
  synth_note_on(note, 127);
  last_arp_note = note;
  arp_note_is_on = true;
  last_arp_step = now; // Mark timing of this note trigger
  arp_step_parity = (arp_step_parity + 1) % 2;
}
