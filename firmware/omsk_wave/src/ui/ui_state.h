#ifndef UI_STATE_H
#define UI_STATE_H

#include "../synth/synth_defs.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum { UI_MODE_PIANO, UI_MODE_PARAMS, UI_MODE_SEQ, UI_MODE_SEQ_EDIT } UIMode;

typedef enum {
  MOD_VCO1 = 0,
  MOD_VCO2,
  MOD_NOISE,
  MOD_MIXER,
  MOD_VCF1,
  MOD_VCF2,
  MOD_FX1,
  MOD_FX2,
  MOD_LFO1,
  MOD_LFO2,
  MOD_EG1,
  MOD_EG2,
  MOD_ARP,
  MOD_GLIDE,
  MOD_SET,
  MOD_MOD,
  MOD_FN,
  MOD_ADV,
  MOD_NONE
} ModuleID;

extern UIMode ui_mode;
extern UIMode last_ui_mode;
extern ModuleID selected_module;
extern ModuleID target_module;

extern bool set_mode_active;
extern bool fn_mode_active;
extern bool fn_button_held;
extern ModuleID set_context_module;
extern ModuleID last_mod_source;
extern int set_context_src_override;

extern bool set_button_held;
extern bool ui_oled_view_graph;
extern bool assignment_mode;
extern uint32_t set_press_time;
extern uint16_t midi_pad_state;

extern char ui_status_msg[32];
extern uint32_t ui_status_msg_timeout_ms;

bool is_modulator(ModuleID m);
int get_mod_source_idx(ModuleID m);
const char *module_name(ModuleID m);
ParamID get_base_param_id(ModuleID m);

#endif
