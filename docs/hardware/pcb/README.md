# OMSK PCB

## BOM

### Basic

| name                                 |  pcs | type          | package | position          | description                      |
| ------------------------------------ | ---: | ------------- | ------- | ----------------- | -------------------------------- |
| 1N4148W                              |   17 | Diode         | SOD-323 | D1 - D17          | keyboard matrix + MIDI           |
| 100nF x7r 50v                        |    4 | Capacitor     | 0805    | C1, C3, C4, C5    | MIDI jacks + PWM Audio           |
| 47uF x7r 50v                         |    1 | Capacitor     | 1210    | C2                | PWM audio                        |
| 10R   for 3v3  or 220 Ohm for 5v out |    1 | Resistor      | 0603    | R4                | MIDI jacks                       |
| 33R for 3v3  or 220 Ohm for 5v out   |    1 | Resistor      | 0603    | R3                | MIDI jacks                       |
| 100R                                 |    1 | Resistor      | 0603    | R6                | PWM audio                        |
| 220R                                 |    2 | Resistor      | 0603    | R1, R5            | PWM audio + MIDI jacks           |
| 470R                                 |    1 | Resistor      | 0603    | R2                | MIDI jacks                       |
| 1.8k                                 |    1 | Resistor      | 0603    | R7                | PWM audio                        |
| PJ-313 (3 color)                     |    3 | jack sockets  | THD     | J2, J3, J4        | Audio + MIDI jacks               |
| Alps EC12 8.5 mm                     |    4 | Encoder       | THD     | ROTENC1 - ROTENC4 | Encoder for more info view below |
| H11L1 SMD                            |    1 | Optocoupler   | SOP-6   | U1                | MIDI jacks                       |
| Kailh choc sockets                   |   16 | Switch socket | SMD     | SW1 - SW16        | keyboard                         |
| Kailh choc v2                        |   16 | Switch        | THD     | SW1 - SW16        | keyboard                         |
| NC7WZ16P6X                           |    1 | IC            | SC-70-6 | U2                | MIDI jacks + PWM audio           |
| rp2040/rp2350 zero                   |    1 | MCU           | Module  | MCU1              | MCU (rp2350 recomended)          |
| WS2812B MINI-E                       |   22 | RGB LED       | 3228SMD | LED1 - LED22      | WS2812B MINI-E                   |

#### Encoders

You can choose any one of the following encoder models:

| name         | torque                 | # of detent/pulses | description |
| ------------ | ---------------------- | -----------------: | ----------- |
| EC12E1220301 | Standard 3 to 20mN･m   |                 12 |             |
| EC12E1240301 | Lightest (jog) 3±2mN･m |                 12 |             |
| EC12E2420301 | Standard 3 to 20mN･m   |                 24 | recommended |
| EC12E2440301 | Lightest (jog) 3±2mN･m |                 24 | recommended |

### Extra

| name                            |  pcs | type    | package | description                                                    |
| ------------------------------- | ---: | ------- | ------- | -------------------------------------------------------------- |
| MIDI cable (DIN5 M to jack 3.5) |    1 | Cable   | Cable   | TRS to DIN5 MIDI cable (Type A) for use external MIDI hardware |
| YS-SK6812MINI-E                 |   22 | RGB LED | 3228SMD | RGB LEDs                                                       |

#### OLED

You can choose any one of the following display models:

| name                             | pcs | type    | package | description |
| -------------------------------- | --- | ------- | ------- | ----------- |
| TZT 1.3" OLED 64×128 i2c SSD1312 | 1   | Display | Module  |             |
| TZT 1.3" OLED 64×128 i2c SH1107  | 1   | Display | Module  | recommended |

##### Pins

| Module pin | MCU pin |
| ---------- | ------- |
| **GND**    | GND     |
| **VCC**    | 3.3V    |
| **SDA**    | GPIO20  |
| **SCL**    | GPIO21  |

#### Headphone amp

Formally, you should not connect headphones directly to the DAC or PWM output without an amplifier; however, in practice, it works fine directly, making an amplifier highly optional. If you decide to use one, you can choose any of the following headphone amplifier models:

| IC                | mW    | size  | price | description |
| ----------------- | ----- | ----- | ----- | ----------- |
| TPA6132           | 25    | S     | 90    |             |
| TDA1308           | 40-80 | S     | 71    |             |
| MAX4410           | 80    | M     | 157   |             |
| MAX97220 +++      | 125   | 20x24 | 225   | tested      |
| OPA1622           | 145   | L     | 113   |             |
| LM4881            | 150   | M     | 60    |             |
| TPA6112 & SGM4812 | 150   | M     | 183   |             |

#### DAC

GY-PCM5102 I2S PCM5102A module

##### Pins

| Module pin | MCU pin |
| ---------- | ------- |
| **GND**    | GND     |
| **VCC**    | 5V      |
| **BCK**    | GPIO17  |
| **LRCK**   | GPIO18  |
| **DIN**    | GPIO19  |

## Build guide

- Solder all components starting from the lowest profile to the highest in the following order:
  - Diodes
  - Hotswap sockets
  - RGB LEDs
  - For NC7WZ16P6X (U2): the Pin 1 indicator on the IC consists of two dots on the bottom side (where the leftmost dot marks Pin 1), which aligns with the silkscreen tick mark on the PCB footprint.
  - Resistors
  - Capacitors
  - Jacks (recommended colors: green for MIDI IN, yellow for MIDI OUT, black for Audio OUT)
  - H11L1 (U1): the Pin 1 indicator on the IC is a dot on the bottom side, which aligns with the silkscreen tick mark on the PCB footprint.
  - Encoders
  - Solder two jumpers on the board to the `LINE` position (if you plan to use PWM audio).
  - Solder the `5V/3V3` jumper to the required position. This jumper controls the supply voltage of the NC7WZ16P6X (U2) IC. This affects the voltage of the MIDI OUT jack (for 5V, you must change resistors R3-R4 to 220 Ohm) and the PWM audio output level (5V increases the volume significantly, but might introduce noise from the USB power line).
- Connect external modules if needed:
  - **DAC**
    - Solder the jumpers on the module: 1L, 2L, 3H, 4L
    - Desolder or cut off the audio jack from the module, as it takes too much space and won't fit in the case.
    - Connect using wires:
      - GND on the module to GND on the PCB
      - LOUT on the module to JACK L OUT on the PCB
      - ROUT on the module to JACK R OUT on the PCB
      - VIN to 5V
      - LRCK to GPIO18
      - DIN to GPIO19
      - BCK to GPIO17
      - Solder two jumpers on the board to the `AMP` position
  - **OLED**
    - GND to GND
    - VCC to 3.3V
    - SDA to GPIO20
    - SCL to GPIO21
  - **Headphone amp**
    - GND
    - VCC to 3.3V
    - L- to GND
    - R- to GND
    - L+ to the audio source PWM/DAC LEFT CHANNEL
    - R+ to the audio source PWM/DAC RIGHT CHANNEL
    - LOUT to the audio jack OUT LEFT CHANNEL
    - ROUT to the audio jack OUT RIGHT CHANNEL
    - Solder two jumpers on the board to the `AMP` position
- Assemble the device into the case
- Flash the required firmware
