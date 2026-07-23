#ifndef MIDI_MAP_H
#define MIDI_MAP_H

#include <stdint.h>
#include <stdbool.h>
#include "../ui/ui_state.h"
#include "../sw_config.h"

// MIDI CC to hardware emulation
// All CC numbers configured in config.h

// CC values for 16 buttons — derived from config.h
// Order: ROW1..ROW4, COL1..COL4
static const uint8_t BTN_CC_MAP[16] = {
    MIDI_CC_BTN_ROW1_COL1, MIDI_CC_BTN_ROW1_COL2, MIDI_CC_BTN_ROW1_COL3, MIDI_CC_BTN_ROW1_COL4,
    MIDI_CC_BTN_ROW2_COL1, MIDI_CC_BTN_ROW2_COL2, MIDI_CC_BTN_ROW2_COL3, MIDI_CC_BTN_ROW2_COL4,
    MIDI_CC_BTN_ROW3_COL1, MIDI_CC_BTN_ROW3_COL2, MIDI_CC_BTN_ROW3_COL3, MIDI_CC_BTN_ROW3_COL4,
    MIDI_CC_BTN_ROW4_COL1, MIDI_CC_BTN_ROW4_COL2, MIDI_CC_BTN_ROW4_COL3, MIDI_CC_BTN_ROW4_COL4,
};

// CC values for 4 encoders — derived from config.h
static const uint8_t ENC_CC_MAP[4] = {
    MIDI_CC_ENC1, MIDI_CC_ENC2, MIDI_CC_ENC3, MIDI_CC_ENC4
};

// Process a USB MIDI CC message, updates UIState and synth params
// Returns true if something changed
bool midi_map_process(UIState* state, void* voices_ptr, uint8_t cc, uint8_t value);
float get_normalized_param_value(void* voices_ptr, int voice_idx, int param_id, int page_id);

#endif // MIDI_MAP_H
