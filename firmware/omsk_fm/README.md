# Omsk FM Synthesizer

Omsk FM is a 6-operator Frequency Modulation (FM) synthesizer firmware designed for RP2350/RP2040 microcontrollers. It offers a maximally close digital reproduction of the classic Yamaha DX7 engine. The folder structure, build system, and hardware UI framework are aligned with the `omsk_wave` synthesizer codebase.

### 🧬 Origin & Codebase
The **Omsk FM** project was written almost from scratch specifically for the Raspberry Pi Pico architecture (dual-core pipeline with oversampling).
* **Connection to Dexed**: Only the graph structure array (coordinate array) for rendering algorithm schemes on the OLED screen was borrowed from [Dexed](https://github.com/asb2m10/dexed). Omsk FM also supports the SysEx bank format produced by Dexed (for easy patch loading).
* **Connection to picoX7 / Ken Shirriff**: The codebase is *not* a fork of [picoX7](https://github.com/SloeComputers/picoX7). However, to achieve bit-accurate DX7 sound, Omsk FM uses **decoded YM21280 ROM tables** (sine, log, and envelope step tables) obtained by [Ken Shirriff](https://www.righto.com/) via chip reverse-engineering, adapted by John D. Haughton in the picoX7 project.

---

## 🎹 Synthesizer Engine Architecture

The core synthesis engine simulates the exact parameters, routing, and modulation structures of the Yamaha DX7.

### 1. Six Operators (OP1 - OP6)

Each operator consists of:

* **Oscillator**: Digital sine wave oscillator.
  * **Frequency Mode**: Ratio (0.50 to 31.99) or Fixed Frequency (1.000 Hz to 9772 Hz).
  * **Coarse/Fine Adjustments**: Coarse tuning (0 to 31) and fine tuning (0 to 99).
  * **Detune**: Detunes the operator frequency by -7 to +7 steps.
* **Envelope Generator (EG)**: 4 Rates ($R_1, R_2, R_3, R_4$) and 4 Levels ($L_1, L_2, L_3, L_4$).
* **Keyboard Level Scaling**: Modifies the output level based on the midi key played.
  * **Break Point (BPT)**: Center key (0 to 99, representing C-1 to G9; C3 is 39).
  * **Left/Right Depth**: Scaling depth (0 to 99) applied to the left and right of the break point.
  * **Left/Right Curves**: Negative Linear (`-LIN`), Negative Exponential (`-EXP`), Positive Exponential (`+EXP`), or Positive Linear (`+LIN`).
* **Keyboard Rate Scaling**: Scales envelope rates based on key pitch (0 to 7).
* **Sensitivity**:
  * **Key Velocity Sensitivity (KVS)**: Response of operator output level to note velocity (0 to 7).
  * **Amplitude Modulation Sensitivity (AMS)**: Sensitivity to LFO/EG bias modulation (0 to 3).
* **Output Level**: Base output level of the operator (0 to 99).

### 2. Algorithms & Feedback

* **32 Algorithms**: The 6 operators can be routed in 32 classic algorithms mapping modulators and carriers.
* **Feedback Loop**: A configurable feedback loop (0 to 7) applies self-modulation to a designated operator in each algorithm.
* **Oscillator Sync**: Configurable global key-sync for oscillators.

### 3. Pitch Envelope Generator (Pitch EG)

* Global envelope generator modulating the master pitch of all operators.
* Uses 4 Rates ($R_1, R_2, R_3, R_4$) and 4 Levels ($L_1, L_2, L_3, L_4$).

### 4. Low Frequency Oscillator (LFO)

* **Speed**: LFO rate (0 to 99).
* **Delay**: Fade-in delay (0 to 99).
* **Pitch Modulation Depth (PMD)**: Pitch modulation amount (0 to 99).
* **Amplitude Modulation Depth (AMD)**: Amplitude modulation amount (0 to 99).
* **LFO Sync**: Key-sync LFO phase reset (on/off).
* **Waveforms**: Triangle, Saw Down, Saw Up, Square, Sine, and Sample & Hold.

---

## Operating Modes

Switching between operating modes is done by **simultaneously pressing OCT↓ and OCT↑** (buttons 13 and 14 in Piano mode, or **MIXER** and **FX2** in PARAMS mode).

### Piano Mode (Play Mode)

This is the default mode when powering on the device.

By default, the following matrix note layout is active on the 4x4 matrix:

| enc1 | enc2 | enc3 | enc4 |
| ---- | ---- | ---- | ---- |
| C    | D#   | F#   | A    |
| C#   | E    | G    | A#   |
| D    | F    | G#   | B    |
| OCT↓ | OCT↑ | ARP  | ADV  |

> [!NOTE]
> You can customize and change the physical key layout using configuration parameters. For details, see [Key Layout Settings in config.md](docs/config.md#1-hardware-sharing-options).

* **Encoders 1-4**: Control parameters of the last selected module.
* **Keys 1-12 (Rows 1-3)**: Keyboard. Play chromatic scale notes (C, C#, D... B).
* **Button 13 (Bottom row, 1)**: OCT↓ (Octave Down).
* **Button 14 (Bottom row, 2)**: OCT↑ (Octave Up).
* **Button 15 (Bottom row, 3)**: ARP (Arpeggiator).
* **Button 16 (Bottom row, 4)**: ADV (Advanced).

* **OCT↓ + OCT↑** > Switch to PARAMS/Setup Mode
* **OCT↓ + ADV** > Cycle Key for scale snapping
* **OCT↑ + ARP** > Toggle HOLD
* **ARP + ADV** > Toggle SEQ Mode
* **LFO1 + EG1** (Pads 2+3 in Params mode) > PRESET Mode (Hold for Save, Tap for Load)

#### LED Feedback (Piano Mode)

LEDs under matrix buttons show keyboard layout and control state:

* **White keys** (C, D, E, F, G, A, B): Soft white low brightness. Bright white on key press or MIDI note.
* **Black keys** (C#, D#, F#, G#, A#): Dim blue/cyan low brightness. Bright blue/cyan on key press or MIDI note.
* **OCT↓ and OCT↑ buttons**: Purple. Color shifts with octave transpose (more blue down, more red up), bright purple on press.
* **ARP and ADV buttons**: Dim orange. Bright orange when corresponding layer/mode is active.

#### ARP Section

| Rate | Mode | Swing | Octave |
| ---- | ---- | ----- | ------ |
| C    | D#   | F#    | A      |
| C#   | E    | G     | A#     |
| D    | F    | G#    | B      |
| OCT↓ | OCT↑ | PIANO | ADV    |

##### Ranges

| Module | Min    | Max  | Unit | Mapping / Formula (0..127)      |
| ------ | ------ | ---- | ---- | ------------------------------- |
| Rate   | 1/64   | 1/1  | --   |                                 |
| Mode   | OFF    | DRNK | ---  | OFF, UP, DOWN, UP-DN, RND, DRNK |
| Swing  | OFF/50 | 75   | %    | SIN>TRI>SAW>RSAW>SQR>PWM>PAM4   |
| Octave | -3     | +3   | oct  |                                 |

#### ADV Section

In ADV mode (selected via ADV button), the interface shifts to global settings:

| Tempo | Scale | Chord | MIDI  |
| ----- | ----- | ----- | ----- |
| C     | D#    | F#    | A     |
| C#    | E     | G     | A#    |
| D     | F     | G#    | B     |
| OCT↓  | OCT↑  | Key   | PIANO |

* **Pads 1-12**: Set Scale Key (C, C#, D... B)
* **Pad 16 (ADV/PIANO)**: Return to Piano Mode

##### Tempo

* Off sync
* External sync
* 30
* 31
* …
* 300

###### Sync behaviour

When SYNC is enabled, time-based parameters are tied to musical beats and are specified as fractional parts of a beat (for example: 1/1 = whole note, 1/2 = half note, 1/4 = quarter note, 1/8 = eighth note, 1/8t = eighth-note triplet, etc.). The following knobs/parameters become beat-synced and accept sync-notation instead of absolute time:

* `Tempo` (global BPM reference)
* `ARP > Rate`
* `LFO1 > Rate` and `LFO2 > Rate` (LFO frequency is expressed as a fraction of a beat in SYNC mode)
* `GLIDE > Time`
* `FX1 (Delay) > Time`
* `EG1/EG2 > Attack`, `EG1/EG2 > Decay`, and `EG1/EG2 > Release` (envelope times are interpreted as beat fractions in SYNC mode). `Sustain` remains a level/percentage and is not beat-synced.

In SYNC mode the LFO frequency is also specified as a fraction of the beat (e.g. `1/4` = one cycle per quarter note). Triplet and dotted divisions from the `Sync MODE` list are supported.

####### Sync range

* 8/1
* 8/1t
* 4/1
* 4/1t
* 1/1
* 1/1t
* 1/2
* 1/2t
* 1/4
* 1/4t
* 1/8
* 1/8t
* 1/16
* 1/16t
* 1/32
* 1/32t
* 1/64
* 1/64t

##### Scales (MIDI Note Scale Quantizer)

* OFF / Chromatic / Thru Mode
* Major
* Minor
* Harmonic Minor
* Melodic Minor
* Dorian
* Locrian
* Lydian
* Blues
* Major Pentatonic
* Minor Pentatonic
* Augmented

##### Chords (Polyphonic Note Expansion)

When Chord mode is active, pressing a single note triggers additional notes to form a chord.

| **Category**   | **Full Name**         | **Abbr**       | **Intervals (semitones)** | **Description**               |
| -------------- | --------------------- | -------------- | ------------------------- | ----------------------------- |
| **Basic**      | OFF                   | **OFF**        | 0                         | No chord expansion            |
| **Intervals**  | Minor 2nd             | **m2**         | 0, 1                      | Minor second                  |
|                | Major 2nd             | **M2**         | 0, 2                      | Major second                  |
|                | Minor 3rd             | **m3**         | 0, 3                      | Minor third                   |
|                | Major 3rd             | **M3**         | 0, 4                      | Major third                   |
|                | Perfect 4th           | **P4**         | 0, 5                      | Perfect fourth                |
|                | Tritone               | **Tri**        | 0, 6                      | Tritone                       |
|                | Perfect 5th           | **P5**         | 0, 7                      | Perfect fifth                 |
|                | Minor 6th             | **m6**         | 0, 8                      | Minor sixth                   |
|                | Major 6th             | **M6**         | 0, 9                      | Major sixth                   |
|                | Minor 7th (Int)       | **m7i**        | 0, 10                     | Minor seventh (interval)      |
|                | Major 7th (Int)       | **M7i**        | 0, 11                     | Major seventh (interval)      |
|                | Octave                | **Oct**        | 0, 12                     | Octave                        |
| **Triads**     | Major                 | **M**          | 0, 4, 7                   | Major triad                   |
|                | Minor                 | **m**          | 0, 3, 7                   | Minor triad                   |
|                | Diminished            | **Dim**        | 0, 3, 6                   | Diminished triad              |
|                | Augmented             | **Aug**        | 0, 4, 8                   | Augmented triad               |
|                | Suspended 2           | **Sus2**       | 0, 2, 7                   | Suspended 2nd                 |
|                | Suspended 4           | **Sus4**       | 0, 5, 7                   | Suspended 4th                 |
| **7th Chords** | Major 7th             | **M7**         | 0, 4, 7, 11               | Major 7th chord               |
|                | Dominant 7th          | **Dom7**       | 0, 4, 7, 10               | Dominant 7th chord            |
|                | Minor 7th             | **m7**         | 0, 3, 7, 10               | Minor 7th chord               |
|                | Half-Diminished       | **m7b5**       | 0, 3, 6, 10               | Half-diminished 7th           |
|                | Diminished 7th        | **Dim7**       | 0, 3, 6, 9                | Diminished 7th                |
|                | Minor-Major 7th       | **mM7**        | 0, 3, 7, 11               | Minor-major 7th               |
|                | Augmented-M 7th       | **AugM7**      | 0, 4, 8, 11               | Augmented major 7th           |
|                | Augmented 7th         | **Aug7**       | 0, 4, 8, 10               | Augmented 7th                 |
| **Extended**   | Major 6th             | **M6**         | 0, 4, 7, 9                | Major 6th chord               |
|                | Minor 6th             | **m6**         | 0, 3, 7, 9                | Minor 6th chord               |
|                | Add 9                 | **Add9**       | 0, 4, 7, 14               | Major add 9                   |
|                | 7th Sus 4             | **7s4**        | 0, 5, 7, 10               | 7th suspended 4th             |
|                | M 7th Sus 4           | **M7s4**       | 0, 5, 7, 11               | Major 7th suspended 4th       |
| **Advanced**   | 7 Sharp 5             | **7#5**        | 0, 4, 8, 10               | Dominant 7 sharp 5            |
|                | 7 Flat 5              | **7b5**        | 0, 4, 6, 10               | Dominant 7 flat 5             |
|                | Quartal               | **Quart**      | 0, 5, 10, 15              | Quartal stack                 |
|                | Lydian                | **Lyd**        | 0, 4, 6, 7                | Lydian chord (#4)             |

### Param

The 4x4 button matrix defines parameter editing pages:

|          |          |                |             |
| :------- | :------- | :------------- | :---------- |
| Freq     | LVL&MOD  | LFO            | EG          |
| KBDscale | Filt [G] | Algo & FB [GS] | PitchEG [G] |
| OP1      | OP2      | OP3            | MEM         |
| OP4      | OP5      | OP6            | Sys         |

#### Page Logic and Parameter Navigation

1. **Operator Local Parameters (Local)**:
   * **Freq**, **LVL&MOD**, **LFO**, **EG**, **KBDscale** — apply to currently selected operator (OP1..OP6).
2. **Global Parameters (Global)**:
   * **Filt [G]** (Filter), **PitchEG [G]** (Pitch Envelope) — global settings across the patch.
   * **Algo & FB [GS]** (Algorithm and Feedback) — global algorithm routing setup.
3. **Enabling / Disabling Operators**:
   * Double tapping an operator button (**OP1..OP6**) toggles (mutes/unmutes) that operator in the active algorithm.
4. **Dual-Page Parameter Switching**:
   * Parameters containing 2 pages (marked `[2]`, e.g., EG, Keyboard Scaling, LFO, Pitch EG, Filt) switch pages on consecutive button presses.
   * Pressing a parameter button for the first time opens Page 1.
   * Example: Press **EG** -> Page 1 (Lvl) opens. Press **EG** again -> Page 2 (rate) opens. Press **LFO**, then **EG** again -> Page 1 (Lvl) opens.

## Memory

* Mode (Load/Save)
* Cartridge number (1-32)
* Slot number (1-32)
* Prog (1-32) — instant patch switching in RAM. Useful for navigating banks after MIDI SysEx load.

## System

* Transpose
* Monophonic Mode (On|off)
* MIDI CH 1-16
* BPM

## Filt

* Tune
* Cutoff
* Resonance
* Level

### 2/2

## Frequency

* Mode (Ratio / Fixed): Frequency mode (harmonic ratio or fixed frequency in Hz).
* Tune
* Coarse
* Fine

## Levels & Modulation

* Level: Base output level of operator.
* A Mod Sens (Amplitude Modulation Sensitivity): LFO modulation sensitivity on operator amplitude.
* Key Vel (Keyboard Velocity Sensitivity): Velocity response of operator output level.
* ---

## EG [2]

### 1/2 (Lvl)

* (1, 2, 3, 4): Envelope target level values.

### 2/2 (rate)

* (1, 2, 3, 4): Envelope rate/time values between levels.

## Keyboard Scaling [2]

### 1/2 (Left)

Parameters modifying operator behavior based on key pitch:

* L Depth
* L Curve
* Breakpoint: Key note where scaling begins.
* Rate Scaling: Envelope speed scaling based on pitch (higher notes decay faster).

### 2/2 (Right)

* R Depth: Level depth left and right of breakpoint.
* R Curve: Curve shape (linear, exponential, etc.) left and right of breakpoint.
* Breakpoint: Key note where scaling begins.
* Rate Scaling: Envelope speed scaling based on pitch.

## Algorithm & FB [G]

* Feedback: Feedback level (typically on OP6 or operator chain) for noise and harsh saw timbres.
* Algorithm: Choice of 32 operator routing schemes (defines carriers vs modulators).

## LFO [2]

### 1/2

* Wave: LFO waveform (triangle, saw down, saw up, square, sine, sample & hold).
* Speed: LFO rate/speed.
* Delay: Fade-in delay after key press.
* P Mod Sens (Pitch Modulation Sensitivity): Overall pitch modulation sensitivity.

### 2/2

* PMD (Pitch Modulation Depth): Pitch modulation depth (vibrato).
* AMD (Amplitude Modulation Depth): Amplitude modulation depth (tremolo).
* LFO Key Sync: Sync LFO phase to key press (On/Off).
* OSC Key Sync: Sync main oscillator phases on key press (On/Off).

## Pitch EG [2]

Controls overall pitch changes over time:

### 1/2

* Pitch EG Level (1, 2, 3, 4): Pitch offset target levels.

### 2/2

* Pitch EG Rate (1, 2, 3, 4): Rates/times between pitch levels.

## Seq

Plays specified notes in a loop. Up to 64 steps (16 steps * 4 pages). Each page has its RGB LED color (green, blue, purple, orange). Muted step glows at 5% brightness.

### Play steps

| Speed | Swing | Mode  | note length |
| ----- | ----- | ----- | ----------- |
| STP1  | STP2  | STP3  | STP4        |
| STP5  | STP6  | STP7  | STP8        |
| STP9  | STP10 | STP11 | STP12       |
| STP13 | STP14 | STP15 | STP16       |

#### UI

| Speed | Swing | Mode       | Len  |
| ----- | ----- | ---------- | ---- |
| page  | step  | play/pause | slot |
| 1/4   | 1/64  | play/pause | 1/16 |

##### Color Scheme & Indication Logic (v2)

###### 1. Step Status (16 Matrix Pads)

* **Empty Step:** Off (Black).
* **Active Step (Trig):** Green (if cycle condition 2/4/8 is currently met).
* **Standby Step:** Yellow (if cycle condition 2/4/8 is NOT currently met).
* **Mute:** Pink.
* **Stop Step (Sequence End):** Red.
* **Current Step (Playhead):** White (overlaid on top with max brightness).

###### 2. Probability

Brightness defines triggering probability (for Green, Yellow, Pink):

* **100%:** Max brightness (255).
* **75-85%:** High brightness (190-210).
* **40-50%:** Medium brightness (110-130).
* **10-25%:** Low brightness (40-70).
* **0%:** Step becomes visually black.

###### 3. Status LEDs (2 RGB LEDs)

####### LED 3: Transport Status (Play/Pause)

* **Play:** Solid Green.
* **Pause/Stop:** Red (or Orange).

####### LED 4: Navigation (Pages 1-4)

* **Page 1 (1-16):** Red.
* **Page 2 (17-32):** Green.
* **Page 3 (33-48):** Blue.
* **Page 4 (49-64):** Purple.

###### Layering Priority

Evaluate LED color conditions in this exact order:

1. **Cursor:** If `current_step == LED_index`, color = **WHITE (MAX)**.
2. **Stop:** If `step == stop_step`, color = **RED**.
3. **Mute:** If `is_muted`, color = **PINK** (brightness = Probability).
4. **Loop Condition:**
   * If cycle condition matches, color = **GREEN** (brightness = Probability).
   * If cycle condition does NOT match, color = **YELLOW** (brightness = Probability).
5. **Default:** If step inactive, color = **BLACK**.

#### Play Mode Descriptions

* **Forward** (UP): plays steps 1→2→3→...→16→1 sequentially
* **Backward** (DOWN): plays steps in reverse 16→15→14→...→1→16
* **Pingpong** (UP-DN): plays forward 1→16, then immediately backward 16→1
* **Snake** (SNK): plays in a serpentine pattern across 4x4 grid (1→2→3→4→8→7→6→5→9→10→11→12→16→15→14→13)
* **Random** (RND): selects a random step on each beat
* **Drunk**: random walk — moves to an adjacent step (±1) on each beat

STP"X" > Mute step — Disables playback for specified step

#### Chords

| Chord                | Action        | Notes                                                                                         |
| -------------------- | ------------- | --------------------------------------------------------------------------------------------- |
| STP"X" > + Speed enc | probability   |                                                                                               |
| STP3+STP4            | Load/Save seq | STP3+STP4 > 1-16 key > load 1-16 preset                                                       |
| STP5+STP9            | Prev page     |                                                                                               |
| STP8+STP12           | Next page     |                                                                                               |
| STP13+STP14          | Param         |                                                                                               |
| STP15+STP16          | PIANO mode    |                                                                                               |
| STP10+STP11          | Play/Pause    | Play/Pause sequence playback                                                                  |
| STP14+STP15          | Stop step     | Sets a stop flag on the beat. If flags are set on steps 5, 8 and 9, only steps 1-4 are played |
| STP1+STP2            | Edit step     | Enter edit mode for the specified step                                                        |
| STP13+STP16          | Copy mode     | STP1.Down>STP2.Down>STP2.Up>STP1.Down>source step>target step                                 |

#### Ranges

| Knob        | Min  | Max | Unit | Note                             |
| ----------- | ---- | --- | ---- | -------------------------------- |
| speed       | 1/16 | 8   | x    |                                  |
| swing       | off  | 75  | %    |                                  |
| mode        | 1    | 8   | -    |                                  |
| note length | 0    | 100 | %    | (0, 10, 25, 40, 50, 75, 85, 100) |

### Edit Mode

| VEL  | CHRD | Every | Prob |
| ---- | ---- | ----- | ---- |
| C    | D    | E     | F    |
| G    | A    | B     | C#   |
| D#   | F#   | G#    | A#   |
| OCT↓ | OCT↑ | Clear | Done |

#### Ranges

| Knob  | Min | Max | Unit | Note                                          |
| ----- | --- | --- | ---- | --------------------------------------------- |
| VEL   | 0   | 127 |      | velocity for the step                         |
| CHRD  |     |     |      |                                               |
| Every | 1   | 8   | -    | play every loop, every 2.. every 8            |
| Prob  | 0   | 100 | %    | Probability, (0, 10, 25, 40, 50, 75, 85, 100) |
