#ifndef PRA_SYNTH_H
#define PRA_SYNTH_H

#include "synth_defs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void pra_synth_init(void);
int16_t pra_synth_get_sample(void);
void pra_synth_get_stereo(int16_t *left, int16_t *right);
void pra_synth_note_on(uint8_t note);
void pra_synth_note_off(uint8_t note);
void pra_synth_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
void pra_synth_midi_note_off(uint8_t channel, uint8_t note, uint8_t velocity);
void pra_synth_midi_pitch_bend(uint8_t channel, uint8_t lsb, uint8_t msb);
void pra_synth_midi_aftertouch(uint8_t channel, uint8_t pressure);
void pra_synth_midi_control_change(uint8_t channel, uint8_t control_number, uint8_t value);
void pra_synth_all_notes_off(void);
void pra_synth_update_control(void);
void pra_synth_param_change(ParamID param, uint16_t value);
uint16_t pra_synth_get_modulated_param_value(ParamID param);
void pra_synth_set_hold_mode(bool active);
bool pra_synth_is_hold_mode(void);
void pra_synth_set_midi_channel(uint8_t ch);

#ifdef __cplusplus
}
#endif

#endif // PRA_SYNTH_H
