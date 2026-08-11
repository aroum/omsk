#!/usr/bin/env python3
import sys
import re
import time
import math
import threading
import collections
import os
import glob
import select
import tty
import termios

from serial import Serial, SerialException
from serial.tools import list_ports
from rich.live import Live
from rich.text import Text
from rich.panel import Panel
from rich.console import Console
from rich.layout import Layout
from rich.align import Align
from serial.tools import list_ports

# --- Configuration ---
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
WIDTH = 128
HEIGHT = 64

# --- Font 5x7 ---
FONT_5X7 = {
    ' ': [0x00, 0x00, 0x00, 0x00, 0x00],
    '!': [0x00, 0x00, 0x5F, 0x00, 0x00],
    '"': [0x00, 0x07, 0x00, 0x07, 0x00],
    '#': [0x14, 0x7F, 0x14, 0x7F, 0x14],
    '$': [0x24, 0x2A, 0x7F, 0x2A, 0x12],
    '%': [0x23, 0x13, 0x08, 0x64, 0x62],
    '&': [0x36, 0x49, 0x55, 0x22, 0x50],
    "'": [0x00, 0x05, 0x03, 0x00, 0x00],
    '(': [0x00, 0x1C, 0x22, 0x41, 0x00],
    ')': [0x00, 0x41, 0x22, 0x1C, 0x00],
    '*': [0x14, 0x08, 0x3E, 0x08, 0x14],
    '+': [0x08, 0x08, 0x3E, 0x08, 0x08],
    ',': [0x00, 0x50, 0x30, 0x00, 0x00],
    '-': [0x08, 0x08, 0x08, 0x08, 0x08],
    '.': [0x00, 0x60, 0x60, 0x00, 0x00],
    '/': [0x20, 0x10, 0x08, 0x04, 0x02],
    '0': [0x3E, 0x51, 0x49, 0x45, 0x3E],
    '1': [0x00, 0x42, 0x7F, 0x40, 0x00],
    '2': [0x42, 0x61, 0x51, 0x49, 0x46],
    '3': [0x21, 0x41, 0x45, 0x4B, 0x31],
    '4': [0x18, 0x14, 0x12, 0x7F, 0x10],
    '5': [0x27, 0x45, 0x45, 0x45, 0x39],
    '6': [0x3C, 0x4A, 0x49, 0x49, 0x30],
    '7': [0x01, 0x71, 0x09, 0x05, 0x03],
    '8': [0x36, 0x49, 0x49, 0x49, 0x36],
    '9': [0x06, 0x49, 0x49, 0x29, 0x1E],
    ':': [0x00, 0x36, 0x36, 0x00, 0x00],
    ';': [0x00, 0x56, 0x36, 0x00, 0x00],
    '<': [0x08, 0x14, 0x22, 0x41, 0x00],
    '=': [0x14, 0x14, 0x14, 0x14, 0x14],
    '>': [0x00, 0x41, 0x22, 0x14, 0x08],
    '?': [0x02, 0x01, 0x51, 0x09, 0x06],
    '@': [0x32, 0x49, 0x79, 0x41, 0x3E],
    'A': [0x7F, 0x09, 0x09, 0x09, 0x7F],
    'B': [0x7F, 0x49, 0x49, 0x49, 0x36],
    'C': [0x3E, 0x41, 0x41, 0x41, 0x22],
    'D': [0x7F, 0x41, 0x41, 0x22, 0x1C],
    'E': [0x7F, 0x49, 0x49, 0x49, 0x41],
    'F': [0x7F, 0x09, 0x09, 0x09, 0x01],
    'G': [0x3E, 0x41, 0x49, 0x49, 0x7A],
    'H': [0x7F, 0x08, 0x08, 0x08, 0x7F],
    'I': [0x00, 0x41, 0x7F, 0x41, 0x00],
    'J': [0x20, 0x40, 0x41, 0x3F, 0x01],
    'K': [0x7F, 0x08, 0x14, 0x22, 0x41],
    'L': [0x7F, 0x40, 0x40, 0x40, 0x40],
    'M': [0x7F, 0x02, 0x0C, 0x02, 0x7F],
    'N': [0x7F, 0x04, 0x08, 0x10, 0x7F],
    'O': [0x3E, 0x41, 0x41, 0x41, 0x3E],
    'P': [0x7F, 0x09, 0x09, 0x09, 0x06],
    'Q': [0x3E, 0x41, 0x51, 0x21, 0x5E],
    'R': [0x7F, 0x09, 0x19, 0x29, 0x46],
    'S': [0x46, 0x49, 0x49, 0x49, 0x31],
    'T': [0x01, 0x01, 0x7F, 0x01, 0x01],
    'U': [0x3F, 0x40, 0x40, 0x40, 0x3F],
    'V': [0x1F, 0x20, 0x40, 0x20, 0x1F],
    'W': [0x3F, 0x40, 0x38, 0x40, 0x3F],
    'X': [0x63, 0x14, 0x08, 0x14, 0x63],
    'Y': [0x07, 0x08, 0x70, 0x08, 0x07],
    'Z': [0x61, 0x51, 0x49, 0x45, 0x43],
    '[': [0x00, 0x7F, 0x41, 0x41, 0x00],
    '\\': [0x02, 0x04, 0x08, 0x10, 0x20],
    ']': [0x00, 0x41, 0x41, 0x7F, 0x00],
    '^': [0x04, 0x02, 0x01, 0x02, 0x04],
    '_': [0x40, 0x40, 0x40, 0x40, 0x40],
    'a': [0x20, 0x54, 0x54, 0x54, 0x78],
    'b': [0x7F, 0x48, 0x44, 0x44, 0x38],
    'c': [0x38, 0x44, 0x44, 0x44, 0x20],
    'd': [0x38, 0x44, 0x44, 0x48, 0x7F],
    'e': [0x38, 0x54, 0x54, 0x54, 0x18],
    'f': [0x08, 0x7E, 0x09, 0x01, 0x02],
    'g': [0x0C, 0x52, 0x52, 0x52, 0x3E],
    'h': [0x7F, 0x08, 0x04, 0x04, 0x78],
    'i': [0x00, 0x44, 0x7D, 0x40, 0x00],
    'j': [0x20, 0x40, 0x44, 0x3D, 0x00],
    'k': [0x7F, 0x10, 0x28, 0x44, 0x00],
    'l': [0x00, 0x41, 0x7F, 0x40, 0x00],
    'm': [0x7C, 0x04, 0x18, 0x04, 0x78],
    'n': [0x7C, 0x08, 0x04, 0x04, 0x78],
    'o': [0x38, 0x44, 0x44, 0x44, 0x38],
    'p': [0x7C, 0x14, 0x14, 0x14, 0x08],
    'q': [0x08, 0x14, 0x14, 0x18, 0x7C],
    'r': [0x7C, 0x08, 0x04, 0x04, 0x08],
    's': [0x48, 0x54, 0x54, 0x54, 0x20],
    't': [0x04, 0x3F, 0x44, 0x40, 0x20],
    'u': [0x3C, 0x40, 0x40, 0x20, 0x7C],
    'v': [0x1C, 0x20, 0x40, 0x20, 0x1C],
    'w': [0x3C, 0x40, 0x30, 0x40, 0x3C],
    'x': [0x44, 0x28, 0x10, 0x28, 0x44],
    'y': [0x0C, 0x50, 0x50, 0x50, 0x3C],
    'z': [0x44, 0x64, 0x54, 0x4C, 0x44],
    '{': [0x00, 0x08, 0x36, 0x41, 0x00],
    '|': [0x00, 0x00, 0x7F, 0x00, 0x00],
    '}': [0x00, 0x41, 0x36, 0x08, 0x00],
    '~': [0x10, 0x08, 0x08, 0x10, 0x08]
}

# --- Static waveform pictograms (columns are bytes like FONT_5X7)
# Each array is 28 columns long and uses bits 0..5 for the 6-pixel-high waveform area
WAVE_ORDER = ['sin', 'saw', 'tri', 'rsaw', 'sqr', 'pam']
WAVEFORMS = {
    'sin': [
        0x08,0x08,0x04,0x04,0x02,0x02,0x01,0x01,0x02,0x02,0x04,0x04,0x08,0x08,
        0x10,0x10,0x20,0x20,0x10,0x10,0x08,0x08,0x04,0x04,0x02,0x02,0x01,0x01
    ],
    'saw': [
        0x01,0x02,0x04,0x08,0x10,0x20,0x01,0x02,0x04,0x08,0x10,0x20,0x01,0x02,
        0x04,0x08,0x10,0x20,0x01,0x02,0x04,0x08,0x10,0x20,0x01,0x02,0x04,0x08
    ],
    'tri': [
        0x01,0x02,0x04,0x08,0x10,0x20,0x10,0x08,0x04,0x02,0x01,0x02,0x04,0x08,
        0x10,0x20,0x10,0x08,0x04,0x02,0x01,0x02,0x04,0x08,0x10,0x20,0x10,0x08
    ],
    'rsaw': [
        0x20,0x10,0x08,0x04,0x02,0x01,0x20,0x10,0x08,0x04,0x02,0x01,0x20,0x10,
        0x08,0x04,0x02,0x01,0x20,0x10,0x08,0x04,0x02,0x01,0x20,0x10,0x08,0x04
    ],
    'sqr': [
        # first half high, second half low
        *([0x02] * 14), *([0x10] * 14)
    ],
    'pam': [
        # stepped PAM-looking pattern (4-level repeating)
        0x04,0x10,0x02,0x20,0x04,0x10,0x02,0x20,0x04,0x10,0x02,0x20,0x04,0x10,
        0x02,0x20,0x04,0x10,0x02,0x20,0x04,0x10,0x02,0x20,0x04,0x10,0x02,0x20
    ]
}

# --- Module Definitions ---
MODULES = [
    "VCO1", "VCO2", "VCF1", "VCF2", 
    "LFO1", "LFO2", "EG1", "EG2", 
    "MIXER", "NOISE", "ARP", "GLIDE", 
    "FX1", "FX2", "SET", "ModW", "NONE"
]

def get_config_port():
    # First look for real devices matching /dev/tty.usbmodem*
    tty_devices = sorted(glob.glob("/dev/tty.usbmodem*"))
    if tty_devices:
        return tty_devices[0]

    # Fallback via pyserial for non-standard port names
    ports = list(list_ports.comports())
    for p in ports:
        if "tty.usbmodem" in p.device:
            return p.device

    return None

# --- OledDisplay Class ---
class OledDisplay:
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.buffer = bytearray((width * height) // 8)
        self.clear()

    def clear(self):
        self.buffer = bytearray((self.width * self.height) // 8)

    def set_pixel(self, x, y, color=1):
        if not (0 <= x < self.width and 0 <= y < self.height):
            return
        idx = (y // 8) * self.width + x
        bit = y % 8
        if color:
            self.buffer[idx] |= (1 << bit)
        else:
            self.buffer[idx] &= ~(1 << bit)

    def draw_line(self, x0, y0, x1, y1, color=1):
        x0, y0, x1, y1 = int(x0), int(y0), int(x1), int(y1)
        dx = abs(x1 - x0)
        dy = abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx - dy
        while True:
            self.set_pixel(x0, y0, color)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 > -dy:
                err -= dy
                x0 += sx
            if e2 < dx:
                err += dx
                y0 += sy

    def draw_circle(self, x0, y0, r, color=1):
        x0, y0, r = int(x0), int(y0), int(r)
        f = 1 - r
        ddF_x = 1
        ddF_y = -2 * r
        x = 0
        y = r
        self.set_pixel(x0, y0 + r, color)
        self.set_pixel(x0, y0 - r, color)
        self.set_pixel(x0 + r, y0, color)
        self.set_pixel(x0 - r, y0, color)
        while x < y:
            if f >= 0:
                y -= 1
                ddF_y += 2
                f += ddF_y
            x += 1
            ddF_x += 2
            f += ddF_x
            self.set_pixel(x0 + x, y0 + y, color)
            self.set_pixel(x0 - x, y0 + y, color)
            self.set_pixel(x0 + x, y0 - y, color)
            self.set_pixel(x0 - x, y0 - y, color)
            self.set_pixel(x0 + y, y0 + x, color)
            self.set_pixel(x0 - y, y0 + x, color)
            self.set_pixel(x0 + y, y0 - x, color)
            self.set_pixel(x0 - y, y0 - x, color)
    
    def fill_circle(self, x0, y0, r, color=1):
        # Simple scanline fill
        x0, y0, r = int(x0), int(y0), int(r)
        for y in range(-r, r + 1):
            for x in range(-r, r + 1):
                if x*x + y*y <= r*r:
                    self.set_pixel(x0 + x, y0 + y, color)

    def draw_str(self, x, y, s, color=1):
        x, y = int(x), int(y)
        for char in s:
            if char not in FONT_5X7:
                continue
            glyph = FONT_5X7[char]
            for i, col in enumerate(glyph):
                for j in range(8):
                    if col & (1 << j):
                        self.set_pixel(x + i, y + j, color)
            x += 6

    def draw_h_line(self, x, y, w, color=1):
        x, y, w = int(x), int(y), int(w)
        for i in range(w):
            self.set_pixel(x + i, y, color)

    def draw_v_line(self, x, y, h, color=1):
        x, y, h = int(x), int(y), int(h)
        for i in range(h):
            self.set_pixel(x, y + i, color)
            
    def draw_box(self, x, y, w, h, color=1):
        x, y, w, h = int(x), int(y), int(w), int(h)
        for i in range(w):
            for j in range(h):
                self.set_pixel(x + i, y + j, color)

    def render_to_text(self):
        lines = []
        for y in range(0, self.height, 2):
            line = ""
            for x in range(self.width):
                # Check top pixel (y)
                idx_top = (y // 8) * self.width + x
                bit_top = y % 8
                top = (self.buffer[idx_top] >> bit_top) & 1
                
                # Check bottom pixel (y+1)
                if y + 1 < self.height:
                    idx_bot = ((y + 1) // 8) * self.width + x
                    bit_bot = (y + 1) % 8
                    bot = (self.buffer[idx_bot] >> bit_bot) & 1
                else:
                    bot = 0
                
                if top and bot:
                    line += "█"
                elif top:
                    line += "▀"
                elif bot:
                    line += "▄"
                else:
                    line += " "
            lines.append(line)
        return "\n".join(lines)

# --- Synth Logic ---
class SynthState:
    def __init__(self):
        self.selected_module = "VCO1"
        self.params = collections.defaultdict(lambda: collections.defaultdict(int))
        self.mod_matrix = collections.defaultdict(lambda: collections.defaultdict(int))
        self.set_mode = False
        self.set_context = "NONE"
        self.online = False  # OFFLINE until connected to device
        self.lock = threading.Lock()
        self.updated = True

    def set_online(self, online: bool):
        with self.lock:
            if self.online != online:
                self.online = online
                self.updated = True

    def update_from_log(self, line):
        line = line.strip()
        with self.lock:
            if not line:
                return

            # Any incoming string from device indicates active connection
            self.online = True

            if "SET_ON" in line:
                self.set_mode = True
                self.updated = True
            elif "SET_OFF" in line:
                self.set_mode = False
                self.updated = True
            elif "MOD_ASSIGN" in line:
                # MOD_ASSIGN LFO1 -> VCO1 P0 = 50%
                parts = line.split(" ")
                if len(parts) >= 7:
                    src = parts[1]
                    dst = parts[3]
                    param = parts[4] # P0, P1...
                    val_str = parts[6].replace("%", "")
                    try:
                        val = int(float(val_str))
                        self.mod_matrix[(dst, param)] = (src, val)
                        self.updated = True
                    except ValueError: pass
            elif line.startswith("MOD_INIT"):
                # MOD_INIT <PID> <SRC> <VAL>
                parts = line.split(" ")
                if len(parts) == 4:
                    pass
            elif "PANEL SELECT" in line:
                mod = line.split(" ")[-1]
                if mod in MODULES:
                    self.selected_module = mod
                    self.updated = True
            elif line.startswith("PANEL"):
                # PANEL <Mod> <Key>=<Val> ...
                parts = line.split(" ")
                if len(parts) >= 3:
                    mod = parts[1]
                    kv = parts[2]
                    if "=" in kv:
                        key, val_str = kv.split("=")
                        try:
                            val = int(val_str)
                            self.params[mod][key] = val
                            self.updated = True
                        except ValueError:
                            pass

    def get_oled_page(self):
        with self.lock:
            if not self.online:
                # OFFLINE page
                return {
                    "offline": True,
                    "module": "OFFLINE",
                    "set_mode": False,
                }

            mod = self.selected_module
            vals = self.params[mod]
            
            # Defaults
            t = ["P0", "P1", "P2", "P3"]
            v = [0, 0, 0, 0]
            lbl = ["", "", "", ""]
            amt = [0, 0, 0, 0]
            
            p_keys = []
            if mod == "VCO1":
                t = ["Trans", "Detun", "Wave", "Shape"]
                v = [vals["Trns"], vals["Detn"], vals["Wave"], vals["Shap"]]
                p_keys = ["P0", "P1", "P2", "P3"]
            elif mod == "VCO2":
                t = ["Trans", "Detun", "Wave", "Shape"]
                v = [vals["Trns"], vals["Detn"], vals["Wave"], vals["Shap"]]
                p_keys = ["P0", "P1", "P2", "P3"]
            elif mod == "VCF1":
                t = ["Cutof", "Reson", "Type", "Mix"]
                v = [vals["Cut"], vals["Res"], vals["Type"], vals["Mix"]]
                p_keys = ["P0", "P1", "P2", "P3"]
            elif mod == "VCF2":
                t = ["Cutof", "Reson", "Type", "Mix"]
                v = [vals["Cut"], vals["Res"], vals["Type"], vals["Mix"]]
                p_keys = ["P0", "P1", "P2", "P3"]
            elif mod == "LFO1":
                t = ["Rate", "Smoth", "Wave", "Shape"]
                v = [vals["Rate"], vals["Smth"], vals["Wave"], vals["Shap"]]
                p_keys = ["P0", "P1", "P2", "P3"]
            elif mod == "LFO2":
                t = ["Rate", "Smoth", "Wave", "Shape"]
                v = [vals["Rate"], vals["Smth"], vals["Wave"], vals["Shap"]]
                p_keys = ["P0", "P1", "P2", "P3"]
            elif mod == "EG1":
                t = ["Attac", "Decay", "Susta", "Relea"]
                v = [vals["Attk"], vals["Dcy"], vals["Sus"], vals["Rels"]]
                p_keys = ["P0", "P1", "P2", "P3"]
            elif mod == "EG2":
                t = ["Attac", "Decay", "Susta", "Relea"]
                v = [vals["Attk"], vals["Dcy"], vals["Sus"], vals["Rels"]]
                p_keys = ["P0", "P1", "P2", "P3"]
            elif mod == "MIXER":
                t = ["V1/V2", "Os/Ns", "Phase", "Mastr"]
                v = [vals["V1V2"], vals["OsNs"], vals["Phs2"], vals["Mast"]]
                p_keys = ["P0", "P1", "P2", "P3"]
            elif mod == "NOISE":
                t = ["Color", "Chord", "—", "—"]
                v = [vals["Colr"], vals["Chrd"], 0, 0]
                p_keys = ["P0", "P1", "P2", "P3"]
            elif mod == "ARP":
                t = ["Rate", "Mode", "Var", "Octav"]
                v = [vals["Rate"], vals["Mode"], vals["Var"], vals["Oct"]]
                p_keys = ["P0", "P1", "P2", "P3"]
            elif mod == "GLIDE":
                t = ["Poly", "Time", "Slope", "Mode"]
                v = [vals["Poly"], vals["Time"], vals["Slop"], vals["Mode"]]
                p_keys = ["P0", "P1", "P2", "P3"]
            elif mod == "FX1":
                t = ["Time", "Fdbck", "Mix", "Tone"]
                v = [vals["Time"], vals["Feed"], vals["Mix"], vals["Tone"]]
                p_keys = ["P0", "P1", "P2", "P3"]
            elif mod == "FX2":
                t = ["Time", "Fdbck", "Mix", "Sprea"]
                v = [vals["Time"], vals["Feed"], vals["Mix"], vals["Sprd"]]
                p_keys = ["P0", "P1", "P2", "P3"]
            
            for i in range(len(p_keys)):
                m_info = self.mod_matrix.get((mod, f"P{i}"))
                if m_info:
                    lbl[i] = m_info[0]
                    amt[i] = m_info[1] # -100 to 100
            
            return {
                "module": mod,
                "set_mode": self.set_mode,
                "knobs": [
                    {"title": t[0], "value": v[0], "mod_label": lbl[0], "mod_amount": amt[0]},
                    {"title": t[1], "value": v[1], "mod_label": lbl[1], "mod_amount": amt[1]},
                    {"title": t[2], "value": v[2], "mod_label": lbl[2], "mod_amount": amt[2]},
                    {"title": t[3], "value": v[3], "mod_label": lbl[3], "mod_amount": amt[3]},
                ],
                "layout": 0
            }

def draw_centered_str(oled, cx, y, s, color=1):
    w = len(s) * 6
    x = cx - w // 2
    oled.draw_str(x, y, s, color)

# --- Formatter ---
def format_val(mod, title, v):
    if title == "Trans":
        oct = round((((v - 64) / 64.0) * 5.0))
        return f"{oct:+.0f}o"
    if title == "Detun":
        cents = ((v - 64) / 64.0) * 200.0
        return f"{cents:+.0f}"
    if title == "Cutof":
        hz = 50.0 * (160.0 ** (v / 127.0))
        if hz > 8000.0: hz = 8000.0
        if hz >= 1000: return f"{hz/1000:.1f}k"
        return f"{hz:.0f}"
    if title == "Reson":
        q = 0.5 + (v / 127.0) * 11.5
        return f"{q:.1f}"
    if title == "Rate":
        if v == 0: return "OFF"
        # Assuming LFO range 0.05 to 20Hz (approx)
        hz = 0.05 * (2.0 ** ((v / 127.0) * 8.64))
        if hz < 10: return f"{hz:.2f}"
        return f"{hz:.1f}"
    if title in ["Attac", "Decay", "Relea", "Time"]:
        # Approx log scale 1ms to 10s
        s = 0.001 * (10.0 ** ((v / 127.0) * 4.0))
        if title == "Time": # FX Time different scale?
            # FX Time: 5ms to 1000ms
            ms = 5.0 * (2.0 ** ((v / 127.0) * 7.64))
            if ms >= 1000: return f"{ms/1000:.2f}s"
            return f"{ms:.0f}m"
        if s < 1.0: return f"{s*1000:.0f}m"
        return f"{s:.1f}s"
    if title == "Phase":
        deg = (v / 127.0) * 180.0
        return f"{deg:.0f}"
    if title == "Type":
        t = (v / 127.0) * 2.0
        if t < 0.25: return "LPF"
        if t < 0.75: return "L/B"
        if t < 1.25: return "BPF"
        if t < 1.75: return "B/H"
        return "HPF"
    if title == "Color":
        if v < 26: return "Whit"
        if v < 52: return "Pink"
        if v < 78: return "Red"
        if v < 104: return "Blue"
        return "Viol"
    if title == "Chord":
        if v == 0: return "OFF"
        if v == 12: return "MAJ"
        if v == 13: return "MIN"
        if v == 18: return "MAJ7"
        if v == 20: return "MIN7"
        return f"{v:03d}" # Fallback
    if title in ["Smoth", "Mix", "Mastr", "Fdbck", "Susta", "Sprea", "Tone", "Drive"]:
        pct = (v / 127.0) * 100.0
        return f"{pct:.0f}%"
    
    return f"{v:03d}"

# --- Waveform Drawing ---
def draw_waveform(oled, x, y, val):
    # Draw static pictogram waveforms from WAVEFORMS.
    # Map value to waveform index 0..5 (sin, saw, tri, rsaw, sqr, pam)
    t = val / 127.0
    seg_pos = t * 6.0
    seg = int(math.floor(seg_pos))
    if seg > 5: seg = 5

    w = 28
    # height of pictogram area is 6 pixels (bits 0..5)
    if seg < 0 or seg >= len(WAVE_ORDER):
        return
    name = WAVE_ORDER[seg]
    cols = WAVEFORMS.get(name)
    if not cols:
        return

    # Draw each column: bits 0..5 map to y..y+5
    for i, col in enumerate(cols[:w]):
        for bit in range(6):
            if (col >> bit) & 1:
                oled.set_pixel(x + i, y + bit, 1)

def draw_knob(oled, cx, cy, radius, val, title, mod, mod_amt, module_name, set_mode):
    # Title
    title_y = cy - radius - 2 - 7
    draw_centered_str(oled, cx, title_y, title)
    
    # Knob
    if set_mode:
        # Invert colors: Filled circle with black line
        oled.fill_circle(cx, cy, radius, 1) # Fill white
        
        start_deg = 225.0
        end_deg = -45.0
        ang = (start_deg + (end_deg - start_deg) * (val / 127.0)) * math.pi / 180.0
        lx = cx + int((radius-2) * math.cos(ang))
        ly = cy - int((radius-2) * math.sin(ang))
        oled.draw_line(cx, cy, lx, ly, 0) # Black line
    else:
        oled.draw_circle(cx, cy, radius)
        oled.draw_circle(cx, cy, radius-1)
        
        start_deg = 225.0
        end_deg = -45.0
        ang = (start_deg + (end_deg - start_deg) * (val / 127.0)) * math.pi / 180.0
        lx = cx + int((radius-2) * math.cos(ang))
        ly = cy - int((radius-2) * math.sin(ang))
        oled.draw_line(cx, cy, lx, ly)
    
    # Value
    val_y = cy + radius + 3
    
    if set_mode:
        # Show -100% to +100%
        pct = (val - 64) * 100 // 64
        buf = f"{pct:+}%"
        draw_centered_str(oled, cx, val_y, buf, 0) # Black text? No, background is black, we draw on top of what?
        # Wait, if we are outside the knob, we draw normally (white on black).
        # The knob is filled, but the text is below it.
        # So text should be white (1).
        draw_centered_str(oled, cx, val_y, buf, 1)
    else:
        if title == "Wave":
            wx = cx - 14
            draw_waveform(oled, wx, val_y, val)
        else:
            buf = format_val(module_name, title, val)
            draw_centered_str(oled, cx, val_y, buf)
    
    # Bar Graph
    bar_y = val_y + 7 + 2
    bar_w = radius * 2
    bar_h = 4
    bar_x = cx - bar_w // 2
    
    oled.draw_h_line(bar_x + 1, bar_y, bar_w - 2)
    oled.draw_h_line(bar_x + 1, bar_y + bar_h - 1, bar_w - 2)
    oled.draw_v_line(bar_x, bar_y + 1, bar_h - 2)
    oled.draw_v_line(bar_x + bar_w - 1, bar_y + 1, bar_h - 2)
    
    center_x = bar_x + bar_w // 2
    if set_mode:
        # Draw from center based on modulation depth (val - 64)
        depth = val - 64
        # map -64..63 to width
        fill_w = int((depth / 64.0) * (bar_w // 2 - 1))
        if fill_w > 0:
            for fx in range(fill_w):
                oled.draw_v_line(center_x + 1 + fx, bar_y + 1, bar_h - 2)
        elif fill_w < 0:
            for fx in range(abs(fill_w)):
                oled.draw_v_line(center_x - 1 - fx, bar_y + 1, bar_h - 2)
    else:
        # Standard modulation amount
        if mod_amt != 0:
            fill_w = int((mod_amt / 100.0) * (bar_w // 2 - 1))
            if fill_w > 0:
                for fx in range(fill_w):
                    oled.draw_v_line(center_x + 1 + fx, bar_y + 1, bar_h - 2)
            elif fill_w < 0:
                for fx in range(abs(fill_w)):
                    oled.draw_v_line(center_x - 1 - fx, bar_y + 1, bar_h - 2)
    
    oled.draw_v_line(center_x, bar_y + 1, bar_h - 2)
    
    center_mark_y = bar_y + bar_h
    oled.set_pixel(cx, center_mark_y, 1)

    mod_text_y = center_mark_y + 3
    label = mod if mod else "---"
    draw_centered_str(oled, cx, mod_text_y, label)


def render_offline(oled):
    oled.clear()
    # Simple centered OFFLINE text on screen
    draw_centered_str(oled, WIDTH // 2, (HEIGHT // 2) - 4, "OFFLINE", 1)


def render_ui(oled, page):
    oled.clear()
    
    if page.get("offline"):
        render_offline(oled)
        return

    radius = 12
    cy = 23
    
    x = [17, 48, 79, 110]
    
    oled.draw_line(0, 0, 127, 0)
    oled.draw_line(0, 63, 127, 63)
    oled.draw_line(0, 0, 0, 63)
    oled.draw_line(127, 0, 127, 63)
    
    set_mode = page.get("set_mode", False)
    for i in range(4):
        k = page["knobs"][i]
        draw_knob(oled, x[i], cy, radius, k["value"], k["title"], k["mod_label"], k["mod_amount"], page.get("module", ""), set_mode)

# --- Main ---
def is_data_available():
    return select.select([sys.stdin], [], [], 0) == ([sys.stdin], [], [])

def main():
    # Initial check for target port candidate (don't exit if missing)
    if len(sys.argv) < 2:
        port = get_config_port()
        if port:
            print(f"Initial port candidate: {port}")
        else:
            print("No serial ports found. Starting in OFFLINE mode, waiting for device...")
    else:
        port = sys.argv[1]
    
    ser = None
    
    # Create shared state
    synth_state = SynthState()
    
    # Start serial thread (handles connection, reading, and auto-reconnect)
    def serial_worker():
        nonlocal ser, port
        while True:
            try:
                if ser is None:
                    # Connection / reconnect attempt
                    # Only accept tty.usbmodem ports, ignoring cu.*, debug-console, etc.
                    candidate = port if (isinstance(port, str) and port.startswith("/dev/tty.usbmodem")) else None
                    if not candidate:
                        candidate = get_config_port()
                    if not candidate:
                        synth_state.set_online(False)
                        time.sleep(1.0)
                        continue
                    try:
                        ser = Serial(candidate, 115200, timeout=0.1)
                        port = candidate
                        synth_state.set_online(True)
                        print(f"Connected to {port}")
                    except SerialException:
                        synth_state.set_online(False)
                        time.sleep(1.0)
                        continue

                if ser and ser.in_waiting:
                    line = ser.readline().decode('utf-8', errors='ignore')
                    if line:
                        if "STEPATENO_OLED_READY" in line:
                            synth_state.set_online(True)
                        synth_state.update_from_log(line)
            except SerialException:
                # Port connection lost — transition to OFFLINE mode and attempt reconnect
                synth_state.set_online(False)
                try:
                    if ser:
                        ser.close()
                except Exception:
                    pass
                ser = None
                time.sleep(1.0)
            except Exception:
                # Catch-all for read errors — wait before retrying
                time.sleep(0.1)

            time.sleep(0.001)
            
    t = threading.Thread(target=serial_worker)
    t.daemon = True
    t.start()

    oled = OledDisplay(WIDTH, HEIGHT)
    console = Console()
    
    # Save terminal settings for raw mode (to catch 'Q')
    old_settings = termios.tcgetattr(sys.stdin)
    try:
        tty.setcbreak(sys.stdin.fileno())
        
        with Live(console=console, refresh_per_second=10) as live:
            while True:
                # Check for 'Q'
                if is_data_available():
                    c = sys.stdin.read(1)
                    if c.lower() == 'q':
                        break
                
                if synth_state.updated:
                    page = synth_state.get_oled_page()
                    render_ui(oled, page)
                    text = oled.render_to_text()
                    
                    body = Align.center(Text(text))
                    panel = Panel(
                        body,
                        title=f"Stepateno OLED - {page['module']}{' [SET]' if page['set_mode'] else ''}",
                        subtitle="Press 'Q' to quit",
                        width=WIDTH + 4,
                        height=(HEIGHT // 2) + 4
                    )
                    live.update(panel)
                    synth_state.updated = False
                
                time.sleep(0.05)
    finally:
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_settings)
        try:
            if ser:
                ser.close()
        except Exception:
            pass

if __name__ == "__main__":
    main()
