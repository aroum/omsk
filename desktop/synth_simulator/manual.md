# PySide6 Analog Synth User Manual

## Overview

The synthesizer supports 8-voice polyphony, vector audio processing (NumPy), flexible signal routing, and deep parameter modulation.

## Operating Modes

The synthesizer has two main button grid display modes. Toggle between modes using the key combination **Z + X**.

### 1. PIANO Mode

The main mode for musical performance.

* **Grid**: Displays playable notes across 3 rows and octave controls on the bottom row.
* **Z Key**: Functions as **OCT-** (shift octave down).
* **Visual Status**:
  * Active played notes light up.
  * If **HOLD** mode is enabled, the **OCT-** key (bottom-left) turns pink.
  * Latched notes in HOLD mode are highlighted in pink.

### 2. Params Mode

The mode for module selection, signal routing, and synth parameter editing.

* **Grid**: Displays module names (`VCO1`, `VCF1`, `LFO1`, `SET`, etc.).
* **Z Key**: Functions as the **HOLD** toggle button.
* **Navigation**: Pressing a module button assigns its parameters to the 4 top knobs.

## Signal Path

The synthesizer utilizes flexible routing. By default, all sound sources bypass filters and route directly to the mixer (Direct Out).

1. **Sources**: `VCO1`, `VCO2`, `NOISE`.
2. **Filter Routing**:
   * Each source can be routed into **VCF1** or **VCF2**.
   * If a source is **not** routed into a filter, it bypasses filtering directly to the mixer section (Dry/Direct signal).
3. **Filters**: `VCF1` and `VCF2` process their incoming source signals.
4. **Mixer**: Sums the outputs from filters and direct dry sources.
5. **FX**: The master sum passes through the Delay effect module.

## Modules and Parameters

### VCO1 / VCO2 (Oscillators)
* **Transpose**: Coarse tuning (+/- 5 octaves).
* **Detune**: Fine tuning (+/- 100 cents).
* **Wave**: Continuous waveform morphing (Sine -> Triangle -> Saw -> Square -> Pulse).
* **Shape**: Waveform shape modification (PWM for Pulse, Saturation for Sine, Morph for Saw).

### Noise
* **Color**: Modifies noise frequency spectrum (pink > white > blue).

### VCF1 / VCF2 (Filters)
* **Cutoff**: Cutoff frequency (50 Hz - 8000 Hz, exponential scale).
* **Res**: Filter resonance.
* **Drive**: Input overdrive & saturation.
* **Mix**: Filter Dry/Wet balance.

### LFO1 / LFO2 (Low Frequency Oscillators)
* **Rate**: LFO frequency (0 - 20 Hz).
* **Wave**: LFO waveform shape.
* **Shape**: Waveform skew / duty cycle modification.
* **Smooth**: LFO output smoothing filter.

### EG1 / EG2 (Envelope Generators)
Classic ADSR envelopes.
* **Attack**: Attack time.
* **Decay**: Decay time.
* **Sustain**: Sustain level.
* **Release**: Release time.

### MIXER
* **VCO1/2 Bal**: Crossfade balance between **VCO1** and **VCO2**.
* **Phase 1-2**: Phase offset between **VCO1** and **VCO2**.
* **VCO/Noise**: Mix balance between combined oscillators (**VCO1+VCO2**) and Noise.
* **Master Vol**: Master output volume.

### FX (Delay Effect)
The effect is applied on the Master bus.
* **Time**: Delay time between original signal and echoes.
* **Fdbk**: Feedback amount (repeats).
* **Mix**: Effect Dry/Wet blend.
* **Tone**: Timbre / filter tone of echoes (darker/brighter).

### ARP (Arpeggiator)
Generates note patterns based on held keys.
* **Rate**: Arpeggiator speed (0 - 5 Hz).
* **Mode**: Pattern mode (OFF, UP, DOWN, UP-DOWN, PING-PONG, RND).
* **Var**: Pattern variation (select first N notes from note buffer).
* **Oct**: Octave range (1 - 4 octaves).

### GLIDE (Portamento)
Sets smooth pitch glide transition between notes.
* **Poly**: Polyphony voice count (1 - 4).
* **Time**: Glide duration (0 - 1000 ms).
* **Slope**: Pitch slide response curve (Lin/Exp).
* **Mode**: Operating mode (Off, Legato, Always).
  * **Off**: Disabled.
  * **Legato**: Glide only when playing tied notes.
  * **Always**: Continuous pitch sliding between all notes.

### HOLD Mode
* Activated via the **HOLD** button.
* Holds played notes in memory.
* Allows performing over latched notes.
* **Toggle-Off**: Pressing a latched (pink) note key again releases it.

## SET and RM Matrix System

### SET Mode (Assign and Edit Modulation / Routing)
Used to establish modulation connections and signal routing.

1. **Assign Modulation**:
   * Select a modulator source (`LFO1/2` or `EG1/2`).
   * Press **SET** (turns pink).
   * Navigate to the target module (e.g., `VCO1`).
   * Click on the **parameter knob** (e.g., `Pitch`).
   * Modulation link is created (default depth 50%).
2. **Edit Depth**:
   * If a modulation connection already exists, press **SET**.
   * Parameter label turns **RED**, and depth indicator bar appears underneath.
   * Turn the knob to adjust modulation depth.
   * Press **SET** again to exit edit mode.
3. **Audio Routing**:
   * Select target filter (`VCF1` or `VCF2`).
   * Press **SET**.
   * Press source button (`VCO1`, `VCO2`, `NOISE`).
   * Source is now routed through this filter.

### RM Mode (Remove Modulation / Routing)
Used to unbind links.

1. **Remove Modulation**:
   * Press **RM** (turns red).
   * Select module and click parameter knob -> removes modulation link for this parameter.
2. **Reset Routing**:
   * Press **RM**.
   * Press source button (`VCO1`...) -> resets routing back to Direct Out.
3. **Clear All Modulator Routes**:
   * Press **RM**.
   * Press modulator button (`LFO1`...) -> clears all assigned targets for this modulator.

## Hotkeys

* 16 onscreen grid buttons map to keyboard keys `[1,2,3,4]`, `[q,w,e,r]`, `[a,s,d,f]`, `[z,x,c,v]`.
* **SET + RM**: Save current configuration to `config.json`.
