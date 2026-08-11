import sys
import os
import json
import numpy as np
import sounddevice as sd
from PySide6.QtWidgets import QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QGridLayout, QFileDialog
from PySide6.QtCore import Qt, QTimer, QEvent, Signal
from constants import SAMPLE_RATE, CONFIG_FILE, UI_FONT_NAME, UI_FONT_SIZE
from audio_engine import GranularEngine
from widgets import Knob, OLED
try:
    import mido
    import threading
    MIDI_AVAILABLE = True
except ImportError:
    MIDI_AVAILABLE = False

os.environ["QT_LOGGING_RULES"] = "qt.qpa.fonts=false"

class Main(QMainWindow):
    midi_signal = Signal(object)
    def __init__(self):
        super().__init__()
        
        self.mod_routes = [{"name": "OFF", "type": "OFF", "src": 0}]
        for s in range(4):
            for d in range(4):
                if s != d: self.mod_routes.append({"name": f"FM V{s+1}>V{d+1}", "type": "FM", "src": s, "dst": d})
        for s in range(4):
            for d in range(4): 
                if s != d: self.mod_routes.append({"name": f"RM V{s+1}*V{d+1}", "type": "RM", "src": s, "dst": d})

        self.engines = [GranularEngine(i) for i in range(4)]
        for eng in self.engines:
            eng.all_engines, eng.MOD_ROUTES = self.engines, self.mod_routes
            eng.params["mod1_src"][2] = eng.params["mod2_src"][2] = len(self.mod_routes) - 1

        self.active_voice, self.active_page = 0, "Grain1"
        self.held_mod, self.mod_interaction_happened = None, False
        self.fn_held = False
        self.disp_mode = "PARAM" # "PARAM" or "GRAIN"
        self.last_activity_ms = 0
        self.SHOW_STATUS_BAR = True 

        self.pages = {
            "Grain1": ["sample_idx", "pos", "size", "dens"],
            "Grain2": ["pitch", "pitch_mode", "max_grains", "grain_amp"],
            "Grain3": ["scan", "direction", "spread", "shape"],
            "Filt":   ["cutoff", "res", "filt_type", "filt_key"],
            "Jit":    [None, None, None, None],
            "LFO1":   ["lfo1_rate", "lfo1_wave", "lfo1_phase", None],
            "LFO2":   ["lfo2_rate", "lfo2_wave", "lfo2_phase", None],
            "EG":     ["atk", "atk_curve", "rel", "rel_curve"],
            "Mod":    ["mod1_src", "mod1_amt", "mod2_src", "mod2_amt"],
            "FX":     ["fx_wf", "fx_ds", "fx_bc", "fx_mix"],
            "Mix":    ["vol", "vol", "vol", "vol"],
            "Sys":    ["viz_scale", "midi_mode", "midi_ch", "master_vol"]
        }
        
        self.init_ui()
        self.load_config()
        self.midi_signal.connect(self.process_midi_msg)
        self.display_timer = QTimer(self)
        self.display_timer.timeout.connect(self.update_display_logic)
        self.display_timer.start(100) # 10Hz check
        
        self.stream = sd.OutputStream(samplerate=SAMPLE_RATE, channels=2, callback=self.audio_callback)
        self.stream.start()

    def audio_callback(self, outdata, frames, time, status):
        outdata.fill(0)
        temp_out = np.zeros((frames, 2))
        for eng in self.engines:
            temp_out += eng.process_audio(frames)
        outdata[:] = np.clip(temp_out, -1.0, 1.0)

    def update_display_logic(self):
        import time
        any_trig = any(e.is_triggered for e in self.engines)
        now = time.time()
        
        # Auto-return to GRAIN view only if something is playing and we are idle
        if any_trig:
            if self.disp_mode == "PARAM" and (now - getattr(self, 'last_activity_time', 0)) > 5.0:
                self.disp_mode = "GRAIN"
            
        self.oled.update()

    def init_ui(self):
        self.setWindowTitle("Granular Master Pro - Modular")
        self.setFixedSize(520, 800)
        self.setStyleSheet("QMainWindow { background: #121214; }")
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(0) 
        
        self.oled = OLED(self)
        layout.addWidget(self.oled, 0, Qt.AlignCenter)
        
        enc_layout = QHBoxLayout()
        self.knobs = []
        for i in range(4):
            k = Knob(f"ENC {i+1}")
            k.id = i
            k.installEventFilter(self)
            k.moved.connect(self.handle_knob_move)
            enc_layout.addWidget(k)
            self.knobs.append(k)
        layout.addLayout(enc_layout)
        
        grid_layout = QGridLayout()
        grid_layout.setSpacing(8)
        self.btns = {}
        grid_map = [
            ["Grain1", "Grain2", "Grain3", "Filt"],
            ["Jit", "LFO1", "LFO2", "EG"],
            ["Mod", "FX", "Mix", "Fn"],
            ["Trig1", "Trig2", "Trig3", "Trig4"]
        ]
        for r in range(4):
            for c in range(4):
                name = grid_map[r][c]
                btn = QPushButton(name.upper())
                btn.setFixedSize(115, 60)
                btn.setStyleSheet(self.get_btn_style(name))
                if name == "Save": btn.clicked.connect(self.save_config)
                else:
                    btn.pressed.connect(lambda n=name: self.on_grid_pressed(n))
                    btn.released.connect(lambda n=name: self.on_grid_released(n))
                grid_layout.addWidget(btn, r, c)
                self.btns[name] = btn
        layout.addLayout(grid_layout)
        
        self.load_btn = QPushButton("IMPORT SAMPLE FOLDER")
        self.load_btn.setFixedHeight(40)
        self.load_btn.setStyleSheet("QPushButton { background: #252528; color: white; border-radius: 4px; font-weight: bold; border: 1px solid #333; } QPushButton:hover { background: #333; }")
        self.load_btn.clicked.connect(self.load_folder_dialog)
        layout.addWidget(self.load_btn)

        from PySide6.QtWidgets import QComboBox, QLabel
        layout.addSpacing(10)
        layout.addWidget(QLabel("MIDI INPUT:", self))
        self.midi_combo = QComboBox()
        self.midi_combo.setFixedHeight(35)
        self.midi_combo.setStyleSheet("QComboBox { background: #1a1a1c; color: white; border: 1px solid #333; padding: 5px; }")
        layout.addWidget(self.midi_combo)
        
        self.init_midi()
        self.update_knobs()

    def get_btn_style(self, name, active=False):
        color, text_color, border = "#252528", "white", "1px solid #333"
        if "Trig" in name:
            idx = int(name[-1]) - 1
            if self.engines[idx].is_triggered: color, text_color = "#00f0ff", "black"
            else: color = "#2a2121" 
            if idx == self.active_voice: border = "2px solid #ffffff"
        else:
            if active or name == self.active_page: color = "#0078d4"
            if name in ["Jit", "LFO1", "LFO2", "EG"] and self.held_mod == name: color = "#ffaa00"
        return f"QPushButton {{ background: {color}; color: {text_color}; border-radius: 6px; font-weight: bold; border: {border}; }}"

    def on_grid_pressed(self, name):
        import time
        now = time.time()
        self.last_activity_time = now

        if name.startswith("Trig"):
            v = int(name[4:]) - 1
            if self.fn_held:
                # Toggle mode if Fn held (latch)
                self.engines[v].is_triggered = not self.engines[v].is_triggered
            else:
                self.engines[v].trigger_on()
            self.active_voice = v
            self.disp_mode = "GRAIN" # Trigger/Note always switches to GRAIN
        else:
            # Page or Function buttons switch to PARAM
            self.disp_mode = "PARAM"
            if name == "Fn":
                self.fn_held = True
                # self.active_page = "Sys" # Optional: switch to Sys on Fn? Firmware doesn't usually do this.
            elif name in ["Jit", "LFO1", "LFO2", "EG"]:
                self.held_mod, self.mod_interaction_happened = name, False
                self.active_page = name
            elif name in self.pages:
                self.active_page = name
            
            self.update_knobs()

        self.update_btn_styles()
        self.update()

    def on_grid_released(self, name):
        if name.startswith("Trig"):
            v = int(name[4:]) - 1
            if not self.fn_held:
                self.engines[v].trigger_off()
        elif name == "Fn":
            self.fn_held = False
        elif name in ["Jit", "LFO1", "LFO2", "EG"]:
            if not self.mod_interaction_happened:
                self.active_page = name
            self.held_mod, self.mod_interaction_happened = None, False
        
        self.update_btn_styles()

    def update_btn_styles(self):
        for k, b in self.btns.items(): b.setStyleSheet(self.get_btn_style(k))

    def handle_knob_move(self, knob_idx, delta):
        p_list = self.pages.get(self.active_page, [None]*4)
        p_key = p_list[knob_idx]
        if not p_key and self.active_page != "Mix": return

        # Match firmware behavior: any encoder move switches to PARAM view
        import time
        self.disp_mode = "PARAM"
        self.last_activity_time = time.time()

        # Global sections: affect all voices
        GLOBAL_SECTIONS = ["Mod", "FX", "Sys", "LFO1", "LFO2"]
        
        if self.active_page == "Mix":
            p = self.engines[knob_idx].params["vol"]
            p[0] = round(max(0, min(1, p[0] + delta * 0.01)), 2)
        else:
            target_engines = self.engines if self.active_page in GLOBAL_SECTIONS else [self.engines[self.active_voice]]
            for eng in target_engines:
                if self.held_mod:
                    p = eng.params[p_key]
                    p[5], p[6] = self.held_mod, max(0, min(1, p[6] + delta * 0.01))
                    self.mod_interaction_happened = True 
                else:
                    p = eng.params[p_key]
                    if p_key in ["sample_idx", "pitch_mode", "filt_type", "lfo1_wave", "lfo2_wave", "mod1_src", "mod2_src", "midi_mode"]:
                        p[0] = max(p[1], min(p[2], int(p[0] + (1 if delta > 0 else -1))))
                        if p_key == "sample_idx": 
                            eng.load_sample()
                    else:
                        range_val = p[2] - p[1]
                        # Uniform sensitivity based on range
                        p[0] = max(p[1], min(p[2], p[0] + delta * 0.01 * range_val))
                self.disp_mode = "PARAM"
                self.last_activity_ms = QTimer.singleShot(0, lambda: None) # Placeholder, we'll use time.time()
                import time
                self.last_activity_time = time.time()
                self.update_knobs()

    def update_knobs(self):
        eng = self.engines[self.active_voice]
        p_list = self.pages.get(self.active_page, [None]*4)
        WAVES = ["SINE", "TRI", "SAW", "S&H"]
        for i in range(4):
            p_key = p_list[i]
            if not p_key:
                self.knobs[i].label = "---"
                self.knobs[i].set_data(0, "", "")
                continue
            if self.active_page == "Mix":
                p = self.engines[i].params["vol"]
                self.knobs[i].label = f"VOICE {i+1}"
            else:
                p = eng.params[p_key]
                self.knobs[i].label = p_key.upper()
            val = p[0]
            if p_key in ["mod1_src", "mod2_src"]: disp = self.mod_routes[int(val)]["name"]
            elif p_key in ["mod1_amt", "mod2_amt", "vol", "pos", "spread", "direction", "grain_amp", "keytrack", "scan", "res", "filt_key", "lfo1_phase", "lfo2_phase"]:
                disp = f"{int(val*100)}%"
            elif p_key in ["size", "atk", "rel"]: disp = f"{val:.3f} s"
            elif p_key == "cutoff": disp = f"{val/1000:.2f}KHZ" if val > 999 else f"{int(val)}HZ"
            elif p_key in ["dens", "lfo1_rate", "lfo2_rate"]: disp = f"{val:.1f} Hz"
            elif p_key == "sample_idx": disp = f"#{int(val) + 1}"
            elif p_key == "pitch": disp = f"{val:.2f} x"
            elif p_key == "shape": disp = str(int(val) + 1)
            elif p_key == "filt_type": disp = eng.FILTERS[max(0, min(3, int(val)))].upper()
            elif "wave" in p_key: disp = WAVES[max(0, min(3, int(val)))]
            elif p_key == "viz_scale": disp = "PITCH" if int(val) == 0 else "PAN"
            elif p_key == "midi_mode": disp = ["V1", "V2", "V3", "V4", "RR", "RND", "OCT"][int(val) % 7]
            else: disp = f"{val:.2f}"
            ratio = (val - p[1]) / (p[2] - p[1]) if p[2] != p[1] else 0
            mod_info = f"{p[5].upper()}: {int(p[6]*100)}%" if p[5] else ""
            self.knobs[i].set_data(ratio, disp, mod_info)

        # UI update finished

    def init_midi(self):
        self.midi_in = None
        if not MIDI_AVAILABLE:
            self.midi_combo.addItems(["MIDI NOT INSTALLED"])
            self.midi_combo.setEnabled(False)
            return
            
        try:
            ports = mido.get_input_names()
        except Exception as e:
            print(f"MIDI Backend Error: {e}")
            self.midi_combo.addItems(["MIDI BACKEND ERROR"])
            self.midi_combo.setEnabled(False)
            return
            
        self.midi_combo.addItems(["NONE"] + ports)
        self.midi_combo.currentTextChanged.connect(self.open_midi_port)
        
        # Mapping from CC to buttons (match config.h / ui_state.h)
        self.cc_map = {
            40: "Grain1", 41: "Grain2", 42: "Grain3", 43: "Filt",
            44: "Jit",    45: "LFO1",   46: "LFO2",   47: "EG",
            48: "Mod",    49: "FX",     50: "Mix",    51: "Fn",
            52: "Trig1",  53: "Trig2",  54: "Trig3",  55: "Trig4"
        }
        # Encoders CC 110-113
        
    def open_midi_port(self, name):
        if not MIDI_AVAILABLE or not name: return
        if self.midi_in: self.midi_in.close()
        if name == "NONE" or name == "MIDI NOT INSTALLED": return
        try:
            self.midi_in = mido.open_input(name, callback=self.midi_callback)
        except Exception as e: print(f"MIDI Error: {e}")

    def midi_callback(self, msg):
        self.midi_signal.emit(msg)

    def process_midi_msg(self, msg):
        if msg.type == 'note_on':
            v = msg.note % 4
            self.on_grid_pressed(f"Trig{v+1}")
        elif msg.type == 'note_off':
            v = msg.note % 4
            self.on_grid_released(f"Trig{v+1}")
        elif msg.type == 'control_change':
            if 110 <= msg.control <= 113:
                # Relative encoder logic from config.h: 63 or less is -, 65 or more is +
                delta = msg.value - 64
                if delta != 0:
                    self.handle_knob_move(msg.control - 110, delta)
            elif msg.control in self.cc_map:
                name = self.cc_map[msg.control]
                if msg.value >= 64: self.on_grid_pressed(name)
                else: self.on_grid_released(name)

    def save_config(self):
        data = {
            "voices": [eng.params for eng in self.engines], 
            "folder": self.engines[0].current_folder,
            "midi_device": self.midi_combo.currentText()
        }
        try:
            with open(CONFIG_FILE, "w") as f: json.dump(data, f)
            if "Save" in self.btns:
                self.btns["Save"].setText("SAVED!")
                QTimer.singleShot(1000, lambda: self.btns["Save"].setText("SAVE"))
        except Exception: pass

    def load_config(self):
        if not os.path.exists(CONFIG_FILE): return
        try:
            with open(CONFIG_FILE, "r") as f: data = json.load(f)
            folder, voice_params = data.get("folder", ""), data.get("voices", [])
            midi_device = data.get("midi_device", "NONE")
            
            for i, eng in enumerate(self.engines):
                if i < len(voice_params):
                    for k, v in voice_params[i].items():
                        if k in eng.params: eng.params[k] = v
                eng.current_folder = folder
                if folder and os.path.exists(folder):
                    eng.file_list = sorted([f for f in os.listdir(folder) if f.lower().endswith(('.wav', '.flac', '.ogg'))])
                    eng.load_sample()
            
            if midi_device and midi_device != "NONE":
                idx = self.midi_combo.findText(midi_device)
                if idx >= 0:
                    self.midi_combo.setCurrentIndex(idx)
                    self.open_midi_port(midi_device)
            
            self.update_knobs()
        except Exception: pass

    def load_folder_dialog(self):
        d = QFileDialog.getExistingDirectory(self, "Select Sample Folder")
        if d:
            files = sorted([f for f in os.listdir(d) if f.lower().endswith(('.wav', '.flac', '.ogg'))])
            for eng in self.engines:
                eng.current_folder, eng.file_list = d, files
                eng.params["sample_idx"][2] = max(0, len(files) - 1)
                eng.load_sample()
            self.update_knobs()

    def closeEvent(self, event):
        """Automatic config save on application close."""
        self.save_config()
        event.accept()

    def eventFilter(self, obj, event):
        if event.type() == QEvent.Wheel and isinstance(obj, Knob):
            delta = event.angleDelta().y() // 120
            self.handle_knob_move(obj.id, delta * 3) 
            return True
        return super().eventFilter(obj, event)

    def keyPressEvent(self, event):
        if event.isAutoRepeat(): return
        txt, key_map = event.text().upper(), {
            "1": "Grain1", "2": "Grain2", "3": "Grain3", "4": "Filt",
            "Q": "Jit", "Й": "Jit", "W": "LFO1", "Ц": "LFO1",
            "E": "LFO2", "У": "LFO2", "R": "EG", "К": "EG",
            "A": "Sys", "Ф": "Sys", "S": "Mod", "Ы": "Mod",
            "D": "Mix", "В": "Mix", "F": "Save", "А": "Save",
            "Z": "Trig1", "Я": "Trig1", "X": "Trig2", "Ч": "Trig2",
            "C": "Trig3", "С": "Trig3", "V": "Trig4", "М": "Trig4"
        }
        if txt in key_map:
            name = key_map[txt]
            if name == "Save": self.save_config()
            else: self.on_grid_pressed(name)

    def keyReleaseEvent(self, event):
        if event.isAutoRepeat(): return
        txt, key_map = event.text().upper(), {
            "Q": "Jit", "Й": "Jit", "W": "LFO1", "Ц": "LFO1", 
            "E": "LFO2", "У": "LFO2", "R": "EG", "К": "EG",
            "Z": "Trig1", "Я": "Trig1", "X": "Trig2", "Ч": "Trig2", 
            "C": "Trig3", "С": "Trig3", "V": "Trig4", "М": "Trig4"
        }
        if txt in key_map: self.on_grid_released(key_map[txt])

if __name__ == "__main__":
    app = QApplication(sys.argv)
    m = Main()
    m.show()
    sys.exit(app.exec())