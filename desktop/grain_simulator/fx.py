import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Slider

SAMPLES = 1000
X = np.linspace(0, 1, SAMPLES)
# Input signal: exactly one sine wave period with amplitude 1.0
INPUT_SINE = np.sin(2 * np.pi * X)

def apply_wavefold(signal, gain):
    """
    Window Wavefold:
    1. Signal under 0.5 (absolute value) is unchanged.
    2. Values above 0.5 are clamped and folded inside the 0.5...1.0 window.
    3. Signal never crosses zero during folding.
    """
    # Amplify signal
    v = signal * gain
    
    def fold_window(x, low, high):
        width = high - low
        # Shift to zero so window width is [0, width]
        x_shifted = x - low
        # Triangle folding math within window
        folded = width - np.abs((x_shifted % (2 * width)) - width)
        return low + folded

    # Output buffer copy
    out = np.copy(v)
    
    # Process positive peaks (above 0.5)
    pos_mask = v > 0.5
    if np.any(pos_mask):
        out[pos_mask] = fold_window(v[pos_mask], 0.5, 1.0)
        
    # Process negative peaks (below -0.5) symmetrically
    neg_mask = v < -0.5
    if np.any(neg_mask):
        # Negative window [-1.0, -0.5]
        out[neg_mask] = -fold_window(np.abs(v[neg_mask]), 0.5, 1.0)
        
    return out

def apply_downsampling(signal, factor):
    """Downsampling (sample rate reduction)."""
    if factor <= 1.0:
        return signal
    
    indices = (np.arange(len(signal)) // factor) * int(factor)
    indices = np.clip(indices, 0, len(signal) - 1).astype(int)
    return signal[indices]

def apply_bitcrush(signal, bits):
    """
    Bit Crush implementation:
    1. Calculate amplitude step quantization levels.
    2. Quantize signal maintaining zero symmetry.
    """
    if bits >= 16:
        return signal
    
    q_levels = 2**(bits - 1)
    if q_levels < 1: q_levels = 1
    
    return np.round(signal * q_levels) / q_levels
    
def process_chain(wf_gain, ds_factor, bc_bits, mix_percent):
    """Effects processing chain with 0-100% Dry/Wet Mix."""
    wet = INPUT_SINE.copy()
    
    # 1. Wavefold
    wet = apply_wavefold(wet, wf_gain)
    
    # 2. Downsampling
    wet = apply_downsampling(wet, ds_factor)
    
    # 3. Bit Crush
    wet = apply_bitcrush(wet, bc_bits)
    
    # 4. Mix (Dry/Wet)
    mix_val = mix_percent / 100.0
    return (1.0 - mix_val) * INPUT_SINE + mix_val * wet

plt.style.use('default')
fig, ax = plt.subplots(figsize=(10, 7))
plt.subplots_adjust(bottom=0.35, left=0.1, right=0.9, top=0.9)

# Initial state: Neutral
initial_output = process_chain(1.0, 1.0, 16.0, 100.0)
line, = ax.plot(X, initial_output, lw=2.5, color='#1f77b4', label='Processed (Wet)', zorder=3)
dry_line, = ax.plot(X, INPUT_SINE, lw=1.5, color='#e0e0e0', ls='--', label='Original (Dry)', zorder=2)

ax.set_ylim(-1.5, 1.5)
ax.set_xlim(0, 1)
ax.set_title("Digital FX Chain: Wavefold & Bitcrush Visualizer", fontsize=12, pad=15)
ax.set_xlabel("Time (1 Sine Cycle)")
ax.set_ylabel("Amplitude")
ax.grid(True, linestyle=':', alpha=0.5)
ax.legend(loc='upper right')

ax_color = '#f9f9f9'

# Enc 1: Wavefold (1.0 to 5.0)
ax_wf = plt.axes([0.25, 0.22, 0.55, 0.03], facecolor=ax_color)
s_wf = Slider(ax_wf, 'Enc 1: Wavefold ', 1.0, 5.0, valinit=1.0, valfmt='%1.1f')

# Enc 2: Downsample (1 to 80)
ax_ds = plt.axes([0.25, 0.17, 0.55, 0.03], facecolor=ax_color)
s_ds = Slider(ax_ds, 'Enc 2: Downsample ', 1.0, 80.0, valinit=1.0, valfmt='x%1.0f')

# Enc 3: Bit Crush (1 to 16 bits)
ax_bc = plt.axes([0.25, 0.12, 0.55, 0.03], facecolor=ax_color)
s_bc = Slider(ax_bc, 'Enc 3: Bit Crush ', 1.0, 16.0, valinit=16.0, valfmt='%1.0f bits')

# Enc 4: Mix (0 to 100%)
ax_mix = plt.axes([0.25, 0.07, 0.55, 0.03], facecolor=ax_color)
s_mix = Slider(ax_mix, 'Enc 4: Mix ', 0.0, 100.0, valinit=100.0, valfmt='%1.0f%%')

def update(val):
    """Update plot on slider change."""
    new_y = process_chain(s_wf.val, s_ds.val, s_bc.val, s_mix.val)
    line.set_ydata(new_y)
    fig.canvas.draw_idle()

s_wf.on_changed(update)
s_ds.on_changed(update)
s_bc.on_changed(update)
s_mix.on_changed(update)

plt.show()