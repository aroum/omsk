# Omsk FM Configuration Specification

Omsk FM configuration parameters are defined in `src/sw_config.h`. This file specifies compiler switches, audio engines, system clock settings, peripheral drivers, and dependencies shared with other synthesizers in the `omsk` project.

---

## 🛠️ Build Configuration Flags (`src/sw_config.h`)

### 1. Hardware Sharing Options

Omsk FM imports core drivers from the `shared` library. The following definitions enable hardware layers:

* `CFG_ENABLE_OLED` (0 or 1): Compiles the 128x64 SSD1306 OLED interface driver and screen rendering loops.
* `CFG_ENABLE_RGB_LED` (0 or 1): Compiles the WS2812 RGB LED controller for the 16-pad matrix and encoder indicators.
* `CFG_PIANO_LAYOUT`: Defines the physical midi mapping of the 16 pads in matrix play mode.

### 2. Audio Engine Settings

* `CFG_ENABLE_PWM8_AUDIO` (0 or 1): Configures 8-bit mono pulse-width modulation audio output on GPIO pins.
* `CFG_ENABLE_BETTER_PWM` (0 or 1): Activates 10-bit hardware PWM with noise shaping to lower the noise floor.
* `CFG_ENABLE_DAC` (0 or 1): Compiles the I2S master clock, bit clock, and data output code to drive external DACs (e.g., PCM5102A).

### 3. MIDI & USB Configuration

* `CFG_ENABLE_USB_MIDI` (0 or 1): Enables USB composite class driver support for MIDI streaming.
* `CFG_ENABLE_JACK_MIDI` (0 or 1): Enables hardware UART MIDI reception and transmission via standard optocoupled jacks.
* `CFG_ENABLE_MIDI_THRU` (0 or 1): Automatically mirrors input MIDI streams directly to MIDI Out.

### 4. Overclocking Settings

To maintain a stable sampling rate with 6 operators per voice (across multiple polyphonic voices), MCU overclocking is recommended:

* `CFG_ENABLE_OVERCLOCK` (0 or 1): Enables custom system clock frequencies on boot.
* `CFG_OVERCLOCK_RP2040_KHZ` (default: 150000): Frequency in kHz (150 MHz) for RP2040 boards.
* `CFG_OVERCLOCK_RP2350_KHZ` (default: 240000): Frequency in kHz (240 MHz) for RP2350 boards.

---

## 🏗️ Folder and Build Alignment

The `omsk_fm` folder matches the structure of `omsk_wave`:

* `CMakeLists.txt`: Defines targets, includes the Pico SDK, and links `shared` hardware drivers.
* `src/main.cpp`: Synthesizer entry point initializing system hardware, starting the MIDI task, and running the background voice rendering loop.
* `src/synth/`: Real-time audio rendering code utilizing lookup tables (LUTs) for oscillators and envelope scaling curves.
* `src/usb/` and `src/midi/`: Standard TinyUSB stack and SysEx parsing buffers.
