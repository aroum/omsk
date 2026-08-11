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
                             QGridLayout, QStackedWidget, QSlider, QFrame)
from PySide6.QtCore import Qt, QTimer, QRect
from PySide6.QtGui import QPainter, QColor, QPen, QFont

SAMPLE_RATE = 44100
MAX_GRAINS = 64
OLED_WIDTH = 128
OLED_HEIGHT = 64
CONFIG_FILE = "granular_config.json"

def generate_window(shape, length):
    if shape == "Hanning": return np.hanning(length)
    if shape == "Tri": return np.bartlett(length)
    if shape == "Rect": return np.ones(length)
    if shape == "Blackman": return np.blackman(length)
    return np.hanning(length)

class StateVariableFilter:
    def __init__(self):
        self.ic1eq = 0.0
        self.ic2eq = 0.0

    def process(self, v0, cutoff, res, mode_idx):
        if mode_idx == 3: return v0
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

class Grain:
    def __init__(self, sample_data, start_idx, length, pitch, pan, reverse, window, amp):
        self.sample_data = sample_data
        self.start_idx = start_idx
        self.length = int(length)
        self.pitch = max(0.001, pitch)
        self.pan = pan 
        self.reverse = reverse
        self.amp = amp
        self.window = window
        self.current_frame = 0.0
        self.active = True

    def get_current_sample_idx(self):
        """Calculates the current position of the grain in the sample data for visualization."""
        rel_pos = self.current_frame * self.pitch
        idx = (self.start_idx - rel_pos if self.reverse else self.start_idx + rel_pos)
        return int(idx) % len(self.sample_data)

    def process(self, frames):
        if not self.active or self.length <= 1: return np.zeros((frames, 2))
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

class GranularEngine:
    SHAPES = ["Hanning", "Tri", "Rect", "Blackman"]
    FILTERS = ["LP", "HP", "BP", "Off"]
    MAPPINGS = ["Fixed", "Velo"]
    VIZ_MODES = ["Pitch", "Pan"]
    WAVES = ["Sine", "Tri", "Saw", "S&H"]
    LFO_DESTS = ["None", "Pos", "Size", "Dens", "Pitch", "Scan", "Cutoff", "Res", "Spread", "Shape"]

    def __init__(self):
        self.sample_data = np.zeros(SAMPLE_RATE)
        self.active_grains = []
        self.next_grain_time = 0
        self.current_folder = ""
        self.file_list = []
        self.sample_idx = 0.0
        self.current_filename = "No Sample"
        
        self.pos, self.size, self.density, self.pitch = 0.5, 0.05, 20.0, 1.0
        self.jitter = {"pos": 0.0, "size": 0.0, "density": 0.0, "pitch": 0.0}
        self.scan_speed, self.grain_direction, self.spread = 0.0, 0.0, 0.0
        self.grain_shape_idx, self.viz_mode_idx = 0.0, 0.0
        self.mapping_mode_idx = 0.0 
        self.cutoff, self.resonance, self.amp_jitter, self.master_vol = 10000.0, 0.1, 0.0, 0.5
        self.filter_type_idx, self.amp_attack, self.amp_release = 3.0, 0.05, 0.3
        
        self.lfos = [{"rate": 1.0, "wave_idx": 0.0, "dest_idx": 0.0, "depth": 0.0, "phase": 0.0} for _ in range(2)]
        self.filter_l, self.filter_r = StateVariableFilter(), StateVariableFilter()
        self.playback_pos, self.env_pos, self.is_triggered, self.master_env = 0.0, [], False, 0.0
        self.window_lut = {name: generate_window(name, 1024) for name in self.SHAPES}

    def _load_current_idx(self):
        if not self.file_list: return
        idx = int(self.sample_idx) % len(self.file_list)
        path = os.path.join(self.current_folder, self.file_list[idx])
        try:
            data, sr = sf.read(path)
            if len(data.shape) > 1: data = data[:, 0]
            mask = np.abs(data) > 0.02
            if np.any(mask):
                indices = np.where(mask)[0]
                data = data[indices[0]:indices[-1]]
            peak = np.max(np.abs(data))
            if peak > 0: data = data / peak
            self.sample_data = data.astype(np.float32)
            self.current_filename = self.file_list[idx]
            step = max(1, len(self.sample_data) // OLED_WIDTH)
            self.env_pos = [np.max(np.abs(self.sample_data[i:i+step])) for i in range(0, len(self.sample_data), step)][:OLED_WIDTH]
        except: self.current_filename = "Error"

    def process_audio(self, frames):
        out = np.zeros((frames, 2))
        if len(self.sample_data) < 100: return out
        target_env = 1.0 if self.is_triggered else 0.0
        env_step = frames / SAMPLE_RATE
        if target_env > self.master_env: self.master_env = min(target_env, self.master_env + env_step / max(0.001, self.amp_attack))
        else: self.master_env = max(target_env, self.master_env - env_step / max(0.001, self.amp_release))
        if self.master_env <= 0 and not self.is_triggered:
            self.active_grains = []
            return out
        mods = {d.lower(): 0.0 for d in self.LFO_DESTS}
        for lfo in self.lfos:
            lfo["phase"] = (lfo["phase"] + lfo["rate"] * frames / SAMPLE_RATE) % 1.0
            val = np.sin(2 * np.pi * lfo["phase"]) if lfo["wave_idx"] == 0 else 2.0*(lfo["phase"]-0.5)
            dest = self.LFO_DESTS[int(lfo["dest_idx"])].lower()
            if dest in mods: mods[dest] += val * lfo["depth"]
        self.playback_pos = (self.playback_pos + (self.scan_speed + mods["scan"]) * frames / SAMPLE_RATE) % 1.0
        self.next_grain_time -= frames
        if self.next_grain_time <= 0 and len(self.active_grains) < MAX_GRAINS:
            dens = max(1.0, self.density + mods["dens"] * 50)
            self.next_grain_time = max(10, (SAMPLE_RATE / dens) * (1.0 + random.uniform(-self.jitter["density"], self.jitter["density"])))
            p = (self.pos + self.playback_pos + mods["pos"] + random.uniform(-self.jitter["pos"], self.jitter["pos"])) % 1.0
            sz = max(0.001, self.size + mods["size"] + random.uniform(-self.jitter["size"], self.jitter["size"]))
            pt = max(0.01, self.pitch + mods["pitch"] + random.uniform(-self.jitter["pitch"], self.jitter["pitch"]))
            new_g = Grain(self.sample_data, int(p * len(self.sample_data)), sz * SAMPLE_RATE, pt, 
                         0.5 + random.uniform(-0.5, 0.5) * max(0, min(1, self.spread + mods["spread"])), 
                         random.random() < self.grain_direction, self.window_lut[self.SHAPES[int(self.grain_shape_idx)]], 1.0)
            self.active_grains.append(new_g)
        mixed = np.zeros((frames, 2))
        for g in self.active_grains[:]:
            mixed += g.process(frames)
            if not g.active: self.active_grains.remove(g)
        f_type = int(self.filter_type_idx)
        if f_type != 3:
            cut = max(20, min(20000, self.cutoff + mods["cutoff"] * 10000))
            for i in range(frames):
                mixed[i, 0] = self.filter_l.process(mixed[i, 0], cut, self.resonance, f_type)
                mixed[i, 1] = self.filter_r.process(mixed[i, 1], cut, self.resonance, f_type)
        return (mixed * (self.master_vol * self.master_env)).astype(np.float32)

    def to_dict(self):
        return {
            "current_folder": self.current_folder, "sample_idx": self.sample_idx,
            "pos": self.pos, "size": self.size, "density": self.density, "pitch": self.pitch,
            "jitter": self.jitter, "scan_speed": self.scan_speed, "grain_direction": self.grain_direction,
            "spread": self.spread, "grain_shape_idx": self.grain_shape_idx, "viz_mode_idx": self.viz_mode_idx,
            "mapping_mode_idx": self.mapping_mode_idx, "cutoff": self.cutoff, "resonance": self.resonance,
            "amp_jitter": self.amp_jitter, "master_vol": self.master_vol, "filter_type_idx": self.filter_type_idx,
            "amp_attack": self.amp_attack, "amp_release": self.amp_release, "lfos": self.lfos
        }

    def from_dict(self, d):
        for key, value in d.items():
            if hasattr(self, key): setattr(self, key, value)
        if self.current_folder and os.path.exists(self.current_folder):
            self.file_list = sorted([f for f in os.listdir(self.current_folder) if f.lower().endswith(('.wav', '.flac', '.ogg'))])
            self._load_current_idx()

class OLED(QWidget):
    def __init__(self, main_app):
        super().__init__()
        self.main_app = main_app
        self.setFixedSize(OLED_WIDTH*4 + 16, OLED_HEIGHT*4 + 46) 
        QTimer(self, timeout=self.update).start(1000//30)

    def paintEvent(self, event):
        p = QPainter(self)
        engine = self.main_app.engines[self.main_app.active_idx]
        p.fillRect(8, 8, OLED_WIDTH*4, OLED_HEIGHT*4, QColor(10, 10, 12))
        p.save()
        p.translate(8, 8); p.scale(4, 4)
        
        # Draw waveform envelope
        if engine.env_pos:
            p.setPen(QPen(QColor(255, 255, 255), 1))
            for i in range(len(engine.env_pos)-1):
                p.drawLine(i, 32-int(engine.env_pos[i]*30), i+1, 32-int(engine.env_pos[i+1]*30))
                p.drawLine(i, 32+int(engine.env_pos[i]*30), i+1, 32+int(engine.env_pos[i+1]*30))
        
        # Playhead scan indicator
        center_x = int((engine.pos + engine.playback_pos) % 1.0 * 128)
        jitter_px = int(engine.jitter["pos"] * 128)
        p.setPen(QPen(QColor(255, 255, 255, 150), 1))
        p.drawLine(max(0, center_x - jitter_px), 0, max(0, center_x - jitter_px), 64)
        p.drawLine(min(127, center_x + jitter_px), 0, min(127, center_x + jitter_px), 64)
        
        # Render MOVING Grains based on their real-time sample index
        data_len = len(engine.sample_data)
        if data_len > 0:
            for g in engine.active_grains:
                # Calculate X based on real-time sample position
                gx = int((g.get_current_sample_idx() / data_len) * 128)
                # Height represents pitch
                gy = 64 - int((g.pitch / 4.0) * 64)
                # Visual fade based on lifespan (optional logic, but here we just show movement)
                p.setPen(QPen(QColor(255, 255, 255), 1))
                p.drawPoint(gx, max(2, min(62, gy)))
        
        p.restore()
        p.setPen(QPen(QColor(60, 60, 65), 8))
        p.drawRect(4, 4, self.width()-8, self.height()-8)
        p.setPen(QColor("#aaa"))
        p.setFont(QFont("Segoe UI", 10, QFont.Bold))
        p.drawText(QRect(0, OLED_HEIGHT*4 + 12, self.width(), 25), Qt.AlignCenter, engine.current_filename.upper())

class TriggerPad(QFrame):
    def __init__(self, idx, main_app):
        super().__init__()
        self.idx, self.main_app = idx, main_app
        self.setMinimumHeight(80) 
        self.setCursor(Qt.PointingHandCursor)
        QTimer(self, timeout=self.update).start(50)

    def mousePressEvent(self, event):
        self.main_app.set_active_engine(self.idx)

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        engine = self.main_app.engines[self.idx]
        is_active = self.main_app.active_idx == self.idx
        bg = QColor(200, 50, 50) if engine.is_triggered else (QColor(80, 80, 85) if is_active else QColor(45, 45, 48))
        p.setBrush(bg); p.setPen(QPen(QColor(255, 255, 255) if is_active else QColor(30, 30, 32), 1))
        p.drawRoundedRect(2, 2, self.width()-4, self.height()-4, 8, 8)
        p.setPen(Qt.white); p.setFont(QFont("Arial", 11, QFont.Bold))
        p.drawText(self.rect(), Qt.AlignCenter, f"VOICE {self.idx+1}")

class LayerWidget(QWidget):
    def __init__(self, params, main_app):
        super().__init__()
        self.params, self.main_app = params, main_app
        self.sliders, self.labels, self.val_labels = [], [], []
        l = QVBoxLayout(self); grid = QGridLayout(); grid.setSpacing(10)
        for i, p in enumerate(params):
            lbl = QLabel(p[0]); lbl.setAlignment(Qt.AlignCenter); lbl.setFixedWidth(80)
            lbl.setStyleSheet("color: #aaa; font-weight: bold; font-family: 'Segoe UI', Arial; font-size: 12px;")
            grid.addWidget(lbl, 0, i); self.labels.append(lbl)
            s = QSlider(Qt.Vertical); s.setRange(0, 1000); s.setFixedHeight(180)
            s.valueChanged.connect(lambda v, idx=i: self.on_slider(idx, v))
            grid.addWidget(s, 1, i, Qt.AlignCenter); self.sliders.append(s)
            vl = QLabel("0"); vl.setAlignment(Qt.AlignCenter)
            vl.setStyleSheet("color: #aaa; font-weight: bold; font-family: 'Segoe UI', Arial; font-size: 12px;")
            grid.addWidget(vl, 2, i); self.val_labels.append(vl)
        l.addLayout(grid)

    def update_ui(self):
        eng = self.main_app.engines[self.main_app.active_idx]
        is_fn = self.main_app.fn_active
        for i, p in enumerate(self.params):
            name, attr, vmin, vmax, vstep, fn_name, fn_attr, fvmin, fvmax, fstep = p
            t_attr = fn_attr if is_fn and fn_attr != "none" else attr
            t_min = fvmin if is_fn and fn_attr != "none" else vmin
            t_max = fvmax if is_fn and fn_attr != "none" else vmax
            val = self._get_val(eng, t_attr)
            if t_attr == "sample_idx": t_max = max(1, len(eng.file_list) - 1) if eng.file_list else 1
            ratio = (val - t_min) / (t_max - t_min) if t_max != t_min else 0
            self.sliders[i].blockSignals(True)
            self.sliders[i].setValue(int(ratio * 1000))
            self.sliders[i].blockSignals(False)
            self.labels[i].setText(fn_name if is_fn and fn_name != "none" else name)
            self.val_labels[i].setText(self._format_val(eng, t_attr, val))

    def _get_val(self, obj, path):
        parts = path.split('.')
        for p in parts:
            if isinstance(obj, list): obj = obj[int(p)]
            elif isinstance(obj, dict): obj = obj[p]
            else: obj = getattr(obj, p)
        return obj

    def _format_val(self, eng, attr, val):
        if "size" in attr: return f"{val:.3f} s"
        if "density" in attr: return f"{val:.1f} Hz"
        if any(x in attr for x in ["pos", "spread", "direction", "jitter", "depth", "master_vol"]): return f"{val*100:.0f}%"
        if "cutoff" in attr or "rate" in attr: return f"{val:.1f} Hz"
        if "attack" in attr or "release" in attr: return f"{val:.3f} s"
        if "sample_idx" in attr: return f"#{int(val) + 1}"
        if "wave_idx" in attr: return eng.WAVES[int(val)]
        if "dest_idx" in attr: return eng.LFO_DESTS[int(val)]
        if "filter_type" in attr: return eng.FILTERS[int(val)]
        if "grain_shape" in attr: return eng.SHAPES[int(val)]
        if "viz_mode" in attr: return eng.VIZ_MODES[int(val)]
        if "mapping" in attr: return eng.MAPPINGS[int(val)]
        return f"{val:.2f}"

    def on_slider(self, idx, v):
        eng = self.main_app.engines[self.main_app.active_idx]
        is_fn = self.main_app.fn_active
        p = self.params[idx]
        t_attr = p[6] if is_fn and p[6] != "none" else p[1]
        t_min = p[7] if is_fn and p[6] != "none" else p[2]
        t_max = p[8] if is_fn and p[6] != "none" else p[3]
        t_step = p[9] if is_fn and p[6] != "none" else p[4]
        if t_attr == "sample_idx": t_max = max(1, len(eng.file_list) - 1) if eng.file_list else 1
        raw_val = t_min + (v/1000.0) * (t_max - t_min)
        val = round(raw_val / t_step) * t_step
        parts = t_attr.split('.'); curr = eng
        for p_name in parts[:-1]:
            if isinstance(curr, list): curr = curr[int(p_name)]
            elif isinstance(curr, dict): curr = curr[p_name]
            else: curr = getattr(curr, p_name)
        last = parts[-1]
        if isinstance(curr, dict): curr[last] = val
        else: setattr(curr, last, val)
        if t_attr == "sample_idx": 
            eng._load_current_idx()
            self.main_app.oled.update()
        self.val_labels[idx].setText(self._format_val(eng, t_attr, val))

class Main(QMainWindow):
    def __init__(self):
        super().__init__()
        self.engines = [GranularEngine() for _ in range(4)]
        self.active_idx = 0
        self.fn_active = False
        self.init_ui()
        self.load_config()
        self.stream = sd.OutputStream(samplerate=SAMPLE_RATE, channels=2, callback=self.audio_callback)
        self.stream.start()

    def audio_callback(self, outdata, frames, time, status):
        outdata.fill(0)
        for eng in self.engines: outdata += eng.process_audio(frames)

    def keyPressEvent(self, event):
        if event.isAutoRepeat(): return
        if event.key() == Qt.Key_1: self.engines[0].is_triggered = True
        elif event.key() == Qt.Key_2: self.engines[1].is_triggered = True
        elif event.key() == Qt.Key_3: self.engines[2].is_triggered = True
        elif event.key() == Qt.Key_4: self.engines[3].is_triggered = True
        super().keyPressEvent(event)

    def keyReleaseEvent(self, event):
        if event.isAutoRepeat(): return
        if event.key() == Qt.Key_1: self.engines[0].is_triggered = False
        elif event.key() == Qt.Key_2: self.engines[1].is_triggered = False
        elif event.key() == Qt.Key_3: self.engines[2].is_triggered = False
        elif event.key() == Qt.Key_4: self.engines[3].is_triggered = False
        super().keyReleaseEvent(event)

    def save_config(self):
        data = [eng.to_dict() for eng in self.engines]
        try:
            with open(CONFIG_FILE, "w") as f: json.dump(data, f)
        except Exception as e: print(f"Error saving config: {e}")

    def load_config(self):
        if os.path.exists(CONFIG_FILE):
            try:
                with open(CONFIG_FILE, "r") as f:
                    data = json.load(f)
                    for i, d in enumerate(data):
                        if i < len(self.engines): self.engines[i].from_dict(d)
                self.set_active_engine(0)
            except Exception as e: print(f"Error loading config: {e}")

    def closeEvent(self, event):
        self.save_config()
        super().closeEvent(event)

    def init_ui(self):
        self.setWindowTitle("Granular Master")
        self.setStyleSheet("""
            QMainWindow { background: #222225; }
            QPushButton { 
                background: #3c3c3c; color: #eee; border-radius: 8px; 
                font-weight: bold; font-family: 'Segoe UI', Arial; border: 1px solid #333;
            }
            QPushButton:hover { background: #4a4a4a; }
            QLabel { font-family: 'Segoe UI', Arial; }
        """)
        cw = QWidget(); self.setCentralWidget(cw)
        layout = QVBoxLayout(cw); layout.setContentsMargins(30, 20, 30, 20); layout.setSpacing(15)
        self.oled = OLED(self); layout.addWidget(self.oled, 0, Qt.AlignCenter)
        self.stack = QStackedWidget(); layout.addWidget(self.stack)

        l1_p = [("POS", "pos", 0, 1, 0.001, "J_POS", "jitter.pos", 0, 0.5, 0.001),
                ("SIZE", "size", 0.005, 0.5, 0.001, "J_SIZE", "jitter.size", 0, 0.5, 0.001),
                ("DENS", "density", 0.1, 100, 0.1, "J_DENS", "jitter.density", 0, 1.0, 0.01),
                ("PITCH", "pitch", 0.1, 4.0, 0.01, "J_PTCH", "jitter.pitch", 0.0, 1.0, 0.01)]
        l2_p = [("SCAN", "scan_speed", -2.0, 2.0, 0.01, "SAMPLE", "sample_idx", 0, 128, 1),
                ("DIR", "grain_direction", 0, 1, 1, "none", "none", 0, 0, 1),
                ("SPREAD", "spread", 0, 1, 0.01, "VIZ", "viz_mode_idx", 0, 1, 1),
                ("SHAPE", "grain_shape_idx", 0, 3, 1, "MAP", "mapping_mode_idx", 0, 1, 1)]
        l3_p = [("CUT", "cutoff", 20, 20000, 1, "TYPE", "filter_type_idx", 0, 3, 1),
                ("RES", "resonance", 0, 1, 0.01, "none", "none", 0, 0, 1),
                ("ATK", "amp_attack", 0.001, 2, 0.001, "J_VOL", "amp_jitter", 0, 1, 0.01),
                ("VOL", "master_vol", 0, 1, 0.01, "REL", "amp_release", 0.001, 5, 0.001)]
        l4_p = [("L1_RATE", "lfos.0.rate", 0.1, 30, 0.1, "DEST 1", "lfos.0.dest_idx", 0, 10, 1),
                ("L1_WAVE", "lfos.0.wave_idx", 0, 3, 1, "DEP 1", "lfos.0.depth", 0, 1, 0.01),
                ("L2_RATE", "lfos.1.rate", 0.1, 30, 0.1, "DEST 2", "lfos.1.dest_idx", 0, 10, 1),
                ("L2_WAVE", "lfos.1.wave_idx", 0, 3, 1, "DEP 2", "lfos.1.depth", 0, 1, 0.01)]
        
        self.layers = []
        for p in [l1_p, l2_p, l3_p, l4_p]:
            w = LayerWidget(p, self); self.layers.append(w); self.stack.addWidget(w)

        nav_layout = QHBoxLayout(); nav_layout.setSpacing(10)
        self.nav_btns = []
        for i, n in enumerate(["Grain", "Move", "Filt", "Mod"]):
            b = QPushButton(n.upper()); b.setFixedHeight(40)
            b.clicked.connect(lambda ch, idx=i: self.switch_layer(idx))
            nav_layout.addWidget(b); self.nav_btns.append(b)
        layout.addLayout(nav_layout)

        pads_layout = QHBoxLayout(); pads_layout.setSpacing(10)
        for i in range(4):
            pad = TriggerPad(i, self); pads_layout.addWidget(pad)
        layout.addLayout(pads_layout)

        bottom_layout = QHBoxLayout(); bottom_layout.setSpacing(10)
        self.fn_btn = QPushButton("SHIFT / FN"); self.fn_btn.setCheckable(True)
        self.fn_btn.setFixedHeight(45); self.fn_btn.toggled.connect(self.set_fn)
        bottom_layout.addWidget(self.fn_btn, 2)
        self.load_btn = QPushButton("LOAD FOLDER"); self.load_btn.setFixedHeight(45)
        self.load_btn.clicked.connect(self.load_folder_dialog)
        bottom_layout.addWidget(self.load_btn, 2)
        layout.addLayout(bottom_layout)
        self.switch_layer(0)

    def switch_layer(self, idx):
        self.stack.setCurrentIndex(idx)
        for i, b in enumerate(self.nav_btns):
            b.setStyleSheet("background: #0078d4; border: 1px solid #005a9e;" if i == idx else "")

    def set_fn(self, active):
        self.fn_active = active
        self.fn_btn.setStyleSheet("background: #e81123; font-weight: bold;" if active else "")
        for l in self.layers: l.update_ui()

    def set_active_engine(self, idx):
        self.active_idx = idx
        for l in self.layers: l.update_ui()
        self.oled.update()

    def load_folder_dialog(self):
        d = QFileDialog.getExistingDirectory(self, "Select Sample Folder")
        if d:
            for eng in self.engines:
                eng.current_folder = d
                eng.file_list = sorted([f for f in os.listdir(d) if f.lower().endswith(('.wav', '.flac', '.ogg'))])
                eng._load_current_idx()
            self.set_active_engine(self.active_idx)

if __name__ == "__main__":
    app = QApplication(sys.argv)
    m = Main(); m.show()
    sys.exit(app.exec())