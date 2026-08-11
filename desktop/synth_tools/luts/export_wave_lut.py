import numpy as np
import os

from wave_app import apply_custom_fold, sample_wave_raw_basic, PAM4_PATTERNS
from wave_app import apply_pulse_fold, normalize_wave, generate_wave

def bandlimit(wave, max_h):
    if max_h >= 127: 
        return normalize_wave(wave)
    F = np.fft.rfft(wave)
    # fade to reduce ringing
    fade_len = max(1, max_h // 4)
    start_fade = max_h - fade_len
    F[max_h:] = 0
    for i in range(start_fade, max_h):
        frac = (i - start_fade) / fade_len
        F[i] *= np.cos(frac * np.pi / 2.0)
    y = np.fft.irfft(F, n=len(wave))
    return normalize_wave(y)

def export_table():
    print("Generating Memory-Optimized LUTs (1MB target)...")
    SHAPES_BASIC = 64
    SHAPES_PULSE = 16
    PWMS_PULSE = 16
    SAMPLES = 256
    MIPMAPS = 8
    
    harmonics = [128, 128, 128, 64, 32, 16, 8, 4]
    
    lut_basic = np.zeros((MIPMAPS, 4, SHAPES_BASIC, SAMPLES), dtype=np.int16)
    lut_pulse = np.zeros((MIPMAPS, PWMS_PULSE, SHAPES_PULSE, SAMPLES), dtype=np.int16)
    
    phase = np.linspace(0, 1.0, SAMPLES, endpoint=False)
    types = ['SIN', 'TRI', 'SAW', 'RSAW']
    
    print("Generating basic waves...")
    for s_idx in range(SHAPES_BASIC):
        shape_val = s_idx * (127.0 / (SHAPES_BASIC - 1))
        for w_idx, wtype in enumerate(types):
            raw = sample_wave_raw_basic(phase, wtype, 0.5)
            y_base = apply_custom_fold(raw, shape_val)
            for mip in range(MIPMAPS):
                y_mip = bandlimit(y_base, harmonics[mip])
                lut_basic[mip, w_idx, s_idx, :] = np.clip(np.round(y_mip * 32767), -32767, 32767).astype(np.int16)
                
    print("Generating pulse waves...")
    pwms = np.linspace(0.5, 0.01, PWMS_PULSE)
    for pw_idx, pw in enumerate(pwms):
        for s_idx in range(SHAPES_PULSE):
            shape_val = s_idx * (127.0 / (SHAPES_PULSE - 1))
            y_base = apply_pulse_fold(phase, pw, shape_val)
            for mip in range(MIPMAPS):
                y_mip = bandlimit(y_base, harmonics[mip])
                lut_pulse[mip, pw_idx, s_idx, :] = np.clip(np.round(y_mip * 32767), -32767, 32767).astype(np.int16)

    print("Generating normalization gain+bias tables (128x128)...")
    lut_norm_gain = np.ones((128, 128), dtype=np.float32)
    lut_norm_bias = np.zeros((128, 128), dtype=np.float32)
    phase_1024 = np.linspace(0, 1.0, 1024, endpoint=False)
    for w in range(128):
        for s in range(128):
            y = generate_wave(phase_1024, w, s)
            y_min, y_max = np.min(y), np.max(y)
            bias = (y_max + y_min) / 2.0
            lut_norm_bias[w, s] = bias
            max_abs = np.max(np.abs(y - bias))
            if max_abs > 1e-6:
                lut_norm_gain[w, s] = 1.0 / max_abs

    c_path = '../sw/src/tables/vco_lut_data.c'
    h_path = '../sw/src/tables/vco_lut_data.h'

    print("Writing files...")
    with open(c_path, 'w') as f:
        f.write('#include <stdint.h>\n')
        f.write('#include "vco_lut_data.h"\n\n')
        
        f.write(f'const int16_t lut_basic[{MIPMAPS}][4][{SHAPES_BASIC}][{SAMPLES}] = {{\n')
        for mip in range(MIPMAPS):
            f.write('  {\n')
            for w in range(4):
                f.write('    {\n')
                for s_idx in range(SHAPES_BASIC):
                    f.write('      {')
                    f.write(', '.join(map(str, lut_basic[mip, w, s_idx])))
                    f.write('},\n')
                f.write('    },\n')
            f.write('  },\n')
        f.write('};\n\n')

        f.write(f'const int16_t lut_pulse[{MIPMAPS}][{PWMS_PULSE}][{SHAPES_PULSE}][{SAMPLES}] = {{\n')
        for mip in range(MIPMAPS):
            f.write('  {\n')
            for pw_idx in range(PWMS_PULSE):
                f.write('    {\n')
                for s_idx in range(SHAPES_PULSE):
                    f.write('      {')
                    f.write(', '.join(map(str, lut_pulse[mip, pw_idx, s_idx])))
                    f.write('},\n')
                f.write('    },\n')
            f.write('  },\n')
        f.write('};\n\n')

        f.write('const float lut_pam4[128][16] = {\n')
        for i in range(128):
            f.write('  {')
            f.write(', '.join(map(str, PAM4_PATTERNS[i])))
            f.write('},\n')
        f.write('};\n\n')

        f.write('const float lut_norm_gain[128][128] = {\n')
        for w in range(128):
            f.write('  {')
            f.write(', '.join(f"{val:.6f}f" for val in lut_norm_gain[w]))
            f.write('},\n')
        f.write('};\n\n')

        f.write('const float lut_norm_bias[128][128] = {\n')
        for w in range(128):
            f.write('  {')
            f.write(', '.join(f"{val:.6f}f" for val in lut_norm_bias[w]))
            f.write('},\n')
        f.write('};\n')
        
    with open(h_path, 'w') as f:
        f.write('#pragma once\n#include <stdint.h>\n\n')
        f.write(f'extern const int16_t lut_basic[{MIPMAPS}][4][{SHAPES_BASIC}][{SAMPLES}];\n')
        f.write(f'extern const int16_t lut_pulse[{MIPMAPS}][{PWMS_PULSE}][{SHAPES_PULSE}][{SAMPLES}];\n')
        f.write(f'extern const float lut_pam4[128][16];\n')
        f.write(f'extern const float lut_norm_gain[128][128];\n')
        f.write(f'extern const float lut_norm_bias[128][128];\n')
        
if __name__ == '__main__':
    export_table()
