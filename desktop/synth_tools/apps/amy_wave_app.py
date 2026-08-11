import sys
import os
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.widgets import Slider

# Add AMY library to path
# We use the isolated build in emulator/bin/
current_dir = os.path.dirname(os.path.abspath(__file__))
amy_bin_path = os.path.abspath(os.path.join(current_dir, 'bin'))

if not os.path.exists(os.path.join(amy_bin_path, 'amy')):
    print(f"Error: Isolated AMY package not found in {amy_bin_path}")
    print("Please run: python3 emulator/build_amy_clean.py")
    sys.exit(1)

sys.path.insert(0, amy_bin_path)

try:
    import amy
except ImportError as e:
    print(f"Error: Could not import 'amy' from {amy_bin_path}: {e}")
    print("Make sure you have run the clean build script: python3 emulator/build_amy_clean.py")
    sys.exit(1)

# Initialize AMY
amy.restart()

# Constants
SAMPLERATE = 44100
BLOCK_SIZE = 256
N_BLOCKS = 4
N_SAMPLES = BLOCK_SIZE * N_BLOCKS  # 1024 samples
FREQ = SAMPLERATE / N_SAMPLES      # Exact 1 period in N_SAMPLES

# Mapping of AMY wave names to IDs for the UI
WAVE_MAP = {
    'SINE': 0,
    'PULSE': 1,
    'SAW_DOWN': 2,
    'SAW_UP': 3,
    'TRIANGLE': 4,
    'NOISE': 5,
    'WAVETABLE': 19,
    'PCM': 7,
    'OMSK-A': 10019, # Special ID to handle in update
    'OMSK-B': 10119, # Special ID to handle in update
}

WAVE_NAMES = list(WAVE_MAP.keys())

# --- Ported OMSK Logic ---
PAM4_PATTERNS = []
for i in range(128):
    if i < 16:
        base = np.linspace(-1.0, 1.0, 16)
        if i % 2 == 0:
            PAM4_PATTERNS.append(np.round(base * 3) / 3)
        else:
            PAM4_PATTERNS.append(np.flip(np.round(base * 3) / 3))
    elif i < 64:
        np.random.seed(i)
        pattern = np.random.choice([-1.0, -0.33, 0.33, 1.0], 16)
        PAM4_PATTERNS.append(pattern.tolist())
    else:
        t = np.linspace(0, 1, 16)
        val = np.sin(2 * np.pi * t * (1 + i/32))
        PAM4_PATTERNS.append((np.round(val * 3) / 3).tolist())

def pwm_from_t(t):
    t = np.clip(t, 0.0, 1.0)
    return 0.5 - t * 0.49

def apply_custom_fold(x, shape_val):
    if shape_val <= 0: return x
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
    p = phase % 1.0
    sig = np.where(p < pwm_base, 1.0, -1.0)
    if shape_val <= 0: return sig
    gain = 1.0 + (shape_val / 127.0) * 3.0
    num_cycles = 1 + int(shape_val / 50)
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

def wave_sin(phase): return np.sin(2 * np.pi * phase)
def wave_tri(phase): return 4.0 * np.abs((phase + 0.25) % 1.0 - 0.5) - 1.0
def wave_saw_simple(phase): return 2.0 * (phase % 1.0) - 1.0
def wave_rsaw(phase): return 1.0 - 2.0 * (phase % 1.0)
def wave_pam_raw(phase, pattern_idx):
    idx = int(pattern_idx) % 128
    segment = (np.floor(phase * 16)).astype(int) % 16
    return np.array([PAM4_PATTERNS[idx][s] for s in segment])

def sample_wave_raw_basic(phase, wtype, pwm):
    if wtype == 'SIN': return wave_sin(phase)
    if wtype == 'TRI': return wave_tri(phase)
    if wtype == 'SAW': return wave_saw_simple(phase)
    if wtype == 'RSAW': return wave_rsaw(phase)
    return np.zeros_like(phase)

def sample_wave(phase, wtype, pwm, pam_pattern, shape_val):
    if wtype == 'PAM':
        actual_pattern_idx = (pam_pattern + int(shape_val)) % 128
        return wave_pam_raw(phase, actual_pattern_idx)
    if wtype in ['PULSE', 'SQR']:
        base_pwm = pwm if wtype == 'PULSE' else 0.5
        return apply_pulse_fold(phase, base_pwm, shape_val)
    raw_sig = sample_wave_raw_basic(phase, wtype, pwm)
    return apply_custom_fold(raw_sig, shape_val)

def generate_wave(phase, wave_param, shape_param):
    if wave_param < 32:
        seg = wave_param // 8
        t = float(wave_param % 8) / 7.0
        types = [('SIN', 'TRI'), ('TRI', 'SAW'), ('SAW', 'RSAW'), ('RSAW', 'PULSE')]
        if seg == 2:
            s_saw = sample_wave(phase, 'SAW', 0.5, 0, shape_param)
            phase_rsaw = (phase + 0.5) % 1.0
            s_rsaw = sample_wave(phase_rsaw, 'RSAW', 0.5, 0, shape_param)
            s_tri = sample_wave(phase, 'TRI', 0.5, 0, shape_param)
            if t < 0.5:
                curr_t = t * 2.0
                return (1.0 - curr_t) * s_saw + curr_t * s_tri
            else:
                curr_t = (t - 0.5) * 2.0
                return (1.0 - curr_t) * s_tri + curr_t * s_rsaw
        a_type, b_type = types[seg]
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
        if 85 <= wave_param <= 93 and a_type == 'SIN': phase_a = (phase_a + 0.5) % 1.0
        phase_b = (phase + 0.5) % 1.0
        s_a = sample_wave(phase_a, a_type, pwm_val, pam_val, shape_param)
        s_b = sample_wave(phase_b, b_type, pwm_val, pam_val, shape_param)
        return np.where(phase < 0.5, s_a, s_b)

def normalize_wave(y):
    y_min, y_max = np.min(y), np.max(y)
    if np.abs(y_max - y_min) < 1e-6: return y - y_min
    bias = (y_max + y_min) / 2.0
    y_centered = y - bias
    max_abs = np.max(np.abs(y_centered))
    if max_abs > 1e-6: return y_centered / max_abs
    return y_centered

def bake_omsk_wavetables():
    print("Baking OMSK Wavetables...")
    phase = np.linspace(0, 1.0, 256, endpoint=False)
    all_frames = []
    for i in range(128):
        y = generate_wave(phase, i, 0)
        y = normalize_wave(y)
        pcm = (y * 32767).astype(np.int16)
        all_frames.append(pcm.tobytes())
    data1 = b"".join(all_frames[:64])
    amy.load_sample_bytes(data1, preset=100)
    data2 = b"".join(all_frames[64:])
    amy.load_sample_bytes(data2, preset=101)
    print("Baking complete. Patches 100 & 101 loaded.")

# Bake on startup
bake_omsk_wavetables()


def get_waveform(wave_id, duty, preset):
    """Render 1 period of the waveform using AMY engine."""
    # Reset timebase (16384), synths (262144) and events (65536) to ensure we start at time 0
    # Also set global volume to 10.0 (AMY scales final output by 0.1 internally)
    amy.send(reset=344064, volume=10)
    
    # Ensure velocity is 1 to make the oscillator audible, 
    # and set amp coefficients to only use the constant term (1), ignoring envelopes.
    # We provide all 9 coefficients to be safe.
    amy.send(osc=0, wave=wave_id, freq=FREQ, amp='1,0,0,0,0,0,0,0,0', vel=1, duty=duty, preset=preset, phase=0)
    
    # Render exactly N_SAMPLES
    seconds = N_SAMPLES / SAMPLERATE
    samples = amy.render(seconds)
    
    # amy.render returns (N_SAMPLES, AMY_NCHANS)
    # Take first channel
    if samples.ndim > 1:
        return samples[:, 0]
    return samples


# UI Setup
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
ax.set_xlim(0, N_SAMPLES)
ax.grid(True, alpha=0.3)

x = np.arange(N_SAMPLES)
[line] = ax.plot(x, np.zeros(N_SAMPLES), lw=2, color='#00ffcc')
ax.set_title('AMY Wave Browser')

# Sliders
ax_wave = plt.axes([0.2, 0.20, 0.65, 0.03], facecolor='#333333')
ax_duty = plt.axes([0.2, 0.13, 0.65, 0.03], facecolor='#333333')
ax_preset = plt.axes([0.2, 0.06, 0.65, 0.03], facecolor='#333333')

slider_wave = Slider(ax_wave, 'Wave Type', 0, len(WAVE_NAMES)-1, valinit=len(WAVE_NAMES)-2, valstep=1)
slider_duty = Slider(ax_duty, 'Duty/Shape', 0.0, 1.0, valinit=0.0)
slider_preset = Slider(ax_preset, 'Preset', 0, 1024, valinit=0, valstep=1)

for s in [slider_wave, slider_duty, slider_preset]:
    s.label.set_color('white')
    s.valtext.set_color('white')

def update(val):
    wave_name = WAVE_NAMES[int(slider_wave.val)]
    wave_id = WAVE_MAP[wave_name]
    duty = slider_duty.val
    # Default preset from slider
    preset = int(slider_preset.val)
    
    # Handle custom OMSK waves
    if wave_name == 'OMSK-A':
        wave_id = 19
        preset = 100
    elif wave_name == 'OMSK-B':
        wave_id = 19
        preset = 101
    
    y = get_waveform(wave_id, duty, preset)
    line.set_ydata(y)
    ax.set_title(f'Wave: {wave_name} | Duty/Morph: {duty:.2f} | Preset: {preset}')
    fig.canvas.draw_idle()


slider_wave.on_changed(update)
slider_duty.on_changed(update)
slider_preset.on_changed(update)

# Initial update
update(None)

print("AMY Wave Browser started.")
print(f"Showing exactly 1 period ({N_SAMPLES} samples at {SAMPLERATE}Hz).")
plt.show()
