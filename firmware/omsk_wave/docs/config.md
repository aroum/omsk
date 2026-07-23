# Synth Firmware Configuration (`omsk_synth`)

All hardware and software settings for the synthesizer are specified at compile-time using C/C++ preprocessor macros. They are divided into two header files:

1. `shared/hw_config.h` — hardware parameters (pins, overclocking, LED brightness).
2. `omsk_synth/src/sw_config.h` — parameters for sound engine, display, MIDI, and interface.

---

## 1. Hardware Settings (`shared/hw_config.h`)

This file is shared between the synthesizer and granular firmwares.

### LED Brightness Setting

The maximum brightness limit for WS2812 LEDs is set using the `RGB_MAX_BRIGHTNESS` macro (value range `0` to `255`, default is set to `63` for comfortable glare-free lighting):

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

## 2. System Settings (`omsk_synth/src/sw_config.h`)

These parameters are specific to the synthesizer engine.

### Audio Outputs and Volume

- `CFG_MASTER_VOLUME_PERCENT` — master volume percentage (default `100`).
- `CFG_ENABLE_USB_AUDIO` — experimental USB audio output (1 — on, 0 — off).
- `CFG_ENABLE_PWM8_AUDIO` — monophonic 8-bit PWM output.
- `CFG_ENABLE_DAC` — stereo 16-bit / 48 kHz output via I2S DAC.

### OLED Display and Interface

- `CFG_ENABLE_OLED` — enables display driver.
- `CFG_SLEEP_TIMEOUT_MIN` — sleep mode timeout in minutes (0 — disabled, >0 — sleep after N minutes). When sleeping, OLED display and RGB LEDs turn off. Wake-up occurs on any physical interaction (button presses, encoder turns) or receiving MIDI messages.
- `CFG_LOGO_SUBTITLE` — firmware version subtitle string on boot logo (e.g., `"v1.0-dev"`).
- `CFG_ENABLE_RGB_LED` — enables RGB LEDs.

### MIDI Settings

- `CFG_ENABLE_USB_MIDI` — enables MIDI rx/tx over USB.
- `CFG_ENABLE_JACK_MIDI` — enables TRS MIDI IN/OUT jacks.
- `CFG_ENABLE_MIDI_THRU` — enables forwarding incoming MIDI stream directly to MIDI OUT (1 — on, 0 — off).
- `CFG_PITCH_BEND_RANGE_SEMITONES` — Pitch Bend range in semitones (default `2`).

### Synthesizer Engine Modes (Filter, EG, OSC)

You can choose calculation modes to optimize CPU load:

- **Filter Selection (`CFG_FILTER_MODE`)**:
  - `FILTER_MODE_PRA32_TABLES` (0) — table-based with LPF steps based on [digital-synth-pra32-u2](https://github.com/risgk/digital-synth-pra32-u2) (high quality, low CPU overhead, LUT size ~2.6 MB).
  - `FILTER_MODE_MORPH_SVF` (1) — real-time filter math (~8% load per voice, 0 bytes LUT size).
  - `FILTER_MODE_3_TABLES` (2) — interpolation across 3 tables (~2.2% load per voice, LUT size ~1.4 MB).
  - `FILTER_MODE_MOOG_LADDER` (3) — high quality Moog Ladder 24 dB/oct filter (LUT size ~7.4 KB, ~5.5% load per voice).
- **Envelope Modes (`CFG_EG_MODE`)**: `EG_MODE_TABLE` (table envelope) or `EG_MODE_CALC` (mathematical calculation).
- **LFO Modes (`CFG_LFO_MODE`)**: `LFO_MODE_TABLE` (table LFO) or `LFO_MODE_CALC` (mathematical calculation).

### Overclocking and Logging

- `CFG_ENABLE_OVERCLOCK` — enables CPU overclocking (150 MHz for RP2040, 240 MHz for RP2350).
- `CFG_ENABLE_DEBUG` — enables debug log output via USB serial (macro `DBG_PRINTF` can be used).
