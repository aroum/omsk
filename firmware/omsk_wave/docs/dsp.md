# OMSK Synth — DSP Technical Specification

This document provides a mathematical description of the summing, mixing, and modulation algorithms used in the DSP core of OMSK Synth.

---

## 1. Mixer Summing

To prevent digital clipping while maintaining a stable volume level when playing multiple voices, a power conservation law normalization system (Square Root Compensation) is applied.

### Voice Mixer

Within each of the 4 voices, three sources (VCO1, VCO2, Noise) are mixed using a compensation table. For optimization, the LUT `lut_gain[128]` is used:
$$g[i] = \sqrt{\frac{i}{127}}$$

Single voice mix formula:
$$V_{voice} = \frac{g_1 \cdot VCO_1 + g_2 \cdot VCO_2 + g_3 \cdot Noise}{\sqrt{g_1^2 + g_2^2 + g_3^2} + \epsilon}$$
*Where $\epsilon = 10^{-9}$ is a small value to prevent division by zero.*

---

### Polyphonic Summing (4 Voices)

When playing chords (up to 4 voices simultaneously), each voice has its own **Velocity** level. To prevent digital overload (clipping), summing is performed with normalization based on the square root of the sum of powers.

#### Velocity Weighting
For each voice $n$, a weight $W_n$ is calculated based on key velocity:
$$W_n = \sqrt{\frac{Velocity_n}{127}}$$

#### Final Summing Algorithm
Let $S_n$ be the final signal of voice $n$ (after mixer, filter, and VCA envelope).
1. **Signal Summing:** Sum all signals multiplied by their individual Velocity weight:
   $$Sum = \sum_{n=1}^{4} (S_n \cdot W_n)$$

2. **Normalization Factor Calculation:** Calculate the square root of the sum of squared weights for all active voices:
   $$Norm = \sqrt{\sum_{n=1}^{4} W_n^2 + \epsilon}$$

3. **Final Output:**
   $$Out = \frac{Sum}{Norm}$$

#### Pseudocode Implementation Example
```c
// Calculate weights for each voice (typically on note press)
float w[4];
for(int n=0; n<4; n++) {
    w[n] = lut_gain[velocity[n]];
}

// In each audio block:
float voice_sum = 0;
float power_sum = 0;

for(int n=0; n<4; n++) {
    if (voice_active[n]) {
        voice_sum += voice_signal[n] * w[n];
        power_sum += w[n] * w[n]; // Weight squared
    }
}

// Square root normalization
float output = voice_sum / (sqrt(power_sum) + 1e-9f);
```

---

## 2. Mathematical Model for Slow Modulation (LFO, EG, ModWheel)

Any parameter (knob) supports assignment of one of the sources: LFO1, LFO2, EG1, EG2, ModWh. Modulation depth $D$ is specified in range $[-1.0; +1.0]$ ($-100\%$ to $+100\%$).

### Data Separation
- $K$: Base user-set value (UI). Not altered by modulation.
- $K_{mod}$: Final value for DSP, clamped to range $[K_{min}; K_{max}]$.

### Signal Preparation (Normalization)
All incoming signals $S_{raw}$ are converted to a unified unipolar representation $M \in [0; 1]$.

| Source    | Original Range   | Transformation to $M$   | Note                             |
| --------- | ---------------- | ----------------------- | -------------------------------- |
| **LFO**   | $[-1; +1]$        | $M = (S_{raw} + 1) / 2$ | Shift up and halve               |
| **EG**    | $[0; 1]$          | $M = S_{raw}$           | Already unipolar                 |
| **ModWh** | $[0; 127]$        | $M = S_{raw} / 127$     | Normalized to $[0; 1]$           |

### Offset Calculation ($\Delta K$)
The signal always moves either strictly "upwards" or "downwards" relative to $K$:
- $U = K_{max} - K$ (headroom up)
- $Down = K - K_{min}$ (headroom down)

**Offset Formula:**
- If $D \ge 0$: $\Delta K = M \cdot D \cdot U$
- If $D < 0$: $\Delta K = M \cdot D \cdot Down$

### Calculation Pseudocode
```c
// Executed when K changes (user knob adjustment)
float U = K_max - K;
float Down = K - K_min;

// Executed every audio cycle (DSP)
float deltaK = 0.0f;

if (D > 0) {
    deltaK = M * D * U;
} else if (D < 0) {
    deltaK = M * D * Down; // D is negative, deltaK will be <= 0
} else {
    deltaK = 0.0f; // Case D = 0
}

float K_mod = clamp(K + deltaK, K_min, K_max);
```
