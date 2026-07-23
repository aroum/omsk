#ifndef SYNTH_H
#define SYNTH_H

#include "synth_defs.h"
#include <stdbool.h>
#include <stdint.h>


// Global parameters instance
extern SynthParams params;

void synth_init(void);

// Get next audio sample (16-bit signed)
int16_t synth_get_sample(void);

// Note Control (Polyphonic)
void synth_note_on(uint8_t note, uint8_t velocity);
void synth_note_off(uint8_t note);

// Legacy/Helper setters (maps to params)
void synth_set_lpf_cutoff(uint8_t val);
void synth_set_resonance(uint8_t val);
void synth_set_lfo_freq(uint8_t val);
void synth_set_param(int module, int param_idx, uint8_t value);

bool synth_preset_save(uint8_t slot);
bool synth_preset_load(uint8_t slot);
void synth_apply_all_params(void);

#endif
