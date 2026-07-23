#include <stdint.h>
#include "amy.h"

// Platform shims for AMY on OMSK (RP2350)
// We handle our own audio and MIDI, so these are minimal or empty.

void amy_platform_init() {
    // OMSK handles audio/DMA init separately.
    // If we need AMY-specific global init, it goes here.
}

void amy_platform_deinit() {
    // Cleanup if needed.
}

void run_midi() {
    // OMSK has its own MIDI handling in midi_handler.c
}

void stop_midi() {
    // OMSK handles MIDI shutdown if needed.
}
