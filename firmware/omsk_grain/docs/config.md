# Granular Firmware Configuration (`omsk_grain`)

All hardware and software settings for the instrument are specified at compile-time using C/C++ preprocessor macros. They are divided into two header files:
1. `shared/hw_config.h` — hardware parameters (pins, overclocking, LED brightness).
2. `omsk_grain/src/sw_config.h` — audio engine and interface parameters.

---

## 1. Hardware Settings (`shared/hw_config.h`)

This file is shared between the synthesizer and granular firmwares.

### LED Brightness Setting
The maximum brightness limit for WS2812 LEDs is set using the `RGB_MAX_BRIGHTNESS` macro (value range from `0` to `255`, default set to `63` for comfortable glare-free lighting):
```c
#define RGB_MAX_BRIGHTNESS 63
```

### CPU Frequency (Overclock)
The system clock frequency is set depending on the chip used (RP2040 or RP2350):
```c
#ifdef PICO_RP2040
#define CFG_OVERCLOCK_KHZ 133000 // For RP2040 (133 MHz)
#else
#define CFG_OVERCLOCK_KHZ 240000 // For RP2350 (240 MHz)
#endif
```

### Physical Pin Assignments (Pinout)
- **Keyboard Matrix**: `PIN_MATRIX_ROW1..4` and `PIN_MATRIX_COL1..4`
- **Encoders**: `PIN_ENCODER1..4_A` and `PIN_ENCODER1..4_B`
- **Audio Outputs**:
  - PWM: `PIN_AUDIO_PWM_L` (GPIO 29) and `PIN_AUDIO_PWM_R` (GPIO 16)
  - I2S DAC: `PIN_DAC_I2S_BCK` (GPIO 17), `PIN_DAC_I2S_LRCK` (GPIO 18), `PIN_DAC_I2S_DATA` (GPIO 19)
- **OLED Display (I2C)**: `PIN_OLED_SDA` (GPIO 20) and `PIN_OLED_SCL` (GPIO 21)
- **RGB LEDs**: `PIN_RGB_LED` (GPIO 8)
- **MIDI Jack (Serial)**: `PIN_MIDI_JACK_OUT` (GPIO 0) and `PIN_MIDI_JACK_IN` (GPIO 1)

---

## 2. System Settings (`omsk_grain/src/sw_config.h`)

These parameters are specific to the granular engine.

### Audio Output Selection
You can select the physical audio output method by uncommenting one of the macros:
```c
// #define AUDIO_OUT_DAC // Output via I2S DAC (PCM5102, etc.)
#define AUDIO_OUT_PWM // Output via two PWM channels (default)
```

### Sleep Timeout
Time in minutes after which, in the absence of activity, the OLED screen and LEDs turn off (0 — sleep disabled):
```c
#define CFG_SLEEP_TIMEOUT_MIN 1
```

### Sampler Parameters
Parameters for importing and playing back samples from built-in memory:
```c
#define CONFIG_BIT_DEPTH            8     // Sample bit depth (8-bit)
#define CONFIG_SAMPLE_RATE         22050  // Sampling rate (22050 Hz)
#define CONFIG_SILENCE_THRESHOLD_DB -50   // Silence threshold for auto-trim (dB)
#define CONFIG_ENVELOPE_POINTS      128   // Number of sample envelope points
```
