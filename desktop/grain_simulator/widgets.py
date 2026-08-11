import numpy as np
from PySide6.QtWidgets import QWidget
from PySide6.QtCore import Qt, QTimer, QRect, Signal
from PySide6.QtGui import QPainter, QColor, QPen, QFont, QImage
from constants import UI_FONT_NAME, UI_FONT_SIZE

class Knob(QWidget):
    moved = Signal(int, float)
    def __init__(self, label, parent=None):
        super().__init__(parent)
        self.label, self.id = label, 0
        self.value_ratio, self.display_val, self.mod_text = 0.0, "", ""
        self.setMinimumSize(85, 130)
        self.setCursor(Qt.SizeVerCursor)
        self.last_y = 0

    def set_data(self, val_ratio, display_val, mod_info=""):
        self.value_ratio, self.display_val, self.mod_text = val_ratio, display_val, mod_info
        self.update()

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton: self.last_y = event.position().y()

    def mouseMoveEvent(self, event):
        if event.buttons() & Qt.LeftButton:
            curr_y = event.position().y()
            delta = (self.last_y - curr_y) 
            if abs(delta) >= 1:
                self.moved.emit(self.id, delta)
                self.last_y = curr_y

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        cx, cy, r = self.width()//2, self.height()//2, 30
        
        # Outer circle
        p.setPen(Qt.NoPen)
        p.setBrush(QColor(45, 45, 48))
        p.drawEllipse(cx-r, cy-r, r*2, r*2)
        
        # Rotation arc
        pen = QPen(Qt.white, 4)
        pen.setCapStyle(Qt.RoundCap)
        p.setPen(pen)
        # Start at 225 deg, span -270 deg
        span = int(self.value_ratio * -270 * 16)
        p.drawArc(cx-r+4, cy-r+4, (r-4)*2, (r-4)*2, 225 * 16, span)
        
        # Indicator line
        angle = 225 - (self.value_ratio * 270)
        rad = np.radians(angle)
        p.setPen(QPen(Qt.white, 3, Qt.SolidLine, Qt.RoundCap))
        p.drawLine(cx + int((r-15)*np.cos(rad)), cy - int((r-15)*np.sin(rad)), 
                   cx + int((r-5)*np.cos(rad)), cy - int((r-5)*np.sin(rad)))

class OLED(QWidget):
    def __init__(self, main_app):
        super().__init__()
        self.main_app = main_app
        self.scale_factor = 4.0
        self.setFixedSize(128*self.scale_factor, 64*self.scale_factor) 
        QTimer(self, timeout=self.update).start(1000//30)

    def paintEvent(self, event):
        # Draw everything to a 128x64 buffer first for pixel-perfect look
        buffer = QImage(128, 64, QImage.Format_ARGB32)
        buffer.fill(Qt.black)
        
        p = QPainter(buffer)
        p.setRenderHint(QPainter.Antialiasing, False)
        p.setRenderHint(QPainter.TextAntialiasing, False)
        
        show_bar = self.main_app.SHOW_STATUS_BAR
        if self.main_app.disp_mode == "GRAIN":
            self.draw_grain_view(p, show_bar)
        else:
            self.draw_param_view(p, show_bar)
        p.end()
        
        # Now scale the buffer to the widget size
        full_p = QPainter(self)
        full_p.setRenderHint(QPainter.Antialiasing, False)
        full_p.setRenderHint(QPainter.SmoothPixmapTransform, False) # Nearest Neighbor
        full_p.drawImage(self.rect(), buffer)

    def draw_header(self, p, title):
        p.setPen(Qt.white)
        font = QFont("Monaco")
        font.setPixelSize(8)
        font.setStyleStrategy(QFont.NoAntialias)
        p.setFont(font)
        
        # Draw title (adjust y to 8 for 8-pixel font)
        p.drawText(0, 8, title.upper())
        
        # Voice indicators
        for v in range(4):
            x = 80 + v * 12
            v_name = f"V{v+1}"
            is_active = (v == self.main_app.active_voice)
            is_trig = self.main_app.engines[v].is_triggered
            
            if is_active:
                p.fillRect(x-1, 1, 11, 8, Qt.white)
                p.setPen(Qt.black)
                p.drawText(x, 8, v_name)
                p.setPen(Qt.white)
            elif is_trig:
                p.drawRect(x-1, 1, 10, 8)
                p.drawText(x, 8, v_name)
            else:
                p.drawText(x, 8, v_name)
        
        p.drawLine(0, 10, 128, 10)

    def draw_param_view(self, p, show_bar):
        y_start = 12 if show_bar else 2
        if show_bar:
            self.draw_header(p, self.main_app.active_page)
            
        page_name = self.main_app.active_page
        p_keys = self.main_app.pages.get(page_name, [None]*4)
        
        font = QFont("Monaco")
        font.setPixelSize(8)
        font.setStyleStrategy(QFont.NoAntialias)
        p.setFont(font)

        MOD_SRCS = ["---", "JIT", "LF1", "LF2", "EG"]
        
        for i in range(4):
            col_x = i * 32
            col_w = 32
            key = p_keys[i]
            if not key and page_name != "Mix": 
                # Draw placeholder
                p.setPen(Qt.white)
                tw = p.fontMetrics().horizontalAdvance("---")
                p.drawText(col_x + (col_w - tw)//2, y_start + 8, "---")
                continue
            
            eng = self.main_app.engines[i if page_name == "Mix" else self.main_app.active_voice]
            param = eng.params.get(key if key else "vol")
            val, lo, hi = param[0], param[1], param[2]
            ratio = (val - lo) / (hi - lo) if hi != lo else 0
            
            # 1. Parameter Name (Centered)
            name_map = {
                "pos": "POS", "size": "SIZE", "dens": "DENS", "pitch": "PITCH",
                "sample_idx": "SAMPLE", "max_grains": "GRAINS", "grain_amp": "AMP", "keytrack": "KEYTRK",
                "pitch_mode": "P.MODE", "scan": "SCAN", "direction": "DIR", "spread": "SPREAD", "shape": "SHAPE",
                "cutoff": "CUTOFF", "res": "RES", "filt_type": "F.TYPE", "filt_key": "F.KEY",
                "atk": "ATK", "atk_curve": "A.CURV", "rel": "REL", "rel_curve": "R.CURV",
                "lfo1_rate": "RATE", "lfo1_wave": "WAVE", "lfo1_phase": "PHASE", "lfo1_sync": "SYNC",
                "vol": "VOL", "mod1_src": "M1.SRC", "mod1_amt": "M1.AMT", "mod2_src": "M2.SRC", "mod2_amt": "M2.AMT",
                "fx_wf": "FOLD", "fx_ds": "DNSMPL", "fx_bc": "BCRSH", "fx_mix": "MIX",
                "viz_scale": "VIZ", "midi_mode": "MIDI", "midi_ch": "CH", "master_vol": "M.VOL"
            }
            display_name = name_map.get(key, (key if key else f"V{i+1}").upper())
            tw = p.fontMetrics().horizontalAdvance(display_name)
            p.setPen(Qt.white)
            p.drawText(col_x + (col_w - tw)//2, y_start + 8, display_name)

            # 2. Status Indicator (Vertical fill box, centered, width 10)
            box_w = 10
            box_x = col_x + (col_w - box_w) // 2
            y_box = y_start + 10
            h_box = 18
            p.drawRect(box_x, y_box, box_w, h_box)
            
            # Ratio calculation for display
            if key in ["mod1_src", "mod2_src"]:
                ratio = val / 24.0
            elif key == "fx_ds":
                ratio = (val - 1.0) / 79.0
            elif key == "fx_bc":
                ratio = (16.0 - val) / 15.0
            else:
                ratio = (val - lo) / (hi - lo) if hi != lo else 0
                
            fill_h = int(ratio * (h_box - 2))
            if fill_h > 0:
                p.fillRect(box_x + 1, y_box + h_box - 1 - fill_h, box_w - 2, fill_h, Qt.white)
            
            # 3. Parameter Value Text (Centered)
            if key in ["mod1_src", "mod2_src"]:
                route = self.main_app.mod_routes[int(val)]
                disp = route["name"]
            elif key == "sample_idx":
                disp = f"#{int(val)+1}"
            elif key == "fx_ds":
                disp = f"x{int(val)}"
            else:
                disp = f"{int(ratio*100)}%"
            
            vw = p.fontMetrics().horizontalAdvance(disp)
            p.drawText(col_x + (col_w - vw)//2, y_box + h_box + 9, disp)

            # 4. Modulation Bar (Horizontal, centered, width 30)
            y_mod = y_box + h_box + 12
            w_mod = 30
            mod_x = col_x + (col_w - w_mod) // 2
            p.drawRect(mod_x, y_mod, w_mod, 4)
            m_amt = param[6] if len(param) > 6 else 0.0
            m_fill = int(m_amt * (w_mod - 2))
            if m_fill > 0:
                p.fillRect(mod_x + 1, y_mod + 1, m_fill, 2, Qt.white)

            # 5. Modulation Source Label (Centered)
            src_name = param[5] if len(param) > 5 and param[5] else "---"
            if src_name in ["Jit", "LFO1", "LFO2", "EG"]:
                src_disp = src_name.replace("LFO", "LF").upper()
            else:
                src_disp = "---"
            sw = p.fontMetrics().horizontalAdvance(src_disp)
            p.drawText(col_x + (col_w - sw)//2, y_mod + 12, src_disp)

    def draw_grain_view(self, p, show_bar):
        eng = self.main_app.engines[self.main_app.active_voice]
        if show_bar:
            idx = int(eng.params["sample_idx"][0])
            self.draw_header(p, f"SMPL {idx+1}/{len(eng.file_list) or 1}")
            cy, max_h, cur_y = 37, 24, 11
        else:
            cy, max_h, cur_y = 32, 28, 0
            
        if eng.env_pos:
            p.setPen(Qt.white)
            for i in range(min(127, len(eng.env_pos)-1)):
                h0, h1 = int(eng.env_pos[i]*max_h), int(eng.env_pos[i+1]*max_h)
                p.drawLine(i, cy - h0, i+1, cy - h1)
                p.drawLine(i, cy + h0, i+1, cy + h1)
        
        # Range cursors (pos ± mod_amt)
        pos_param = eng.params.get("pos")
        pos = pos_param[0]
        mod_amt = pos_param[6] if len(pos_param) > 6 else 0.0
        
        center_pos = (pos + eng.playback_pos) % 1.0
        
        p.setPen(Qt.white)
        cx = int(center_pos * 127)
        if mod_amt < 0.01:
            p.drawLine(max(0, min(127, cx)), cur_y, max(0, min(127, cx)), 63)
        else:
            l_pos = (center_pos - mod_amt) % 1.0
            r_pos = (center_pos + mod_amt) % 1.0
            lx, rx = int(l_pos * 127), int(r_pos * 127)
            p.drawLine(max(0, min(127, lx)), cur_y, max(0, min(127, lx)), 63)
            p.drawLine(max(0, min(127, rx)), cur_y, max(0, min(127, rx)), 63)
        
        # Grains
        p.setPen(Qt.white)
        show_pan = int(eng.params["viz_scale"][0]) != 0
        for g in eng.active_grains:
            gx = int((g.get_current_sample_idx() / (len(eng.sample_data) or 1)) * 127)
            if show_pan:
                # pan is 0.0 (Left, top) to 1.0 (Right, bottom)
                gy = cur_y + int(g.pan * (63 - cur_y))
            else:
                gy = 63 - int((g.pitch / 4.0) * (63 - cur_y))
            p.drawPoint(gx, max(cur_y+1, min(63, gy)))