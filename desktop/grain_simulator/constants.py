import numpy as np

SAMPLE_RATE = 44100
MAX_TOTAL_GRAINS = 128
OLED_WIDTH = 128
OLED_HEIGHT = 64
CONFIG_FILE = "granular_pro_config.json"
UI_FONT_NAME = "Segoe UI"
UI_FONT_SIZE = 12 

def generate_window(shape, length):
    """Generates a window function for grain envelopes."""
    if shape == "Hanning": return np.hanning(length)
    if shape == "Tri": return np.bartlett(length)
    if shape == "Rect": return np.ones(length)
    if shape == "Blackman": return np.blackman(length)
    return np.hanning(length)