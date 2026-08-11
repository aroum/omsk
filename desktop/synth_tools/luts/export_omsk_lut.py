import numpy as np
import os

def get_eg_ms(val):
    """Exact logarithmic mapping from env_app.py: 0 to 2000ms"""
    if val == 0: return 0.0
    return 2000.0 * (pow(100.0, val / 127.0) - 1.0) / 99.0

def generate_luts():
    SAMPLE_RATE = 48000
    print(f"Generating OMSK Engine LUTs (Sample Rate: {SAMPLE_RATE}Hz)...")

    # 1. LFO Frequency Table (0.01Hz to 50Hz, Exponential)
    lfo_freq = np.zeros(128)
    for i in range(128):
        # 0.01 * (5000^ (i/127))
        lfo_freq[i] = 0.01 * pow(5000.0, i / 127.0)
    
    # 2. EG Increment Table (0 to 2000ms)
    eg_inc = np.zeros(128)
    for i in range(128):
        ms = get_eg_ms(i)
        if ms < 0.1: # Practically instant
            eg_inc[i] = 1.0
        else:
            samples = (ms / 1000.0) * SAMPLE_RATE
            eg_inc[i] = 1.0 / samples

    # 3. Glide Rate Table (0 to 1000ms)
    glide_time = np.zeros(128)
    for i in range(128):
        ms = 10.0 * (pow(100.0, i/127.0) - 1.0) # 0 to 990ms
        if ms < 0.1:
            glide_time[i] = 1.0 # Instant
        else:
            samples = (ms / 1000.0) * SAMPLE_RATE
            glide_time[i] = 1.0 / samples

    # 4. Pan Tables (Equal Power: sin/cos)
    pan_l = np.zeros(128)
    pan_r = np.zeros(128)
    for i in range(128):
        angle = (i / 127.0) * (np.pi / 2.0)
        pan_l[i] = np.cos(angle)
        pan_r[i] = np.sin(angle)

    # 5. Detune Factor (-100 to +100 cents)
    detune_factor = np.zeros(128)
    for i in range(128):
        cents = (i - 64) * (100.0 / 64.0)
        detune_factor[i] = pow(2.0, cents / 1200.0)

    # 6. Smooth Alpha Table (0.1ms to 500ms time constant)
    smooth_alpha = np.zeros(128)
    for i in range(128):
        ms = 0.1 * pow(5000.0, i / 127.0)
        tau = ms / 1000.0
        smooth_alpha[i] = 1.0 - np.exp(-1.0 / (tau * SAMPLE_RATE))

    # 7. Mix Volume Table (Logarithmic/Audio taper)
    mix_volume = np.zeros(128)
    for i in range(128):
        if i == 0: 
            mix_volume[i] = 0.0
        else:
            mix_volume[i] = pow(10.0, (i - 127) / 40.0) # 60dB range

    # 8. MIDI to Frequency Table (Standard tuning A4=440)
    midi_to_freq = np.zeros(128)
    for i in range(128):
        midi_to_freq[i] = 440.0 * pow(2.0, (i - 69) / 12.0)

    c_path = '../sw/src/tables/omsk_lut_data.c'
    h_path = '../sw/src/tables/omsk_lut_data.h'

    print("Writing OMSK LUT files...")
    with open(c_path, 'w') as f:
        f.write('#include <stdint.h>\n')
        f.write('#include "omsk_lut_data.h"\n\n')
        
        def write_table(name, data):
            f.write(f'const float {name}[128] = {{\n  ')
            for i, val in enumerate(data):
                f.write(f'{val:.8f}f, ')
                if (i + 1) % 4 == 0: f.write('\n  ')
            f.write('};\n\n')

        write_table('lfo_freq_lut', lfo_freq)
        write_table('eg_inc_lut', eg_inc)
        write_table('glide_time_lut', glide_time)
        write_table('pan_l_lut', pan_l)
        write_table('pan_r_lut', pan_r)
        write_table('detune_factor_lut', detune_factor)
        write_table('smooth_alpha_lut', smooth_alpha)
        write_table('mix_volume_lut', mix_volume)
        write_table('midi_to_freq_lut', midi_to_freq)

    with open(h_path, 'w') as f:
        f.write('#pragma once\n#include <stdint.h>\n\n')
        f.write('#ifdef __cplusplus\nextern "C" {\n#endif\n\n')
        names = ['lfo_freq_lut', 'eg_inc_lut', 'glide_time_lut', 'pan_l_lut', 
                 'pan_r_lut', 'detune_factor_lut', 'smooth_alpha_lut', 'mix_volume_lut',
                 'midi_to_freq_lut']
        for name in names:
            f.write(f'extern const float {name}[128];\n')
        f.write('\n#ifdef __cplusplus\n}\n#endif\n')

if __name__ == '__main__':
    generate_luts()
