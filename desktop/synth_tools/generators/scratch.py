import numpy as np
def wave_saw_simple(phase):
    return 2.0 * ((phase + 0.75) % 1.0) - 1.0
def wave_rsaw(phase):
    return 1.0 - 2.0 * ((phase + 0.75) % 1.0)
from wave_app import wave_sin, wave_tri
phase = np.linspace(0, 1.0, 1024, endpoint=False)
import wave_app
wave_app.wave_saw_simple = wave_saw_simple
wave_app.wave_rsaw = wave_rsaw
for w in range(32):
    y = wave_app.generate_wave(phase, w, 0)
    print(f'w={w}, max={np.max(np.abs(y)):.3f}')
