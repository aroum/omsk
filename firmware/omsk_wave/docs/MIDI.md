# Complete MIDI CC Documentation for Synth Firmware (`omsk_synth`)

This file contains a comprehensive description of all MIDI Control Change (MIDI CC) messages for the synthesizer firmware. The device allows full control over synthesis parameters, built-in sequencer, Hold function, and emulating physical button presses/encoder rotations.

---

## 1. Synthesizer and Effects Parameters

All parameters related to sound and modulation are controlled by the following CC codes:

| CC      | Firmware Parameter        | Default Value | Value Range | Description                                                               |
| ------- | ------------------------- | ------------- | ----------- | ------------------------------------------------------------------------- |
| **7**   | `PARAM_AMP_GAIN`          | 100 (100%)    | `0-127`     | **Master Volume** of the synthesizer.                                     |
| **9**   | `PARAM_VCF1_DRIVE`        | 0             | `0-127`     | **VCF1 Drive** (saturation/overdrive level for VCF1 filter).              |
| **10**  | `PARAM_PAN`               | 64 (Center)   | `0-127`     | **Stereo Pan** (0 — left only, 127 — right only).                         |
| **14**  | `PARAM_LFO1_RATE`         | 10 (~1 Hz)    | `0-127`     | **LFO1 Rate** (modulation speed).                                         |
| **20**  | `PARAM_VCO1_TRANSPOSE`    | 5 (0 oct.)    | `0-10`      | **VCO1 Transpose** coarse octaves (5 = 0, range +/- 5 octaves).           |
| **21**  | `PARAM_VCO1_DETUNE`       | 64 (0 c)      | `0-127`     | **VCO1 Detune** fine (+/- 100 cents).                                     |
| **22**  | `PARAM_VCO1_SHAPE`        | 0             | `0-127`     | **VCO1 Shape** (smooth morph / pulse width symmetry).                     |
| **23**  | `PARAM_VCO2_TRANSPOSE`    | 5 (0 oct.)    | `0-10`      | **VCO2 Transpose** coarse octaves (5 = 0, range +/- 5 octaves).           |
| **24**  | `PARAM_VCO2_DETUNE`       | 64 (0)        | `0-127`     | **VCO2 Detune** fine (+/- 100 cents).                                     |
| **25**  | `PARAM_VCO2_WAVE`         | 0             | `0-127`     | **VCO2 Base Waveform** (selection from wavetable bank).                   |
| **26**  | `PARAM_VCO2_SHAPE`        | 0             | `0-127`     | **VCO2 Shape** (morph / PWM for VCO2).                                    |
| **27**  | `PARAM_VCF2_MIX`          | 127 (100%)    | `0-127`     | **VCF2 Mix** (level of filtered signal from 2nd filter in mix).           |
| **28**  | `PARAM_LFO1_WAVE`         | 0             | `0-127`     | **LFO1 Waveform** (0=sine, triangle, saw, reverse saw, random/S&H).       |
| **29**  | `PARAM_LFO1_SHAPE`        | 0             | `0-127`     | **LFO1 Shape** (distortion or phase shift).                               |
| **30**  | `PARAM_LFO2_RATE`         | 0             | `0-127`     | **LFO2 Rate** (speed of 2nd LFO).                                         |
| **31**  | `PARAM_LFO2_SMOOTH`       | 0             | `0-127`     | **LFO2 Smooth** (removes sharp edges for smoother modulation).            |
| **56**  | `PARAM_VCF2_DRIVE`        | 0             | `0-127`     | **VCF2 Drive** (saturation level for VCF2).                               |
| **57**  | `PARAM_VCF1_MIX`          | 127 (100%)    | `0-127`     | **VCF1 Mix** (level of 1st filter in mix).                                |
| **58**  | `PARAM_FX1_TIME`          | 64            | `0-127`     | **Chorus (FX1) Time** (delay line time).                                  |
| **59**  | `PARAM_FX1_FEEDBACK`      | 64            | `0-127`     | **Chorus (FX1) Feedback**.                                                |
| **60**  | `PARAM_FX1_SPREAD`        | 64            | `0-127`     | **Chorus (FX1) Depth**.                                                   |
| **61**  | `PARAM_FX1_MIX`           | 0             | `0-127`     | **Chorus (FX1) Wet/Dry Mix**.                                             |
| **62**  | `PARAM_MOD_ROUTING1`      | 0             | `0-127`     | **Modulation Slot 1 Routing**.                                            |
| **63**  | `PARAM_MOD_DEPTH1`        | 0             | `0-127`     | **Modulation Slot 1 Depth**.                                              |
| **65**  | `PARAM_MOD_ROUTING2`      | 0             | `0-127`     | **Modulation Slot 2 Routing**.                                            |
| **67**  | `PARAM_MOD_DEPTH2`        | 0             | `0-127`     | **Modulation Slot 2 Depth**.                                              |
| **68**  | `PARAM_SUB_OSC`           | 0             | `0-127`     | **Sub-Oscillator Volume**.                                                |
| **69**  | `PARAM_LFO_OSC_DST`       | 0             | `0-127`     | **LFO to Oscillator Destination**.                                        |
| **70**  | `PARAM_VCO1_WAVE`         | 0             | `0-127`     | **VCO1 Base Waveform** (selection from wavetable bank).                   |
| **71**  | `PARAM_VCF1_RES`          | 64            | `0-127`     | **VCF1 Resonance** (Q-factor of 1st filter channel).                      |
| **72**  | `PARAM_EG1_RELEASE`       | 0             | `0-127`     | **EG1 Release Time** (volume envelope release time).                      |
| **73**  | `PARAM_EG1_ATTACK`        | 0             | `0-127`     | **EG1 Attack Time** (volume envelope attack time).                        |
| **74**  | `PARAM_VCF1_CUTOFF`       | 112           | `0-127`     | **VCF1 Cutoff Frequency** (first low-pass or band-pass filter).           |
| **75**  | `PARAM_VCF_KEY_TRACK`     | 0             | `0-127`     | **Filter Key Tracking** — filter opens further on higher notes.           |
| **76**  | `PARAM_VCF_EG_AMT`        | 0             | `0-127`     | **Filter Envelope Amount** (EG Amount to cutoff).                         |
| **77**  | `PARAM_VCF2_CUTOFF`       | 127           | `0-127`     | **VCF2 Cutoff Frequency**.                                                |
| **78**  | `PARAM_VCF2_RES`          | 64            | `0-127`     | **VCF2 Resonance**.                                                       |
| **79**  | `PARAM_EG_OSC_DST`        | 0             | `0-127`     | **Envelope to Oscillator Destination**.                                   |
| **80**  | `PARAM_NOISE_COLOR`       | 64            | `0-127`     | **Noise Color** (pink, white, blue).                                      |
| **81**  | `PARAM_ARP_RATE`          | 12            | `0-17`      | **Arpeggiator Rate** (synced divisions or free rate).                     |
| **82**  | `PARAM_ARP_MODE`          | 0 (Off)       | `0-5`       | **Arpeggiator Mode** (0=Off, 1=Up, 2=Down, 3=Up-Down, 4=Random, 5=Drunk). |
| **83**  | `PARAM_ARP_SWING`         | 0             | `0-127`     | **Arpeggiator Swing**.                                                    |
| **84**  | `PARAM_EG_AMP_MOD`        | 0             | `0-127`     | **Volume EG Modulation** (EG Depth on VCA).                               |
| **85**  | `PARAM_EG2_ATTACK`        | 0             | `0-127`     | **EG2 Attack Time** (filter/oscillator modulation envelope).              |
| **86**  | `PARAM_EG2_DECAY`         | 0             | `0-127`     | **EG2 Decay Time**.                                                       |
| **87**  | `PARAM_EG2_SUSTAIN`       | 127           | `0-127`     | **EG2 Sustain Level**.                                                    |
| **88**  | `PARAM_MIX_PHASE2`        | 0             | `0-127`     | **VCO2 Phase Offset** relative to VCO1.                                   |
| **89**  | `PARAM_EG2_RELEASE`       | 0             | `0-127`     | **EG2 Release Time**.                                                     |
| **90**  | `PARAM_MIX_VCO2_VOL`      | 127           | `0-127`     | **VCO2 Mixer Volume**.                                                    |
| **91**  | `PARAM_FX2_MIX`           | 0             | `0-127`     | **Delay/Reverb (FX2) Wet/Dry Mix**.                                       |
| **92**  | `PARAM_FX2_TIME`          | 64            | `0-127`     | **Delay Time**.                                                           |
| **93**  | `PARAM_FX2_FEEDBACK`      | 64            | `0-127`     | **Delay Feedback**.                                                       |
| **94**  | `PARAM_FX2_TONE`          | 127           | `0-127`     | **Delay Tail Tone** (high/low pass filtering in feedback loop).           |
| **95**  | `PARAM_EG_VEL_SENS`       | 0             | `0-127`     | **Velocity Sensitivity to EG**.                                           |
| **96**  | `PARAM_AMP_VEL_SENS`      | 0             | `0-127`     | **Velocity Sensitivity to Volume**.                                       |
| **97**  | `PARAM_EG1_ATTACK_CURVE`  | 0             | `0-127`     | **EG1 Attack Curve**.                                                     |
| **98**  | `PARAM_EG1_DECAY_CURVE`   | 0             | `0-127`     | **EG1 Decay Curve**.                                                      |
| **99**  | `PARAM_EG1_RELEASE_CURVE` | 0             | `0-127`     | **EG1 Release Curve**.                                                    |
| **100** | `PARAM_EG2_ATTACK_CURVE`  | 0             | `0-127`     | **EG2 Attack Curve**.                                                     |
| **101** | `PARAM_CHORD_MODE`        | 0 (Off)       | `0-32`      | **Chord Mode** (chord expansion).                                         |
| **102** | `PARAM_ARP_OCT`           | 3 (0 oct.)    | `0-6`       | **Arpeggiator Octave Range**.                                             |
| **103** | `PARAM_GLIDE_POLY`        | 0             | `0-127`     | **Unison Glide**.                                                         |
| **104** | `PARAM_EG2_DECAY_CURVE`   | 0             | `0-127`     | **EG2 Decay Curve**.                                                      |
| **105** | `PARAM_GLIDE_MODE`        | 0             | `0-127`     | **Glide Mode**.                                                           |
| **106** | `PARAM_MIX_VCO1_VOL`      | 127           | `0-127`     | **VCO1 Mixer Volume**.                                                    |
| **107** | `PARAM_VCF_MODE`          | 0             | `0-127`     | **Filter Mode** (LPF / BPF / HPF).                                        |
| **108** | `PARAM_EG2_RELEASE_CURVE` | 0             | `0-127`     | **EG2 Release Curve**.                                                    |
| **109** | `PARAM_VOICE_MODE`        | 0             | `0-127`     | **Voice Allocation** (polyphonic / monophonic).                           |
| **118** | `PARAM_PAN`               | 64            | `0-127`     | Duplicate CC for stereo pan (same as CC 10).                              |
| **4**   | `PARAM_ADV_SYNC_MODE`     | 8 (1/4)       | `0-17`      | **Sequencer Sync Division** (beat grid division from 8/1 to 1/64t).       |
| **6**   | `PARAM_ADV_SCALE_KEY`     | 0 (C)         | `0-11`      | **Root Key for Scale Quantizer** (0=C, 1=C#, ..., 11=B).                  |

---

## 2. Hold, Preset, and Sequencer Control

Dedicated CC codes for direct playback control, preset/sequence load & save, and step editing:

| CC      | Function                     | Value Range | Description                                                                                  |
| ------- | ---------------------------- | ----------- | -------------------------------------------------------------------------------------------- |
| **32**  | **Preset Load**              | `0-15`      | **Load Preset** from specified slot (0-15).                                                  |
| **33**  | **Preset Save**              | `0-15`      | **Save Current Preset** to specified slot (0-15).                                            |
| **34**  | **Step Enable**              | `0-63`      | **Enable Sequencer Step** at index (0-63). Activates note on step.                           |
| **35**  | **Step Disable**             | `0-63`      | **Disable Sequencer Step** at index (0-63). Deactivates note on step.                        |
| **36**  | **Step Stop Set**            | `0-63`      | **Set Stop Flag** on step index (0-63). Sequencer resets to step 0 when reaching this point. |
| **37**  | **Step Stop Clear**          | `0-63`      | **Clear Stop Flag** on step index (0-63).                                                    |
| **38**  | **Sequence Save**            | `0-15`      | **Save Sequencer Pattern** to slot (0-15).                                                   |
| **39**  | **Sequence Load**            | `0-15`      | **Load Sequencer Pattern** from slot (0-15).                                                 |
| **66**  | **HOLD Mode**                | `0-127`     | Note hold mode (Sostenuto/Latch). Values `>= 64` turn mode on, `< 64` turn it off.           |
| **114** | **Sequencer Start/Stop**     | `0-127`     | Start/Stop step sequencer. `>= 64` starts playback (`seq_start`), `< 64` stops (`seq_stop`). |
| **115** | **Sequencer Tempo (BPM)**    | `0-127`     | Sets sequencer tempo. Scales `0-127` to **30 to 300 BPM**.                                   |
| **116** | **Sequencer Division/Speed** | `0-127`     | Step duration division (SeqSpeed: `0-6` for 1/16 to 4x).                                     |
| **117** | **Retrigger Mode**           | `0-127`     | Reset envelope phases on each step. `>= 64` enables retrigger, `< 64` disables it.           |
| **119** | **Sequencer Playback Mode**  | `0-127`     | Step direction (SeqMode: `0-5` for Forward, Backward, Ping-pong, Snake, Random, Drunk).      |

---

## 3. Interface Emulation (Buttons and Encoders)

These CC codes allow full emulation of physical controls on the instrument panel:

### Matrix Buttons Emulation (16 buttons)

Transmitting a value `>= 64` simulates a Press, while `< 64` simulates a Release.

- **CC 40**: Matrix Button 1 (Row 1, Col 1)
- **CC 41**: Matrix Button 2 (Row 1, Col 2)
- **CC 42**: Matrix Button 3 (Row 1, Col 3)
- **CC 43**: Matrix Button 4 (Row 1, Col 4)
- **CC 44**: Matrix Button 5 (Row 2, Col 1)
- **CC 45**: Matrix Button 6 (Row 2, Col 2)
- **CC 46**: Matrix Button 7 (Row 2, Col 3)
- **CC 47**: Matrix Button 8 (Row 2, Col 4)
- **CC 48**: Matrix Button 9 (Row 3, Col 1)
- **CC 49**: Matrix Button 10 (Row 3, Col 2)
- **CC 50**: Matrix Button 11 (Row 3, Col 3)
- **CC 51**: Matrix Button 12 (Row 3, Col 4)
- **CC 52**: Matrix Button 13 (Row 4, Col 1 / OCT- button)
- **CC 53**: Matrix Button 14 (Row 4, Col 2 / OCT+ button)
- **CC 54**: Matrix Button 15 (Row 4, Col 3 / ARP button)
- **CC 55**: Matrix Button 16 (Row 4, Col 4 / ADV button)

### Encoder Emulation (4 Knobs)

Encoder rotation is encoded as relative CC value changes.

- **CC 110**: Encoder 1 Rotation
- **CC 111**: Encoder 2 Rotation
- **CC 112**: Encoder 3 Rotation
- **CC 113**: Encoder 4 Rotation

---

## 4. Standard Performance Messages

- **Pitch Bend**: Pitch bend wheel. Range adjusted via CC 120 (`PARAM_PITCH_BEND_RANGE`).
- **CC 1**: Modulation Wheel (Modwheel). Controls modulation intensity assigned in matrix.
- **CC 2**: Breath Controller. Used as an independent modulation source.
- **Aftertouch (Channel Pressure)**: Key pressure. Used as a modulation source.
- **CC 64**: Sustain Pedal. Holds volume envelope (VCA) open for played notes.
- **CC 120 / 123**: All Notes Off / Panic. Immediately silences all active voices.
