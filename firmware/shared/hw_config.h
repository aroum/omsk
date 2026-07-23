#ifndef HW_CONFIG_H
#define HW_CONFIG_H

#include <stdint.h>

// =============================================================================
// KEYBOARD MATRIX
// =============================================================================

#define PIN_MATRIX_COL1 10
#define PIN_MATRIX_COL2 9
#define PIN_MATRIX_COL3 7
#define PIN_MATRIX_COL4 6

#define PIN_MATRIX_ROW1 14
#define PIN_MATRIX_ROW2 13
#define PIN_MATRIX_ROW3 12
#define PIN_MATRIX_ROW4 11

// =============================================================================
// ENCODERS
// =============================================================================

#define PIN_ENCODER1_A 2
#define PIN_ENCODER1_B 3
#define PIN_ENCODER2_A 4
#define PIN_ENCODER2_B 5
#define PIN_ENCODER3_A 15
#define PIN_ENCODER3_B 26
#define PIN_ENCODER4_A 27
#define PIN_ENCODER4_B 28

#define ENCODER_RESOLUTION 4

// =============================================================================
// AUDIO OUTPUT PINS
// =============================================================================
// PWM Audio pins (Pin 29 L, Pin 21 R as per schematic)
#define PIN_AUDIO_PWM_L 29
#define PIN_AUDIO_PWM_R 16

// I2S DAC pins
#define PIN_DAC_I2S_BCK 17
#define PIN_DAC_I2S_LRCK 18
#define PIN_DAC_I2S_DATA 19

// =============================================================================
// DISPLAY PINS
// =============================================================================
// DISPLAY TYPE SELECTION
// 0 = SSD1312 128x64 (Standard horizontal screen)
// 1 = SH1107 64x128 (Turtle vertical screen)
#define CFG_OLED_TYPE 1

#define PIN_OLED_SCL 21
#define PIN_OLED_SDA 20 
#define OLED_I2C i2c0
#define OLED_FLIP
#define OLED_BRIGHTNESS_PERCENT 15

// =============================================================================
// RGB LED PINS
// =============================================================================

#define PIN_RGB_LED 8
#define NUM_RGB_LEDS 22 //  7-22 led per switch; 0-6 additional leds
#define RGB_MAX_BRIGHTNESS 63

// RGB LED Indices
#define LED_ENCODER_1 0
#define LED_ENCODER_2 1
#define LED_MODE_1    2
#define LED_MODE_2    3
#define LED_ENCODER_3 4
#define LED_ENCODER_4 5

// Array mapping matrix button index to the physical LED index in the WS2812 strip
static const uint8_t MATRIX_LED_MAP[16] = {
    9, 8, 7, 6,       // Row 0
    10, 11, 12, 13,   // Row 1
    17, 16, 15, 14,   // Row 2
    18, 19, 20, 21    // Row 3
};

// =============================================================================
// MIDI JACKS (Serial MIDI)
// =============================================================================
#define PIN_MIDI_JACK_OUT 0
#define PIN_MIDI_JACK_IN 1
#define MIDI_THRU

// =============================================================================
// DEBOUNCE
// =============================================================================
#define DEBOUNCE_TIME_MS 10

// =============================================================================
// SYSTEM
// =============================================================================
#ifdef PICO_RP2040
#define CFG_OVERCLOCK_KHZ 133000 // Safer for USB than 133MHz
#else
#define CFG_OVERCLOCK_KHZ 240000 // RP2350
#endif

#endif // HW_CONFIG_H
