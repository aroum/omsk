# Granular Synthesizer

This project is a granular synthesizer with a 128x64 OLED display interface.

Samples are embedded into the firmware. They are pre-converted to mono and downsampled. Configurable parameters should be available in the config (bit depth, sampling rate).

## Buttons Layout

| row/col | col1           | col2          | col3          | col4          |
| ------- | -------------- | ------------- | ------------- | ------------- |
| row1    | Grain 1 [V]    | Grain 2 [V]   | Grain 3  [V]  | Filter[V]     |
| row2    | Jitter  [-]    | LFO 1  [V]    | LFO 2 [V]     | EG [V]        |
| row3    | Modulation [G] | FX [G]        | Mix [G]       | Sys[G]        |
| row4    | Trigger 1 [-]  | Trigger 2 [-] | Trigger 3 [-] | Trigger 4 [-] |

- [V] - Per Voice
- [G] - Global
- [-] - None

## Knob Modulation

- 'JIT' - Random jitter
- 'LFO1' - LFO 1
- 'LFO2' - LFO 2
- 'EG+' / 'EG-' - Envelope Generator (positive/inverted)
- '---' - None
- 'XXX' - [no modulation]

## Screen layout

| page_name                    | -                            | -                            | VOICES                       |
| ---------------------------- | ---------------------------- | ---------------------------- | ---------------------------- |
| param_name_1                 | param_name_2                 | param_name_3                 | param_name_4                 |
| param_indicator_1            | param_indicator_2            | param_indicator_3            | param_indicator_4            |
| param_value_1                | param_value_2                | param_value_3                | param_value_4                |
| param_modulation_indicator_1 | param_modulation_indicator_2 | param_modulation_indicator_3 | param_modulation_indicator_4 |
| param_modulation_name_1      | param_modulation_name_2      | param_modulation_name_3      | param_modulation_name_4      |

## Page Details

### Grain 1 (Per Voice)

| #          | enc 1                     | enc 2                        | enc 3           | enc 4                         |
| ---------- | ------------------------- | ---------------------------- | --------------- | ----------------------------- |
| Short Name | SAMPLE                    | POSIT                        | SIZE            | DENS                          |
| Full Name  | Sample selection          | Position                     | Size            | Density                       |
| Desc       | Select sample from memory | Sample start point           | Grain duration  | Frequency of grain Birth Rate |
| Range      | 1:1: # of samples         | 0:0.001:sample length in sec | 0.001:0.001:1.0 | 0.1:0.1:100                   |
| Unit       |                           | sec                          | sec             | Hz                            |

### Grain 2 (Per Voice)

| #          | enc 1                | enc 2                      | enc 3           | enc 4                      |
| ---------- | -------------------- | -------------------------- | --------------- | -------------------------- |
| Short Name | PITCH                | P.MODE                     | GRAINS          | VOL_G                      |
| Full Name  | Grain Pitch          | Pitch Mode                 | Max Grain Count | Grain Volume               |
| Desc       | Grain playback speed |                            |                 | Individual grain amplitude |
| Range      | free: 0.1:0.1:4.0    | Free / Semitones / Octaves | 1:1:32          | 0:1:100                    |
|            | Sem -24:1:+24        |                            |                 |                            |
|            | Oct -3:1:+3          |                            |                 |                            |
| Unit       | X/Semitones/oct      | ---                        | pcs             | %                          |

### Grain 3 (Per Voice)

- **enc 3:** Stereo Spread — controls the pan width of grains.
  - 0% (Mono): All grains are routed to center (L: 100%, R: 100%).
  - 1–100%: Random panning of grains. Higher values widen the stereo field (up to hard separation: one grain fully in left channel, next in right).

| #          | enc 1             | enc 2                           | enc 3         | enc 4                                       |
| ---------- | ----------------- | ------------------------------- | ------------- | ------------------------------------------- |
| Short Name | SCAN              | REV                             | SPREAD        | SHAPE                                       |
| Full Name  | Scan Speed        | Grain reverse direction         | Stereo Spread | Grain Shape                                 |
| Desc       | Playhead movement | Probability of reverse playback |               | Envelope selection from LUT: `grain_env.py` |
| Range      | -2.0:0.01:+2.0    | 0:1:100                         | 0:1:100       | 1:1:# of env                                |
| Unit       | x                 | %                               | %             | ---                                         |

### Filter (Per Voice)

| #          | enc 1            | enc 2      | enc 3              | enc 4                                               |
| ---------- | ---------------- | ---------- | ------------------ | --------------------------------------------------- |
| Short Name | CUT              | RES        | TYPE               | KTRK                                                |
| Full Name  | Cutoff           | Resonance  | Filter Type        | Keytrack                                            |
| Desc       | Cutoff frequency | Q-factor   |                    | Scaling based on MIDI note. Not working for trig1-4 |
| Range      | 20Hz:log:16kHz   | 0.5:0.1:13 | Off / LP / HP / BP | 0:1:100                                             |
| Unit       | HZ/KHZ           | ---        | ---                | %                                                   |
 999HZ should become 1.00KHZ

### EG (Per Voice)

Amplitude Envelope

See `eg.py`
For CURV use lut

| #          | enc 1        | enc 2                                                       | enc 3         | enc 4         |
| ---------- | ------------ | ----------------------------------------------------------- | ------------- | ------------- |
| Short Name | ATK          | A.CURV                                                      | REL           | R.CURV        |
| Full Name  | Attack Time  | Attack Curve                                                | Release Curve | Release Time  |
| Desc       |              | < 0 — convex (logarithmic); > 0 — concave (exponential)    |               |               |
| Range      | 0.0:0.01:5.0 | -1.0:0.1:+1.0                                               | 0.0:0.01:5.0  | -1.0:0.1:+1.0 |
| Unit       | sec          | ---                                                         | sec           | ---           |

### Sys (Global)

| #          | enc 1                   | enc 2                                       | enc 3                   | enc 4         |
| ---------- | ----------------------- | ------------------------------------------- | ----------------------- | ------------- |
| Short Name | YSCALE                  | V_MODE                                      | CH                      | VOL_M         |
| Full Name  | Y-Scale [no modulation] | Voice choice for MIDI notes [no modulation] | MIDI CH [no modulation] | Master Volume |
| Desc       | Visualization Y-scale   |                                             |                         |               |
| Range      | Pitch / Pan             | V1 / V2 / V3 / V4 / RR / RND / OCT          | 1:1:16                  | 0:1:100       |
| Unit       | ---                     | ---                                         | ch                      | %             |

[SYS] + [TRIG1-4] > HOLD Voice 1-4

#### Voice choice

- V1/2/3/4 MONOPHONIC
- Round robin
- Random
- Octave based (oct1 notes for voice1, oct2 for voice2...)

#### Impact of MIDI Notes on Pitch

Played MIDI notes affect the pitch (playback speed) of generated grains according to the **Keytrack** parameter (`KEYTRK`, `PARAM_KEYTRACK`) of the corresponding voice:

- **Base Note**: Reference note is C4 (MIDI note 60). When playing C4, playback pitch remains original ($1.0x$).
- **Pitch Tracking**: When playing other notes, pitch multiplier for new grains is calculated exponentially:
  $$\text{pitch\_multiplier} = 2^{\text{keytrack} \cdot (N - 60) / 12}$$
  where $N$ is the MIDI note number, and $\text{keytrack}$ is the `KEYTRK` parameter from $0.0$ to $1.0$ ($0\%$ to $100\%$, default is $100\%$).
- **Integration with Voice Allocation Modes (`V_MODE`)**:
  - In **V1–V4** modes, MIDI notes transpose only the corresponding monophonic voice.
  - In **RR (Round Robin)** and **RND (Random)** modes, each MIDI note is dynamically bound to its voice, allowing full polyphonic chords. Each voice individually tracks the pitch of its assigned note.
  - In **OCT** mode, voices track notes in their respective octaves (Voice 1 tracks 1st octave, Voice 2 tracks 2nd, etc.).
- **Stabilizing Release Phase and Physical Buttons**:
  - When releasing a MIDI key, the voice enters Release phase (amplitude envelope decay), and `current_note` resets. To avoid a sudden pitch jump back to reference C4 during release, new grains continue to generate using the pitch of the **last note played on this voice** (`last_note`).
  - Front panel physical trigger buttons `TRIG 1–4` trigger the voice using the pitch of the **last played MIDI note** on it (or C4 if no MIDI notes have been played yet).

### FX (Global)

(See 'fx.py')

| #          | enc 1    | enc 2        | enc 3     | enc 4   |
| ---------- | -------- | ------------ | --------- | ------- |
| Short Name | FOLD     | DNSMPL       | BCRSH     | MIX     |
| Full Name  | Wavefold | Downsampling | Bit Crush | Mix     |
| Desc       |          |              |           | dry/wet |
| Range      | 0:1:100  | x1:1:x80     | 1:1:16    | 0:1:100 |
| Unit       | %        | x            | bit       | %       |

### Mix (Global)

| #          | enc 1          | enc 2          | enc 3          | enc 4          |
| ---------- | -------------- | -------------- | -------------- | -------------- |
| Short Name | VOL_1          | VOL_2          | VOL_3          | VOL_4          |
| Full Name  | Voice 1 Volume | Voice 2 Volume | Voice 3 Volume | Voice 4 Volume |
| Desc       |                |                |                |                |
| Range      | 0:1:100        | 0:1:100        | 0:1:100        | 0:1:100        |
| Unit       | %              | %              | %              | %              |

### Mod (Global)

| #          | enc 1                      | enc 2       | enc 3                      | enc 4       |
| ---------- | -------------------------- | ----------- | -------------------------- | ----------- |
| Short Name | M1.S>D                     | M1.DPH      | M2.S>D                     | M2.DPH      |
| Full Name  | Mod 1 Source > Destination | Mod 1 Depth | Mod 2 Source > Destination | Mod 2 Depth |
| Desc       |                            |             |                            |             |
| Range      |                            | 0:1:100     | Available FM/RM Routings   | 0:1:100     |
| Unit       | ---                        | %           | ---                        | %           |

#### Available FM/RM Routings

- FM1>2
- FM1>3
- FM1>4
- FM2>1
- FM2>3
- FM2>4
- FM3>1
- FM3>2
- FM3>4
- FM4>1
- FM4>2
- FM4>3
- RM1x2
- RM1x3
- RM1x4
- RM2x3
- RM2x4
- RM3x4

### LFO 1 & 2  (Global)

| #          | enc 1      | enc 2                | enc 3       | enc 4                |
| ---------- | ---------- | -------------------- | ----------- | -------------------- |
| Short Name | RATE       | WFM                  | PHASE       | SYNC                 |
| Full Name  | Rate       | Waveform             | Start Phase | Sync Mode            |
| Desc       | LFO freq   | LFO waveform type    | Start phase | Start LFO with ...   |
| Range      | 0.1:0.1:40 | Sin/Tri/Saw/RSAW/S&H | 0:1:180     | V1/V2/V3/V4/ANY/FREE |
| Unit       | Hz         | ---                  | deg         | ---                  |

### Jitter  (Global)

| #          | enc 1 | enc 2 | enc 3 | enc 4 |
| ---------- | ----- | ----- | ----- | ----- |
| Short Name | ---   | ---   | ---   | ---   |
| Full Name  | ---   | ---   | ---   | ---   |
| Desc       | ---   | ---   | ---   | ---   |
| Range      | ---   | ---   | ---   | ---   |
| Unit       | ---   | ---   | ---   | ---   |

- Nothing, only for chord

## RGB LEDs

The synthesizer is equipped with 22 WS2812 RGB LEDs providing visual feedback on parameters, selected menu pages, button activity, and voices.

### Brightness Setting

The maximum brightness limit for LEDs is set in the configuration file `hw_config.h` using the `RGB_MAX_BRIGHTNESS` macro (value range `0` to `255`, default is set to `63` for comfortable glare-free lighting):

```c
#define RGB_MAX_BRIGHTNESS 63
```

### LED Assignments

#### 1. Encoder LEDs (1, 2, 5, 6)

Display current parameter value assigned to the corresponding physical encoder (1, 2, 3, and 4).

- **Scheme**: Uses a gradient from **Blue** (min value) to **Red** (max value) through **Purple** (cold-to-hot).
- LEDs show values only for active parameters on the currently selected page. If no parameter is assigned to the encoder, the LED turns off.

#### 2. Page Mode LEDs (3, 4)

Display currently active page. Color matches the corresponding button color (see below).

#### 3. Page Selection Button LEDs (7-18)

Correspond to menu buttons from `BTN_GRAIN1` to `BTN_SYS`.

- **Page Colors**:
  - **Grain 1-3** — green, dark green, light green (7,8,9)
  - **Filter** — blue (10)
  - **Jitter** — yellow (11)
  - **LFO 1-2** — pink (12,13)
  - **EG** — orange (14)
  - **Modulation** — cyan (18)
  - **FX** — light blue (17)
  - **Mix** — purple (16)
  - **Sys** — white (15)
- **Behavior**: In idle mode, buttons glow dimly (15% of max brightness). When pressed, the corresponding LED lights up to full brightness.

#### 4. Voice LEDs (19-22)

Show status and activity of the four synthesizer voices (Voice 1–4):

- **LED 19 (Voice 1)** — red
- **LED 20 (Voice 2)** — green
- **LED 21 (Voice 3)** — blue
- **LED 22 (Voice 4)** — magenta
- **Behavior**: In idle mode, glow dimly. Upon voice activation (`TRIG 1-4` button press or incoming MIDI note), light up brightly.

## MIDI Interfaces and Settings

The synthesizer supports full control and bidirectional MIDI message exchange over both USB and hardware connectors (MIDI Jack).

### 1. USB MIDI

The device is recognized by computers as a standard Class-Compliant USB MIDI controller. No drivers required. USB MIDI operates in duplex mode: receives notes and CC commands to control synthesis, and transmits information about physical controls.

### 2. Hardware MIDI Jacks (TRS / DIN)

Physical MIDI IN and MIDI OUT connectors are wired to hardware UART0 on the microcontroller:

- **MIDI RX (IN)**: GPIO 1 (`PIN_MIDI_JACK_IN` in `hw_config.h`)
- **MIDI TX (OUT)**: GPIO 0 (`PIN_MIDI_JACK_OUT` in `hw_config.h`)
- **Baud rate**: standard MIDI — 31250 bps.

### 3. MIDI THRU Functionality

In `hw_config.h`, you can enable the `#define MIDI_THRU` macro. This option turns the synthesizer into a router/hub:

- All incoming bytes from physical MIDI IN are instantly echoed to physical MIDI OUT (hardware echo).
- Incoming messages from physical MIDI IN are duplicated to USB MIDI Out.
- Incoming messages from USB MIDI are forwarded to physical MIDI OUT.

### 4. Voice Allocation

You can change how incoming MIDI notes are allocated across the 4 synth voices. Configured in `SYS` -> `MIDI_MODE` menu (encoder 3):

- **V1 / V2 / V3 / V4** (values 0–3): Monophonic mode. Notes are routed strictly to the selected voice (1, 2, 3, or 4 respectively).
- **RR (Round Robin)** (value 4): Cyclic allocation. Each new note activates the next voice (1 -> 2 -> 3 -> 4 -> 1...).
- **RND (Random)** (value 5): Random allocation to currently free voices.
- **OCT (Octave)** (value 6): Voice selection depends on note octave (1st octave — Voice 1, 2nd octave — Voice 2, etc.).

The `MIDI_CH` parameter on the `SYS` page (encoder 4) sets active MIDI channel from `1` to `16` (or `0` for Omni mode).

### 5. MIDI CC Map

You can emulate button presses and encoder rotations via MIDI Control Change (CC) messages. Default mapping in `hw_config.h` is as follows:

#### Encoders (Relative Mode)

Encoders send and receive relative values (values $\le 63$ decrement parameter, $\ge 65$ increment parameter):

- **Encoder 1**: CC 110 (`MIDI_CC_ENC1`)
- **Encoder 2**: CC 111 (`MIDI_CC_ENC2`)
- **Encoder 3**: CC 112 (`MIDI_CC_ENC3`)
- **Encoder 4**: CC 113 (`MIDI_CC_ENC4`)

#### Matrix Buttons (16 buttons)

Buttons respond to CC values ($\ge 64$ treated as Press, $< 64$ as Release):

- **Row 1 (Grain 1, 2, 3, Filter)**: CC 40 – 43
- **Row 2 (Jitter, LFO 1, 2, EG)**: CC 44 – 47
- **Row 3 (Modulation, FX, Mix, Sys)**: CC 48 – 51
- **Row 4 (Triggers 1, 2, 3, 4)**: CC 52 – 55
