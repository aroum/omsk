#ifndef SYNTH_H
#define SYNTH_H

#include "synth_defs.h"
#include <stdbool.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

// Global parameters instance
extern SynthParams params;

void synth_init(void);

// Get next audio sample (16-bit signed)
int16_t synth_get_sample(void);

// Note Control (Polyphonic)
void synth_note_on(uint8_t note, uint8_t velocity);
void synth_note_off(uint8_t note);

bool synth_preset_save(uint8_t slot);
bool synth_preset_load(uint8_t slot);
void synth_apply_all_params(void);
uint8_t synth_get_param_value(ParamID param);

#ifdef __cplusplus
}
#endif

#endif
