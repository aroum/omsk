import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Slider

def apply_hpf(data, cutoff):
    """
    Simple single-pole High Pass Filter (HPF).
    cutoff: from 0.0 (no filtering) to 1.0 (maximum filtering).
    """
    if cutoff <= 0:
        return data
    
    # Smoothing factor (alpha)
    # Higher cutoff means smaller alpha, filtering more low frequencies
    alpha = 1.0 - (cutoff * 0.95)
    
    filtered = np.zeros_like(data)
    prev_input = data[0]
    prev_output = 0
    
    for i in range(len(data)):
        filtered[i] = alpha * (prev_output + data[i] - prev_input)
        prev_input = data[i]
        prev_output = filtered[i]
        
    return filtered

def generate_supersaw(t, detune_amount, mix_amount, hpf_amount):
    """
    Super Saw algorithm emulation (7 oscillators).
    Based on formulas and curves from Adam Szabo's research paper.
    """
    # Detune offsets (detune curve) from Szabo's paper
    detune_offsets = [
        -0.11002313, -0.06288439, -0.01952356, 
        0.0, 
        0.01991221, 0.06216538, 0.10745242
    ]
    
    # Initialize random phases for each oscillator
    np.random.seed(42) 
    phases = np.random.rand(len(detune_offsets))
    
    side_amp = mix_amount
    main_amp = 1.0 
    
    wave = np.zeros_like(t)
    f0 = 1.0 # Fundamental frequency
    
    for i, offset in enumerate(detune_offsets):
        freq = f0 * (1.0 + offset * detune_amount * 0.5)
        
        # Classic sawtooth generation
        osc = 1.0 - 2.0 * ((t * freq + phases[i]) % 1.0)
        
        if offset == 0.0:
            wave += main_amp * osc
        else:
            wave += side_amp * osc
            
    # Apply HPF filter
    wave = apply_hpf(wave, hpf_amount)
            
    # Normalization
    max_val = np.max(np.abs(wave))
    if max_val > 0:
        wave /= max_val
        
    return wave

def update(val):
    """Update plot on user slider interaction."""
    detune = s_detune.val
    mix = s_mix.val
    hpf = s_hpf.val
    
    new_wave = generate_supersaw(t, detune, mix, hpf)
    line.set_ydata(new_wave)
    fig.canvas.draw_idle()

# Time array to display approximately 2 full periods
t = np.linspace(0, 2.0, 2000)

# Oscilloscope visual layout
fig, ax = plt.subplots(figsize=(10, 6))
plt.subplots_adjust(bottom=0.3)

initial_detune = 0.4
initial_mix = 0.6
initial_hpf = 0.2
y = generate_supersaw(t, initial_detune, initial_mix, initial_hpf)

line, = ax.plot(t, y, lw=1.5, color='#00ff41', antialiased=True)
ax.set_ylim(-1.2, 1.2)
ax.set_title("Super Saw Visualization (Detune + Mix + HPF)", fontsize=12, color='white', pad=20)
ax.set_facecolor('#0a0a0a')
fig.patch.set_facecolor('#0a0a0a')

ax.grid(True, which='both', color='#1a1a1a', linestyle='-')
ax.tick_params(colors='#444444', labelsize=8)

# Sliders layout
ax_color = '#222222'
ax_detune = plt.axes([0.2, 0.15, 0.6, 0.025], facecolor=ax_color)
ax_mix    = plt.axes([0.2, 0.10, 0.6, 0.025], facecolor=ax_color)
ax_hpf    = plt.axes([0.2, 0.05, 0.6, 0.025], facecolor=ax_color)

s_detune = Slider(ax_detune, 'Detune ', 0.0, 1.0, valinit=initial_detune, color='#00aa00')
s_mix    = Slider(ax_mix,    'Mix    ', 0.0, 1.0, valinit=initial_mix,    color='#00aa00')
s_hpf    = Slider(ax_hpf,    'HPF    ', 0.0, 1.0, valinit=initial_hpf,    color='#0088ff')

for s in [s_detune, s_mix, s_hpf]:
    s.label.set_color('white')
    s.valtext.set_color('white')

s_detune.on_changed(update)
s_mix.on_changed(update)
s_hpf.on_changed(update)

plt.show()