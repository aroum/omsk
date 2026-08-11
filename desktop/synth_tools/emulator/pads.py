import sys
import json
import os
import math
import mido
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QGridLayout, QPushButton,
    QVBoxLayout, QHBoxLayout, QLabel, QSpinBox, QGroupBox, QComboBox,
    QScrollArea, QMessageBox, QDial
)
from PySide6.QtCore import Qt, Signal, QObject, QRectF, QPointF
from PySide6.QtGui import QPainter, QPen, QColor

# Keyboard keys configuration for the 4x4 grid (supporting both English and Russian layouts)
KEY_MAP = [
    [(Qt.Key_1, None), (Qt.Key_2, None), (Qt.Key_3, None), (Qt.Key_4, None)],
    [(Qt.Key_Q, 1049), (Qt.Key_W, 1062), (Qt.Key_E, 1059), (Qt.Key_R, 1050)],
    [(Qt.Key_A, 1060), (Qt.Key_S, 1067), (Qt.Key_D, 1042), (Qt.Key_F, 1040)],
    [(Qt.Key_Z, 1071), (Qt.Key_X, 1063), (Qt.Key_C, 1057), (Qt.Key_V, 1052)]
]

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_FILE = os.path.join(SCRIPT_DIR, "pad_config.json")

class MidiManager(QObject):
    """Manages MIDI output connection and error handling."""
    def __init__(self):
        super().__init__()
        self.output = None
        self.current_port_name = None
        self.available_ports = []
        try:
            self.available_ports = mido.get_output_names()
        except Exception as e:
            print(f"Warning: Failed to get MIDI output ports. Error: {e}")

    def open_port(self, name):
        if not name:
            return False
        if self.output:
            self.output.close()
        try:
            self.output = mido.open_output(name)
            self.current_port_name = name
            return True
        except Exception as e:
            print(f"Failed to open MIDI port '{name}': {e}")
            return False

    def send_cc(self, channel, cc, value):
        if self.output:
            try:
                # mido uses 0-15 channel indexing
                msg = mido.Message('control_change', channel=channel, control=cc, value=value)
                self.output.send(msg)
            except Exception as e:
                print(f"Failed to send MIDI CC: {e}")

class PadButton(QPushButton):
    """Custom pad button with visual active/inactive states."""
    def __init__(self, row, col, text):
        super().__init__(text)
        self.row = row
        self.col = col
        self.setFixedSize(80, 80)
        self.setFocusPolicy(Qt.NoFocus)
        self.update_style(False)

    def update_style(self, active):
        if active:
            self.setStyleSheet("""
                QPushButton {
                    background-color: #2ecc71;
                    color: white;
                    border-radius: 10px;
                    font-size: 18px;
                    font-weight: bold;
                    border: 2px solid #27ae60;
                }
            """)
        else:
            self.setStyleSheet("""
                QPushButton {
                    background-color: #34495e;
                    color: white;
                    border-radius: 10px;
                    font-size: 18px;
                    font-weight: bold;
                    border: 2px solid #2c3e50;
                }
                QPushButton:hover {
                    background-color: #3e5871;
                }
            """)

    def set_active(self, active):
        self.update_style(active)

class MidiKnob(QDial):
    def __init__(self, index, label, parent=None):
        super().__init__(parent)
        self.index = index
        self.label = label
        self.setFixedSize(80, 80)
        self.setRange(0, 127)
        self.setWrapping(False)
        self.setNotchesVisible(False)
        self.last_value = 0
        self.setFocusPolicy(Qt.StrongFocus)

    def set_mode(self, mode):
        if mode == "Absolute":
            self.setWrapping(False)
        else:
            self.setWrapping(True)
        self.last_value = self.value()

    def enterEvent(self, event):
        self.update()
        super().enterEvent(event)

    def leaveEvent(self, event):
        self.update()
        super().leaveEvent(event)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        
        w, h = self.width(), self.height()
        size = min(w, h)
        rect = QRectF((w - size)/2 + 4, (h - size)/2 + 4, size - 8, size - 8)
        
        # Draw outer plate
        pen_track = QPen(QColor("#1e272e"), 4)
        painter.setPen(pen_track)
        painter.setBrush(QColor("#2d3436"))
        painter.drawEllipse(rect)
        
        val_range = self.maximum() - self.minimum()
        val_pct = (self.value() - self.minimum()) / val_range if val_range > 0 else 0.0
        
        if self.wrapping():
            # 360 degree endless rotation, starting at 12 o'clock (90 deg CCW) and rotating clockwise
            ptr_angle_deg = 90 - 360 * val_pct
        else:
            # 270 degree rotation, starting at 7 o'clock (225 deg CCW) and rotating clockwise
            ptr_angle_deg = 225 - 270 * val_pct
            
            # Draw active arc
            active_span = -270 * val_pct
            pen_active = QPen(QColor("#3498db"), 4)
            painter.setPen(pen_active)
            painter.setBrush(Qt.NoBrush)
            painter.drawArc(rect, int(225 * 16), int(active_span * 16))
        
        # Draw inner rotating cap
        inner_rect = rect.adjusted(6, 6, -6, -6)
        if self.underMouse() or self.hasFocus():
            cap_color = QColor("#3e4a56")
        else:
            cap_color = QColor("#2c3e50")
            
        painter.setPen(QPen(QColor("#1a252f"), 2))
        painter.setBrush(cap_color)
        painter.drawEllipse(inner_rect)
        
        # Draw indicator pointer line
        ptr_angle_rad = math.radians(ptr_angle_deg)
        center = rect.center()
        r_cap = inner_rect.width() / 2
        px = center.x() + r_cap * math.cos(ptr_angle_rad)
        py = center.y() - r_cap * math.sin(ptr_angle_rad)
        
        pen_ptr = QPen(QColor("#2ecc71"), 4)
        pen_ptr.setCapStyle(Qt.RoundCap)
        painter.setPen(pen_ptr)
        painter.drawLine(center.x(), center.y(), px, py)
        
        # Draw label text in center
        painter.setPen(QColor("white"))
        font = painter.font()
        font.setPointSize(12)
        font.setBold(True)
        painter.setFont(font)
        painter.drawText(inner_rect, Qt.AlignCenter, self.label)

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("PySide6 MIDI Pad Controller")
        self.setMinimumSize(820, 450)

        self.midi = MidiManager()
        self.pads = []
        self.configs = [] 
        
        # Load configurations or apply default values
        self.load_settings()

        self.init_ui()
        
        # Attempt to open port from configuration or first available
        self.setup_initial_port()

    def setup_initial_port(self):
        saved_port = self.current_config_data.get("selected_port")
        if saved_port in self.midi.available_ports:
            self.port_combo.setCurrentText(saved_port)
            self.midi.open_port(saved_port)
        elif self.midi.available_ports:
            first_port = self.midi.available_ports[0]
            self.port_combo.setCurrentText(first_port)
            self.midi.open_port(first_port)
        
        if not self.midi.available_ports:
            QMessageBox.warning(self, "MIDI Warning", 
                "No MIDI ports found. Ensure drivers or virtual cables are configured.")

    def load_settings(self):
        """Loads data from the JSON configuration file."""
        self.current_config_data = {}
        if os.path.exists(CONFIG_FILE):
            try:
                with open(CONFIG_FILE, 'r') as f:
                    self.current_config_data = json.load(f)
                    self.configs = self.current_config_data.get("pad_configs", [])
            except Exception as e:
                print(f"Failed to load configuration: {e}")

        # Ensure the config has 20 elements (16 pads + 4 knobs)
        if not self.configs:
            self.configs = []
            
        if len(self.configs) < 20:
            while len(self.configs) < 16:
                self.configs.append({'cc': 10 + len(self.configs), 'ch': 0})
            while len(self.configs) < 20:
                self.configs.append({'cc': 30 + (len(self.configs) - 16), 'ch': 0})

        self.knobs_mode = self.current_config_data.get("knobs_mode", "Absolute")

    def save_settings(self):
        """Saves current configuration to the JSON file."""
        data = {
            "selected_port": self.midi.current_port_name,
            "knobs_mode": self.knobs_mode,
            "pad_configs": self.configs
        }
        try:
            with open(CONFIG_FILE, 'w') as f:
                json.dump(data, f, indent=4)
        except Exception as e:
            print(f"Failed to save configuration: {e}")

    def init_ui(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QHBoxLayout(central_widget)

        # --- LEFT PANEL ---
        left_layout = QVBoxLayout()
        
        port_layout = QHBoxLayout()
        
        lbl_midi = QLabel("MIDI Out:")
        lbl_midi.setStyleSheet("font-weight: bold;")
        lbl_midi.setFixedWidth(60)
        port_layout.addWidget(lbl_midi)
        
        self.port_combo = QComboBox()
        self.port_combo.setFixedWidth(150)
        self.port_combo.addItems(self.midi.available_ports)
        self.port_combo.currentTextChanged.connect(self.on_port_changed)
        port_layout.addWidget(self.port_combo)
        
        refresh_btn = QPushButton("🔄")
        refresh_btn.setFixedWidth(40)
        refresh_btn.clicked.connect(self.refresh_ports)
        port_layout.addWidget(refresh_btn)
        
        port_layout.addSpacing(30)
        
        lbl_mode = QLabel("Knobs Mode:")
        lbl_mode.setStyleSheet("font-weight: bold;")
        port_layout.addWidget(lbl_mode)
        
        self.mode_combo = QComboBox()
        self.mode_combo.setFixedWidth(120)
        self.mode_combo.addItems(["Absolute", "Relative"])
        self.mode_combo.setCurrentText(self.knobs_mode)
        self.mode_combo.currentTextChanged.connect(self.on_knobs_mode_changed)
        port_layout.addWidget(self.mode_combo)
        
        port_layout.addStretch()
        left_layout.addLayout(port_layout)

        grid_widget = QWidget()
        self.grid_layout = QGridLayout(grid_widget)
        
        # Add Knobs above buttons (row 0)
        self.knobs = []
        for i in range(4):
            knob = MidiKnob(i, f"K{i+1}")
            knob.set_mode(self.knobs_mode)
            knob.valueChanged.connect(lambda val, idx=i: self.knob_changed(idx, val))
            self.grid_layout.addWidget(knob, 0, i)
            self.knobs.append(knob)
        
        chars = ["1234", "QWER", "ASDF", "ZXCV"]
        for r in range(4):
            row_pads = []
            for c in range(4):
                pad = PadButton(r, c, chars[r][c])
                pad.pressed.connect(lambda r=r, c=c: self.trigger_pad(r, c, True))
                pad.released.connect(lambda r=r, c=c: self.trigger_pad(r, c, False))
                # Shift pads down by 1 row to accommodate knobs in row 0
                self.grid_layout.addWidget(pad, r + 1, c)
                row_pads.append(pad)
            self.pads.append(row_pads)
        
        left_layout.addWidget(grid_widget)
        left_layout.addStretch()
        main_layout.addLayout(left_layout, 2)

        # --- RIGHT PANEL ---
        config_group = QGroupBox("MIDI CC / Channel")
        config_group.setStyleSheet("font-weight: bold;")
        config_scroll = QScrollArea()
        config_scroll.setWidgetResizable(True)
        config_container = QWidget()
        config_layout = QVBoxLayout(config_container)

        # Knobs Configuration Header
        knob_header = QLabel("<b>Knobs</b>")
        knob_header.setStyleSheet("color: #3498db; font-size: 14px; font-weight: bold; margin-top: 5px;")
        config_layout.addWidget(knob_header)

        for i in range(4):
            h_box = QHBoxLayout()
            lbl = QLabel(f"<b>K{i+1}</b>")
            lbl.setFixedWidth(25)
            h_box.addWidget(lbl)
            
            idx = 16 + i
            cc_spin = QSpinBox()
            cc_spin.setRange(0, 127)
            cc_spin.setValue(self.configs[idx]['cc'])
            cc_spin.setPrefix("CC ")
            cc_spin.valueChanged.connect(lambda val, idx=idx: self.update_config(idx, 'cc', val))
            
            ch_spin = QSpinBox()
            ch_spin.setRange(1, 16)
            ch_spin.setValue(self.configs[idx]['ch'] + 1)
            ch_spin.setPrefix("CH ")
            ch_spin.valueChanged.connect(lambda val, idx=idx: self.update_config(idx, 'ch', val - 1))
            
            h_box.addWidget(cc_spin)
            h_box.addWidget(ch_spin)
            config_layout.addLayout(h_box)

        # Pads Configuration Header
        pads_header = QLabel("<b>Pads</b>")
        pads_header.setStyleSheet("color: #2ecc71; font-size: 14px; font-weight: bold; margin-top: 10px;")
        config_layout.addWidget(pads_header)

        for i in range(16):
            row_idx = i // 4
            col_idx = i % 4
            char = chars[row_idx][col_idx]
            
            h_box = QHBoxLayout()
            lbl = QLabel(f"<b>{char}</b>")
            lbl.setFixedWidth(25)
            h_box.addWidget(lbl)
            
            cc_spin = QSpinBox()
            cc_spin.setRange(0, 127)
            cc_spin.setValue(self.configs[i]['cc'])
            cc_spin.setPrefix("CC ")
            cc_spin.valueChanged.connect(lambda val, idx=i: self.update_config(idx, 'cc', val))
            
            ch_spin = QSpinBox()
            ch_spin.setRange(1, 16)
            ch_spin.setValue(self.configs[i]['ch'] + 1)
            ch_spin.setPrefix("CH ")
            ch_spin.valueChanged.connect(lambda val, idx=i: self.update_config(idx, 'ch', val - 1))
            
            h_box.addWidget(cc_spin)
            h_box.addWidget(ch_spin)
            config_layout.addLayout(h_box)

        config_layout.addStretch()

        config_scroll.setWidget(config_container)
        group_layout = QVBoxLayout(config_group)
        group_layout.addWidget(config_scroll)
        
        main_layout.addWidget(config_group, 1)

    def on_port_changed(self, name):
        if self.midi.open_port(name):
            self.save_settings()

    def refresh_ports(self):
        try:
            new_ports = mido.get_output_names()
            current = self.port_combo.currentText()
            self.port_combo.clear()
            self.port_combo.addItems(new_ports)
            if current in new_ports:
                self.port_combo.setCurrentText(current)
            self.midi.available_ports = new_ports
        except Exception as e:
            print(f"Failed to refresh ports: {e}")

    def on_knobs_mode_changed(self, mode):
        self.knobs_mode = mode
        for knob in self.knobs:
            knob.set_mode(mode)
        self.save_settings()

    def knob_changed(self, knob_idx, new_val):
        knob = self.knobs[knob_idx]
        cc = self.configs[16 + knob_idx]['cc']
        ch = self.configs[16 + knob_idx]['ch']
        
        if self.knobs_mode == "Absolute":
            self.midi.send_cc(ch, cc, new_val)
        else:
            diff = (new_val - knob.last_value + 64) % 128 - 64
            if diff > 0:
                for _ in range(diff):
                    self.midi.send_cc(ch, cc, 65)
            elif diff < 0:
                for _ in range(abs(diff)):
                    self.midi.send_cc(ch, cc, 63)
        
        knob.last_value = new_val

    def update_config(self, index, key, value):
        self.configs[index][key] = value
        self.save_settings()

    def trigger_pad(self, row, col, pressed):
        index = row * 4 + col
        cc = self.configs[index]['cc']
        ch = self.configs[index]['ch']
        val = 127 if pressed else 0
        self.pads[row][col].set_active(pressed)
        self.midi.send_cc(ch, cc, val)

    def keyPressEvent(self, event):
        if event.isAutoRepeat(): return
        key = event.key()
        for r in range(4):
            for c in range(4):
                en_key, ru_key = KEY_MAP[r][c]
                if key == en_key or (ru_key is not None and key == ru_key):
                    self.trigger_pad(r, c, True)

    def keyReleaseEvent(self, event):
        if event.isAutoRepeat(): return
        key = event.key()
        for r in range(4):
            for c in range(4):
                en_key, ru_key = KEY_MAP[r][c]
                if key == en_key or (ru_key is not None and key == ru_key):
                    self.trigger_pad(r, c, False)

    def closeEvent(self, event):
        """Called when window is closing."""
        self.save_settings()
        super().closeEvent(event)

if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    window = MainWindow()
    window.show()
    sys.exit(app.exec())