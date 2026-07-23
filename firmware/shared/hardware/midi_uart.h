#ifndef SHARED_MIDI_UART_H
#define SHARED_MIDI_UART_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the MIDI UART on hardware
void midi_uart_init(void);

// Write a raw byte to the MIDI UART tx line
void midi_uart_write_byte(uint8_t byte);

// Read a raw byte from the MIDI UART rx line if available
bool midi_uart_read_byte(uint8_t *byte);

#ifdef __cplusplus
}
#endif

#endif // SHARED_MIDI_UART_H
