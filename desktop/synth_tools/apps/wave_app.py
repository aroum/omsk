import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Slider

# --- Global constants and patterns ---
# Generate 128 PAM4 patterns (16 segments each)
PAM4_PATTERNS = []

# Fill 128 patterns with various waveform segment variations
for i in range(128):
    if i < 16:
        # Linear ramps and step transitions
        base = np.linspace(-1.0, 1.0, 16)
        if i % 2 == 0:
            PAM4_PATTERNS.append(np.round(base * 3) / 3)
        else:
            PAM4_PATTERNS.append(np.flip(np.round(base * 3) / 3))
    elif i < 64:
        # Pseudo-random sequences with deterministic seed
        np.random.seed(i)
        pattern = np.random.choice([-1.0, -0.33, 0.33, 1.0], 16)
        PAM4_PATTERNS.append(pattern.tolist())
    else:
        # Complex harmonic and cyclic shapes
        t = np.linspace(0, 1, 16)
        val = np.sin(2 * np.pi * t * (1 + i/32))
        PAM4_PATTERNS.append((np.round(val * 3) / 3).tolist())

def pam4_pattern_from_t(t):
    """Convert normalized (0-1) value to pattern index (0-127)."""
    t = np.clip(t, 0.0, 1.0)
    idx = int(t * 127.99)
    return idx

def pwm_from_t(t):
    t = np.clip(t, 0.0, 1.0)
    return 0.5 - t * 0.49

# --- Waveform transformation functions ---

def apply_custom_fold(x, shape_val):
    """
    Wavefolding in the [0.5, 1.0] range.
    Signal folds back from 1.0 down to 0.5, then 0.5 up to 1.0.
    """
    if shape_val <= 0:
        return x
        
    gain = 1.0 + (shape_val / 127.0) * 5.0
    y = x * gain
    
    def fold_logic(val):
        abs_v = np.abs(val)
        sign = np.sign(val)
        mask = abs_v > 0.5
        
        v_shifted = abs_v[mask] - 0.5
        v_folded = v_shifted % 1.0
        
        v_folded = np.where(v_folded > 0.5, 1.0 - v_folded, v_folded)
        
        abs_v[mask] = v_folded + 0.5
        return abs_v * sign

    return fold_logic(y)

def apply_pulse_fold(phase, pwm_base, shape_val):
    """
    Smooth folding for pulse/square wave.
    Pulse top folds in the [0.5, 1.0] range.
    """
    p = phase % 1.0
    # Base pulse signal
    sig = np.where(p < pwm_base, 1.0, -1.0)
    
    if shape_val <= 0:
        return sig

    # Gain intensity for fold
    gain = 1.0 + (shape_val / 127.0) * 3.0
    
    # Number of fold cycles
    num_cycles = 1 + int(shape_val / 50)
    
    # Active pulse high section
    active = p < pwm_base
    if np.any(active):
        p_norm = p[active] / pwm_base
        tri_base = (p_norm * num_cycles) % 1.0
        tri_base = 1.0 - np.abs(tri_base * 2.0 - 1.0)
        
        val = 0.5 + tri_base * (gain * 0.5)
        
        v_shifted = val - 0.5
        v_folded = v_shifted % 1.0
        v_folded = np.where(v_folded > 0.5, 1.0 - v_folded, v_folded)
        
        sig[active] = v_folded + 0.5

    # Symmetric negative pulse low section
    inactive = p >= pwm_base
    if np.any(inactive):
        p_norm = (p[inactive] - pwm_base) / (1.0 - pwm_base)
        tri_base = (p_norm * num_cycles) % 1.0
        tri_base = 1.0 - np.abs(tri_base * 2.0 - 1.0)
        
        val = 0.5 + tri_base * (gain * 0.5)
        v_shifted = val - 0.5
        v_folded = v_shifted % 1.0
        v_folded = np.where(v_folded > 0.5, 1.0 - v_folded, v_folded)
        
        sig[inactive] = -(v_folded + 0.5)
        
    return sig

# --- Base wave generators ---

def wave_sin(phase):
    return np.sin(2 * np.pi * phase)

def wave_tri(phase):
    return 4.0 * np.abs((phase + 0.25) % 1.0 - 0.5) - 1.0

def wave_saw_simple(phase):
    return 2.0 * (phase % 1.0) - 1.0

def wave_rsaw(phase):
    return 1.0 - 2.0 * (phase % 1.0)

def wave_pulse_raw(phase, pwm):
    p = phase % 1.0
    return np.where(p < pwm, 1.0, -1.0)

def wave_pam_raw(phase, pattern_idx):
    """Select pattern from extended 128-element list."""
    idx = int(pattern_idx) % 128
    segment = (np.floor(phase * 16)).astype(int) % 16
    return np.array([PAM4_PATTERNS[idx][s] for s in segment])

# --- Sampling System ---

def sample_wave(phase, wtype, pwm, pam_pattern, shape_val):
    if wtype == 'PAM':
        actual_pattern_idx = (pam_pattern + int(shape_val)) % 128
        return wave_pam_raw(phase, actual_pattern_idx)
    
    if wtype in ['PULSE', 'SQR']:
        base_pwm = pwm if wtype == 'PULSE' else 0.5
        return apply_pulse_fold(phase, base_pwm, shape_val)
    
    raw_sig = sample_wave_raw_basic(phase, wtype, pwm)
    return apply_custom_fold(raw_sig, shape_val)

def sample_wave_raw_basic(phase, wtype, pwm):
    if wtype == 'SIN': return wave_sin(phase)
    if wtype == 'TRI': return wave_tri(phase)
    if wtype == 'SAW': return wave_saw_simple(phase)
    if wtype == 'RSAW': return wave_rsaw(phase)
    return np.zeros_like(phase)

def generate_wave(phase, wave_param, shape_param):
    if wave_param < 32:
        seg = wave_param // 8
        t = float(wave_param % 8) / 7.0
        
        types = [('SIN', 'TRI'), ('TRI', 'SAW'), ('SAW', 'RSAW'), ('RSAW', 'PULSE')]
        if seg == 2:
            s_saw = sample_wave(phase, 'SAW', 0.5, 0, shape_param)
            # Invert phase of second half-wave (0.5 offset)
            phase_rsaw = (phase + 0.5) % 1.0
            s_rsaw = sample_wave(phase_rsaw, 'RSAW', 0.5, 0, shape_param)
            s_tri = sample_wave(phase, 'TRI', 0.5, 0, shape_param)
            curr_t = t * 2.0
            if t < 0.5:
                return (1.0 - curr_t) * s_saw + curr_t * s_tri
            else:
                curr_t -= 1.0
                return (1.0 - curr_t) * s_tri + curr_t * s_rsaw
        
        a_type, b_type = types[seg] if seg < 4 else ('RSAW', 'PULSE')
        s1 = sample_wave(phase, a_type, 0.5, 0, shape_param)
        s2 = sample_wave(phase, b_type, 0.5, 0, shape_param)
        return (1.0 - t) * s1 + t * s2

    elif wave_param < 64:
        pw = 0.5 - (float(wave_param - 32) / 31.0) * 0.49
        return sample_wave(phase, 'PULSE', pw, 0, shape_param)

    elif wave_param < 80:
        base_idx = int((wave_param - 64) * (127.0 / 15.0))
        return sample_wave(phase, 'PAM', 0.5, base_idx, shape_param)

    else:
        hybrid_pairs = [
            ('SIN', 'TRI'), ('SIN', 'SAW'), ('SIN', 'RSAW'), ('SIN', 'SQR'),
            ('SIN', 'PULSE'), ('SIN', 'PAM'), ('TRI', 'SAW'), ('TRI', 'RSAW'),
            ('TRI', 'SQR'), ('TRI', 'PULSE'), ('TRI', 'PAM'), ('SAW', 'RSAW'),
            ('RSAW', 'SAW'), ('SAW', 'SQR'), ('SAW', 'PULSE'), ('SAW', 'PAM'),
            ('RSAW', 'SQR'), ('RSAW', 'PULSE'), ('RSAW', 'PAM'), ('SQR', 'PULSE'),
            ('SQR', 'PAM'), ('PULSE', 'PAM')
        ]
        
        pos = (wave_param - 80) / 47.0
        num_segs = len(hybrid_pairs) - 1
        segf = pos * num_segs
        idx = int(np.floor(segf))
        t = segf - float(idx)
        
        a_type, b_type = hybrid_pairs[idx]
        pwm_val = pwm_from_t(t)
        pam_val = int(t * 127.0) 
        
        phase_a = (phase + 0.25) % 1.0 if a_type in ['PULSE', 'SQR'] else phase
        # Invert phase for waves 85-93
        if 85 <= wave_param <= 93 and a_type == 'SIN':
            phase_a = (phase_a + 0.5) % 1.0
        # Invert phase for second half-cycle (phase_b)
        phase_b = (phase + 0.5) % 1.0
        if b_type == 'PAM':
            phase_b = (phase_b) % 1.0
        
        s_a = sample_wave(phase_a, a_type, pwm_val, pam_val, shape_param)
        s_b = sample_wave(phase_b, b_type, pwm_val, pam_val, shape_param)

        return np.where(phase < 0.5, s_a, s_b)

def normalize_wave(y):
    y_min, y_max = np.min(y), np.max(y)
    if np.abs(y_max - y_min) < 1e-6:
        return y - y_min
    bias = (y_max + y_min) / 2.0
    y_centered = y - bias
    max_abs = np.max(np.abs(y_centered))
    if max_abs > 1e-6:
        return y_centered / max_abs
    return y_centered

# --- UI Section ---

if __name__ == '__main__':
    fig, ax = plt.subplots(figsize=(10, 6))
    plt.subplots_adjust(bottom=0.3)
    
    fig.patch.set_facecolor('#1e1e1e')
    ax.set_facecolor('#1e1e1e')
    ax.title.set_color('white')
    ax.xaxis.label.set_color('white')
    ax.yaxis.label.set_color('white')
    ax.tick_params(colors='white')
    for spine in ax.spines.values():
        spine.set_edgecolor('#444444')
        
    ax.set_ylim(-1.2, 1.2)
    ax.set_xlim(0, 1.0)
    ax.grid(True, alpha=0.3)
    
    phase = np.linspace(0, 1.0, 1024, endpoint=False)
    
    initial_y = normalize_wave(generate_wave(phase, 0, 0))
    [line] = ax.plot(phase, initial_y, lw=2, color='#ff0055')
    ax.set_title(f'Wave: 0 | Shape: 0')
    
    ax_wave = plt.axes([0.2, 0.15, 0.65, 0.03], facecolor='#333333')
    ax_shape = plt.axes([0.2, 0.08, 0.65, 0.03], facecolor='#333333')
    
    slider_wave = Slider(ax=ax_wave, label='Wave (0-127)', valmin=0, valmax=127, valinit=0, valstep=1)
    slider_shape = Slider(ax=ax_shape, label='Shape (0-127)', valmin=0, valmax=127, valinit=0, valstep=1)
    
    slider_wave.label.set_color('white')
    slider_wave.valtext.set_color('white')
    slider_shape.label.set_color('white')
    slider_shape.valtext.set_color('white')
    
    def update(val):
        w = int(slider_wave.val)
        s = int(slider_shape.val)
        
        y = generate_wave(phase, w, s)
        y = normalize_wave(y)

        line.set_ydata(y)
        ax.set_title(f'Wave: {w} | Shape: {s}')
        fig.canvas.draw_idle()
        
    slider_wave.on_changed(update)
    slider_shape.on_changed(update)
    
    plt.show()