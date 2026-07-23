#ifndef MIDI_HELPERS_H
#define MIDI_HELPERS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Convert 14-bit pitch bend value (LSB, MSB) to semitones offset.
// lsb, msb are in range 0..127.
// 8192 is the center value (no bend).
static inline float midi_pitch_bend_to_semitones(uint8_t lsb, uint8_t msb, float range_semitones) {
    int16_t bend = (int16_t)(((msb & 0x7F) << 7) | (lsb & 0x7F)) - 8192;
    return ((float)bend / 8192.0f) * range_semitones;
}

#ifdef __cplusplus
}
#endif

#endif // MIDI_HELPERS_H
