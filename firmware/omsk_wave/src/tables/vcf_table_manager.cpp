#include "vcf_table_manager.h"
#include <math.h>
#include "../synth/pra32-u2-constants.h"

int32_t g_vcf1_ram_lut[32][32][5];
int32_t g_vcf2_ram_lut[32][32][5];

void vcf_load_type_to_ram(int filter_num, uint8_t type) {
    int32_t (*target)[32][5] = (filter_num == 1) ? g_vcf1_ram_lut : g_vcf2_ram_lut;
    
    float fs = 48000.0f;
    float scale = (float)(1 << 30); // FILTER_TABLE_FRACTION_BITS = 30

    for (int res_idx = 0; res_idx < 32; res_idx++) {
        // Q mapping: 0.5 to 13.0
        float Q = 0.5f * powf(26.0f, (float)res_idx / 31.0f);
        
        for (int cut_idx = 0; cut_idx < 32; cut_idx++) {
            // Cutoff mapping: 20Hz to 20000Hz (exponential)
            float freq = 20.0f * powf(1000.0f, (float)cut_idx / 31.0f);
            if (freq > 22000.0f) freq = 22000.0f;

            float w0 = 2.0f * 3.14159265359f * freq / fs;
            if (w0 > 3.14f) w0 = 3.14f; 
            
            float cos_w0 = cosf(w0);
            float sin_w0 = sinf(w0);
            float alpha = sin_w0 / (2.0f * Q);

            float a0 = 1.0f + alpha;
            float a1 = -2.0f * cos_w0;
            float a2 = 1.0f - alpha;

            float b0, b1, b2;
            if (type == 0) { // LPF
                b0 = (1.0f - cos_w0) * 0.5f;
                b1 = 1.0f - cos_w0;
                b2 = (1.0f - cos_w0) * 0.5f;
            } else if (type == 1) { // BPF
                b0 = alpha;
                b1 = 0.0f;
                b2 = -alpha;
            } else if (type == 2) { // HPF
                b0 = (1.0f + cos_w0) * 0.5f;
                b1 = -(1.0f + cos_w0);
                b2 = (1.0f + cos_w0) * 0.5f;
            } else if (type == 3) { // BSF (Notch)
                b0 = 1.0f;
                b1 = -2.0f * cos_w0;
                b2 = 1.0f;
            } else { // APF (All-pass)
                b0 = 1.0f - alpha;
                b1 = -2.0f * cos_w0;
                b2 = 1.0f + alpha
            }

            target[res_idx][cut_idx][0] = (int32_t)((b0 / a0) * scale);
            target[res_idx][cut_idx][1] = (int32_t)((b1 / a0) * scale);
            target[res_idx][cut_idx][2] = (int32_t)((b2 / a0) * scale);
            target[res_idx][cut_idx][3] = (int32_t)((a1 / a0) * scale);
            target[res_idx][cut_idx][4] = (int32_t)((a2 / a0) * scale);
        }
    }
}
