#include "grain.h"
#include "grain_env.h"
#include "../tables/audio_data.h"
#include <math.h>

Grain::Grain() : active(false) {}

void Grain::init(int s_id, float s_idx, float len, float p, float pn, bool rev, int shp, float a) {
    sample_id = s_id;
    start_idx = s_idx;
    pitch = p;
    if (pitch < 0.001f) pitch = 0.001f;
    length = len;
    pan = pn;
    reverse = rev;
    shape_idx = shp;
    amp = a;
    current_frame = 0.0f;
    current_idx = s_idx;
    active = true;
}

bool Grain::process(float fm_shift, float* out_l, float* out_r) {
    if (!active || length <= 1.0f) {
        return false;
    }

    if (current_frame >= length - 1.0f) {
        active = false;
        return false;
    }

    float effective_pitch = pitch + fm_shift;
    if (effective_pitch < 0.001f) effective_pitch = 0.001f;

    if (reverse) {
        current_idx -= effective_pitch;
    } else {
        current_idx += effective_pitch;
    }

    uint32_t data_len = raw_len(sample_id);
    if (data_len == 0) {
        active = false;
        return false;
    }

    // Wrap index using fast check instead of fmodf
    if (current_idx >= (float)data_len) {
        current_idx -= (float)data_len;
    } else if (current_idx < 0.0f) {
        current_idx += (float)data_len;
    }
    
    // Safety fallback in case effective_pitch was large enough to cross boundary multiple times
    if (current_idx >= (float)data_len || current_idx < 0.0f) {
        current_idx = fmodf(current_idx, (float)data_len);
        if (current_idx < 0.0f) current_idx += (float)data_len;
    }

    uint32_t idx_low = (uint32_t)current_idx;
    uint32_t idx_high = idx_low + 1;
    if (idx_high >= data_len) idx_high = 0;
    float frac = current_idx - (float)idx_low;

    // Linear interpolation
    // audio_data is stored as uint8_t or uint16_t, we need to scale to [-1.0, 1.0]
    // Assuming 8-bit for now (0-255 -> -1.0 to 1.0)
    // Wait, the config is dynamic, but raw_val returns uint8_t or uint16_t.
    // If it's uint8_t: (val / 127.5f) - 1.0f
    // Since audio2h main.go writes `raw_val` returning uint8_t for 8 bit, we can use a macro or just check size.
    // For now assume 8-bit WAV unsigned data format.
    float val_low = SAMPLE_TO_FLOAT(raw_val(sample_id, idx_low));
    float val_high = SAMPLE_TO_FLOAT(raw_val(sample_id, idx_high));

    float sample = (1.0f - frac) * val_low + frac * val_high;

    // Windowing
    float win_pos = current_frame / length;
    float win_val = GrainEnvelope::get_morphed_envelope((float)shape_idx, win_pos);

    float output = sample * win_val * amp;

    float g_l = (pan <= 0.5f) ? 1.0f : 2.0f * (1.0f - pan);
    float g_r = (pan >= 0.5f) ? 1.0f : 2.0f * pan;
    *out_l += output * g_l;
    *out_r += output * g_r;

    current_frame += 1.0f;
    return true;
}
