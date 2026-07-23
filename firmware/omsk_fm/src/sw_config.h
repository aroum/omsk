#ifndef SW_CONFIG_H
#define SW_CONFIG_H

#include "../../shared/hw_config.h"

// Piano key labels for matrix mode (row x col)
#define CFG_PIANO_LAYOUT_ROWS 4
#define CFG_PIANO_LAYOUT_COLS 4
#define CFG_PIANO_LAYOUT_LABEL_CHARS 5

static const char CFG_PIANO_LAYOUT[CFG_PIANO_LAYOUT_ROWS][CFG_PIANO_LAYOUT_COLS]
                                  [CFG_PIANO_LAYOUT_LABEL_CHARS] = {
                                      {"C", "D#", "F#", "A"},
                                      {"C#", "E", "G", "A#"},
                                      {"D", "F", "G#", "B"},
                                      {"OCT-", "OCT+", "ARP", "ADV"}};

// =============================================================================
// AUDIO
// =============================================================================
#define CFG_MASTER_VOLUME_PERCENT 100

// USB Audio
#define CFG_ENABLE_USB_AUDIO 0

// PWM Audio
#define CFG_ENABLE_PWM8_AUDIO 1    // mono 8 bit
#define CFG_ENABLE_BETTER_PWM  1   // 10-bit PWM with noise shaping for lower noise floor

// I2S DAC
#define CFG_ENABLE_DAC 0

// =============================================================================
// DISPLAY
// =============================================================================
#define CFG_ENABLE_OLED 1
#define SHOW_STATUS_BAR
#define SHOW_ICONS

// Sleep timeout in minutes. 0 = disabled.
#define CFG_SLEEP_TIMEOUT_MIN 5

// Logo subtitle
#define CFG_LOGO_SUBTITLE "v1.0-dev"

// =============================================================================
// RGB LED
// =============================================================================
#define CFG_ENABLE_RGB_LED 1

// =============================================================================
// MIDI
// =============================================================================
#define CFG_ENABLE_USB_MIDI 1
#define CFG_ENABLE_JACK_MIDI 1
#define CFG_ENABLE_MIDI_THRU 1 // MIDI In -> Out duplication
#define CFG_PITCH_BEND_RANGE_SEMITONES 2

// =============================================================================
// SYSTEM
// =============================================================================
#define CFG_ENABLE_SEQUENCER 1

// Overclock clocks for I2S/Audio Stability
#define CFG_ENABLE_OVERCLOCK 1

// Recommended clock speeds
#define CFG_OVERCLOCK_RP2040_KHZ 150000
#define CFG_OVERCLOCK_RP2350_KHZ 240000

// DX7 YM21280 Decoded Algorithms Mode
#define CFG_FM_YM21280_DECODE 1

// Debug logger
#define CFG_ENABLE_DEBUG 1

#define CFG_ENABLE_USB                                                         \
  (CFG_ENABLE_USB_MIDI || CFG_ENABLE_USB_AUDIO || CFG_ENABLE_DEBUG)

#if CFG_ENABLE_DEBUG
#ifdef __cplusplus
extern "C" {
#endif
void usb_debug_printf(const char *fmt, ...);
#ifdef __cplusplus
}
#endif
#define DBG_PRINTF(...) usb_debug_printf(__VA_ARGS__)
#else
#define DBG_PRINTF(...) ((void)0)
#endif

#endif // SW_CONFIG_H
