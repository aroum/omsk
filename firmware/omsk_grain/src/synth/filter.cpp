#include "filter.h"
#include <math.h>

StateVariableFilter::StateVariableFilter()
    : ic1eq(0.0f), ic2eq(0.0f),
      cached_cutoff(-1.0f), cached_res(-1.0f), cached_sample_rate(-1.0f),
      a1(0.0f), a2(0.0f), a3(0.0f), k(0.0f) {}

void StateVariableFilter::update_coefs(float cutoff, float res, float sample_rate) {
    // tanf() is expensive — only recalculate when parameters actually changed
    if (cutoff == cached_cutoff && res == cached_res && sample_rate == cached_sample_rate)
        return;

    cached_cutoff      = cutoff;
    cached_res         = res;
    cached_sample_rate = sample_rate;

    float g = tanf(M_PI * cutoff / sample_rate);
    k  = 2.0f - 2.0f * res;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

float StateVariableFilter::process(float v0, float cutoff, float res, int mode_idx, float sample_rate) {
    if (mode_idx == 3) return v0;

    if (cutoff < 20.0f) cutoff = 20.0f;
    if (cutoff > sample_rate / 2.1f) cutoff = sample_rate / 2.1f;

    if (res < 0.0f) res = 0.0f;
    if (res > 0.99f) res = 0.99f;

    update_coefs(cutoff, res, sample_rate);

    float v3 = v0 - ic2eq;
    float v1 = a1 * ic1eq + a2 * v3;
    float v2 = ic2eq + a2 * ic1eq + a3 * v3;

    ic1eq = 2.0f * v1 - ic1eq;
    ic2eq = 2.0f * v2 - ic2eq;

    if (mode_idx == 0) return v2;            // LP
    if (mode_idx == 1) return v0 - k * v1 - v2; // HP
    if (mode_idx == 2) return v1;            // BP

    return v0;
}

