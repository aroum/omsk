#ifndef SW_CONFIG_H
#define SW_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include "../../shared/hw_config.h"

// =============================================================================
// MIDI CONFIGURATION
// =============================================================================
#define MIDI_CHANNEL 1

typedef enum { MSG_NOTE, MSG_CC } midi_msg_t;

typedef enum {
  ENC_MODE_ABSOLUTE, // Sends continuous 0-127 values
  ENC_MODE_RELATIVE  // Sends 63 for left, 65 for right
} encoder_mode_t;

typedef struct {
  midi_msg_t type;
  uint8_t index; // Note number or CC number
} control_config_t;

typedef struct {
  midi_msg_t type;
  uint8_t index;
  encoder_mode_t mode;
} encoder_config_t;

// =============================================================================
// MIDI Hardware Options
// =============================================================================
#define CFG_ENABLE_USB_MIDI 1
#define CFG_ENABLE_JACK_MIDI 1
#define CFG_ENABLE_MIDI_THRU 1 // MIDI In -> Out duplication

// =============================================================================
// RGB LED MAPPING
// =============================================================================
#define LED_COUNT NUM_RGB_LEDS
#define RGB_BRIGHTNESS_PERCENT 10

// Array of 22 RGB colors (0xRRGGBB)
static const uint32_t LED_COLORS[LED_COUNT] = {
    0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, // 0-4
    0x00FFFF, 0xFFFFFF, 0xFF8000, 0x8000FF, 0x00FF80, // 5-9
    0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, // 10-14
    0x00FFFF, 0xFFFFFF, 0xFF8000, 0x8000FF, 0x00FF80, // 15-19
    0xFF0000, 0x00FF00                                // 20-21
};

// =============================================================================
// MATRIX BUTTON MAPPING
// =============================================================================
#define MATRIX_ROWS 4
#define MATRIX_COLS 4
#define BTN_COUNT (MATRIX_ROWS * MATRIX_COLS)

// 16 Buttons MIDI Assignments (linear index: row * COLS + col)
static const control_config_t BUTTON_CONFIGS[BTN_COUNT] = {
    {MSG_NOTE, 60}, {MSG_NOTE, 61}, {MSG_NOTE, 62}, {MSG_NOTE, 63}, // Row 0
    {MSG_NOTE, 64}, {MSG_NOTE, 65}, {MSG_NOTE, 66}, {MSG_NOTE, 67}, // Row 1
    {MSG_CC, 10},   {MSG_CC, 11},   {MSG_CC, 12},   {MSG_CC, 13},   // Row 2
    {MSG_CC, 14},   {MSG_CC, 15},   {MSG_CC, 16},   {MSG_CC, 17}    // Row 3
};

// =============================================================================
// ENCODER MAPPING
// =============================================================================
#define ENCODER_COUNT 4

// 4 Encoders MIDI Assignments
static const encoder_config_t ENCODER_CONFIGS[ENCODER_COUNT] = {
    {MSG_CC, 20, ENC_MODE_ABSOLUTE}, // Enc 1
    {MSG_CC, 21, ENC_MODE_ABSOLUTE}, // Enc 2
    {MSG_CC, 22, ENC_MODE_RELATIVE}, // Enc 3
    {MSG_CC, 23, ENC_MODE_RELATIVE}  // Enc 4
};

// Status LED (Glows while at least one button is held)
#define PIN_STATUS_LED PICO_DEFAULT_LED_PIN

// USB Configuration
#define USB_VENDOR_ID 0xCAFE
#define USB_PRODUCT_ID 0x4006
#define USB_MANUFACTURER "Omsk"
#define USB_PRODUCT "Omsk MIDI"

#endif // SW_CONFIG_H
