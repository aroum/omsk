import sys
import os
import numpy as np
import sounddevice as sd
import soundfile as sf
import random
import json

from PySide6.QtWidgets import (QWidget)
from PySide6.QtCore import Qt, QTimer, QRect, QPoint, QSize, QEvent, Signal
from PySide6.QtGui import QPainter, QColor, QPen, QFont

def generate_window(shape, length):
    """Generates a window function for grain envelopes."""
    if shape == "Hanning": return np.hanning(length)
    if shape == "Tri": return np.bartlett(length)
    if shape == "Rect": return np.ones(length)
    if shape == "Blackman": return np.blackman(length)
    return np.hanning(length)


class OLED(QWidget):
    def __init__(self, main_app):
        super().__init__()
        self.main_app = main_app
        self.oled_width, self.oled_height = 128, 64
        self.scale_factor = 3.5
        self.setFixedSize(self.oled_width*self.scale_factor + 36, self.oled_height*self.scale_factor + 80) 
        QTimer(self, timeout=self.update).start(1000//30)

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        engine = self.main_app.engines[self.main_app.active_voice]
        vw, vh = self.oled_width*self.scale_factor, self.oled_height*self.scale_factor
        p.setPen(QPen(QColor(80, 80, 80), 8))
        p.drawRect(QRect(18, 18, vw, vh + 50).adjusted(-4, -4, 4, 4))
        p.fillRect(18, 18, vw, vh, Qt.black)
        p.save()
        p.translate(18, 18)
        p.scale(self.scale_factor, self.scale_factor)
        if engine.env_pos:
            p.setPen(QPen(Qt.white, 1))
            for i in range(len(engine.env_pos)-1):
                p.drawLine(i, 32-int(engine.env_pos[i]*30), i+1, 32-int(engine.env_pos[i+1]*30))
                p.drawLine(i, 32+int(engine.env_pos[i]*30), i+1, 32+int(engine.env_pos[i+1]*30))
        px = int(((engine.params["pos"][0] + engine.playback_pos) % 1.0) * self.oled_width)
        p.setPen(QPen(Qt.white, 1))
        p.drawLine(px, 0, px, self.oled_height)
        for g in engine.active_grains:
            gx = int((g.get_current_sample_idx() / (len(engine.sample_data) or 1)) * self.oled_width)
            gy = self.oled_height - int((g.pitch / 4.0) * self.oled_height)
            p.drawPoint(gx, max(1, min(63, gy)))
        p.restore()
        p.setPen(Qt.white)
        p.setFont(QFont(UI_FONT_NAME, UI_FONT_SIZE, QFont.Bold))
        p.drawText(QRect(18, vh + 18, vw, 40), Qt.AlignCenter, engine.current_filename.upper())
