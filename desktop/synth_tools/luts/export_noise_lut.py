import math
import os

# Configuration from sw/src/synth/audio.h
SAMPLE_RATE = 48000

def get_noise_coeffs(color):
    """
    Identical logic to sw/src/synth/pra_synth.c:noise_update_coeffs
    Returns (mode, b0, b1, b2, a1, a2)
    """
    f_val = color / 127.0
    q = 0.707
    freq = 0.0
    mode = 0

    if f_val < 0.45:
        norm = (0.45 - f_val) / 0.45
        freq = 20000.0 * pow(0.005, norm)
        mode = 1  # LPF
    elif f_val > 0.55:
        norm = (f_val - 0.55) / 0.45
        freq = 20.0 * pow(1000.0, norm)
        mode = 2  # HPF
    else:
        mode = 0  # White (Bypass)

    if mode == 0:
        return 0, 1.0, 0.0, 0.0, 0.0, 0.0

    w0 = 2.0 * math.pi * freq / SAMPLE_RATE
    cos_w0 = math.cos(w0)
    sin_w0 = math.sin(w0)
    alpha = sin_w0 / (2.0 * q)

    if mode == 1:
        b0 = (1.0 - cos_w0) * 0.5
        b1 = 1.0 - cos_w0
        b2 = (1.0 - cos_w0) * 0.5
    else:
        b0 = (1.0 + cos_w0) * 0.5
        b1 = -(1.0 + cos_w0)
        b2 = (1.0 + cos_w0) * 0.5

    a0 = 1.0 + alpha
    a1 = -2.0 * cos_w0
    a2 = 1.0 - alpha

    return mode, b0/a0, b1/a0, b2/a0, a1/a0, a2/a0

def export_noise_lut():
    print(f"Generating Noise Filter LUT (Sample Rate: {SAMPLE_RATE}Hz)...")
    
    lut_coeffs = []
    lut_modes = []

    for color in range(128):
        mode, b0, b1, b2, a1, a2 = get_noise_coeffs(color)
        lut_modes.append(mode)
        lut_coeffs.append([b0, b1, b2, a1, a2])

    c_path = '../sw/src/tables/noise_lut_data.c'
    h_path = '../sw/src/tables/noise_lut_data.h'

    # Ensure directory exists
    dir_path = os.path.dirname(c_path)
    if not os.path.exists(dir_path):
        os.makedirs(dir_path)

    print("Writing noise_lut_data.c...")
    with open(c_path, 'w') as f:
        f.write('#include <stdint.h>\n')
        f.write('#include "noise_lut_data.h"\n\n')
        
        f.write('const uint8_t g_noise_mode_lut[128] = {\n  ')
        for i, m in enumerate(lut_modes):
            f.write(f'{m}, ')
            if (i + 1) % 16 == 0: f.write('\n  ')
        f.write('\n};\n\n')

        f.write('const float g_noise_filter_lut[128][5] = {\n')
        for i in range(128):
            coeffs = lut_coeffs[i]
            f.write(f'  {{{coeffs[0]:.8f}f, {coeffs[1]:.8f}f, {coeffs[2]:.8f}f, {coeffs[3]:.8f}f, {coeffs[4]:.8f}f}},\n')
        f.write('};\n')

    print("Writing noise_lut_data.h...")
    with open(h_path, 'w') as f:
        f.write('#pragma once\n')
        f.write('#include <stdint.h>\n\n')
        f.write('#ifdef __cplusplus\nextern "C" {\n#endif\n\n')
        f.write('extern const uint8_t g_noise_mode_lut[128];\n')
        f.write('extern const float g_noise_filter_lut[128][5];\n\n')
        f.write('#ifdef __cplusplus\n}\n#endif\n')

    print("Done!")

if __name__ == '__main__':
    export_noise_lut()
