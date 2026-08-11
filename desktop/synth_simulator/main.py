import sys
import argparse
import json
import os
import numpy as np
from functools import partial
from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                               QHBoxLayout, QGridLayout, QPushButton, QLabel, QDial, QFrame, QSizePolicy, QStyleFactory)
from PySide6.QtCore import Qt, Slot, QTimer, QPointF, Signal
from PySide6.QtGui import QPainter, QColor, QPen, QKeyEvent
from audio_engine import AudioEngine
from ui_components import OscilloscopeWidget, ValueBarLabel, ModulatableDial
from synth_data import *

class SynthWindow(QMainWindow):
    def __init__(self, debug=False):
        super().__init__()
        self.setWindowTitle("PySide6 Analog Synth")
        self.resize(800, 700)
        
        # Audio Engine
        self.engine = AudioEngine(debug=debug)
        self.engine.start()
        
        # Global UI State
        self.mode = "PIANO" # "PIANO" or "SETTINGS"
        self.pressed_keys = set()
        self.active_voice_keys = {} # key_code -> freq
        
        # Piano State
        self.octave_shift = 0 # Momentary shift (-1 or +1)
        self.hold_active = False
        self.latched_keys = {} # Dict of (r,c) -> freq currently latched
        self.held_freqs = {} # Dict of (r,c) -> freq currently physically held (for ARP release reliability)
        self.physically_pressed_grid = set() # Set of (r,c) physically held by user (keyboard or mouse)
        self.z_pressed = False
        self.z_combo_used = False
        self.ignore_latch = set() # Set of (r,c) to ignore on release (for toggle off behavior)
        
        # Settings State
        self.current_module = "VCO1"
        self.set_mode_active = False 
        self.mod_source_module = None 
        self.rm_mode_active = False
        
        # Mapping Configuration
        self.knob_mappings = KNOB_MAPPINGS
        self.layer_colors = LAYER_COLORS
        
        # Grid Mapping (Row, Col)
        self.key_grid_map = KEY_GRID_MAP
        
        # Labels for Modes
        self.labels_piano = LABELS_PIANO
        self.labels_settings = LABELS_SETTINGS
        
        # Note Frequencies (Base C3=261.63 approx)
        self.note_map = NOTE_MAP

        # UI Components
        self.main_widget = QWidget()
        self.setCentralWidget(self.main_widget)
        self.main_layout = QVBoxLayout(self.main_widget)
        
        # Combined container for Knobs + Grid to ensure alignment
        self.controls_widget = QWidget()
        self.controls_layout = QGridLayout(self.controls_widget)
        self.controls_layout.setAlignment(Qt.AlignCenter)
        self.controls_layout.setVerticalSpacing(18)
        self.controls_layout.setHorizontalSpacing(12)
        
        # Lock layout to prevent shifting
        for r in range(5): self.controls_layout.setRowStretch(r, 0)
        for c in range(4): self.controls_layout.setColumnStretch(c, 0)
        
        # Force fixed row heights / col widths
        self.controls_layout.setRowMinimumHeight(0, 120) # Knobs row
        for r in range(1, 5):
            self.controls_layout.setRowMinimumHeight(r, 60) # Button rows
            
        for c in range(4):
            self.controls_layout.setColumnMinimumWidth(c, 80) # Button cols
        
        self.setup_knobs(self.controls_layout)
        self.setup_grid(self.controls_layout)
        
        self.main_layout.addWidget(self.controls_widget)
        
        # Oscilloscope
        self.scope = OscilloscopeWidget(self.engine)
        self.main_layout.addWidget(self.scope)
        
        # Timer for UI updates
        self.ui_timer = QTimer(self)
        self.ui_timer.timeout.connect(self.update_ui_visuals)
        self.ui_timer.start(33) 
        
        # Initial Update
        self.update_knob_display()
        self.update_grid_labels()
        self.apply_styles()
        self.update_button_styles()
        
        self.load_config()

    def setup_knobs(self, grid_layout):
        self.knobs = []
        self.knob_labels = []
        self.knob_values = []
        
        for i in range(4):
            # Container for each knob stack (Label + Dial + Value)
            container = QWidget()
            container.setFixedWidth(80) # Lock width to match buttons
            v_layout = QVBoxLayout(container)
            v_layout.setAlignment(Qt.AlignCenter)
            v_layout.setContentsMargins(0, 0, 0, 0)
            v_layout.setSpacing(5)
            
            lbl = QLabel(f"Param {i+1}")
            lbl.setAlignment(Qt.AlignCenter)
            lbl.setStyleSheet("font-weight: bold;")
            lbl.setFixedSize(80, 25) # Fixed width 80
            self.knob_labels.append(lbl)
            sp_lbl = lbl.sizePolicy()
            sp_lbl.setRetainSizeWhenHidden(True)
            lbl.setSizePolicy(sp_lbl)
            
            dial = ModulatableDial()
            dial.setFixedSize(50, 50)
            dial.setRange(0, 1000) 
            dial.setValue(0)
            dial.setNotchesVisible(True)
            dial.valueChanged.connect(lambda val, idx=i: self.on_knob_change(idx, val))
            dial.clicked.connect(lambda idx=i: self.on_knob_click(idx))
            
            self.knobs.append(dial)
            sp_dial = dial.sizePolicy()
            sp_dial.setRetainSizeWhenHidden(True)
            dial.setSizePolicy(sp_dial)

            val_lbl = ValueBarLabel()
            val_lbl.setFixedWidth(80) # Fixed width 80
            self.knob_values.append(val_lbl)
            sp_val = val_lbl.sizePolicy()
            sp_val.setRetainSizeWhenHidden(True)
            val_lbl.setSizePolicy(sp_val)
            
            v_layout.addWidget(lbl)
            v_layout.addWidget(dial, 0, Qt.AlignCenter)
            v_layout.addWidget(val_lbl)
            
            # Add to Grid Row 0, Col i
            grid_layout.addWidget(container, 0, i, alignment=Qt.AlignCenter)
            
    def setup_grid(self, grid_layout):
        self.grid_buttons = {} # (r, c) -> QPushButton
        
        for r in range(4):
            for c in range(4):
                btn = QPushButton()
                btn.setCheckable(False) # We manage state manually or via setDown
                btn.setFixedSize(80, 60)
                # Mouse click support for settings mode (or piano mode)
                # btn.clicked.connect(partial(self.on_grid_button_click, r, c))
                btn.pressed.connect(partial(self.handle_grid_press, r, c))
                btn.released.connect(partial(self.handle_grid_release, r, c))
                
                # Add to Grid Row r+1, Col c
                grid_layout.addWidget(btn, r+1, c, alignment=Qt.AlignCenter)
                self.grid_buttons[(r,c)] = btn

    def keyPressEvent(self, event):
        key = event.key()
        if event.isAutoRepeat():
            return
            
        self.pressed_keys.add(key)
        
        # Check Mode Switch Combo (Z + X)
        if Qt.Key_Z in self.pressed_keys and Qt.Key_X in self.pressed_keys:
            self.toggle_mode()
            self.z_combo_used = True
            return

        # Check Hold Combo (C + V)
        if Qt.Key_C in self.pressed_keys and Qt.Key_V in self.pressed_keys:
            if not self.z_combo_used: # Reuse z_combo_used concept or just use hold logic
                self.set_hold_active(not self.hold_active)
                self.z_combo_used = True # Prevent normal release logic
            return

        if key == Qt.Key_Z:
            self.z_pressed = True
            self.z_combo_used = False
            
        if key in self.key_grid_map:
            r, c = self.key_grid_map[key]
            self.handle_grid_press(r, c)
            
    def keyReleaseEvent(self, event):
        key = event.key()
        if event.isAutoRepeat():
            return
            
        if key in self.pressed_keys:
            self.pressed_keys.remove(key)

        was_combo = False
        if key == Qt.Key_Z:
            was_combo = self.z_combo_used
            self.z_pressed = False
            self.z_combo_used = False
            
        if key in self.key_grid_map:
            r, c = self.key_grid_map[key]
            self.handle_grid_release(r, c, was_combo=was_combo)

    def toggle_mode(self):
        # Switch between PIANO and SETTINGS
        if self.mode == "PIANO":
            self.mode = "SETTINGS"
            print("Switched to SETTINGS Mode")
            # Reset momentary octave shift when leaving PIANO mode
            self.octave_shift = 0
        else:
            self.mode = "PIANO"
            print("Switched to PIANO Mode")
            # Exit SET/RM modes if active?
            self.set_mode_active = False
            self.rm_mode_active = False
            # Also reset octave shift when returning to PIANO to be safe
            self.octave_shift = 0
            
        self.update_grid_labels()
        self.update_button_styles()

    def set_hold_active(self, active):
        self.hold_active = active
        print(f"HOLD Active: {self.hold_active}")
        
        if not self.hold_active:
            # Release all latched notes
            # latched_keys is dict: (r,c) -> freq
            for (r, c), latched_freq in list(self.latched_keys.items()):
                # Only release if NOT physically held
                # How to check physically held?
                # We can check if (r,c) is in active_voice_keys?
                # No, active_voice_keys tracks sounding voices.
                # We need to know if the USER is pressing it.
                # We can deduce from pressed_keys mapping.
                
                is_physically_held = False
                for k in self.pressed_keys:
                    if self.key_grid_map.get(k) == (r, c):
                        is_physically_held = True
                        break
                
                # Also check mouse? (grid_buttons down?)
                if self.grid_buttons[(r,c)].isDown():
                     is_physically_held = True
                     
                if not is_physically_held:
                    if (r,c) in self.active_voice_keys:
                        freq = self.active_voice_keys.pop((r,c))
                        self.engine.note_off(freq)
                    
                    # Release from ARP using stored frequency
                    self.engine.arp_release(latched_freq)
            
            self.latched_keys.clear()
            
        self.update_button_styles()

    def handle_grid_press(self, r, c):
        # Simulate visual press
        btn = self.grid_buttons.get((r, c))
        if btn: btn.setDown(True)
        self.physically_pressed_grid.add((r, c))
        self.update_button_styles()
        
        if self.mode == "PIANO":
            if r < 3: # Note Rows
                if (r, c) in self.note_map:
                    base_freq = self.note_map[(r, c)]
                    # Apply Octave Shift
                    freq = base_freq * (2.0 ** self.octave_shift)
                    
                    # Store freq for reliable release
                    self.held_freqs[(r, c)] = freq
                    
                    # ARP Logic: Always register physical press
                    self.engine.arp_press(freq)
                    
                    # Sound Logic
                    arp_active = self.engine.params["ARP"]["mode"] > 0.16
                    
                    # Toggle Logic for HOLD Mode
                    if self.hold_active and (r, c) in self.latched_keys:
                        # User wants to toggle OFF a latched note
                        latched_freq = self.latched_keys.pop((r, c))
                        
                        # Stop sound
                        if (r, c) in self.active_voice_keys:
                            old_freq = self.active_voice_keys.pop((r, c))
                            self.engine.note_off(old_freq)
                        
                        # Stop ARP (remove the latched frequency, which might differ from current press if octave changed)
                        self.engine.arp_release(latched_freq)
                        
                        self.ignore_latch.add((r, c))
                        return # Don't re-trigger
                    
                    if not arp_active:
                        self.engine.note_on(freq)
                        self.active_voice_keys[(r,c)] = freq
            
            elif r == 3: # Control Row
                if c == 0: # OCT - (Was Z)
                    # Z is removed from map, so this might not be reachable via keyboard
                    # But reachable via Mouse Click on UI
                    self.octave_shift = max(-5, self.octave_shift - 1)
                    print(f"Octave Shift: {self.octave_shift}")
                elif c == 1: # OCT +
                    self.octave_shift = min(5, self.octave_shift + 1)
                    print(f"Octave Shift: {self.octave_shift}")
        
        elif self.mode == "SETTINGS":
            # Logic moved to release to handle combos/holding
            pass

    def handle_grid_release(self, r, c, was_combo=False):
        btn = self.grid_buttons.get((r, c))
        if btn: btn.setDown(False)
        if (r, c) in self.physically_pressed_grid:
            self.physically_pressed_grid.remove((r, c))
        self.update_button_styles()
        
        if self.mode == "PIANO":
            if r < 3:
                if (r, c) in self.note_map:
                    # Use stored freq if available to ensure matching note_off
                    # This fixes stuck notes if Octave Shift changed while holding key
                    freq = self.held_freqs.get((r, c))
                    if freq is None:
                         # Fallback (should not happen if logic is correct)
                         base_freq = self.note_map[(r, c)]
                         freq = base_freq * (2.0 ** self.octave_shift)
                    
                    # Cleanup held_freqs
                    if (r,c) in self.held_freqs:
                        del self.held_freqs[(r,c)]
                    
                    # ARP Logic: Handle physical release
                    # If HOLD is active, we DO NOT release from ARP (unless toggling off via ignore_latch)
                    if (not self.hold_active) or ((r, c) in self.ignore_latch):
                        self.engine.arp_release(freq)
                
                # Check for Toggle Off Ignore
                if (r, c) in self.ignore_latch:
                    self.ignore_latch.remove((r, c))
                    self.update_button_styles()
                    return

                if self.hold_active:
                    # Latch Mode (Drone)
                    self.latched_keys[(r,c)] = freq
                    
                    # Ensure it is sounding (if it was silent due to Arp)
                    if (r,c) not in self.active_voice_keys:
                         # Recalculate freq as we didn't find it in active_keys
                         # freq variable is already set above, but let's be safe if it came from active_voice_keys logic
                         if (r,c) not in self.active_voice_keys:
                             self.engine.note_on(freq)
                             self.active_voice_keys[(r,c)] = freq
                         
                    # Ensure color update
                    self.update_button_styles()
                else:
                    # Normal Release
                    if (r,c) in self.active_voice_keys:
                        freq = self.active_voice_keys.pop((r,c))
                        self.engine.note_off(freq)
                        
                    # Also remove from latched if present (re-press case)
                    if (r,c) in self.latched_keys:
                        del self.latched_keys[(r,c)]
            
            elif r == 3:
                if c == 0: # OCT - Release
                    # self.octave_shift += 1 # Momentary behavior?
                    # Code had += 1 on release for OCT-? That undoes the press?
                    # "Momentary shift" comment suggests yes.
                    self.octave_shift = min(5, self.octave_shift + 1)
                    print(f"Octave Shift: {self.octave_shift}")
                elif c == 1: # OCT + Release
                    self.octave_shift = max(-5, self.octave_shift - 1)
                    print(f"Octave Shift: {self.octave_shift}")
        
        elif self.mode == "SETTINGS":
            if was_combo: return
            if len(self.pressed_keys) > 0: return
            self.on_grid_button_click(r, c)

    def on_grid_button_click(self, r, c):
        # Logic for SETTINGS mode mainly
        # If Piano mode, clicks should probably play notes too?
        # User said "Keyboard has two modes".
        
        if self.mode == "SETTINGS":
            name = self.labels_settings[r][c]
            
            if name == "SET":
                self.toggle_set_mode()
            elif name == "RM":
                self.toggle_rm_mode()
            elif name == "MOD":
                self.current_module = "MOD"
                self.update_knob_display()
            elif name == "ARP":
                self.current_module = "ARP"
                self.update_knob_display()
            elif name == "GLIDE":
                self.current_module = "GLIDE"
                self.update_knob_display()
            else:
                # Module Select
                self.select_module(name)
                
            self.update_button_styles()

    def update_grid_labels(self):
        labels = self.labels_piano if self.mode == "PIANO" else self.labels_settings
        for r in range(4):
            for c in range(4):
                btn = self.grid_buttons[(r, c)]
                btn.setText(labels[r][c])

    def update_button_styles(self):
        # Update colors based on Mode
        
        # Check Active States
        hold_is_active = self.hold_active
        
        arp_mode_val = self.engine.params["ARP"]["mode"]
        arp_is_active = arp_mode_val > 0.166
        
        glide_mode_val = self.engine.params["GLIDE"]["mode"]
        glide_is_active = glide_mode_val > 0.25
        
        fx_mix_val = self.engine.params["FX"]["mix"]
        fx_is_active = fx_mix_val > 0.05
        
        mixer_color = self.layer_colors["MIXER"] # #888888
        pink_color = "#FF00FF"
        
        for r in range(4):
            for c in range(4):
                btn = self.grid_buttons[(r, c)]
                text = btn.text()
                
                style = "color: #fff; font-weight: bold; font-size: 14px;"
                bg_color = "#444"
                border = "border: 1px solid #555;"
                
                # Default Coloring Logic
                if self.mode == "PIANO":
                    if r < 3:
                        # Notes
                        is_physically_pressed = (r,c) in self.physically_pressed_grid
                        is_latched = (r,c) in self.latched_keys
                        if self.hold_active and (is_physically_pressed or is_latched):
                             bg_color = pink_color # Pink if Hold Active and (Pressed OR Latched)
                        elif text.endswith("#"):
                            bg_color = "#222" # Black key
                        else:
                            bg_color = "#666" # White key
                    else:
                        # Controls (Row 3)
                        if text == "fn2": # Key V
                             bg_color = pink_color if self.hold_active else "#333"
                        else:
                             bg_color = "#333"
                        
                else: # SETTINGS
                    if text == "SET":
                        bg_color = pink_color if self.set_mode_active else "#444"
                    elif text == "RM":
                        bg_color = "#FF0000" if self.rm_mode_active else "#444"
                    elif text == "FX":
                        bg_color = pink_color if fx_is_active else self.layer_colors["FX"]
                    elif text in self.layer_colors:
                        # Module colors
                        base = self.layer_colors[text]
                        
                        # Logic for ARP/GLIDE if they are modules:
                        if text == "ARP" and not arp_is_active:
                            bg_color = mixer_color
                        elif text == "GLIDE" and not glide_is_active:
                            bg_color = mixer_color
                        else:
                            bg_color = base
                            
                        # Highlight Selected Module
                        if text == self.current_module:
                             style += "color: #000;" 
                             border = "border: 2px solid #fff;"
                    
                    # Check Routing for Source Modules (VCO/NOISE)
                    if text in ["VCO1", "VCO2", "NOISE"]:
                        dest = self.engine.signal_routing.get(text, "VCF1")
                        if dest == "VCF1":
                            bg_color = self.layer_colors["VCF1"]
                        elif dest == "VCF2":
                            bg_color = self.layer_colors["VCF2"]

                # OVERRIDE: Status Highlighting (Row 3: 0, 1, 2)
                # In PIANO mode, we only highlight status on dedicated keys
                # In SETTINGS mode, we highlight the module buttons
                
                if r == 3:
                    if self.mode == "SETTINGS":
                        if c == 0 and fx_is_active:
                            bg_color = pink_color
                        elif c == 1 and arp_is_active:
                            bg_color = pink_color
                        elif c == 2 and glide_is_active:
                            bg_color = pink_color
                    else: # PIANO mode
                        # (3,0) = OCT-
                        # (3,1) = OCT+
                        # (3,2) = fn1
                        # (3,3) = fn2 (Hold) -> already handled above in text == "fn2"
                        pass

                # Apply Style
                # Pressed state: White border
                pressed_style = f"QPushButton:pressed {{ background-color: {bg_color}; border: 2px solid white; }}"
                
                btn.setStyleSheet(f"QPushButton {{ background-color: {bg_color}; {style} {border} }} {pressed_style}")

    def select_module(self, name):
        # Logic from old select_module
        name = name.strip()
        print(f"Selected: {name}")
        
        # Handle RM
        if self.rm_mode_active:
             if name in ["LFO1", "LFO2", "EG1", "EG2"]:
                 # Clear assignments
                 keys = [k for k, v in self.engine.mod_assignments.items() if v[0] == name]
                 for k in keys: del self.engine.mod_assignments[k]
             elif name in ["VCF1", "VCF2"]:
                 # Clear routing to
                 keys = [k for k, v in self.engine.signal_routing.items() if v == name]
                 for k in keys: self.engine.signal_routing[k] = None
             elif name in ["VCO1", "VCO2", "NOISE"]:
                 self.engine.signal_routing[name] = None
             
             self.rm_mode_active = False
             self.update_button_styles()
             return

        # Handle SET
        if self.set_mode_active:
            # Routing
            if self.mod_source_module in ["VCF1", "VCF2"]:
                if name in ["VCO1", "VCO2", "NOISE"]:
                    self.engine.signal_routing[name] = self.mod_source_module
                    self.set_mode_active = False
                    self.update_button_styles()
                    return
            
            # Modulation Target Select
            # If we are in SET mode and source is LFO/EG, selecting a new module
            # should just switch view to that module so we can click a knob.
            if self.mod_source_module in ["LFO1", "LFO2", "EG1", "EG2"]:
                self.current_module = name
                self.update_knob_display()
                self.update_button_styles()
                # DO NOT return here, because we want to update the current module view
                # but remain in SET mode until a KNOB is clicked.
                return

        self.current_module = name
        self.update_knob_display()
        self.update_button_styles()

    def toggle_set_mode(self):
        if self.rm_mode_active: # Save Config
            self.save_config()
            self.rm_mode_active = False
            self.set_mode_active = False
        else:
            self.set_mode_active = not self.set_mode_active
            if self.set_mode_active:
                self.mod_source_module = self.current_module
            else:
                self.mod_source_module = None
        self.update_button_styles()
        self.update_knob_display()

    def toggle_rm_mode(self):
        if self.set_mode_active: # Save Config
            self.save_config()
            self.set_mode_active = False
            self.rm_mode_active = False
        else:
            self.rm_mode_active = not self.rm_mode_active
            self.set_mode_active = False
        self.update_button_styles()
        self.update_knob_display()

    def on_knob_change(self, idx, val_int):
        # Multi-turn support: we use the internal value from the dial directly if it was a mouse move
        # but for compatibility we still support the val_int (which is 0-1000)
        dial = self.knobs[idx]
        if hasattr(dial, '_internal_value'):
            val = dial._internal_value
        else:
            val = val_int / 1000.0
        
        mapping = self.knob_mappings.get(self.current_module, [])
        if idx < len(mapping):
            param_key = mapping[idx][1]
            
            # Handle Modulation Depth Editing (If SET mode is active and assignment exists)
            key = (self.current_module, param_key)
            if self.set_mode_active and key in self.engine.mod_assignments:
                source, _ = self.engine.mod_assignments[key]
                # Update depth
                self.engine.mod_assignments[key] = (source, val)
                self.update_knob_display()
                return

            self.engine.params[self.current_module][param_key] = val
            self.update_knob_display()

    def on_knob_click(self, idx):
        print(f"Knob {idx} clicked. Set Mode: {self.set_mode_active}, RM Mode: {self.rm_mode_active}")
        if self.set_mode_active:
            # Check if we are clicking an already assigned knob to edit depth?
            # Or assigning new source.
            # If mod_source_module is None (maybe cleared?), we can't assign.
            # But mod_source_module is set when entering SET mode from a module.
            
            source = self.mod_source_module
            if source:
                print(f"  Debug SET: Source={source}, Target={self.current_module}")
                
                if source in ["LFO1", "LFO2", "EG1", "EG2"]:
                    target_mod = self.current_module
                    mapping = self.knob_mappings.get(target_mod, [])
                    print(f"  Debug Mapping: {mapping}")
                    
                    if idx < len(mapping):
                        param = mapping[idx][1]
                        print(f"  Assigning {source} to {target_mod}.{param}")
                        
                        # Default depth 0.5 or keep existing if updating?
                        # If just assigning, set default.
                        # If we want to edit depth, we usually turn the knob.
                        # Clicking confirms/assigns.
                        
                        self.engine.mod_assignments[(target_mod, param)] = (source, 0.5)
                        self.set_mode_active = False
                        self.update_button_styles()
                        self.update_knob_display()
                    else:
                        print(f"  Index {idx} out of range for mapping length {len(mapping)}")
                else:
                    print(f"  Source {source} is not a modulator (LFO/EG)")
            else:
                # If no source selected, maybe we just want to exit SET mode?
                self.set_mode_active = False
                self.update_button_styles()
                    
        elif self.rm_mode_active:
             target_mod = self.current_module
             mapping = self.knob_mappings.get(target_mod, [])
             if idx < len(mapping):
                 param = mapping[idx][1]
                 if (target_mod, param) in self.engine.mod_assignments:
                     del self.engine.mod_assignments[(target_mod, param)]
             self.rm_mode_active = False
             self.update_button_styles()
             self.update_knob_display()

    def update_knob_display(self):
        mapping = self.knob_mappings.get(self.current_module, [])
        
        for i in range(4):
            if i < len(mapping):
                lbl_text, param_key = mapping[i]
                self.knob_labels[i].setText(lbl_text)
                
                val = self.engine.params[self.current_module].get(param_key, 0.0)
                target_val = val
                
                # Check Assignment
                key = (self.current_module, param_key)
                is_assigned = key in self.engine.mod_assignments
                
                if self.set_mode_active and is_assigned:
                    # Depth Editing Mode Visuals
                    source, depth = self.engine.mod_assignments[key]
                    self.knob_labels[i].setStyleSheet("color: red; font-weight: bold;")
                    self.knob_values[i].set_text(f"Depth: {int(depth*100)}%")
                    target_val = depth
                else:
                    # Normal Mode
                    self.knob_labels[i].setStyleSheet("font-weight: bold; color: white;")
                    self.knob_values[i].set_text(self.get_display_value(param_key, val))
                    target_val = val

                # Multi-turn support for WAVE
                if "wave" in param_key:
                    self.knobs[i].set_multiturn(3.0)
                else:
                    self.knobs[i].set_multiturn(1.0)
                
                # Use internal value setter to handle multi-turn visual mapping
                self.knobs[i].set_internal_value(target_val)
                
                self.knobs[i].setVisible(True)
                self.knob_labels[i].setVisible(True)
                self.knob_values[i].setVisible(True)
            else:
                self.knobs[i].setVisible(False)
                self.knob_labels[i].setVisible(False)
                self.knob_values[i].setVisible(False)

    def get_display_value(self, param_key, value):
        # Format
        if "transpose" in param_key: return f"{round((value-0.5)*10):+.0f} Oct"
        
        if "cutoff" in param_key: 
            # 50 Hz to 8000 Hz
            freq = 50.0 * (160.0 ** value)
            if freq > 1000:
                return f"{freq/1000:.1f} kHz"
            return f"{int(freq)} Hz"
            
        if "rate" in param_key: 
            if "LFO" in self.current_module:
                 return f"{value*20.0:.1f} Hz"
            return f"{value*5.0:.1f} Hz"
        if "time" in param_key: return f"{value*1000:.0f} ms"
        
        if "drive" in param_key: return f"{value*20:.1f} dB"
        if "vol" in param_key or "mix" in param_key or "shape" in param_key or "feedback" in param_key or "sustain" in param_key:
             return f"{int(value*100)}%"
        
        if "phase" in param_key: return f"{int(value*180)}°"
        if "detune" in param_key: 
            # -100 to 100 cents
            v = (value - 0.5) * 200
            return f"{int(v)} cts"
        
        if "res" in param_key or "resonance" in param_key: return f"{int(value*100)}%"
        
        if "tone" in param_key:
            v = (value - 0.5) * 200 # -100 to 100
            return f"{int(v)}%"
            
        if "wave" in param_key:
            if value < 0.1: return "Sine"
            elif value < 0.3: return "Saw"
            elif value < 0.5: return "Triangle"
            elif value < 0.7: return "Rev Saw"
            elif value < 0.9: return "Square"
            else: return "PAM4"
            
        if "color" in param_key:
            if value < 0.4: return "Pink"
            elif value < 0.6: return "White"
            else: return "Blue"
            
        if "attack" in param_key or "decay" in param_key or "release" in param_key or "smooth" in param_key:
            sec = value * 2.0 # Assume max 2s? Or dynamic
            if sec < 1.0:
                return f"{int(sec*1000)} ms"
            else:
                return f"{sec:.2f} s"
                
        if "vcf_type" in param_key:
            t = value * 2.0
            if t < 0.1: return "LPF"
            if t < 0.9: return f"LPF-BPF {int((t-0.1)/0.8*100)}%"
            if t < 1.1: return "BPF"
            if t < 1.9: return f"BPF-HPF {int((t-1.1)/0.8*100)}%"
            return "HPF"
        
        if "mod" in self.current_module.lower() and "mode" in param_key:
            modes = [
                "None", 
                "Sync 1>2", "Sync 1>N", 
                "AM 1>2", "AM 1>N", 
                "FM 1>2", "FM 1>N", 
                "RM 1>2", "RM 1>N"
            ]
            if "mode2" in param_key:
                modes = [
                    "None", 
                    "Sync 2>1", "Sync 2>N", 
                    "AM 2>1", "AM 2>N", 
                    "FM 2>1", "FM 2>N", 
                    "RM 2>1", "RM 2>N"
                ]
            idx = int(value * (len(modes)-1) + 0.5)
            return modes[np.clip(idx, 0, len(modes)-1)]
            
        if "mode" in param_key:
            # ARP Mode
            if self.current_module == "ARP":
                if value < 0.166: return "OFF"
                elif value < 0.333: return "UP"
                elif value < 0.500: return "DOWN"
                elif value < 0.666: return "UP-DN"
                elif value < 0.833: return "PING"
                else: return "RND"
            # Glide Mode
            if self.current_module == "GLIDE":
                if value < 0.333: return "Off"
                elif value < 0.666: return "Legato"
                else: return "Always"
                
        if "oct_range" in param_key:
            return f"{1 + int(value * 3.99)}"
            
        if "variation" in param_key:
            return f"{1 + int(value * 3.99)}"
            
        if "polyphony" in param_key:
             return f"{1 + int(value * 3.99)}"
             
        if "slope" in param_key:
             return "Exp" if value > 0.5 else "Lin"

        return f"{value:.2f}"

    def update_ui_visuals(self):
        # Oscilloscope
        self.scope.update_scope()
        
        # Update Modulation Bars
        mod_vals = self.engine.last_mod_values
        mapping = self.knob_mappings.get(self.current_module, [])
        
        for i in range(4):
            if i < len(mapping):
                param_key = mapping[i][1]
                key = (self.current_module, param_key)
                
                if key in self.engine.mod_assignments:
                    source, depth = self.engine.mod_assignments[key]
                    
                    # Get mod signal value (default 0)
                    raw_val = mod_vals.get(source, 0.0)
                    
                    # Calculate display value (scaled by depth)
                    # LFO is -1 to 1. EG is 0 to 1.
                    # Bar handles -1 to 1.
                    
                    # Color
                    color_hex = self.layer_colors.get(source, "#FFFFFF")
                    
                    self.knob_values[i].set_bar_value(raw_val * depth, QColor(color_hex))
                else:
                    self.knob_values[i].set_bar_value(0.0, QColor(0,0,0,0))
            else:
                self.knob_values[i].set_bar_value(0.0, QColor(0,0,0,0))
        
    def load_config(self):
        filename = "config.json"
        if os.path.exists(filename):
            try:
                with open(filename, 'r') as f:
                    state = json.load(f)
                self.engine.load_state_dict(state)
                self.update_knob_display()
            except: pass

    def save_config(self):
        try:
            with open("config.json", 'w') as f:
                json.dump(self.engine.get_state_dict(), f, indent=4)
            self.setWindowTitle("Saved!")
            QTimer.singleShot(1000, lambda: self.setWindowTitle("PySide6 Analog Synth"))
        except: pass

    def apply_styles(self):
        self.setStyleSheet("""
            QMainWindow { background-color: #222; color: #fff; }
            QLabel { color: #fff; }
            QDial { background-color: #333; }
        """)

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = SynthWindow()
    window.show()
    sys.exit(app.exec())
