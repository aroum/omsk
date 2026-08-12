# OMSK Desktop Simulators & Tools

This directory contains desktop Python applications (simulators, emulators, and development utilities) for the OMSK synthesizer platform. All tools share a unified virtual environment managed by [`uv`](https://github.com/astral-sh/uv).

---

## Quick Start with `uv`

### 1. Install `uv` (if not already installed)

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

### 2. Synchronize Dependencies

Run from the `desktop/` directory to create/update the virtual environment:

```bash
uv sync
```

### 3. Run Applications

Launch any simulator or tool using `uv run`:

```bash
# OMSK Analog Synth Simulator
uv run synth_simulator/main.py

# OMSK Granular Synth Simulator
uv run grain_simulator/main.py

# OLED Hardware Emulator
uv run synth_tools/emulator/oled_emulator.py
```

---

## Directory Structure & Subfolders

```
desktop/
├── grain_simulator/    # PySide6 simulation of the Granular synth engine
├── synth_simulator/    # PySide6 simulation of the Analog/Wavetable synth engine
└── synth_tools/        # Utility scripts, LUT generators, apps, and hardware emulators
```

### 1. `grain_simulator/`

Desktop simulation of the **OMSK Granular Synthesizer** engine (`omsk_grain`).

- **`main.py`**: Interactive PySide6 GUI for grain parameter manipulation, real-time audio playback, and MIDI control.
- **`main_piano.py`**: Granular synth simulator variant with an interactive piano roll / visual keyboard playback.
- **`audio_engine.py`**: Core python audio engine implementation for real-time grain generation, envelope synthesis, and audio output via `sounddevice`.
- **`grain_env.py`**, **`eg.py`**, **`fx.py`**: DSP modules for grain windowing, envelope generators, and spatial/delay effects.
- **`widgets.py`**, **`shared_funcs.py`**: Custom PySide6 UI widgets (knobs, wave displays) and helper functions.
- **`granular_config.json`**, **`granular_pro_config.json`**: Preset configurations for the granular engine.

### 2. `synth_simulator/`

Desktop simulation of the **OMSK Analog / Wavetable Synthesizer** engine (`omsk_synth`).

- **`main.py`**: Full PySide6 desktop interface reproducing physical panel knobs, sliders, and modulation controls.
- **`audio_engine.py`**: Real-time multi-voice polyphonic synthesizer engine supporting oscillators, resonant filters, LFOs, and envelopes.
- **`ui_components.py`**, **`synth_data.py`**: UI component wrappers, layout structures, and patch data definitions.
- **`config.json`**: Initial synthesizer patch and audio driver configuration.
- **`manual.md`**: User guide and signal flow documentation for the synth simulator.

### 3. `synth_tools/`

Development utilities, look-up table (LUT) exporters for microcontroller firmware, interactive Web/Gradio apps, and display/hardware emulators.

- **`apps/`**: Interactive web applications (using Gradio) for testing and visualizing synthesizer sub-modules:
  - `wave_app.py`: Oscillators and waveform morphing visualizer.
  - `filter_app.py`: Resonant filter frequency response calculator.
  - `env_app.py`: ADSR envelope shape generator.
  - `noise_app.py`: Digital noise & pseudo-random generator tester.
  - `amy_wave_app.py`: AMY synth engine integration test bench.
- **`emulator/`**: Software emulators for hardware components:
  - `oled_emulator.py`: 128x64 pixel OLED display emulator (SH1107 / SSD1312) using PySide6.
  - `pads.py`: 4x4 matrix button controller emulator with LED feedback rendering.
- **`generators/`**: DSP audio generators and synth engine build helpers (`supersaw.py`, `build_amy_clean.py`).
- **`luts/`**: C/C++ Look-Up Table export scripts for target microcontrollers:
  - `export_omsk_lut.py`: Generates firmware LUT headers for math/pitch functions.
  - `export_wave_lut.py`: Generates wavetable array headers for Pico SDK firmware.
  - `export_noise_lut.py`: Exports pre-calculated noise tables.
