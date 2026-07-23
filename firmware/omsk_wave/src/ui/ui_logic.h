#pragma once

#include "../sw_config.h"
#include "../synth/synth.h"
#include "../synth/synth_defs.h"
#include "ui_oled.h"
#include "ui_state.h"
#include <stdbool.h>
#include <stdint.h>

// Globals
extern UIMode ui_mode;
extern ModuleID selected_module;
extern ModuleID target_module;
extern bool set_mode_active;
extern bool fn_mode_active;
extern ModuleID set_context_module;
extern ModuleID last_mod_source;
extern int set_context_src_override;
extern bool preset_mode_active;
extern int mod_edit_param_idx;
extern int mod_edit_source_idx;
extern int octave;
extern bool hold_mode;
extern bool arp_mode;
extern uint16_t held_keys;
extern uint16_t latched_keys;
extern uint32_t last_arp_step;
extern int arp_note_index;
extern int arp_notes[16];
extern int arp_note_count;
extern int last_arp_note;
extern bool arp_was_on;
extern bool set_button_held;
extern bool assignment_mode;
extern uint32_t set_press_time;
extern int active_notes[16];
extern uint16_t midi_note_mask;
extern uint32_t preset_hold_start[16];
extern bool preset_hold_used[16];

// Constants
extern const int base_notes[12];
extern const ModuleID btn_to_mod[16];

// Functions
const char *get_piano_key_label_from_index(int idx);
bool is_modulator(ModuleID m);
bool is_filter(ModuleID m);
bool is_source(ModuleID m);
int get_mod_source_idx(ModuleID m);
const char *module_name(ModuleID m);
void ui_handle_pad_pressed(uint8_t pad_index);
void ui_handle_pad_released(uint8_t pad_index);
void ui_process_preset_longpress(uint32_t now);
void handle_module_select_from_pad(uint8_t pad_index);
int get_module_index(ModuleID m);
ModuleID get_module_below(ModuleID m);
void handle_params_encoders_for_module(ModuleID mod, int d1, int d2, int d3,
                                       int d4);
void handle_params_encoders_lower_row(int d1, int d2, int d3, int d4);
ParamID get_base_param_id(ModuleID mod);
void build_oled_page(OledPage *out);
void get_param_color(ModuleID mod, uint8_t val, uint8_t *r, uint8_t *g,
                     uint8_t *b);
float mtof(int note);
const char *chord_mode_name(uint8_t mode);
int encode_step_from_delta(int delta);
void update_param(uint8_t *param, int delta);
void update_param_wrap(uint8_t *param, int delta);
void update_param_with_id(ParamID param_id, uint8_t *param, int delta);
void update_param_wrap_with_id(ParamID param_id, uint8_t *param, int delta);
void log_chord_mode_if_changed(void);
void get_waveform_name(uint8_t val, char *buf);
void handle_params_encoders(int d1, int d2, int d3, int d4);
void get_encoder_led(int enc_idx, uint8_t *r, uint8_t *g, uint8_t *b);
void update_arp_notes(void);
void process_arp(uint32_t now);
void log_panel_changes_for_module(ModuleID mod, int d1, int d2, int d3, int d4);
void ui_set_status(const char *msg, uint32_t ms);
void ui_midi_note_on(uint8_t note, uint8_t velocity);
void ui_midi_note_off(uint8_t note);
