#ifndef LEDS_H
#define LEDS_H

#include <stdint.h>

void leds_init(void);
void leds_set_pixel(int index, uint8_t r, uint8_t g, uint8_t b);
void leds_show(void);
void leds_set_all(uint8_t r, uint8_t g, uint8_t b);

#endif
