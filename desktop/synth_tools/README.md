# OMSK Synth Tools

This directory contains utility tools, hardware emulators, and table generators for the OMSK synthesizer family.

## Directory Structure

- **`apps/`**: Interactive Gradio applications for DSP visualization and parameter tuning:
  - `amy_wave_app.py` — Interactive interface for modeling AMY engine waveforms.
  - `env_app.py` — Visualizer for ADSR envelope parameters.
  - `filter_app.py` — Interactive VCF filter simulator.
  - `noise_app.py` — Generator and spectrum visualizer for noise sources.
  - `wave_app.py` — VCO waveform designer and simulator.
- **`emulator/`**: Hardware emulators for OMSK:
  - `oled_emulator.py` — Graphical OLED display emulator (SSD1306/SSD1312/SH1107) over serial connection (Serial / USB CDC).
  - `pads.py` and `pad_config.json` — Keyboard and 4x4 matrix pad simulator.
  - `run.sh` — Quick start script for the screen emulator.
- **`generators/`**: Code generation and test signal scripts:
  - `build_amy_clean.py` — Script to clean and build the AMY library.
  - `supersaw.py` — Super Saw effect generation algorithm.
  - `main.py` — Stub entry point.
  - `scratch.py` — Scratchpad file for experiments.
- **`luts/`**: Look-Up Table (LUT) exporter scripts for firmware target:
  - `export_noise_lut.py` — Exports colored noise filtering tables.
  - `export_omsk_lut.py` — Common OMSK math and pitch lookup tables.
  - `export_wave_lut.py` — Exports oscillator waveform lookup tables.

## Running

All tools share the common virtual environment located in the `desktop` root directory.

To run any script, use `uv` from the `desktop/` directory:

```bash
# Example: launch the OLED display emulator
uv run synth_tools/emulator/oled_emulator.py
```
