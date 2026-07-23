#include "grain_env.h"
#include <math.h>

float GrainEnvelope::get_morphed_envelope(float val, float x) {
    if (val < 0.0f) val = 0.0f;
    if (val >= (NUM_GRAIN_SHAPES - 1)) {
        val = (NUM_GRAIN_SHAPES - 1) - 0.0001f;
    }
    
    int idx_low = (int)val;
    float t = val - (float)idx_low;
    
    // Convert x [0.0, 1.0] to lut index [0, GRAIN_ENV_LUT_SIZE - 1]
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    
    float scaled_x = x * (GRAIN_ENV_LUT_SIZE - 1);
    int x_idx = (int)scaled_x;
    int x_next = x_idx + 1;
    if (x_next >= GRAIN_ENV_LUT_SIZE) x_next = GRAIN_ENV_LUT_SIZE - 1;
    
    float x_t = scaled_x - (float)x_idx;
    
    if (t < 0.0001f) {
        // Fast path: no morphing between shapes needed
        float y_low_1 = GRAIN_ENV_LUT[idx_low][x_idx];
        float y_low_2 = GRAIN_ENV_LUT[idx_low][x_next];
        return y_low_1 + x_t * (y_low_2 - y_low_1);
    }
    
    int idx_high = idx_low + 1;
    
    // Interpolate for low shape
    float y_low_1 = GRAIN_ENV_LUT[idx_low][x_idx];
    float y_low_2 = GRAIN_ENV_LUT[idx_low][x_next];
    float y_low = y_low_1 + x_t * (y_low_2 - y_low_1);
    
    // Interpolate for high shape
    float y_high_1 = GRAIN_ENV_LUT[idx_high][x_idx];
    float y_high_2 = GRAIN_ENV_LUT[idx_high][x_next];
    float y_high = y_high_1 + x_t * (y_high_2 - y_high_1);
    
    // Morph between shapes
    return (1.0f - t) * y_low + t * y_high;
}
