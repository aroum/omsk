# Shared Hardware & Driver Components (`firmware/shared`)

This folder contains the shared library of hardware drivers, low-level abstractions, and helper modules (DSP, PIO, third-party libraries) utilized by all firmware applications in the OMSK project.

---

## 📁 Directory Structure

* `hw_config.h` — Main hardware configuration header: GPIO pin assignments (button matrix, encoders, OLED display, DAC/PWM audio, RGB LEDs, MIDI), display type selection, WS2812 LED mapping, and MCU overclocking settings.
* `image_logo.h` — Bitmap header for the OMSK boot logo displayed on the OLED screen.
* `hardware/` — Low-level device drivers:
  * `matrix.h` / `matrix.cpp` — Driver for the 4x4 switch matrix.
  * `encoders.h` / `encoders.cpp` — Driver for rotary encoder handling.
  * `midi_uart.h` / `midi_uart.cpp` — Hardware UART MIDI (DIN/TRS Jack) driver.
  * `midi_helpers.h` — Helper utilities for building and parsing MIDI messages.
  * `colors.h` — Color palettes and preset definitions for WS2812 RGB LEDs.
* `dsp/` — Digital Signal Processing utilities:
  * `fast_math.h` / `fast_math.cpp` — Fast math lookup table functions.
* `pio/` — Programs for RP2040/RP2350 Programmable I/O (PIO) state machines:
  * `audio.pio` — PWM audio output state machine.
  * `i2s_tx.pio` — I2S transmitter state machine for external DACs (e.g., PCM5102).
  * `ws2812.pio` — WS2812B addressable LED driver state machine.
  * `button_matrix.pio` — PIO matrix scanning driver.
* `lib/` — Third-party libraries and dependencies:
  * `u8g2` — Graphics library for OLED displays.
  * `tinyusb` — USB stack for USB MIDI and CDC interfaces.
  * `CMSIS-DSP`, `pico-extras`, `pico_audio_i2s_32b`, `pico_fft` — Audio processing and I2S/DSP support libraries.

---

## 🛠️ Configuration Parameters (`hw_config.h`)

The `hw_config.h` file defines physical pin routing and core hardware configuration settings across all firmwares.

### 1. Keyboard Matrix (4x4 Matrix Switches)
* `PIN_MATRIX_COL1` .. `PIN_MATRIX_COL4` (GPIO 10, 9, 7, 6): Column GPIO pins for matrix scanning.
* `PIN_MATRIX_ROW1` .. `PIN_MATRIX_ROW4` (GPIO 14, 13, 12, 11): Row GPIO pins for matrix scanning.

### 2. Encoders
* `PIN_ENCODER1_A` / `PIN_ENCODER1_B` (GPIO 2, 3) — Encoder 1 channels A/B.
* `PIN_ENCODER2_A` / `PIN_ENCODER2_B` (GPIO 4, 5) — Encoder 2 channels A/B.
* `PIN_ENCODER3_A` / `PIN_ENCODER3_B` (GPIO 15, 26) — Encoder 3 channels A/B.
* `PIN_ENCODER4_A` / `PIN_ENCODER4_B` (GPIO 27, 28) — Encoder 4 channels A/B.
* `ENCODER_RESOLUTION` (default: 4): Number of pulses/detents per physical step.

### 3. Audio Output Pins
* `PIN_AUDIO_PWM_L` (GPIO 29) / `PIN_AUDIO_PWM_R` (GPIO 16): PWM mono/stereo audio output pins.
* `PIN_DAC_I2S_BCK` (GPIO 17), `PIN_DAC_I2S_LRCK` (GPIO 18), `PIN_DAC_I2S_DATA` (GPIO 19): I2S pins for external DACs (Bit Clock, Left/Right Clock, and Serial Data).

### 4. Display Pins
* `CFG_OLED_TYPE`: Selects the OLED display controller/orientation:
  * `0`: SSD1312 128x64 (Standard horizontal screen).
  * `1`: SH1107 64x128 (Vertical "Turtle" screen).
* `PIN_OLED_SCL` (GPIO 21), `PIN_OLED_SDA` (GPIO 20): I2C clock and data lines.
* `OLED_I2C` (`i2c0`): Hardware I2C peripheral instance used.
* `OLED_FLIP`: Rotates the display output 180 degrees.
* `OLED_BRIGHTNESS_PERCENT` (default: 15): Screen brightness percentage.

### 5. RGB LED Pins & Mapping (WS2812B)
* `PIN_RGB_LED` (GPIO 8): Data output pin for WS2812B strip.
* `NUM_RGB_LEDS` (default: 22): Total number of LEDs (16 matrix key backlights + 6 encoder/mode status LEDs).
* `RGB_MAX_BRIGHTNESS` (default: 63): Maximum brightness scaling limit (0-255).
* `LED_ENCODER_1` .. `LED_ENCODER_4`, `LED_MODE_1`, `LED_MODE_2`: Status indicator LED indices.
* `MATRIX_LED_MAP[16]`: Mapping array associating 4x4 matrix key indices with physical WS2812 strip indices.

### 6. MIDI Jacks (Serial Hardware MIDI)
* `PIN_MIDI_JACK_OUT` (GPIO 0) / `PIN_MIDI_JACK_IN` (GPIO 1): Hardware UART MIDI pins (TRS Type A).
* `MIDI_THRU`: Enables automatic hardware forwarding of incoming MIDI In data to MIDI Out.

### 7. Debounce & System Overclocking
* `DEBOUNCE_TIME_MS` (default: 10): Switch debounce filtering window in milliseconds.
* `CFG_OVERCLOCK_KHZ`: Target MCU boot clock frequency in kHz (133000 kHz for RP2040, 240000 kHz for RP2350).
