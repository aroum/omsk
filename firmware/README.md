# Firmware Overview (`firmware`)

This repository contains the firmware source code for the OMSK hardware platform (RP2040 / RP2350 microcontrollers).

## Project Structure

- **`omsk_wave/`**: 4-voice hybrid polyphonic synthesizer firmware (wavetable, Moog filters, step sequencer, FX).
- **`omsk_fm/`**: 6-operator Frequency Modulation (FM) synthesizer firmware compatible with DX7 SysEx patches.
- **`omsk_grain/`**: 4-voice granular synthesizer firmware with custom sample engine.
- **`omsk_oled_test/`**: Minimal test utility for I2C OLED display initialization, alignment, and u8g2 graphics driver verification (SSD1312 / SH1107).
- **`shared/`**: Common hardware drivers, pinout definitions (`hw_config.h`), library code, and utilities shared across all firmwares.

## Building

All firmwares are built using the Pico C/C++ SDK via the central script:
```bash
./build_all.sh -p rp2350 -cs
```

To build a specific project (e.g. OLED test utility):
```bash
./build_all.sh -p rp2350 -t omsk_oled_test
```
