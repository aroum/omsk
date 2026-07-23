#include "fx.h"
#include <math.h>

DigitalFX::DigitalFX() : downsample_hold_val(0.0f), downsample_counter(0.0f) {}

void DigitalFX::reset() {
    downsample_counter = 0.0f;
    downsample_hold_val = 0.0f;
}

float DigitalFX::apply_wavefold(float signal, float gain) {
    float v = signal * gain;
    
    // Wavefold window logic from python:
    // Window is [0.5, 1.0]. Anything above 0.5 folds back.
    // If v > 0.5: x_shifted = v - 0.5. width = 0.5.
    // folded = width - abs((x_shifted % (2*width)) - width)
    // folded = 0.5 - abs(fmodf(v - 0.5, 1.0) - 0.5)
    // out = 0.5 + folded
    
    auto fold_window = [](float x, float low, float high) {
        float width = high - low;
        float x_shifted = x - low;
        // fmodf for floating point modulo
        float mod_val = fmodf(x_shifted, 2.0f * width);
        if (mod_val < 0) mod_val += 2.0f * width;
        float folded = width - fabsf(mod_val - width);
        return low + folded;
    };

    if (v > 0.5f) {
        return fold_window(v, 0.5f, 1.0f);
    } else if (v < -0.5f) {
        return -fold_window(fabsf(v), 0.5f, 1.0f);
    }
    
    return v;
}

float DigitalFX::apply_downsampling(float signal, float factor) {
    if (factor <= 1.0f) {
        return signal;
    }
    
    if (downsample_counter == 0.0f) {
        downsample_hold_val = signal;
    }
    
    downsample_counter += 1.0f;
    if (downsample_counter >= factor) {
        downsample_counter = 0.0f;
    }
    
    return downsample_hold_val;
}

float DigitalFX::apply_bitcrush(float signal, float bits) {
    if (bits >= 16.0f) {
        return signal;
    }
    
    static const float POW2_BITS_LUT[16] = {
        1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f,
        256.0f, 512.0f, 1024.0f, 2048.0f, 4096.0f, 8192.0f, 16384.0f, 32768.0f
    };
    
    int idx = (int)bits - 1;
    if (idx < 0) idx = 0;
    if (idx > 15) idx = 15;
    
    float q_levels = POW2_BITS_LUT[idx];
    
    return roundf(signal * q_levels) / q_levels;
}

float DigitalFX::process(float signal, float wf_gain, float ds_factor, float bc_bits, float mix_percent) {
    float wet = signal;
    
    wet = apply_wavefold(wet, wf_gain);
    wet = apply_downsampling(wet, ds_factor);
    wet = apply_bitcrush(wet, bc_bits);
    
    float mix_val = mix_percent / 100.0f;
    return (1.0f - mix_val) * signal + mix_val * wet;
}
