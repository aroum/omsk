#ifndef UI_LOGIC_H
#define UI_LOGIC_H

#include "ui_state.h"
#include "ui_oled.h"
#include <stdint.h>

void ui_init(void);
const char *get_piano_key_label_from_index(int idx);
void ui_handle_pad_pressed(uint8_t pad_index);
void ui_handle_pad_released(uint8_t pad_index);
void handle_params_encoders(int d1, int d2, int d3, int d4);
void handle_params_encoders_lower_row(int d1, int d2, int d3, int d4);
void process_arp(uint32_t now);
void ui_set_status(const char *msg, uint32_t ms);
void get_encoder_led(const OledPage *page, int enc_idx, uint8_t *r, uint8_t *g, uint8_t *b);
void ui_midi_note_on(uint8_t note, uint8_t velocity);
void ui_midi_note_off(uint8_t note);
void ui_process_preset_longpress(uint32_t now);
void build_oled_page(OledPage *out);
extern const ModuleID btn_to_mod[16];

#endif // UI_LOGIC_H
