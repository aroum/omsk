#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Shared RAM buffers for all voices
// 8 resonance levels, 32 cutoff steps, 5 biquad coefficients
// Size: 8 * 32 * 5 * 4 = 5120 bytes each (Total 10.2KB)
extern int32_t g_vcf1_ram_lut[32][32][5];
extern int32_t g_vcf2_ram_lut[32][32][5];

// Pre-calculate coefficients for a specific type into a target RAM buffer
// types: 0=LPF, 1=BPF, 2=HPF
void vcf_load_type_to_ram(int filter_num, uint8_t type);

#ifdef __cplusplus
}
#endif
