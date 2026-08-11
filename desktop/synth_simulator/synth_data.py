from PySide6.QtCore import Qt

# Mapping Configuration
KNOB_MAPPINGS = {
    "VCO1": [("Transpose", "transpose"), ("Detune", "detune"), ("Wave", "wave"), ("Shape", "shape")],
    "VCF1": [("Cutoff", "cutoff"), ("Resonance", "resonance"), ("Type", "vcf_type"), ("Mix", "mix")],
    "LFO1": [("Rate", "rate"), ("Smooth", "smooth"), ("Wave", "wave"), ("Shape", "shape")],
    "EG1":  [("Attack", "attack"), ("Decay", "decay"), ("Sustain", "sustain"), ("Release", "release")],
    
    "VCO2": [("Transpose", "transpose"), ("Detune", "detune"), ("Wave", "wave"), ("Shape", "shape")],
    "VCF2": [("Cutoff", "cutoff"), ("Resonance", "resonance"), ("Type", "vcf_type"), ("Mix", "mix")],
    "LFO2": [("Rate", "rate"), ("Smooth", "smooth"), ("Wave", "wave"), ("Shape", "shape")],
    "EG2":  [("Attack", "attack"), ("Decay", "decay"), ("Sustain", "sustain"), ("Release", "release")],
    
    "MIXER": [("VCO1/2 Bal", "vco1_vol"), ("Phase 1-2", "phase2"), ("VCO/Noise", "noise_vol"), ("Master Vol", "vco2_vol")],
    "NOISE": [("Color", "color"), ("-", "unused1"), ("-", "p3"), ("-", "p4")],
    "FX":    [("Time", "time"), ("Feedback", "feedback"), ("Tone", "tone"), ("Mix", "mix")],
    
    "ARP":   [("Rate", "rate"), ("Swing", "swing"), ("Mode", "mode"), ("Oct", "oct_range")],
    "MOD":   [("Mode1", "mode1"), ("Depth1", "depth1"), ("Mode2", "mode2"), ("Depth2", "depth2")],
    "GLIDE": [("Time", "time"), ("Slope", "slope"), ("Mode", "mode"), ("Poly", "polyphony")],
    "HOLD":  [], # No params
    "SET":   [], # Mode
    "RM":    [], # Mode
}

LAYER_COLORS = {
    "LFO1": "#00FFFF", "LFO2": "#0000FF",
    "EG1":  "#FFA500", "EG2":  "#FF0000",
    "VCO1": "#888888", "VCO2": "#888888",
    "VCF1": "#AA00AA", "VCF2": "#00AA00",
    "MIXER": "#888888", "NOISE": "#888888", "FX": "#888888",
    "ARP": "#FFFF00", "GLIDE": "#00FF00", "HOLD": "#FF00FF",
    "MOD": "#00CCFF",
}

# Grid Mapping (Row, Col)
KEY_GRID_MAP = {
    Qt.Key_1: (0, 0), Qt.Key_2: (0, 1), Qt.Key_3: (0, 2), Qt.Key_4: (0, 3),
    Qt.Key_Q: (1, 0), Qt.Key_W: (1, 1), Qt.Key_E: (1, 2), Qt.Key_R: (1, 3),
    Qt.Key_A: (2, 0), Qt.Key_S: (2, 1), Qt.Key_D: (2, 2), Qt.Key_F: (2, 3),
    Qt.Key_Z: (3, 0), Qt.Key_X: (3, 1), Qt.Key_C: (3, 2), Qt.Key_V: (3, 3),
}

# Labels for Modes
LABELS_PIANO = [
    ["C", "C#", "D", "D#"],
    ["E", "F", "F#", "G"],
    ["G#", "A", "A#", "B"],
    ["OCT -", "OCT +", "fn1", "fn2"]
]

LABELS_SETTINGS = [
    ["VCO1", "VCF1", "LFO1", "EG1"],
    ["VCO2", "VCF2", "LFO2", "EG2"],
    ["NOISE", "MOD", "ARP", "SET"],
    ["FX", "MIXER", "GLIDE", "RM"]
]

# Note Frequencies (Base C3=261.63 approx)
# Row 0: C3...
BASE_C = 261.63
NOTE_MAP = {
    (0,0): BASE_C, (0,1): BASE_C * 2**(1/12), (0,2): BASE_C * 2**(2/12), (0,3): BASE_C * 2**(3/12),
    (1,0): BASE_C * 2**(4/12), (1,1): BASE_C * 2**(5/12), (1,2): BASE_C * 2**(6/12), (1,3): BASE_C * 2**(7/12),
    (2,0): BASE_C * 2**(8/12), (2,1): BASE_C * 2**(9/12), (2,2): BASE_C * 2**(10/12), (2,3): BASE_C * 2**(11/12),
}
