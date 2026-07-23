#ifndef OMSK_CORE_H
#define OMSK_CORE_H

#include "../synth/synth_defs.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void omsk_core_init(void);
void omsk_core_note_on(uint8_t note, uint8_t velocity);
void omsk_core_note_off(uint8_t note);
void omsk_core_process(float *out_l, float *out_r);
void omsk_core_background_process(void);
void omsk_core_set_param(uint8_t param_id, uint16_t value);
void omsk_core_all_notes_off(void);
void omsk_core_pitch_bend(uint8_t lsb, uint8_t msb);
void omsk_core_set_pitch_bend_range(uint8_t semitones);
void omsk_core_set_sustain(bool on);
void omsk_core_update_control(void);
void omsk_core_set_modwheel(uint8_t value);
void omsk_core_set_aftertouch(uint8_t value);
void omsk_core_set_breath(uint8_t value);

#ifdef __cplusplus
}
#endif

#endif // OMSK_CORE_H
