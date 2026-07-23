# Omsk FM MIDI Specification

Omsk FM supports standard MIDI input (note on/off, control change, program change, pitch bend) over USB MIDI (TinyUSB) and hardware MIDI Jack interfaces. It provides full compatibility with the Yamaha DX7 MIDI SysEx specification, allowing voice preset loading and parameter editing via patch editors such as **Dexed**.

---

## 📥 Yamaha DX7 SysEx Bulk Dump Formats

### 1. 32-Voice Cartridge Bulk Dump (4104 Bytes)

This format transfers a bank of 32 packed voices. It is sent by Dexed when loading a cart/bank.

* **Structure**:

    ```
    +-----------------+-----------------------------------+
    | Byte (Hex)      | Description                       |
    +-----------------+-----------------------------------+
    | F0              | System Exclusive Start            |
    | 43              | Yamaha Manufacturer ID            |
    | 0n              | Substatus / Midi Channel (0..15)  |
    | 09              | Format Number (32 Voices Bank)    |
    | 20              | Byte Count MSB (4096 bytes data)  |
    | 00              | Byte Count LSB                    |
    | [4096 bytes]    | 32 Packed Voices * 128 bytes each |
    | [1 byte]        | Checksum                          |
    | F7              | System Exclusive End              |
    +-----------------+-----------------------------------+
    ```

* **Checksum Calculation**:
    The checksum is a 7-bit value calculated by summing the 4096 data bytes, negating the sum, and masking to 7 bits:
    $$\text{Checksum} = (-\sum_{i=0}^{4095} \text{Data}[i]) \ \& \ \text{0x7F}$$

* **128-Byte Packed Voice Map**:
    Each of the 32 voices is packed into 128 bytes:
  * **Bytes 0–101**: 6 Operators parameters (17 bytes per operator, from OP6 down to OP1):
    * EG Rates $R_1, R_2, R_3, R_4$ (4 bytes)
    * EG Levels $L_1, L_2, L_3, L_4$ (4 bytes)
    * Keyboard Level Scaling breakpoint, left depth, right depth (3 bytes)
    * Scaling Left curve (2 bits), Right curve (2 bits)
    * Rate scaling (3 bits), Detune (4 bits)
    * AMS (2 bits), Velocity sensitivity (3 bits)
    * Output level (1 byte)
    * Oscillator Mode (1 bit), Coarse frequency (5 bits)
    * Fine frequency (1 byte)
  * **Bytes 102–109**: Global Pitch EG Rates $R_1..R_4$ & Levels $L_1..L_4$
  * **Byte 110**: Algorithm number (0..31)
  * **Byte 111**: Feedback (3 bits), Oscillator Sync (1 bit)
  * **Byte 112**: LFO Speed
  * **Byte 113**: LFO Delay
  * **Byte 114**: LFO Pitch Modulation Depth (PMD)
  * **Byte 115**: LFO Amplitude Modulation Depth (AMD)
  * **Byte 116**: LFO Sync (1 bit), LFO Waveform (3 bits), Pitch Modulation Sensitivity (3 bits)
  * **Byte 117**: Transpose
  * **Bytes 118–127**: Voice Name (10 ASCII characters)

---

### 2. Single Voice Bulk Dump (163 Bytes)

This format transfers parameter data for a single voice patch. It is sent by Dexed when selecting/previewing a patch.

* **Structure**:

    ```
    +-----------------+-----------------------------------+
    | Byte (Hex)      | Description                       |
    +-----------------+-----------------------------------+
    | F0              | System Exclusive Start            |
    | 43              | Yamaha Manufacturer ID            |
    | 0n              | Substatus / Midi Channel (0..15)  |
    | 00              | Format Number (1 Voice)           |
    | 01              | Byte Count MSB (155 bytes data)   |
    | 1B              | Byte Count LSB                    |
    | [155 bytes]     | 1 Packed Voice Data               |
    | [1 byte]        | Checksum                          |
    | F7              | System Exclusive End              |
    +-----------------+-----------------------------------+
    ```

* **Checksum Calculation**:
    $$\text{Checksum} = (-\sum_{i=0}^{154} \text{Data}[i]) \ \& \ \text{0x7F}$$

---

## 🎛️ Parameter Change SysEx

Real-time control changes sent by patch editors for immediate voicing updates.

* **Structure**:

    ```
    F0 43 1n 12 [Parameter Group/Number] [Value] F7
    ```

  * `1n`: Parameter Change Substatus (`10` to `1F` depending on MIDI channel)
  * `12`: Parameter Change Group ID (18 for Voice parameters)
  * `Parameter Group/Number`: Index corresponding to a specific DX7 parameter (0 to 154)
  * `Value`: Parameter value (typically 7-bit, 0 to 99 or 0 to 127)

---

## 🎛️ MIDI Control Change (CC) Mapping

For basic automation and MIDI keyboard controllers, standard parameter offsets are mapped to CC numbers:

* **CC 1**: Modulation Wheel (routes to LFO Pitch/Amp Modulation)
* **CC 2**: Breath Controller (routes to EG Bias/LFO Modulation)
* **CC 7**: Channel Volume (Modulates master output level)
* **CC 64**: Sustain Pedal (on/off envelope hold)
* **CC 74**: Filter Cutoff (Emulated via carrier operator levels)
* **CC 95**: Active Voice Algorithm Select

---

## 🎛️ MIDI Program Change (PC)

Standard MIDI Program Change messages (`0xC0`) are fully supported:

* Sending a Program Change message (0 to 31) will instantly switch the active patch to the corresponding program from the currently loaded bank in RAM (`g_presets`).
* This is especially useful for quickly navigating through a 32-voice cartridge right after loading it via a SysEx bulk dump.
