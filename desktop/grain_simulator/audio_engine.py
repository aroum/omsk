import os
import random
import numpy as np
import soundfile as sf
from constants import SAMPLE_RATE, OLED_WIDTH, generate_window

class StateVariableFilter:
    """Standard SVF filter for resonant processing."""
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
        if mode_idx == 0: return v2 
        if mode_idx == 1: return v0 - k * v1 - v2 
        if mode_idx == 2: return v1 
        return v0

class Grain:
    """Single grain instance handling playback logic."""
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
        rel_pos = self.current_frame * self.pitch
        idx = (self.start_idx - rel_pos if self.reverse else self.start_idx + rel_pos)
        return int(idx) % len(self.sample_data)

    def process(self, frames, fm_signal=None):
        if not self.active or self.length <= 1: return np.zeros((frames, 2))
        out = np.zeros((frames, 2))
        data_len = len(self.sample_data)
        
        for i in range(frames):
            if self.current_frame >= self.length - 1:
                self.active = False
                break
            
            # Effective pitch taking FM into account
            shift = fm_signal[i] if fm_signal is not None else 0
            effective_pitch = max(0.001, self.pitch + shift)
                
            rel_pos = self.current_frame * effective_pitch
            idx = (self.start_idx - rel_pos if self.reverse else self.start_idx + rel_pos)
            
            # --- Linear interpolation for FM ---
            idx_low = int(idx) % data_len
            idx_high = (idx_low + 1) % data_len
            frac = idx - int(idx)
            
            sample = (1.0 - frac) * self.sample_data[idx_low] + frac * self.sample_data[idx_high]
            # ------------------------------------
            
            win_idx = int((self.current_frame / self.length) * (len(self.window)-1))
            val = sample * self.window[win_idx] * self.amp
            
            out[i, 0] = val * (1.0 - self.pan)
            out[i, 1] = val * self.pan
            self.current_frame += 1
            
        return out

class GranularEngine:
    """Core audio engine for a single granular voice."""
    SHAPES = ["Hanning", "Tri", "Rect", "Blackman"]
    FILTERS = ["LP", "HP", "BP", "Off"]

    def __init__(self, voice_id):
        self.voice_id = voice_id
        self.sample_data = np.zeros(SAMPLE_RATE)
        self.active_grains = []
        self.next_grain_time = 0
        self.current_folder = ""
        self.file_list = []
        self.current_filename = "EMPTY"
        
        self.last_buffer = np.zeros(512) 
        self.all_engines = [] 
        self.MOD_ROUTES = [] 
        
        self.params = {
            "pos": [0.5, 0, 1, 0.001, "%", None, 0],
            "size": [0.1, 0.01, 2.0, 0.001, "s", None, 0],
            "dens": [20.0, 1.0, 150.0, 0.1, "Hz", None, 0],
            "pitch": [1.0, 0.1, 4.0, 0.01, "x", None, 0],
            "sample_idx": [0, 0, 127, 1, "#", None, 0],
            "max_grains": [32, 1, 64, 1, "", None, 0],
            "grain_amp": [0.8, 0, 1, 0.01, "", None, 0],
            "keytrack": [0, 0, 1, 0.01, "", None, 0],
            "scan": [0.0, -2.0, 2.0, 0.01, "", None, 0],
            "direction": [0.0, 0, 1, 1, "", None, 0],
            "spread": [0.5, 0, 1, 0.01, "", None, 0],
            "shape": [0, 0, 3, 1, "", None, 0],
            "cutoff": [10000, 20, 20000, 1, "Hz", None, 0],
            "res": [0.1, 0, 0.99, 0.01, "", None, 0],
            "filt_type": [3, 0, 3, 1, "", None, 0],
            "filt_key": [0, 0, 1, 0.01, "", None, 0],
            "atk": [0.01, 0.001, 5.0, 0.001, "s", None, 0],
            "atk_curve": [0.0, -1.0, 1.0, 0.1, "", None, 0],
            "rel": [0.3, 0.001, 5.0, 0.001, "s", None, 0],
            "rel_curve": [0.0, -1.0, 1.0, 0.1, "", None, 0],
            "lfo1_rate": [1.0, 0.1, 50.0, 0.1, "Hz", None, 0],
            "lfo1_wave": [0, 0, 3, 1, "", None, 0],
            "lfo1_phase": [0, 0, 1, 0.01, "", None, 0],
            "lfo2_rate": [1.0, 0.1, 50.0, 0.1, "Hz", None, 0],
            "lfo2_wave": [0, 0, 3, 1, "", None, 0],
            "lfo2_phase": [0, 0, 1, 0.01, "", None, 0],
            "vol": [0.7, 0, 1, 0.01, "", None, 0],
            "mod1_src": [0, 0, 24, 1, "", None, 0], 
            "mod1_amt": [0.0, 0, 1, 0.01, "", None, 0],
            "mod2_src": [0, 0, 24, 1, "", None, 0],
            "mod2_amt": [0.0, 0, 1, 0.01, "", None, 0],
            "fx_wf": [0.0, 0, 1, 0.01, "", None, 0],
            "fx_ds": [1.0, 1, 80, 1, "", None, 0],
            "fx_bc": [16.0, 1, 16, 1, "", None, 0],
            "fx_mix": [0.0, 0, 1, 0.01, "", None, 0],
            "viz_scale": [1.0, 0.1, 5.0, 0.1, "", None, 0],
            "midi_mode": [0, 0, 6, 1, "", None, 0],
            "midi_ch": [1, 1, 16, 1, "", None, 0],
            "master_vol": [1.0, 0, 1, 0.01, "", None, 0],
            "pitch_mode": [0, 0, 2, 1, "", None, 0]
        }

        self.filter_l, self.filter_r = StateVariableFilter(), StateVariableFilter()
        self.playback_pos, self.is_triggered, self.master_env, self.curved_env = 0.0, False, 0.0, 0.0
        self.lfo_phases = [0.0, 0.0]
        self.env_pos = []
        self.window_lut = {name: generate_window(name, 1024) for name in self.SHAPES}

    def trigger_on(self):
        self.is_triggered = True

    def trigger_off(self):
        self.is_triggered = False

    def load_sample(self):
        if not self.file_list: return
        idx = int(self.params["sample_idx"][0]) % len(self.file_list)
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
        except Exception: 
            self.current_filename = "ERROR"

    def get_mod_val(self, mod_name):
        if mod_name == "Jit": return random.uniform(-1, 1)
        if mod_name == "LFO1": return np.sin(2 * np.pi * self.lfo_phases[0])
        if mod_name == "LFO2": return np.sin(2 * np.pi * self.lfo_phases[1])
        if mod_name == "EG": return self.curved_env
        return 0

    def process_audio(self, frames):
        step = frames / SAMPLE_RATE
        self.lfo_phases[0] = (self.lfo_phases[0] + self.params["lfo1_rate"][0] * step) % 1.0
        self.lfo_phases[1] = (self.lfo_phases[1] + self.params["lfo2_rate"][0] * step) % 1.0

        out = np.zeros((frames, 2))
        if len(self.sample_data) < 100: 
            self.last_buffer = np.zeros(frames)
            return out
        
        target = 1.0 if self.is_triggered else 0.0
        if target > self.master_env: 
            self.master_env = min(target, self.master_env + step / max(0.001, self.params["atk"][0]))
        else: 
            self.master_env = max(target, self.master_env - step / max(0.001, self.params["rel"][0]))
        
        if self.master_env <= 0 and not self.is_triggered:
            self.active_grains = []
            self.last_buffer = np.zeros(frames)
            return out

        def get_curved_env(phase, curve):
            if phase <= 0: return 0.0
            if phase >= 1: return 1.0
            if abs(curve) < 0.01: return phase
            if curve > 0:
                c = curve * 5.0
                return (np.exp(c * phase) - 1.0) / (np.exp(c) - 1.0)
            else:
                c = -curve * 5.0
                return 1.0 - (np.exp(c * (1.0 - phase)) - 1.0) / (np.exp(c) - 1.0)
                
        self.curved_env = 0.0
        if self.master_env > 0.001:
            if self.master_env >= 0.999 or self.is_triggered:
                self.curved_env = get_curved_env(self.master_env, self.params["atk_curve"][0])
            else:
                self.curved_env = 1.0 - get_curved_env(1.0 - self.master_env, -self.params["rel_curve"][0])

        def get_p(key):
            p = self.params[key]
            val = p[0]
            if p[5]:
                mod = self.get_mod_val(p[5])
                val += mod * p[6] * (p[2] - p[1])
            return max(p[1], min(p[2], val))

        fm_audio_signal = np.zeros(frames)
        rm_multipliers = np.ones(frames)

        if self.all_engines and self.MOD_ROUTES:
            for i in [1, 2]:
                route_idx = int(self.params[f"mod{i}_src"][0])
                if route_idx == 0: continue
                
                route = self.MOD_ROUTES[route_idx]
                amt = self.params[f"mod{i}_amt"][0]
                mod_eng = self.all_engines[route["src"]]
                
                mod_data = mod_eng.last_buffer
                if len(mod_data) != frames:
                    mod_data = np.zeros(frames)
                
                if route["type"] == "FM":
                    fm_audio_signal += mod_data * amt * 5.0
                elif route["type"] == "RM":
                    current_route_rm = (1.0 - amt) + (mod_data * amt * 5.0)
                    rm_multipliers *= current_route_rm

        if self.is_triggered:
            self.playback_pos = (self.playback_pos + get_p("scan") * step) % 1.0
        self.next_grain_time -= frames
        if self.next_grain_time <= 0 and len(self.active_grains) < int(get_p("max_grains")):
            self.next_grain_time = max(10, (SAMPLE_RATE / get_p("dens")))
            p_idx = (get_p("pos") + self.playback_pos) % 1.0
            
            base_pitch = get_p("pitch")
            p_mode = int(get_p("pitch_mode"))
            if p_mode == 1: # Semi
                base_pitch = 2.0 ** (base_pitch / 12.0)
            elif p_mode == 2: # Oct
                base_pitch = 2.0 ** base_pitch
            
            new_g = Grain(
                self.sample_data, 
                int(p_idx * len(self.sample_data)), 
                get_p("size") * SAMPLE_RATE, 
                base_pitch, 
                0.5 + (random.random() - 0.5) * get_p("spread"), 
                random.random() < get_p("direction"), 
                self.window_lut[self.SHAPES[int(get_p("shape"))]], 
                get_p("grain_amp")
            )
            self.active_grains.append(new_g)
        
        mixed = np.zeros((frames, 2))
        for g in self.active_grains[:]:
            mixed += g.process(frames, fm_audio_signal)
            if not g.active: self.active_grains.remove(g)
            
        # Ring Modulation (RM) block
        if self.all_engines and self.MOD_ROUTES:
            for i in [1, 2]:
                route_idx = int(self.params[f"mod{i}_src"][0])
                if route_idx == 0: continue
                
                route = self.MOD_ROUTES[route_idx]
                if route["type"] == "RM":
                    amt = self.params[f"mod{i}_amt"][0]
                    mod_data = self.all_engines[route["src"]].last_buffer
                    
                    # Mix Dry/Wet based on amt (scale x5.0 matching C++ engine)
                    rm_mix = (1.0 - amt) + (mod_data * amt * 5.0)
                    mixed[:, 0] = mixed[:, 0] * rm_mix
                    mixed[:, 1] = mixed[:, 1] * rm_mix
        
        f_type = int(get_p("filt_type"))
        if f_type != 3:
            cut, res = get_p("cutoff"), get_p("res")
            for i in range(frames):
                mixed[i, 0] = self.filter_l.process(mixed[i, 0], cut, res, f_type)
                mixed[i, 1] = self.filter_r.process(mixed[i, 1], cut, res, f_type)
        
        final_signal = (mixed * (self.params["vol"][0] * self.curved_env))
        self.last_buffer = np.clip((final_signal[:, 0] + final_signal[:, 1]) * 0.5, -1, 1)
            
        return final_signal.astype(np.float32)