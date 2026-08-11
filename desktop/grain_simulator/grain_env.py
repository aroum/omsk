import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Slider


# Number of morphing steps between two adjacent shapes (higher = smoother slider steps)
MORPH_STEPS = 3

# Envelope shape order list
SHAPE_ORDER = [
    "Rexpodec", 
    "Long Attack", 
    "Trapezoid", 

    "Tukey", 

    "Gaussian", 
    "Triangle", 

    "Sinc", 
    "Narrow Pulse",

    "Short Attack", 
    "Expodec"
]


def get_base_envelope(shape_name, x):
    """Returns array values for specific base envelope shape."""
    if shape_name == "Triangle":
        return 1 - np.abs(2 * (x - 0.5))
    
    elif shape_name == "Trapezoid":
        return np.clip(np.minimum(5 * x, 5 * (1 - x)), 0, 1)
    
    elif shape_name == "Tukey":
        alpha = 0.5
        out = np.ones_like(x)
        first_part = x < alpha / 2
        last_part = x > (1 - alpha / 2)
        out[first_part] = 0.5 * (1 + np.cos(np.pi * (2 * x[first_part] / alpha - 1)))
        out[last_part] = 0.5 * (1 + np.cos(np.pi * (2 * (x[last_part] - 1) / alpha + 1)))
        return out

    elif shape_name == "Gaussian":
        return np.exp(-0.5 * ((x - 0.5) / 0.15)**2)

    elif shape_name == "Sinc":
        v = (x - 0.5) * 10
        return np.sinc(v)

    elif shape_name == "Expodec":
        return np.exp(-5 * x)

    elif shape_name == "Rexpodec":
        return np.exp(5 * (x - 1))

    elif shape_name == "Long Attack":
        peak = 0.8
        return np.where(x < peak, x / peak, (1 - x) / (1 - peak))

    elif shape_name == "Short Attack":
        peak = 0.2
        return np.where(x < peak, x / peak, (1 - x) / (1 - peak))

    elif shape_name == "Narrow Pulse":
        return np.exp(-0.5 * ((x - 0.5) / 0.03)**2)

    return np.zeros_like(x)


def get_morphed_envelope(val, x):
    """Calculates interpolated shape based on fractional index."""
    idx_low = int(np.floor(val))
    idx_high = int(np.ceil(val))
    
    # Check bounds
    if idx_high >= len(SHAPE_ORDER):
        return get_base_envelope(SHAPE_ORDER[-1], x)
    
    # Mix blend factor (0 to 1)
    t = val - idx_low
    
    y_low = get_base_envelope(SHAPE_ORDER[idx_low], x)
    y_high = get_base_envelope(SHAPE_ORDER[idx_high], x)
    
    # Linear interpolation between arrays
    return (1 - t) * y_low + t * y_high


x = np.linspace(0, 1, 1000)

plt.style.use('default')
fig, ax = plt.subplots(figsize=(10, 6))
plt.subplots_adjust(bottom=0.25, left=0.1, right=0.9, top=0.85)

# Initial state
initial_val = 0.0
current_y = get_morphed_envelope(initial_val, x)
line, = ax.plot(x, current_y, lw=2.5, color='#1f77b4')
fill = ax.fill_between(x, current_y, color='#1f77b4', alpha=0.15)

# Axis setup
ax.set_ylim(-0.2, 1.2)
ax.set_xlim(0, 1)
ax.set_xlabel("Time (normalized)")
ax.set_ylabel("Amplitude")
title_text = ax.set_title(f"Morphing: {SHAPE_ORDER[0]}", fontsize=14, pad=20)
ax.grid(True, linestyle=':', alpha=0.6)

ax_slider = plt.axes([0.2, 0.08, 0.6, 0.04])
val_step = 1.0 / MORPH_STEPS

slider = Slider(
    ax=ax_slider,
    label='Morph Shape  ',
    valmin=0,
    valmax=len(SHAPE_ORDER) - 1,
    valinit=initial_val,
    valstep=val_step,
    color='#1f77b4'
)


def update(val):
    y_data = get_morphed_envelope(val, x)
    
    line.set_ydata(y_data)
    
    global fill
    fill.remove()
    fill = ax.fill_between(x, y_data, color='#1f77b4', alpha=0.15)
    
    idx_low = int(np.floor(val))
    idx_high = int(np.ceil(val))
    if idx_low == idx_high:
        title_text.set_text(f"Shape: {SHAPE_ORDER[idx_low]}")
    else:
        title_text.set_text(f"Morphing: {SHAPE_ORDER[idx_low]} ➔ {SHAPE_ORDER[idx_high]}")
    
    fig.canvas.draw_idle()

slider.on_changed(update)

plt.show()