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

## Prerequisites & Dependencies

### System Dependencies

To compile firmware for RP2040 / RP2350, you need the ARM GCC toolchain along with the C standard library (`newlib`), CMake, and build tools.

#### macOS (Homebrew)

> [!IMPORTANT]
> Make sure to install `arm-none-eabi-newlib`. Without it, GCC will fail with `stdint.h: No such file or directory` error when compiling C standard headers.

```bash
brew install cmake ninja arm-none-eabi-binutils arm-none-eabi-gcc arm-none-eabi-newlib
```

#### Ubuntu / Debian

```bash
sudo apt update
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential
```

### Submodules & Pico SDK

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

## Memory & Binary Formats (`.uf2` vs `.bin` / `.elf`)

To synthesize high-quality audio, this project relies heavily on pre-calculated Look-Up Tables (LUTs), audio samples, and wavetables stored directly in Flash memory. As synth engines evolved (e.g. granular synthesis with PCM sample data), Flash memory space on the RP2350 Zero (4 MB limit, non-expandable due to compact QFN packaging and pin count constraints) became scarce.

Standard `.uf2` files add significant block header overhead (nearly doubling the file size compared to raw binaries). By switching from drag-and-drop `.uf2` flashing to flashing raw `.bin` or `.elf` binaries directly via [`picotool`](https://github.com/raspberrypi/picotool), **~1.4 MB of Flash space is freed up**, allowing almost **twice as many audio samples, LUTs, and wavetables** to fit into internal storage.

---

## Flashing & Tools

### Installing `picotool`

`picotool` allows flashing `.bin` and `.elf` binaries directly over USB without requiring drag-and-drop or external HW debuggers.

#### macOS

```bash
brew install picotool
```

#### Ubuntu / Debian

```bash
sudo apt install picotool
```

---

## Building & Flashing

Each firmware folder contains its own dedicated build script (`build_all.sh`) supporting clean builds, memory analysis, platform selection (`rp2040` / `rp2350`), and automated flashing via `picotool`.

### Option 1: Flashing via `build_all.sh` (Recommended)

Run the script with the `-f` (or `--flash`) flag. The script will automatically trigger a soft reboot to BOOTSEL mode via USB CDC (1200 baud) and upload the `.bin` binary:

```bash
cd firmware/omsk_wave
./build_all.sh -p rp2350 -c -s -f
```

#### Script Flags & Options:

- `-c`, `--clean`: Remove build directory and re-run CMake configuration from scratch.
- `-s`, `--size`: Print detailed Flash and RAM memory usage report after build.
- `-p <platform>`: Target MCU platform (`rp2040` or `rp2350`, default is `rp2350`).
- `-f`, `--flash`: Flash binary directly to connected device using `picotool`.

### Option 2: Flashing Manually via `picotool`

1. Connect your RP2040 / RP2350 board in **BOOTSEL mode** (hold the `BOOT` button while plugging in USB, or press `RESET` while holding `BOOT`).
2. Flash the binary file (`.bin` or `.elf`) and automatically execute/reboot:

```bash
# Flash .bin file and execute
picotool load build_rp2350/omsk_wave.bin -u

# Or flash .elf file directly
picotool load build_rp2350/omsk_wave.elf -u
```

---

### Build & Flash Examples

Build **`omsk_wave`** for RP2350 and flash to hardware:

```bash
cd firmware/omsk_wave
./build_all.sh -p rp2350 -c -s -f
```

Build **`omsk_fm`** for RP2040 and flash:

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
