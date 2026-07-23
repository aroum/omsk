#ifndef FX_H
#define FX_H

#include <stdint.h>

class DigitalFX {
public:
    DigitalFX();

    // Process a single sample through the FX chain.
    // signal should be in the range [-1.0, 1.0]
    float process(float signal, float wf_gain, float ds_factor, float bc_bits, float mix_percent);

    // Reset internal state (for downsampling)
    void reset();

    float apply_wavefold(float signal, float gain);
    float apply_downsampling(float signal, float factor);
    float apply_bitcrush(float signal, float bits);

private:

    float downsample_hold_val;
    float downsample_counter;
};

#endif // FX_H
