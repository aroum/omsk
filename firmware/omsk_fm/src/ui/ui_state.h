#ifndef UI_STATE_H
#define UI_STATE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  UI_MODE_PIANO,
  UI_MODE_PARAMS,
  UI_MODE_SEQ,
  UI_MODE_SEQ_EDIT
} UIMode;

typedef enum {
  MOD_FREQ = 0,
  MOD_LVL_MOD,
  MOD_LFO,
  MOD_EG,
  MOD_KBDSCALE,
  MOD_FILT,
  MOD_ALGO_FB,
  MOD_PITCH_EG,
  MOD_OP1,
  MOD_OP2,
  MOD_OP3,
  MOD_OP4,
  MOD_OP5,
  MOD_OP6,
  MOD_MEM,
  MOD_SYS,
  MOD_ARP,
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
extern volatile bool g_oled_dirty;
extern volatile uint32_t g_oled_draw_count;

// Piano / Arp / Latch globals
extern int octave;
extern uint16_t held_keys;
extern uint16_t latched_keys;
extern int active_notes[16];
extern uint16_t midi_note_mask;

extern uint8_t active_op;
extern uint8_t sub_page;

#endif // UI_STATE_H
