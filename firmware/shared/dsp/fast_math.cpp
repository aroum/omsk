#include "fast_math.h"
#include <math.h>

static float POW2_TAB[513];
static bool pow2_tab_initialized = false;

static void init_pow2_tab() {
    if (pow2_tab_initialized) return;
    for (int i = 0; i <= 512; i++) {
        POW2_TAB[i] = powf(2.0f, (float)i / 512.0f);
    }
    pow2_tab_initialized = true;
}

extern "C" float fast_exp2(float x) {
    init_pow2_tab();
    
    if (x < -126.0f) return 0.0f;
    if (x > 127.0f) return INFINITY; // Simple overflow representation
    
    int ipart = (int)floorf(x);
    float fpart = x - (float)ipart;
    
    float scaled = fpart * 512.0f;
    int idx = (int)scaled;
    float frac = scaled - (float)idx;
    
    float y0 = POW2_TAB[idx];
    float y1 = POW2_TAB[idx + 1];
    float val = y0 + frac * (y1 - y0);
    
    union {
        unsigned int i;
        float f;
    } u;
    u.i = (unsigned int)((ipart + 127) << 23);
    return val * u.f;
}
