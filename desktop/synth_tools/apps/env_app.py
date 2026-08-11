import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Slider
import os

# --- Configuration ---
SAMPLING_RATE = 1000 # Simulation rate
DURATION = 4.0      # Total visualization time (seconds)
GATE_TIME = 2.0     # When the note is released

def get_eg_ms(val):
    """Exact logarithmic mapping from firmware: 0 to 2000ms"""
    if val == 0: return 0.0
    return 2000.0 * (pow(100.0, val / 127.0) - 1.0) / 99.0

def get_curve_k(curve):
    """Extreme curve range for almost square shapes (k = 20^((64-curve)/64))"""
    return pow(20.0, (64.0 - curve) / 64.0)

def apply_curve(x, k):
    """Core shaping function: y = x^k"""
    return np.power(x, k)

def simulate_eg(a_val, d_val, s_val, r_val, ac_val, dc_val, rc_val):
    t = np.linspace(0, DURATION, int(SAMPLING_RATE * DURATION))
    levels = np.zeros_like(t)
    
    a_time = get_eg_ms(a_val) / 1000.0
    d_time = get_eg_ms(d_val) / 1000.0
    r_time = get_eg_ms(r_val) / 1000.0
    s_level = s_val / 127.0
    
    a_k = get_curve_k(ac_val)
    d_k = get_curve_k(dc_val)
    r_k = get_curve_k(rc_val)
    
    state = "ATTACK"
    attack_start_time = 0.0
    decay_start_time = 0.0
    release_start_time = 0.0
    release_level_start = 0.0
    current_level = 0.0
    
    for i in range(len(t)):
        now = t[i]
        
        # Note Off trigger
        if now >= GATE_TIME and state != "RELEASE":
            state = "RELEASE"
            release_start_time = now
            release_level_start = current_level
            
        if state == "ATTACK":
            dt = now - attack_start_time
            if a_time > 0:
                progress = dt / a_time
                if progress >= 1.0:
                    progress = 1.0
                    state = "DECAY"
                    decay_start_time = now
                current_level = apply_curve(progress, a_k)
            else:
                current_level = 1.0
                state = "DECAY"
                decay_start_time = now
                
        elif state == "DECAY":
            dt = now - decay_start_time
            if d_time > 0:
                progress = dt / d_time
                if progress >= 1.0:
                    progress = 1.0
                    state = "SUSTAIN"
                
                # Decay bows between 1.0 and sustain
                linear_down = 1.0 - progress
                shaped_down = apply_curve(linear_down, d_k)
                current_level = s_level + (1.0 - s_level) * shaped_down
            else:
                current_level = s_level
                state = "SUSTAIN"
                
        elif state == "SUSTAIN":
            current_level = s_level
            
        elif state == "RELEASE":
            dt = now - release_start_time
            if r_time > 0:
                progress = dt / r_time
                if progress >= 1.0:
                    progress = 1.0
                
                # Release bows between release_level_start and 0.0
                linear_down = 1.0 - progress
                shaped_down = apply_curve(linear_down, r_k)
                current_level = release_level_start * shaped_down
            else:
                current_level = 0.0
        
        levels[i] = current_level
        
    return t, levels

# --- UI Setup ---
if __name__ == '__main__':
    fig, ax = plt.subplots(figsize=(10, 6))
    plt.subplots_adjust(bottom=0.35)
    
    fig.patch.set_facecolor('#1e1e1e')
    ax.set_facecolor('#1e1e1e')
    ax.xaxis.label.set_color('white')
    ax.yaxis.label.set_color('white')
    ax.tick_params(colors='white', which='both')
    for spine in ax.spines.values():
        spine.set_edgecolor('#444444')
    ax.grid(True, alpha=0.2)
    
    time_init, levels_init = simulate_eg(30, 64, 64, 64, 64, 64, 64)
    [line] = ax.plot(time_init, levels_init, lw=3, color='#00ffcc')
    
    ax.set_ylim(-0.05, 1.1)
    ax.set_xlim(0, DURATION)
    ax.set_ylabel('Amplitude')
    ax.set_xlabel('Time (seconds)')
    
    # Sliders
    sl_color = '#00ffcc'
    cur_color = '#ffcc00'
    
    ax_a = plt.axes([0.15, 0.25, 0.3, 0.02], facecolor='#333333')
    ax_d = plt.axes([0.15, 0.21, 0.3, 0.02], facecolor='#333333')
    ax_s = plt.axes([0.15, 0.17, 0.3, 0.02], facecolor='#333333')
    ax_r = plt.axes([0.15, 0.13, 0.3, 0.02], facecolor='#333333')
    
    ax_ac = plt.axes([0.6, 0.25, 0.3, 0.02], facecolor='#333333')
    ax_dc = plt.axes([0.6, 0.21, 0.3, 0.02], facecolor='#333333')
    ax_rc = plt.axes([0.6, 0.17, 0.3, 0.02], facecolor='#333333')
    
    slider_a = Slider(ax_a, 'Attack', 0, 127, valinit=30, valstep=1, color=sl_color)
    slider_d = Slider(ax_d, 'Decay', 0, 127, valinit=64, valstep=1, color=sl_color)
    slider_s = Slider(ax_s, 'Sustain', 0, 127, valinit=64, valstep=1, color=sl_color)
    slider_r = Slider(ax_r, 'Release', 0, 127, valinit=64, valstep=1, color=sl_color)
    
    slider_ac = Slider(ax_ac, 'A_Curve', 0, 127, valinit=64, valstep=1, color=cur_color)
    slider_dc = Slider(ax_dc, 'D_Curve', 0, 127, valinit=64, valstep=1, color=cur_color)
    slider_rc = Slider(ax_rc, 'R_Curve', 0, 127, valinit=64, valstep=1, color=cur_color)
    
    for s in [slider_a, slider_d, slider_s, slider_r, slider_ac, slider_dc, slider_rc]:
        s.label.set_color('white')
        s.valtext.set_color('white')

    def update(val):
        t, l = simulate_eg(
            slider_a.val, slider_d.val, slider_s.val, slider_r.val,
            slider_ac.val, slider_dc.val, slider_rc.val
        )
        line.set_data(t, l)
        
        # Update title with MS values
        a_ms = get_eg_ms(slider_a.val)
        d_ms = get_eg_ms(slider_d.val)
        r_ms = get_eg_ms(slider_r.val)
        ax.set_title(f'Envelope: A:{int(a_ms)}ms | D:{int(d_ms)}ms | S:{int(slider_s.val/1.27)}% | R:{int(r_ms)}ms', color='white')
        
        fig.canvas.draw_idle()

    for s in [slider_a, slider_d, slider_s, slider_r, slider_ac, slider_dc, slider_rc]:
        s.on_changed(update)
        
    update(0)
    plt.show()
