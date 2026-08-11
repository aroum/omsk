import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Slider

# Calculate curve shape based on parameter C
def apply_curve(t, start_val, end_val, duration, curve_val):
    if duration == 0:
        return np.array([])
    
    # Normalize time from 0 to 1
    t_norm = t / duration
    
    if abs(curve_val) < 0.01:
        # Linear shape (curve_val == 0)
        y_norm = t_norm
    elif curve_val > 0:
        # Concave / exponential curve
        factor = curve_val * 5  # Steepness factor
        y_norm = (np.exp(factor * t_norm) - 1) / (np.exp(factor) - 1)
    else:
        # Convex / logarithmic curve
        factor = -curve_val * 5
        y_norm = np.log(1 + (np.exp(factor) - 1) * t_norm) / factor

    # Scale to start and end values
    return start_val + (end_val - start_val) * y_norm

# Plot update function
def update(val):
    atk = s_atk.val
    a_curv = s_a_curv.val
    rel = s_rel.val
    r_curv = s_r_curv.val
    
    # Generate phase time grids
    t_atk = np.linspace(0, atk, 500) if atk > 0 else np.array([0.0])
    t_rel = np.linspace(0, rel, 500) if rel > 0 else np.array([0.0])
    
    # Calculate amplitude values
    y_atk = apply_curve(t_atk, 0.0, 1.0, atk, a_curv)
    # Invert release curve direction for consistent feel
    y_rel = apply_curve(t_rel, 1.0, 0.0, rel, -r_curv)
    
    # Offset release phase time grid after attack phase
    t_rel_shifted = t_rel + t_atk[-1]
    
    # Concatenate arrays for plotting
    t_total = np.concatenate([t_atk, t_rel_shifted])
    y_total = np.concatenate([y_atk, y_rel])
    
    # Update plot line
    line.set_xdata(t_total)
    line.set_ydata(y_total)
    
    # Dynamically adjust X limits
    ax.set_xlim(-0.1, t_total[-1] + 0.1)
    fig.canvas.draw_idle()

# Create figure and plot layout
fig, ax = plt.subplots(figsize=(10, 6))
plt.subplots_adjust(left=0.1, bottom=0.35)

# Initial values
init_atk = 1.0
init_a_curv = 0.0
init_rel = 2.0
init_r_curv = 0.0

# Line handle
line, = ax.plot([0], [0], lw=2.5, color='#1f77b4')
ax.set_ylim(-0.05, 1.05)
ax.set_title('Amplitude Envelope Demo', fontsize=14, fontweight='bold')
ax.set_xlabel('Time (seconds)')
ax.set_ylabel('Amplitude')
ax.grid(True, linestyle='--', alpha=0.6)

slider_color = 'lightgray'

# Axes layout for sliders [left, bottom, width, height]
ax_atk = plt.axes([0.1, 0.22, 0.35, 0.03], facecolor=slider_color)
ax_a_curv = plt.axes([0.1, 0.14, 0.35, 0.03], facecolor=slider_color)
ax_rel = plt.axes([0.55, 0.22, 0.35, 0.03], facecolor=slider_color)
ax_r_curv = plt.axes([0.55, 0.14, 0.35, 0.03], facecolor=slider_color)

# Create sliders
s_atk = Slider(ax_atk, 'ATK (sec)', 0.0, 5.0, valinit=init_atk, valstep=0.01)
s_a_curv = Slider(ax_a_curv, 'A.CURV', -1.0, 1.0, valinit=init_a_curv, valstep=0.1)
s_rel = Slider(ax_rel, 'REL (sec)', 0.0, 5.0, valinit=init_rel, valstep=0.01)
s_r_curv = Slider(ax_r_curv, 'R.CURV', -1.0, 1.0, valinit=init_r_curv, valstep=0.1)

# Bind update callbacks
s_atk.on_changed(update)
s_a_curv.on_changed(update)
s_rel.on_changed(update)
s_r_curv.on_changed(update)

# Initial render
update(None)

plt.show()