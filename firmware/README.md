# Firmware Overview (`firmware`)

This repository contains the firmware source code for the OMSK hardware platform (RP2040 / RP2350 microcontrollers).

## Project Structure

- **`omsk_wave/`**: 4-voice hybrid polyphonic synthesizer firmware (wavetable, Moog filters, step sequencer, FX).
- **`omsk_fm/`**: 6-operator Frequency Modulation (FM) synthesizer firmware compatible with DX7 SysEx patches.
- **`omsk_grain/`**: 4-voice granular synthesizer firmware with custom sample engine.
- **`omsk_midi/`**: USB/Hardware MIDI interface and router firmware.
- **`omsk_oled_test/`**: Minimal test utility for I2C OLED display initialization, alignment, and u8g2 graphics driver verification (SSD1312 / SH1107).
- **`shared/`**: Common hardware drivers, pinout definitions (`hw_config.h`), library code, and submodules shared across all firmwares.

---

## Prerequisites & Submodules Initialization

Before building any firmware, ensure the repository submodules (such as TinyUSB, u8g2, CMSIS-DSP, etc.) are fully initialized.

Clone with submodules:
```bash
git clone --recursive https://github.com/aroum/omsk.git
```

Or if you have already cloned the repository without submodules:
```bash
git submodule update --init --recursive
```

Make sure you have the **Pico SDK** installed and `PICO_SDK_PATH` set in your environment:
```bash
export PICO_SDK_PATH=/path/to/pico-sdk
```

---

## Building

Each firmware folder contains its own dedicated build script (`build_all.sh` or `build.sh`) supporting arguments for cleaning, memory analysis, platform selection (`rp2040` / `rp2350`), and flashing via `picotool`.

### Building Individual Firmware

Navigate to the project directory and run the build script:

```bash
cd firmware/omsk_wave
./build_all.sh -p rp2350 -c -s
```

#### Script Flags & Options:
- `-c`, `--clean`: Remove build directory and re-run CMake configuration from scratch.
- `-s`, `--size`: Print detailed Flash and RAM memory usage report after build.
- `-p <platform>`: Target MCU platform (`rp2040` or `rp2350`, default is `rp2350`).
- `-f`, `--flash`: Flash binary directly to connected device using `picotool`.

### Examples

Build **`omsk_wave`** for RP2350 with clean build and memory analysis:
```bash
cd firmware/omsk_wave
./build_all.sh -p rp2350 -c -s
```

Build **`omsk_fm`** for RP2040 and flash to hardware:
```bash
cd firmware/omsk_fm
./build_all.sh -p rp2040 -s -f
```

Build **`omsk_grain`** for RP2350:
```bash
cd firmware/omsk_grain
./build_all.sh -p rp2350 -c -s
```

Build **`omsk_midi`**:
```bash
cd firmware/omsk_midi
./build_all.sh -p rp2350 -c -s
```

Build OLED display test utility:
```bash
cd firmware/omsk_oled_test
./build.sh
```
