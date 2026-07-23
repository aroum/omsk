#ifndef SW_CONFIG_H
#define SW_CONFIG_H

#include "../../shared/hw_config.h"

// =============================================================================
// MIDI CC MAPPINGS (for emulation)
// =============================================================================

// Encoders (Relative Mode: 63 or less is -, 65 or more is +)
#define MIDI_CC_ENC1 110
#define MIDI_CC_ENC2 111
#define MIDI_CC_ENC3 112
#define MIDI_CC_ENC4 113

// Matrix Buttons (16 buttons)
#define MIDI_CC_BTN_ROW1_COL1 40
#define MIDI_CC_BTN_ROW1_COL2 41
#define MIDI_CC_BTN_ROW1_COL3 42
#define MIDI_CC_BTN_ROW1_COL4 43

#define MIDI_CC_BTN_ROW2_COL1 44
#define MIDI_CC_BTN_ROW2_COL2 45
#define MIDI_CC_BTN_ROW2_COL3 46
#define MIDI_CC_BTN_ROW2_COL4 47

#define MIDI_CC_BTN_ROW3_COL1 48
#define MIDI_CC_BTN_ROW3_COL2 49
#define MIDI_CC_BTN_ROW3_COL3 50
#define MIDI_CC_BTN_ROW3_COL4 51

#define MIDI_CC_BTN_ROW4_COL1 52
#define MIDI_CC_BTN_ROW4_COL2 53
#define MIDI_CC_BTN_ROW4_COL3 54
#define MIDI_CC_BTN_ROW4_COL4 55

// =============================================================================
// AUDIO OUTPUT MODE — choose one:
// =============================================================================
//   AUDIO_OUT_PWM  — Dual PWM output (pin 29 L, pin 21 R)
//   AUDIO_OUT_DAC  — I2S DAC output  (PCM5102A etc.)
// #define AUDIO_OUT_DAC
#define AUDIO_OUT_PWM

// =============================================================================
// DISPLAY
// =============================================================================
#define SHOW_STATUS_BAR
#define SHOW_ICONS
#define CFG_LOGO_SUBTITLE "v1.0-dev"

// =============================================================================
// SYSTEM
// =============================================================================

// Sleep timeout in minutes. 0 = disabled.
// Device turns off OLED display and RGB LEDs if there is no activity.
#define CFG_SLEEP_TIMEOUT_MIN 1

#define CONFIG_BIT_DEPTH            8
#define CONFIG_SAMPLE_RATE         22050
#define CONFIG_SILENCE_THRESHOLD_DB -50
#define CONFIG_ENVELOPE_POINTS      128

#endif // SW_CONFIG_H
