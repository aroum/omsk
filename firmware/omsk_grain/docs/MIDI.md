# MIDI Documentation for Granular Firmware (`omsk_grain`)

This file contains a comprehensive description of all MIDI interfaces, settings, voice allocation methods, and the MIDI Control Change (CC) message map for the granular synthesizer.

---

## 1. MIDI Interfaces and Connections

The synthesizer supports full control and bidirectional MIDI message exchange via both USB and hardware connectors.

### USB MIDI

The device is recognized by computers as a standard Class-Compliant USB MIDI controller. No drivers are required. USB MIDI operates in duplex mode: accepting notes and CC commands for synthesis control, and transmitting information about physical control interactions.

### Hardware MIDI Jacks (TRS / DIN)

Physical MIDI IN and MIDI OUT connectors are connected to the microcontroller's hardware UART0 controller:

- **MIDI RX (IN)**: GPIO 1 (`PIN_MIDI_JACK_IN` in [hw_config.h](../../shared/hw_config.h))
- **MIDI TX (OUT)**: GPIO 0 (`PIN_MIDI_JACK_OUT` in [hw_config.h](../../shared/hw_config.h))
- **Baud Rate**: standard MIDI speed — 31250 bps.

### MIDI THRU Functionality

In [hw_config.h](../../shared/hw_config.h), you can activate the `#define MIDI_THRU` macro. This option turns the synthesizer into a hub/router:

- All incoming bytes from physical MIDI IN are instantly echoed to physical MIDI OUT (hardware echo).
- Incoming messages from physical MIDI IN are duplicated to USB MIDI Out.
- Incoming messages from USB MIDI are forwarded to physical MIDI OUT.

---

## 2. Settings and Voice Allocation

You can change how incoming MIDI notes are allocated across the synthesizer's 4 voices. This parameter is configured in the `SYS` -> `V_MODE` menu (controlled by encoder 2):

- **V1 / V2 / V3 / V4** (values 0–3): Monophonic mode. Notes are routed strictly to the selected voice (1, 2, 3, or 4 respectively).
- **RR (Round Robin)** (value 4): Cyclic allocation. Each new note activates the next voice in order (1 -> 2 -> 3 -> 4 -> 1...).
- **RND (Random)** (value 5): Random distribution of notes across currently free voices.
- **OCT (Octave)** (value 6): Voice is selected based on the octave of the played note (1st octave — Voice 1, 2nd octave — Voice 2, etc.).

The `CH` parameter on the `SYS` page (encoder 3) sets the active MIDI channel from `1` to `16` (or `0` for Omni mode — receiving messages on all channels).

---

## 3. MIDI CC Map

### Granular Synthesis & FX Parameters

All sound parameters are controlled via the following CC codes (values `0-127` are automatically scaled to the physical range of each parameter):

| CC      | Firmware Parameter  | Default Value        | Engine Range          | Description                                                                             |
| ------- | ------------------- | --------------------- | --------------------- | --------------------------------------------------------------------------------------- |
| **7**   | `PARAM_MASTER_VOL`  | 1.0 (100%)            | `0.0 ... 1.0`         | **Master Volume** of the granular synth.                                                |
| **14**  | `PARAM_LFO_RATE`    | 1.0 Hz                | `0.1 ... 40.0 Hz`     | **LFO Modulation Rate** (speed of cyclic parameter modulation).                         |
| **20**  | `PARAM_POS`         | 0.5 (50%)             | `0.0 ... 1.0`         | **Sample Read Position** — point in the sample file where grains are fetched from.      |
| **21**  | `PARAM_SIZE`        | 0.1 s                 | `0.001 ... 1.0 s`     | **Grain Size** — duration of an individual grain.                                       |
| **22**  | `PARAM_DENS`        | 20.0 Hz               | `0.1 ... 100.0 Hz`    | **Grain Density** — rate of new grain births (per second).                              |
| **23**  | `PARAM_PITCH`       | 1.0 (1x)              | Depends on mode       | **Grain Pitch / Playback Speed** (in semitones, octaves, or multiplier).                |
| **24**  | `PARAM_SAMPLE_IDX`  | 0                     | `0 ... NUM_SAMPLES-1` | **Sample Select** — index of playback audio file from memory.                           |
| **25**  | `PARAM_MAX_GRAINS`  | 32                    | `1 ... 32`            | **Grain Limit** — max number of active simultaneous grains (voices).                    |
| **26**  | `PARAM_GRAIN_AMP`   | 0.8                   | `0.0 ... 1.0`         | **Grain Amplitude** — individual base volume of each grain.                             |
| **27**  | `PARAM_KEYTRACK`    | 1.0 (100%)            | `0.0 ... 1.0`         | **Key Tracking** — degree to which grain pitch follows pressed MIDI notes.               |
| **28**  | `PARAM_LFO_WAVE`    | 0                     | `0 ... 4`             | **LFO Waveform** (0=sine, 1=triangle, 2=saw, 3=reverse saw, 4=random S&H).              |
| **29**  | `PARAM_PITCH_MODE`  | 0                     | `0 ... 2`             | **Pitch Mode** (0=SPEED continuous, 1=SEMI semitones, 2=OCT octaves).                   |
| **30**  | `PARAM_SCAN`        | 0.0                   | `-2.0 ... 2.0`        | **Auto-Scan** — playhead movement speed across the sample.                              |
| **31**  | `PARAM_DIRECTION`   | 0.0 (0%)              | `0.0 ... 1.0`         | **Direction / Reverse** — probability of grains playing in reverse.                      |
| **15**  | `PARAM_SPREAD`      | 0.0 (0%)              | `0.0 ... 1.0`         | **Stereo Spread** (Pan Spread) — random panning distribution of grains.                 |
| **16**  | `PARAM_SHAPE`       | 0                     | `0 ... 27`            | **Grain Envelope Shape** — envelope morphing from smooth to sharp.                      |
| **71**  | `PARAM_RES`         | 0.7                   | `0.5 ... 13.0`        | **Filter Resonance** — Q-factor of built-in filter (peak at cutoff).                    |
| **72**  | `PARAM_REL`         | 0.3 s                 | `0.001 ... 5.0 s`     | **Voice Release** — main volume envelope decay time after key release.                  |
| **73**  | `PARAM_ATK`         | 0.01 s                | `0.0 ... 5.0 s`       | **Voice Attack** — main volume envelope attack time upon key press.                     |
| **74**  | `PARAM_CUTOFF`      | 20000.0 Hz            | `20.0 ... 20000.0 Hz` | **Filter Cutoff Frequency** — cutoff frequency of built-in voice filter.                |
| **75**  | `PARAM_FILT_KEY`    | 0.0 (0%)              | `0.0 ... 1.0`         | **Filter Key Tracking** — degree to which cutoff varies with key pitch.                 |
| **85**  | `PARAM_ATK_CURVE`   | 0.0                   | `-1.0 ... 1.0`        | **Attack Curve** — attack shape (logarithmic, linear, exponential).                     |
| **86**  | `PARAM_REL_CURVE`   | 0.0                   | `-1.0 ... 1.0`        | **Release Curve** — release shape (logarithmic, linear, exponential).                   |
| **87**  | `PARAM_LFO_PHASE`   | 0                     | `0 ... 180`           | **LFO Initial Phase** in degrees.                                                       |
| **89**  | `PARAM_LFO_SYNC`    | 0                     | `0 ... 5`             | **LFO Sync** (per-voice or free running).                                               |
| **90**  | `PARAM_VOL`         | 0.7                   | `0.0 ... 1.0`         | **Voice Volume** (individual volume of active timbre).                                  |
| **91**  | `PARAM_FX_MIX`      | 0.0 (0%)              | `0.0 ... 1.0`         | **Effect Mix** (Dry/Wet mix for internal delay/reverb).                                 |
| **92**  | `PARAM_MOD1_SRC`    | 0                     | `0 ... 24`            | **Modulation Source 1** (modulation slot choice).                                       |
| **93**  | `PARAM_MOD1_AMT`    | 0.0 (0%)              | `0.0 ... 1.0`         | **Modulation Depth 1** (modulating signal amplitude).                                   |
| **94**  | `PARAM_MOD2_SRC`    | 0                     | `0 ... 24`            | **Modulation Source 2**.                                                                |
| **95**  | `PARAM_MOD2_AMT`    | 0.0 (0%)              | `0.0 ... 1.0`         | **Modulation Depth 2**.                                                                 |
| **96**  | `PARAM_FX_WF`       | 0.0                   | `0.0 ... 1.0`         | **Effect Waveform** (type/character of effect modulation).                              |
| **97**  | `PARAM_FX_DS`       | 1.0                   | `1.0 ... 80.0`        | **Downsampling** — sample rate reduction (Lo-Fi effect).                                |
| **98**  | `PARAM_FX_BC`       | 16.0                  | `1.0 ... 16.0`        | **Bitcrusher** — bit depth reduction (8-bit/Lo-Fi effect).                              |
| **107** | `PARAM_FILT_TYPE`   | 3 (Off)               | `0 ... 3`             | **Filter Type** (0=Low-Pass, 1=High-Pass, 2=Band-Pass, 3=Off).                         |
| **109** | `PARAM_MIDI_MODE`   | 0                     | `0 ... 6`             | **Note Allocation Mode** across voices (polyphonic, Round Robin, etc.).                 |
| **118** | `PARAM_MIDI_CH`     | 1                     | `1 ... 16`            | **MIDI Channel** — channel number on which notes/CC are received.                       |

### Interface Emulation (Buttons and Encoders)

Transmitting a value `>= 64` simulates a button Press, while `< 64` simulates a Release.

#### Matrix Buttons (16 buttons)

- **CC 40**: Matrix Button 1 (Row 1, Col 1 / Page select button 1)
- **CC 41**: Matrix Button 2 (Row 1, Col 2 / Page select button 2)
- **CC 42**: Matrix Button 3 (Row 1, Col 3 / Page select button 3)
- **CC 43**: Matrix Button 4 (Row 1, Col 4 / Page select button 4)
- **CC 44**: Matrix Button 5 (Row 2, Col 1)
- **CC 45**: Matrix Button 6 (Row 2, Col 2)
- **CC 46**: Matrix Button 7 (Row 2, Col 3)
- **CC 47**: Matrix Button 8 (Row 2, Col 4)
- **CC 48**: Matrix Button 9 (Row 3, Col 1)
- **CC 49**: Matrix Button 10 (Row 3, Col 2)
- **CC 50**: Matrix Button 11 (Row 3, Col 3)
- **CC 51**: Matrix Button 12 (Row 3, Col 4)
- **CC 52**: Matrix Button 13 (Row 4, Col 1 / Trigger 1)
- **CC 53**: Matrix Button 14 (Row 4, Col 2 / Trigger 2)
- **CC 54**: Matrix Button 15 (Row 4, Col 3 / Trigger 3)
- **CC 55**: Matrix Button 16 (Row 4, Col 4 / Trigger 4)

#### Encoder Emulation (4 Knobs / Relative Mode)

Encoders send and receive relative values (values $\le 63$ decrement the parameter, $\ge 65$ increment the parameter):

- **CC 110**: Encoder 1 Rotation
- **CC 111**: Encoder 2 Rotation
- **CC 112**: Encoder 3 Rotation
- **CC 113**: Encoder 4 Rotation

### Preset Management

The following CC codes are used for loading and saving presets:

| CC     | Function    | Value Range | Description                           |
| ------ | ----------- | ----------- | ------------------------------------- |
| **32** | Preset Load | `0-15`      | Load preset from slot                 |
| **33** | Preset Save | `0-15`      | Save current preset to slot           |

### Standard Performance Messages

- **Pitch Bend**: Pitch wheel. Bends overall pitch of active voices.
- **CC 1**: Modulation Wheel (Modwheel). Controls depth of assigned modulation signal.
- **Aftertouch**: Key pressure. Sends pressure data for modulation.
- **CC 64**: Sustain Pedal. Holds envelopes open, preventing grain decay until pedal release.
