import sys
import os
import numpy as np
import sounddevice as sd
import soundfile as sf
import random
import json
os.environ["QT_LOGGING_RULES"] = "qt.qpa.fonts=false"
from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                             QHBoxLayout, QLabel, QFileDialog, QPushButton, 
                             QGridLayout, QStackedWidget, QSlider)
from PySide6.QtCore import Qt, QTimer, QRect
from PySide6.QtGui import QPainter, QColor, QPen, QFont, QFontDatabase

# --- CONSTANTS ---
SAMPLE_RATE = 44100
MAX_GRAINS = 64
OLED_WIDTH = 128
OLED_HEIGHT = 64
REFRESH_RATE = 30 

# --- UTILS ---
def generate_window(shape, length):
    if shape == "Hanning": return np.hanning(length)
    if shape == "Tri": return np.bartlett(length)
    if shape == "Rect": return np.ones(length)
    if shape == "Blackman": return np.blackman(length)
    return np.hanning(length)

# --- FILTER CLASS (SVF) ---
class StateVariableFilter:
    def __init__(self):
        self.ic1eq = 0.0
        self.ic2eq = 0.0

    def process(self, v0, cutoff, res, mode_idx):
        if mode_idx == 3: # Off
            return v0
        
        cutoff = max(20, min(SAMPLE_RATE // 2.1, cutoff))
        res = max(0.01, min(0.99, res))
        
        g = np.tan(np.pi * cutoff / SAMPLE_RATE)
        k = 2.0 - 2.0 * res 
        a1 = 1.0 / (1.0 + g * (g + k))
        a2 = g * a1
        a3 = g * a2

        v3 = v0 - self.ic2eq
        v1 = a1 * self.ic1eq + a2 * v3
        v2 = self.ic2eq + a2 * self.ic1eq + a3 * v3
        self.ic1eq = 2.0 * v1 - self.ic1eq
        self.ic2eq = 2.0 * v2 - self.ic2eq

        if mode_idx == 0: return v2 # LP
        if mode_idx == 1: return v0 - k * v1 - v2 # HP
        if mode_idx == 2: return v1 # BP
        return v0

# --- ENGINE ---
class Grain:
    def __init__(self, sample_data, start_idx, length, pitch, pan, reverse, window, amp):
        self.sample_data = sample_data
        self.start_idx = start_idx
        self.length = int(length)
        self.pitch = max(0.001, pitch) # Guard against zero or negative pitch
        self.pan = pan 
        self.reverse = reverse
        self.amp = amp
        self.window = window
        self.current_frame = 0.0
        self.active = True

    def process(self, frames):
        if not self.active or self.length <= 1:
            return np.zeros((frames, 2))
        
        out = np.zeros((frames, 2))
        data_len = len(self.sample_data)
        
        for i in range(frames):
            if self.current_frame >= self.length - 1:
                self.active = False
                break
            
            rel_pos = self.current_frame * self.pitch
            idx = (self.start_idx - rel_pos if self.reverse else self.start_idx + rel_pos)
            idx_int = int(idx) % data_len
            
            win_idx = int((self.current_frame / self.length) * (len(self.window)-1))
            val = self.sample_data[idx_int] * self.window[win_idx] * self.amp
            
            out[i, 0] = val * (1.0 - self.pan)
            out[i, 1] = val * self.pan
            self.current_frame += 1
            
        return out

    def get_current_pos_ratio(self):
        rel_pos = self.current_frame * self.pitch
        idx = (self.start_idx - rel_pos if self.reverse else self.start_idx + rel_pos)
        return (idx % len(self.sample_data)) / len(self.sample_data)

class GranularEngine:
    SHAPES = ["Hanning", "Tri", "Rect", "Blackman"]
    MAPPINGS = ["Pitch", "Position"]
    FILTERS = ["LP", "HP", "BP", "Off"]
    LFO_WAVES = ["Sine", "Tri", "Saw", "S&H"]
    LFO_DESTS = [
        "None", "Grain Position", "Grain Size", "Grain Density", 
        "Grain Pitch", "Scan Speed", "Filter Cutoff", "Filter Resonance", 
        "Stereo Spread", "Grain Shape", "Reverse Prob", "Jitter Amount", "Master Vol"
    ]
    VIZ_MODES = ["Pitch", "Pan"]

    def __init__(self):
        self.sample_data = np.zeros(SAMPLE_RATE)
        self.active_grains = []
        self.next_grain_time = 0
        
        self.current_folder = ""
        self.file_list = []
        self.sample_idx = 0.0
        self.current_filename = "No file loaded"
        
        # Params
        self.pos = 0.5
        self.size = 0.05
        self.density = 20.0
        self.pitch = 1.0
        self.jitter = {"pos": 0.0, "size": 0.0, "density": 0.0, "pitch": 0.0}
        
        self.scan_speed = 0.0
        self.grain_direction = 0.0
        self.spread = 0.0 
        self.grain_shape_idx = 0.0
        self.mapping_mode_idx = 0.0
        self.viz_mode_idx = 0.0 
        
        self.cutoff = 10000.0
        self.resonance = 0.1
        self.amp_jitter = 0.0
        self.master_vol = 0.5
        self.filter_type_idx = 3.0
        self.amp_attack = 0.1
        self.amp_release = 0.5
        
        self.lfos = [
            {"rate": 1.0, "wave_idx": 0.0, "dest_idx": 0.0, "depth": 0.0, "phase": 0.0, "last_val": 0.0},
            {"rate": 2.0, "wave_idx": 0.0, "dest_idx": 0.0, "depth": 0.0, "phase": 0.0, "last_val": 0.0}
        ]
        
        self.filter_l = StateVariableFilter()
        self.filter_r = StateVariableFilter()
        
        self.playback_pos = 0.0
        self.env_pos = []
        self.current_note = -1
        self.master_env = 0.0 
        
        self.window_lut = {name: generate_window(name, 1024) for name in self.SHAPES}

    def get_state(self):
        return {
            "pos": self.pos,
            "size": self.size,
            "density": self.density,
            "pitch": self.pitch,
            "jitter": self.jitter,
            "scan_speed": self.scan_speed,
            "grain_direction": self.grain_direction,
            "spread": self.spread,
            "grain_shape_idx": self.grain_shape_idx,
            "mapping_mode_idx": self.mapping_mode_idx,
            "viz_mode_idx": self.viz_mode_idx,
            "cutoff": self.cutoff,
            "resonance": self.resonance,
            "amp_jitter": self.amp_jitter,
            "master_vol": self.master_vol,
            "filter_type_idx": self.filter_type_idx,
            "amp_attack": self.amp_attack,
            "amp_release": self.amp_release,
            "lfos": self.lfos,
            "current_folder": self.current_folder,
            "sample_idx": self.sample_idx
        }

    def set_state(self, state):
        try:
            for key, value in state.items():
                if hasattr(self, key):
                    setattr(self, key, value)
            if self.current_folder and os.path.exists(self.current_folder):
                self.load_folder(self.current_folder)
                self.sample_idx = state.get("sample_idx", 0)
                self._load_current_idx()
        except Exception as e:
            print(f"Error restoring state: {e}")

    def load_folder(self, path):
        self.current_folder = path
        try:
            self.file_list = [f for f in os.listdir(path) if f.lower().endswith(('.wav', '.flac', '.ogg'))]
            self.file_list.sort()
            if self.file_list:
                self.sample_idx = 0
                self._load_current_idx()
                return True
        except: pass
        return False

    def _load_current_idx(self):
        if not self.file_list: return
        idx = int(self.sample_idx) % len(self.file_list)
        path = os.path.join(self.current_folder, self.file_list[idx])
        try:
            data, sr = sf.read(path)
            if len(data.shape) > 1: data = data[:, 0]
            
            threshold = 0.02
            mask = np.abs(data) > threshold
            if np.any(mask):
                start_idx = np.where(mask)[0][0]
                end_idx = np.where(mask)[0][-1]
                data = data[start_idx:end_idx + 1]
            
            peak = np.max(np.abs(data))
            if peak > 0:
                data = data / peak
                
            self.sample_data = data.astype(np.float32)
            self.current_filename = self.file_list[idx]
            self._update_envelope()
        except Exception as e:
            print(f"Error loading {path}: {e}")
            self.current_filename = "Error loading file"

    def _update_envelope(self):
        if len(self.sample_data) == 0: return
        step = max(1, len(self.sample_data) // OLED_WIDTH)
        self.env_pos = [np.max(np.abs(self.sample_data[i:i+step])) if i+step < len(self.sample_data) else 0 for i in range(0, len(self.sample_data), step)]
        self.env_pos = self.env_pos[:OLED_WIDTH]

    def get_audio_callback(self, outdata, frames, time, status):
        outdata.fill(0)
        if len(self.sample_data) < 100: return

        # Global Envelope Logic
        target_env = 1.0 if self.current_note != -1 else 0.0
        env_step = frames / SAMPLE_RATE
        if target_env > self.master_env:
            self.master_env = min(target_env, self.master_env + env_step / max(0.001, self.amp_attack))
        else:
            self.master_env = max(target_env, self.master_env - env_step / max(0.001, self.amp_release))

        if self.master_env <= 0 and self.current_note == -1:
            self.active_grains = []
            return

        # LFO Processing
        mods = {d.lower().replace(" ", "_"): 0.0 for d in self.LFO_DESTS}
        for lfo in self.lfos:
            prev_phase = lfo["phase"]
            lfo["phase"] = (lfo["phase"] + lfo["rate"] * frames / SAMPLE_RATE) % 1.0
            val = 0
            w_idx = int(lfo["wave_idx"])
            if w_idx == 0: val = np.sin(2 * np.pi * lfo["phase"])
            elif w_idx == 1: val = 1.0 - 4.0 * np.abs(np.round(lfo["phase"] - 0.5) - (lfo["phase"] - 0.5))
            elif w_idx == 2: val = 2.0 * (lfo["phase"] - 0.5)
            elif w_idx == 3:
                if lfo["phase"] < prev_phase: lfo["last_val"] = random.uniform(-1, 1)
                val = lfo["last_val"]
            
            dest_name = self.LFO_DESTS[int(lfo["dest_idx"])].lower().replace(" ", "_")
            if dest_name in mods: mods[dest_name] += val * lfo["depth"]

        # Scan and position
        cur_scan = self.scan_speed + mods["scan_speed"]
        self.playback_pos = (self.playback_pos + cur_scan * frames / SAMPLE_RATE) % 1.0
        
        # Grain Birth Logic
        self.next_grain_time -= frames
        if self.next_grain_time <= 0 and len(self.active_grains) < MAX_GRAINS:
            # 1. Base Density + LFO
            base_density = self.density + mods["grain_density"] * 50
            cur_density = max(1.0, base_density)
            
            # 2. Calculate interval and apply Jitter.Density (+/-)
            interval = SAMPLE_RATE / cur_density
            # density jitter 1.0 means variation from 0 to 2x interval
            dens_jit_factor = 1.0 + random.uniform(-self.jitter["density"], self.jitter["density"])
            self.next_grain_time = max(10, interval * dens_jit_factor)
            
            # Position + Jitter (+/-)
            p = (self.pos + self.playback_pos + mods["grain_position"])
            p += random.uniform(-self.jitter["pos"], self.jitter["pos"])
            start_idx = int((p % 1.0) * len(self.sample_data))
            
            # Size + Jitter (+/-)
            sz_jit = random.uniform(-self.jitter["size"], self.jitter["size"])
            sz = max(0.001, self.size + mods["grain_size"] + sz_jit)
            
            # Pitch + Jitter (+/-)
            pt_jit = random.uniform(-self.jitter["pitch"], self.jitter["pitch"])
            pt = self.pitch + mods["grain_pitch"] * 2.0 + pt_jit
            if int(self.mapping_mode_idx) == 0 and self.current_note != -1:
                pt *= (2 ** ((self.current_note - 60) / 12.0))
            pt = max(0.01, pt) # Don't let pitch go to 0 or negative
            
            rev_prob = max(0, min(1, self.grain_direction + mods["reverse_prob"]))
            shape_idx = int(max(0, min(len(self.SHAPES)-1, self.grain_shape_idx + mods["grain_shape"] * 3)))
            
            cur_spread = max(0, min(1, self.spread + mods["stereo_spread"]))
            grain_pan = 0.5 + random.uniform(-0.5, 0.5) * cur_spread
            
            # Amp Jitter
            amp_jit_total = self.amp_jitter + mods["jitter_amount"]
            grain_amp = 1.0 + random.uniform(-amp_jit_total, amp_jit_total)
            
            new_g = Grain(self.sample_data, start_idx, sz * SAMPLE_RATE, pt, 
                         grain_pan, 
                         random.random() < rev_prob, 
                         self.window_lut[self.SHAPES[shape_idx]],
                         max(0, grain_amp))
            self.active_grains.append(new_g)

        # Mixdown
        mixed = np.zeros((frames, 2))
        for g in self.active_grains[:]:
            mixed += g.process(frames)
            if not g.active: self.active_grains.remove(g)
            
        cur_vol = max(0, min(1, self.master_vol + mods["master_vol"]))
        
        # Master Filtering
        f_type = int(self.filter_type_idx)
        if f_type != 3:
            f_cutoff = max(20, min(20000, self.cutoff + mods["filter_cutoff"] * 10000))
            f_res = max(0.01, min(0.99, self.resonance + mods["filter_resonance"]))
            for i in range(frames):
                mixed[i, 0] = self.filter_l.process(mixed[i, 0], f_cutoff, f_res, f_type)
                mixed[i, 1] = self.filter_r.process(mixed[i, 1], f_cutoff, f_res, f_type)

        outdata[:] = (mixed * (cur_vol * self.master_env)).astype(np.float32)

# --- GUI ---
class OLED(QWidget):
    def __init__(self, engine):
        super().__init__()
        self.engine = engine
        self.setFixedSize(OLED_WIDTH*4, OLED_HEIGHT*4 + 20)
        QTimer(self, timeout=self.update).start(1000//30)

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        p.fillRect(0, 0, self.width(), self.height(), QColor(10, 10, 15))
        
        p.setPen(QColor(0, 255, 150))
        p.setFont(QFont("Monospace", 10))
        p.drawText(QRect(5, OLED_HEIGHT*4, OLED_WIDTH*4, 20), Qt.AlignLeft | Qt.AlignVCenter, self.engine.current_filename)

        p.scale(4, 4)
        p.setPen(QPen(QColor(200, 200, 200), 1))
        p.drawLine(0, 32, 128, 32)
        
        if self.engine.env_pos:
            p.setPen(QPen(QColor(200, 200, 200), 1))
            # p.setPen(QPen(QColor(100, 200, 255), 1))
            for i in range(len(self.engine.env_pos)-1):
                h1, h2 = int(self.engine.env_pos[i] * 30), int(self.engine.env_pos[i+1] * 30)
                p.drawLine(i, 32-h1, i+1, 32-h2)
                p.drawLine(i, 32+h1, i+1, 32+h2)

        bx = int((self.engine.pos + self.engine.playback_pos) % 1.0 * 128)
        range_px = int(self.engine.jitter["pos"] * 128)
        p.setPen(QPen(QColor(255, 255, 255, 255), 1))
        p.drawLine(max(0, bx - range_px), 0, max(0, bx - range_px), 64)
        p.drawLine(min(127, bx + range_px), 0, min(127, bx + range_px), 64)
        
        p.setPen(Qt.white)
        viz_mode = int(self.engine.viz_mode_idx)
        for g in self.engine.active_grains:
            gx = int(g.get_current_pos_ratio() * 128)
            # Mapping pitch range (0.0 to 4.0) to OLED height (64 to 0 pixels)
            if viz_mode == 0:
                # 0.0 pitch = bottom (64), 4.0 pitch = top (0)
                gy = 64 - int((g.pitch / 4.0) * 64)
            else:
                # Pan visualization (0.0 to 1.0)
                gy = int(g.pan * 64)
                
            gy = max(2, min(62, gy))
            p.drawPoint(gx, gy)

class Keybd(QWidget):
    def __init__(self, engine):
        super().__init__()
        self.engine = engine
        self.setFixedHeight(60)
    
    def paintEvent(self, event):
        p = QPainter(self)
        w = self.width() / 13
        for i in range(13):
            is_black = (i % 12) in [1, 3, 6, 8, 10]
            color = QColor(40, 40, 40) if is_black else QColor(200, 200, 200)
            if self.engine.current_note != -1 and self.engine.current_note % 12 == i % 12: 
                color = QColor(0, 200, 100)
            p.fillRect(i*w, 0, w-1, self.height(), color)

    def mousePressEvent(self, event):
        note = 60 + int(event.position().x() / (self.width() / 13))
        self.engine.current_note = -1 if self.engine.current_note == note else note
        self.update()

class LayerWidget(QWidget):
    def __init__(self, title, params, engine, main_app):
        super().__init__()
        self.params = params
        self.engine = engine
        self.main_app = main_app
        self.sliders, self.labels, self.val_labels = [], [], []
        
        layout = QVBoxLayout(self)
        title_lbl = QLabel(title)
        title_lbl.setStyleSheet("font-weight: bold; color: #aaa;")
        layout.addWidget(title_lbl)
        
        grid = QGridLayout()
        for i, p in enumerate(params):
            l = QLabel(p[0])
            l.setAlignment(Qt.AlignCenter)
            grid.addWidget(l, 0, i)
            self.labels.append(l)
            
            s = QSlider(Qt.Vertical)
            s.setRange(0, 1000)
            s.valueChanged.connect(lambda v, idx=i: self.update_p(idx, v))
            grid.addWidget(s, 1, i)
            self.sliders.append(s)
            
            vl = QLabel("0.0")
            vl.setAlignment(Qt.AlignCenter)
            vl.setStyleSheet("font-size: 10px; color: #0f0;")
            grid.addWidget(vl, 2, i)
            self.val_labels.append(vl)
        layout.addLayout(grid)

    def format_val(self, attr, val):
        if "wave" in attr: return self.engine.LFO_WAVES[int(max(0, min(3, val)))]
        if "dest" in attr: return self.engine.LFO_DESTS[int(max(0, min(len(self.engine.LFO_DESTS)-1, val)))]
        if "shape" in attr: return self.engine.SHAPES[int(max(0, min(3, val)))]
        if "filter_type" in attr: return self.engine.FILTERS[int(max(0, min(3, val)))]
        if "viz_mode" in attr: return self.engine.VIZ_MODES[int(max(0, min(1, val)))]
        if "mapping" in attr: return self.engine.MAPPINGS[int(max(0, min(1, val)))]
        if "sample_idx" in attr: return str(int(val) + 1) if self.engine.file_list else "None"
        
        if "scan_speed" in attr: return f"{val:+.2f}x"
        if "cutoff" in attr: return f"{val/1000:.2f}KHZ" if val > 999 else f"{int(val)}HZ"
        if "jitter" in attr or "spread" in attr or "depth" in attr or attr == "direction": return f"{val*100:.0f}%"
        return f"{val:.2f}"

    def update_ui(self):
        is_fn = self.main_app.fn_active
        for i, p in enumerate(self.params):
            name, attr, vmin, vmax, fn_name, fn_attr, fvmin, fvmax = p
            self.labels[i].setText(fn_name if is_fn and fn_name != "none" else name)
            
            t_attr = fn_attr if is_fn and fn_attr != "none" else attr
            t_min = fvmin if is_fn and fn_attr != "none" else vmin
            t_max = fvmax if is_fn and fn_attr != "none" else vmax
            
            if "." in t_attr:
                parts = t_attr.split(".")
                val = getattr(self.engine, parts[0])[parts[1]]
            elif "lfo_" in t_attr:
                parts = t_attr.split("_")
                # Detect field from the name
                f_base = "_".join(parts[1:-1])
                field = f_base + "_idx" if f_base in ["wave", "dest"] else f_base
                idx = int(parts[-1])
                val = self.engine.lfos[idx][field]
            else:
                val = getattr(self.engine, t_attr, 0.0)
            
            try: val = float(val)
            except: val = 0.0

            ratio = (val - t_min) / (t_max - t_min) if t_max != t_min else 0
            self.sliders[i].blockSignals(True)
            self.sliders[i].setValue(int(ratio * 1000))
            self.sliders[i].blockSignals(False)
            self.val_labels[i].setText(self.format_val(t_attr, val))

    def update_p(self, idx, slider_val):
        is_fn = self.main_app.fn_active
        p = self.params[idx]
        t_attr = p[5] if is_fn and p[5] != "none" else p[1]
        t_min = p[6] if is_fn and p[5] != "none" else p[2]
        t_max = p[7] if is_fn and p[5] != "none" else p[3]
        
        v = t_min + (slider_val/1000.0) * (t_max - t_min)
        
        if "lfo_" in t_attr:
            parts = t_attr.split("_")
            idx_lfo = int(parts[-1])
            f_base = "_".join(parts[1:-1])
            field = f_base + "_idx" if f_base in ["wave", "dest"] else f_base
            self.engine.lfos[idx_lfo][field] = v
        elif "." in t_attr:
            parts = t_attr.split(".")
            getattr(self.engine, parts[0])[parts[1]] = v
        else:
            setattr(self.engine, t_attr, v)
            if t_attr == "sample_idx": self.engine._load_current_idx()
        
        self.val_labels[idx].setText(self.format_val(t_attr, v))

class Main(QMainWindow):
    def __init__(self):
        super().__init__()
        self.engine = GranularEngine()
        self.fn_active = False
        self.init_ui()
        
        self.load_settings()
        
        self.stream = sd.OutputStream(samplerate=SAMPLE_RATE, channels=2, callback=self.engine.get_audio_callback)
        self.stream.start()

    def init_ui(self):
        self.setWindowTitle("Granular Synth DEMO")
        self.setStyleSheet("background: #1a1a1a; color: #ddd; font-family: 'Segoe UI', sans-serif;")
        cw = QWidget()
        self.setCentralWidget(cw)
        layout = QVBoxLayout(cw)
        
        top = QHBoxLayout()
        btn = QPushButton("LOAD FOLDER")
        btn.setFixedSize(120, 40)
        btn.clicked.connect(self.load_folder_dialog)
        top.addWidget(btn)
        top.addStretch()
        top.addWidget(OLED(self.engine))
        top.addStretch()
        layout.addLayout(top)

        self.stack = QStackedWidget()
        layout.addWidget(self.stack)
        
        # (Name, Attr, Min, Max, FnName, FnAttr, FnMin, FnMax)
        l1_p = [("POS", "pos", 0, 1, "J_POS", "jitter.pos", 0, 0.5),
                ("SIZE", "size", 0.01, 0.5, "J_SIZE", "jitter.size", 0, 2),
                ("DENS", "density", 1, 100, "J_DENS", "jitter.density", 0, 2),
                ("PITCH", "pitch", 0.1, 4.0, "J_PTCH", "jitter.pitch", 0.0, 2)]
        
        l2_p = [("SCAN", "scan_speed", -2.0, 2.0, "SAMPLE", "sample_idx", 0, 128),
                ("DIR", "grain_direction", 0.0, 1.0, "none", "none", 0, 0),
                ("SPREAD", "spread", 0.0, 1.0, "VIZ", "viz_mode_idx", 0, 1),
                ("SHAPE", "grain_shape_idx", 0, 27, "MAP", "mapping_mode_idx", 0, 1)]
        
        l3_p = [("CUT", "cutoff", 20, 20000, "TYPE", "filter_type_idx", 0, 3),
                ("RES", "resonance", 0, 1, "none", "none", 0, 0),
                ("J_VOL", "amp_jitter", 0, 1, "ATK", "amp_attack", 0.001, 2),
                ("VOL", "master_vol", 0, 1, "REL", "amp_release", 0.001, 5)]
        
        l4_p = [("LFO1_RATE", "lfo_rate_0", 0.1, 30, "DEST1", "lfo_dest_0", 0, 12),
                ("LFO1_WAVE", "lfo_wave_0", 0, 3, "DEPTH1", "lfo_depth_0", 0, 1),
                ("LFO2_RATE", "lfo_rate_1", 0.1, 30, "DEST2", "lfo_dest_1", 0, 12),
                ("LFO2_WAVE", "lfo_wave_1", 0, 3, "DEPTH2", "lfo_depth_1", 0, 1)]
        
        self.layer_widgets = []
        for title, p in [("L1: CORE", l1_p), ("L2: SCAN & SHAPE", l2_p), ("L3: MIX & AMP", l3_p), ("L4: MODULATION", l4_p)]:
            w = LayerWidget(title, p, self.engine, self)
            self.layer_widgets.append(w)
            self.stack.addWidget(w)
        block_name = ["Grain", "Move", "Filt", "LFO"]
        btns = QHBoxLayout()
        for i in range(4):
            b = QPushButton(f"{block_name[i]}")
            b.clicked.connect(lambda ch, idx=i: self.stack.setCurrentIndex(idx))
            btns.addWidget(b)
        layout.addLayout(btns)
        
        layout.addWidget(Keybd(self.engine))
        
        self.fn_btn = QPushButton("SHIFT / FN (TOGGLE)")
        self.fn_btn.setCheckable(True)
        self.fn_btn.setFixedHeight(40)
        self.fn_btn.toggled.connect(self.set_fn)
        layout.addWidget(self.fn_btn)
        
        self.set_fn(False)

    def save_settings(self):
        """Saves current state to settings.json file."""
        state = {
            "engine": self.engine.get_state(),
            "ui": {
                "active_tab": self.stack.currentIndex()
            }
        }
        try:
            with open("settings.json", "w", encoding="utf-8") as f:
                json.dump(state, f, indent=4)
        except Exception as e:
            print(f"Failed to save settings: {e}")

    def load_settings(self):
        """Loads state from settings.json file."""
        if os.path.exists("settings.json"):
            try:
                with open("settings.json", "r", encoding="utf-8") as f:
                    state = json.load(f)
                
                # Restore engine state
                if "engine" in state:
                    self.engine.set_state(state["engine"])
                
                # Restore UI state
                if "ui" in state:
                    self.stack.setCurrentIndex(state["ui"].get("active_tab", 0))
                
                # Update all sliders across all layer widgets
                for w in self.layer_widgets:
                    w.update_ui()
                    
            except Exception as e:
                print(f"Failed to load settings: {e}")

    def set_fn(self, active): 
        self.fn_active = active
        self.fn_btn.setStyleSheet("background: #d33; color: white; font-weight: bold;" if active else "background: #444;")
        for w in self.layer_widgets: w.update_ui()

    def load_folder_dialog(self):
        d = QFileDialog.getExistingDirectory(self, "Select Folder with WAVs")
        if d: 
            if self.engine.load_folder(d):
                max_f = max(0, len(self.engine.file_list) - 1)
                # Update parameter tuple for Block 2, Slider 1
                # Structure: (Name, Attr, Min, Max, FnName, FnAttr, FnMin, FnMax)
                p = list(self.layer_widgets[1].params[0])
                p[7] = max_f  # Set sample count as FnMax
                self.layer_widgets[1].params[0] = tuple(p)
                
                self.set_fn(self.fn_active) 

    def closeEvent(self, event):
        self.save_settings() # Save settings before exiting
        self.stream.stop()
        super().closeEvent(event)

if __name__ == "__main__":
    app = QApplication(sys.argv)
    m = Main()
    m.show()
    sys.exit(app.exec())