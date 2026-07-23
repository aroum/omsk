#ifndef MIDI_MAP_H
#define MIDI_MAP_H

#include <stdint.h>

void midi_map_init(void);
void midi_map_process(uint8_t status, uint8_t d1, uint8_t d2);

#endif // MIDI_MAP_H
