import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Slider
import re
import os

# --- Configuration ---
SAMPLING_RATE = 48000
FRACTION_BITS = 30
LUT_FILE_PATH = os.path.join(os.path.dirname(__file__), '../sw/src/tables/vcf_lut_data.cpp')

def parse_luts(file_path):
    """
    Parses g_vcf_lpf_lut, g_vcf_bpf_lut, g_vcf_hpf_lut from the C++ file.
    Returns a dict with 3D numpy arrays: [mode][resonance][cutoff][coef_index]
    """
    if not os.path.exists(file_path):
        print(f"Error: {file_path} not found.")
        return None

    with open(file_path, 'r') as f:
        content = f.read()

    # Find the three main tables
    tables = {}
    table_names = ['g_vcf_lpf_lut', 'g_vcf_bpf_lut', 'g_vcf_hpf_lut', 'g_vcf_bsf_lut', 'g_vcf_apf_lut']
    
    for name in table_names:
        # Extract the array content between { and }; for each table name
        # Use raw string for regex to avoid syntax warnings
        pattern = rf"const int32_t {name}\[\d+\]\[128\]\[5\] = \{{(.*?)\}};"
        match = re.search(pattern, content, re.DOTALL)
        if match:
            table_str = match.group(1)
            # Find all innermost coefficient groups: {b0, b1, b2, a1, a2}
            # [^{}]* matches any character except { or }
            groups = re.findall(r'\{([^{}]*)\}', table_str, re.DOTALL)
            
            data = []
            for g in groups:
                 # Split by comma, remove trailing/leading spaces, and convert to int
                 # Some values might have + or - signs, which int() handles
                 coefs_str = g.replace('\n', ' ').split(',')
                 coefs = []
                 for c in coefs_str:
                     c_cleaned = c.strip()
                     if c_cleaned:
                         coefs.append(int(c_cleaned))
                 if len(coefs) == 5:
                     data.append(coefs)
            
            # Reshape to (8, 128, 5)
            try:
                arr = np.array(data).reshape(32, 128, 5)
                tables[name] = arr
            except ValueError as e:
                print(f"Error reshaping table {name}: {e}. Found {len(data)} items.")
        else:
            print(f"Warning: Could not find table {name} in {file_path}")
            
    return tables

# --- Filter Calculation ---

def get_filter_response(b, a, sample_rate=48000):
    """Calculates freq response from normalized biquad coefficients using numpy."""
    n_points = 2048
    w = np.geomspace(20, sample_rate / 2, n_points)
    
    # Angular frequency normalized to sampling rate
    w_norm = 2 * np.pi * w / sample_rate
    
    # Complex exponentials
    z1 = np.exp(-1j * w_norm)
    z2 = np.exp(-2j * w_norm)
    
    # Transfer function: (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
    num = b[0] + b[1] * z1 + b[2] * z2
    den = a[0] + a[1] * z1 + a[2] * z2
    
    h = num / den
    
    mag = 20 * np.log10(np.abs(h) + 1e-10)
    phase = np.angle(h, deg=True)
    return w, mag, phase

# --- UI Setup ---

if __name__ == '__main__':
    print("Loading LUTs from firmware source...")
    luts = parse_luts(LUT_FILE_PATH)
    
    if not luts:
        print("Failed to load LUTs. Check path or file content.")
        exit(1)

    # Initial state
    current_mode_idx = 0 # 0:LPF, 1:BPF, 2:HPF, 3:BSF, 4:APF
    current_res_idx = 0
    current_cut_idx = 64
    mode_names = ['LPF', 'BPF', 'HPF', 'BSF', 'APF']
    table_keys = ['g_vcf_lpf_lut', 'g_vcf_bpf_lut', 'g_vcf_hpf_lut', 'g_vcf_bsf_lut', 'g_vcf_apf_lut']

    # Initialize plot
    fig, (ax_mag, ax_phase) = plt.subplots(2, 1, figsize=(10, 8), sharex=True)
    plt.subplots_adjust(bottom=0.25, hspace=0.15)
    
    fig.patch.set_facecolor('#1e1e1e')
    for ax in [ax_mag, ax_phase]:
        ax.set_facecolor('#1e1e1e')
        ax.xaxis.label.set_color('white')
        ax.yaxis.label.set_color('white')
        ax.tick_params(colors='white', which='both')
        for spine in ax.spines.values():
            spine.set_edgecolor('#444444')
        ax.grid(True, alpha=0.3, which='both')

    ax_mag.set_ylabel('Magnitude (dB)')
    ax_mag.set_ylim(-60,30)
    ax_mag.set_xscale('log')
    ax_mag.set_xlim(20, 20000)

    ax_phase.set_ylabel('Phase (Degrees)')
    ax_phase.set_ylim(-180, 180)
    ax_phase.set_xlabel('Frequency (Hz)')

    def update_plot():
        mode_key = table_keys[current_mode_idx]
        if mode_key not in luts:
            print(f"Mode {mode_key} not in luts!")
            return
            
        coefs = luts[mode_key][current_res_idx][current_cut_idx]
        
        # Scaling
        scale = 1.0 / (1 << FRACTION_BITS)
        b = coefs[0:3] * scale
        a = [1.0, coefs[3] * scale, coefs[4] * scale]
        
        freqs, mag, phase = get_filter_response(b, a, SAMPLING_RATE)
        
        line_mag.set_data(freqs, mag)
        line_phase.set_data(freqs, phase)
        
        freq_hz = 20.0 * pow(1000.0, current_cut_idx / 127.0)
        q_val = 0.5 * pow(26.0, current_res_idx / 31.0)
        peak_db = np.max(mag)
        
        ax_mag.set_title(f'Filter: {mode_names[current_mode_idx]} | Freq: {freq_hz:.1f}Hz | Q: {q_val:.2f} | Peak: {peak_db:.1f}dB', color='white')
        fig.canvas.draw_idle()

    # Initial calculate
    mode_key_init = table_keys[0]
    coefs_init = luts[mode_key_init][0][64]
    scale_init = 1.0 / (1 << FRACTION_BITS)
    b_init = coefs_init[0:3] * scale_init
    a_init = [1.0, coefs_init[3] * scale_init, coefs_init[4] * scale_init]
    freqs_init, mag_init, phase_init = get_filter_response(b_init, a_init, SAMPLING_RATE)

    [line_mag] = ax_mag.plot(freqs_init, mag_init, lw=2, color='#00ffcc')
    [line_phase] = ax_phase.plot(freqs_init, phase_init, lw=1, color='#ffcc00')

    # Sliders
    ax_slider_cut = plt.axes([0.2, 0.12, 0.65, 0.03], facecolor='#333333')
    ax_slider_res = plt.axes([0.2, 0.08, 0.65, 0.03], facecolor='#333333')
    ax_slider_mode = plt.axes([0.2, 0.04, 0.65, 0.03], facecolor='#333333')

    slider_cut = Slider(ax=ax_slider_cut, label='Cutoff', valmin=0, valmax=127, valinit=64, valstep=1, color='#00ffcc')
    slider_res = Slider(ax=ax_slider_res, label='Resonance', valmin=0, valmax=31, valinit=0, valstep=1, color='#ff5500')
    slider_mode = Slider(ax=ax_slider_mode, label='Mode', valmin=0, valmax=4, valinit=0, valstep=1, color='#ffcc00')

    for s in [slider_cut, slider_res, slider_mode]:
        s.label.set_color('white')
        s.valtext.set_color('white')

    def on_change(val):
        global current_cut_idx, current_res_idx, current_mode_idx
        current_cut_idx = int(slider_cut.val)
        current_res_idx = int(slider_res.val)
        current_mode_idx = int(slider_mode.val)
        update_plot()

    slider_cut.on_changed(on_change)
    slider_res.on_changed(on_change)
    slider_mode.on_changed(on_change)

    update_plot()
    print("Launch visualizer...")
    plt.show()
