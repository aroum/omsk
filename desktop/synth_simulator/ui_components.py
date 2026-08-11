from PySide6.QtWidgets import QWidget, QSizePolicy, QLabel, QDial, QVBoxLayout
from PySide6.QtCore import Qt, Signal, QPointF, QTimer
from PySide6.QtGui import QPainter, QColor, QPen
import numpy as np

class OscilloscopeWidget(QWidget):
    def __init__(self, engine, parent=None):
        super().__init__(parent)
        self.engine = engine
        self.setMinimumHeight(200) # Double height
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.setAttribute(Qt.WA_StyledBackground, True)
        
        # Timer for 60 FPS update
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.update_scope)
        self.timer.start(16)
        
    def update_scope(self):
        self.update()
        
    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        
        width = self.width()
        height = self.height()
        half_width = width // 2
        
        # Draw Scope (Left Half)
        data = self.engine.scope_buffer
        if data is None or len(data) == 0:
            return
            
        # Synchronization (Rising Edge Trigger with Threshold)
        # Search in the first half of the buffer to leave room for drawing
        trigger_idx = 0
        threshold = 0.05
        search_limit = len(data) - half_width
        
        for i in range(10, search_limit):
            # Check for zero crossing with positive slope
            if data[i] < 0 and data[i+1] >= 0:
                # Add a small "hysteresis" or threshold check to avoid noise triggering
                if data[i+1] - data[i] > 0.001: 
                    trigger_idx = i
                    break
        
        # If no trigger found, just start from 0
        if trigger_idx == 0:
            # Fallback: maybe look for just positive value if we are stuck
            for i in range(search_limit):
                if data[i] > threshold:
                    trigger_idx = i
                    break
             
        # Scope Plot
        mid_y = height / 2
        
        pen = QPen(QColor(0, 255, 0))
        pen.setWidth(2)
        painter.setPen(pen)
        
        points = []
        samples_to_show = min(len(data) - trigger_idx, half_width)
        
        for x in range(samples_to_show):
            val = data[trigger_idx + x]
            # mid_y - (val * mid_y * 0.9 * 1.2) -> 1.08 factor (approx 1.1)
            # Increased vertical zoom by 20% (0.9 -> 1.08)
            y = mid_y - (val * mid_y * 1.08)
            points.append((x, y))
            
        if len(points) > 1:
            for i in range(len(points) - 1):
                painter.drawLine(points[i][0], points[i][1], points[i+1][0], points[i+1][1])

        # Draw Spectrum (Right Half)
        fft_data = np.abs(np.fft.rfft(data))
        n_bins = len(fft_data)
        # Show up to 10kHz
        max_bin = int(n_bins * (10000.0 / 22050.0))
        
        painter.setBrush(QColor(0, 255, 0, 100)) # Green with alpha
        painter.setPen(Qt.NoPen)
        
        poly_points = []
        poly_points.append(QPointF(half_width, height)) 
        
        if max_bin > 1:
            step_x = half_width / max_bin
            
            for i in range(max_bin):
                mag = fft_data[i]
                y = height - (mag * 4.0) 
                y = max(0, min(height, y))
                
                x = half_width + i * step_x
                poly_points.append(QPointF(x, y))
        
        poly_points.append(QPointF(width, height)) 
        painter.drawPolygon(poly_points)
        
        # Draw divider
        painter.setPen(QPen(QColor("#555")))
        painter.drawLine(half_width, 0, half_width, height)


class ValueBarLabel(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.text = "0.0"
        self.bar_value = 0.0
        self.bar_color = QColor(0, 0, 0, 0)
        self.setFixedHeight(30)
        self.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Fixed)
        
    def set_text(self, text):
        if self.text != text:
            self.text = text
            self.update()
            
    def set_bar_value(self, value, color):
        self.bar_value = value
        self.bar_color = color
        self.update()
        
    def paintEvent(self, event):
        painter = QPainter(self)
        w = self.width()
        h = self.height()
        
        # Draw Text
        painter.setPen(Qt.white)
        font = painter.font()
        font.setBold(True)
        painter.setFont(font)
        painter.drawText(0, 0, w, h-10, Qt.AlignCenter, self.text)
        
        # Draw Bar
        bar_y = h - 8
        bar_h = 6
        center_x = w / 2
        val = max(-1.0, min(1.0, self.bar_value))
        bar_w_pixels = int(abs(val) * (w / 2 - 2))
        
        painter.setBrush(self.bar_color)
        painter.setPen(Qt.NoPen)
        
        # Background
        painter.setBrush(QColor(60, 60, 60))
        painter.drawRect(2, bar_y, w-4, bar_h)
        
        # Active
        painter.setBrush(self.bar_color)
        if val > 0:
            painter.drawRect(center_x, bar_y, bar_w_pixels, bar_h)
        else:
            painter.drawRect(center_x - bar_w_pixels, bar_y, bar_w_pixels, bar_h)
            
        painter.setPen(QPen(QColor(200, 200, 200), 1))
        painter.drawLine(int(center_x), bar_y, int(center_x), bar_y + bar_h)

class ModulatableDial(QDial):
    clicked = Signal() 
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.last_press_pos = None
        self.multiturn_factor = 1.0 # 1.0 = normal (1 turn), 3.0 = 3 turns
        self._internal_value = 0.0 # 0.0 to 1.0 range
        
    def set_multiturn(self, factor):
        self.multiturn_factor = factor
        
    def set_internal_value(self, val):
        """Sets the 0.0-1.0 value and updates the dial position (visual only)"""
        self._internal_value = max(0.0, min(1.0, val))
        # Map 0.0-1.0 to the visual range 0-1000 of the QDial
        # For multiturn, the dial just shows the 'fractional' part of the turn
        product = self._internal_value * self.multiturn_factor
        
        # If we are exactly at the end of the range, stay at 1000
        if self._internal_value >= 1.0:
            visual_val = 1000
        else:
            # Otherwise show the fractional part of the current turn
            # Using a small epsilon to make sure 1.0 on a turn shows 1000 not 0
            frac = product % 1.0
            if frac == 0 and product > 0:
                visual_val = 1000
            else:
                visual_val = int(frac * 1000)
        
        # Block signals to avoid feedback loops when setting programmatically
        self.blockSignals(True)
        self.setValue(visual_val)
        self.blockSignals(False)

    def mousePressEvent(self, event):
        self.last_press_pos = event.position()
        self.last_mouse_y = event.position().y()
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if event.buttons() & Qt.LeftButton:
            # Vertical drag for precise control
            delta_y = self.last_mouse_y - event.position().y()
            self.last_mouse_y = event.position().y()
            
            # Sensitivity: a full drag of say 500 pixels covers the whole range
            sensitivity = 0.002 / self.multiturn_factor
            new_val = self._internal_value + delta_y * sensitivity
            self.set_internal_value(new_val)
            
            # Emit valueChanged manually with the 0-1000 scaled value for compatibility
            self.valueChanged.emit(int(self._internal_value * 1000))
        else:
            super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        if self.last_press_pos:
            delta = event.position() - self.last_press_pos
            dist = abs(delta.x()) + abs(delta.y())
            if dist < 15:
                self.clicked.emit()
        super().mouseReleaseEvent(event)
