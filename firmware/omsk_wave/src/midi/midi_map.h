#ifndef MIDI_MAP_H
#define MIDI_MAP_H

#include "../synth/synth_defs.h"
#include <stdbool.h>
#include <stdint.h>


void midi_map_init(void);
void midi_map_process(uint8_t status, uint8_t d1, uint8_t d2);

void midi_map_set_note_channel(uint8_t ch);
void midi_map_set_cc_mapping(uint8_t cc, uint8_t channel, ParamID param);
void midi_map_clear_cc(uint8_t cc);
void midi_map_consume_pad_event(uint8_t *cc, uint8_t *val);
void midi_map_consume_encoder_deltas(int8_t out[8]);
bool midi_map_consume_modwheel(uint8_t *value);
bool midi_map_consume_aftertouch(uint8_t *value);
bool midi_map_consume_breath(uint8_t *value);
uint8_t midi_map_get_cc_for_param(ParamID param);
uint8_t midi_map_get_value_for_param(ParamID param);
uint8_t midi_map_get_note_channel(void);
uint8_t *midi_map_get_param_ptr(ParamID pid);

#endif
