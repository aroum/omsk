#pragma once
#include <stdint.h>

void midi_init(void);
void midi_poll(void);
void midi_process_byte(uint8_t b);
void midi_send_message(const uint8_t *msg, uint8_t length);
