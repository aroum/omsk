#ifndef SYNTH_H
#define SYNTH_H

#include "synth_defs.h"
#include <stdbool.h>
#include <stdint.h>

extern SynthParams params;

void synth_init(void);
int16_t synth_get_sample(void);
void synth_note_on(uint8_t note, uint8_t velocity);
void synth_note_off(uint8_t note);

void synth_set_lpf_cutoff(uint8_t val);
void synth_set_resonance(uint8_t val);
void synth_set_lfo_freq(uint8_t val);
void synth_set_param(int module, int param_idx, uint8_t value);

#include "fm_synth.h"

bool synth_preset_save(uint8_t slot);
bool synth_preset_load(uint8_t slot);
void synth_apply_all_params(void);

bool fm_library_save(uint8_t cartridge, uint8_t slot, const FmPatch *patch);
bool fm_library_load(uint8_t cartridge, uint8_t slot, FmPatch *patch);

#endif // SYNTH_H
