import numpy as np
import sounddevice as sd
import scipy.signal
import json

class Voice:
    def __init__(self, engine):
        self.engine = engine
        self.active = False
        self.note_freq = 440.0
        self.target_freq = 440.0
        self.current_freq = 440.0
        self.gate = False
        
        # Internal Voice State
        self.phase = {"VCO1": 0.0, "VCO2": 0.0}
        self.env_state = {"EG1": 0, "EG2": 0} 
        self.env_val = {"EG1": 0.0, "EG2": 0.0}
        self.gate_val = 0.0 # Smoothed gate for VCA
        self.filter_state = {
            "VCF1": np.zeros(2), "VCF2": np.zeros(2)
        }

    def note_on(self, freq, from_freq=None):
        # Glide Logic
        glide_mode = self.engine.params["GLIDE"]["mode"]
        glide_time = self.engine.params["GLIDE"]["time"]
        
        self.target_freq = freq
        should_glide = False
        
        # Mode 0: Off (0.0-0.25)
        # Mode 1: Legato (0.25-0.75)
        # Mode 2: Always (0.75-1.0)
        
        if glide_mode > 0.75: # Always
             should_glide = True
        elif glide_mode > 0.25: # Legato
             # Glide only if voice was active or releasing
             if self.active: should_glide = True
             if self.env_state["EG1"] == 3: should_glide = True
        
        if should_glide and from_freq is not None:
            self.current_freq = from_freq
            
        if not should_glide or glide_time < 0.01:
            self.current_freq = freq
            
        self.note_freq = freq # Target
        self.gate = True
        self.active = True
        for eg in ["EG1", "EG2"]:
            self.env_state[eg] = 1 # Attack

    def note_off(self):
        self.gate = False
        for eg in ["EG1", "EG2"]:
            self.env_state[eg] = 3 # Release

    def _get_envelope(self, name, frames):
        p = self.engine.params[name]
        attack_s = p["attack"] * 2.0 + 0.01
        decay_s = p["decay"] * 2.0 + 0.01
        release_s = p["release"] * 2.0 + 0.01
        sustain_l = p["sustain"]
        
        attack_rate = 1.0 / (attack_s * self.engine.sample_rate)
        decay_rate = 1.0 / (decay_s * self.engine.sample_rate)
        release_rate = 1.0 / (release_s * self.engine.sample_rate)
        
        current = self.env_val[name]
        state = self.env_state[name]
        
        target = 0.0
        rate = 0.0
        
        if state == 1: # Attack
            target = 1.0
            rate = attack_rate
        elif state == 2: # Decay (to sustain)
            target = sustain_l
            rate = decay_rate
        elif state == 3: # Release
            target = 0.0
            rate = release_rate
        else: # Idle
            target = 0.0
            rate = 1.0 
        
        step = rate * frames
        if current < target:
            next_val = min(current + step, target)
            if next_val >= target and state == 1:
                self.env_state[name] = 2 
        elif current > target:
            next_val = max(current - step, target)
        else:
            next_val = target
            
        vals = np.linspace(current, next_val, frames)
        self.env_val[name] = next_val
        
        if state == 1 and next_val >= 1.0:
            self.env_state[name] = 2
        
        if state == 3 and next_val <= 0.001:
            self.env_state[name] = 0 # Idle
            self.env_val[name] = 0.0
            
        return vals

    def _generate_waveform(self, phases, wave_val, shape_val):
        """Unified waveform generator with 6-stage morphing and shape control"""
        # Array-based wave_val (modulated)
        # Optimized segment-based generation
        unique_waves = np.unique(np.floor(wave_val).astype(int)) if not np.isscalar(wave_val) else [int(wave_val)]
        needed = set(unique_waves)
        for u in unique_waves:
            if u < 5: needed.add(u + 1)
            
        waves = {}
        for i in needed:
            if i == 0: waves[0] = self.engine._fold(np.sin(2.0 * np.pi * phases), shape_val)
            elif i == 1: waves[1] = self.engine._fold(2.0 * phases - 1.0, shape_val)
            elif i == 2: 
                waves[2] = self.engine._fold(1.0 - np.abs(phases - 0.5) * 4.0, shape_val)
            elif i == 3: waves[3] = self.engine._fold(1.0 - 2.0 * phases, shape_val)
            elif i == 4: 
                pw = 0.5 - (shape_val * 0.45)
                waves[4] = np.where(phases < pw, 1.0, -1.0)
            elif i == 5: 
                avg_shape = np.mean(shape_val) if not np.isscalar(shape_val) else shape_val
                idx = int(np.clip(avg_shape * 99, 0, 99))
                pam_levels = self.engine.pam4_table[idx]
                sym_idx = np.clip((phases * 16).astype(int), 0, 15)
                waves[5] = pam_levels[sym_idx]
            
        if np.isscalar(wave_val):
            w_idx = int(np.clip(wave_val, 0, 4))
            frac = wave_val - w_idx
            if wave_val >= 5.0: return waves[5]
            return waves[w_idx] * (1.0 - frac) + waves[w_idx+1] * frac

        w_idx = np.floor(wave_val).astype(int)
        w_idx = np.clip(w_idx, 0, 4)
        frac = wave_val - w_idx
        
        res = np.zeros(len(phases))
        for i in range(5):
            mask = (w_idx == i)
            if np.any(mask):
                res[mask] = waves[i][mask] * (1.0 - frac[mask]) + waves[i+1][mask] * frac[mask]
        
        mask5 = (wave_val >= 5.0)
        if np.any(mask5): res[mask5] = waves[5][mask5]
            
        return res

    def _get_modulated_value(self, module, param, frames, modulators):
        return self.engine._get_modulated_value(module, param, frames, modulators)

    def _oscillator(self, name, freq, frames, modulators, phase_offset=0, master_phases=None):
        transpose_val = self._get_modulated_value(name, "transpose", frames, modulators)
        detune_val = self._get_modulated_value(name, "detune", frames, modulators)
        wave_val = self._get_modulated_value(name, "wave", frames, modulators)
        shape_val = self._get_modulated_value(name, "shape", frames, modulators)

        if np.isscalar(transpose_val) and np.isscalar(detune_val):
            octave = round((transpose_val - 0.5) * 10.0)
            fine = (detune_val - 0.5) * 2.0 / 12.0
            base_freq = freq * (2.0 ** (octave + fine))
            phase_inc = base_freq / self.engine.sample_rate
        else:
            octave = np.round((transpose_val - 0.5) * 10.0)
            fine = (detune_val - 0.5) * 2.0 / 12.0
            base_freq = freq * (2.0 ** (octave + fine))
            phase_inc = base_freq / self.engine.sample_rate
        
        if master_phases is not None:
            wraps = np.diff(np.floor(master_phases), prepend=np.floor(master_phases[0])) > 0
            phase_inc_arr = np.full(frames, phase_inc) if np.isscalar(phase_inc) else phase_inc
            current_phase = self.phase[name]
            phases = np.empty(frames)
            for i in range(frames):
                if wraps[i]: current_phase = 0.0
                current_phase += phase_inc_arr[i]
                phases[i] = current_phase
            self.phase[name] = phases[-1] % 1.0
            phases = (phases + phase_offset) % 1.0
            sync_phases = np.array([])
        else:
            if np.isscalar(phase_inc):
                phases = self.phase[name] + (np.arange(1, frames + 1) * phase_inc)
            else:
                phases = self.phase[name] + np.cumsum(phase_inc)
            self.phase[name] = phases[-1] % 1.0
            sync_phases = phases
            phases = (phases + phase_offset) % 1.0
            
        return self._generate_waveform(phases % 1.0, wave_val, shape_val), sync_phases

    def _filter(self, name, signal, modulators):
        frames = len(signal)
        cutoff = self._get_modulated_value(name, "cutoff", frames, modulators)
        res = self._get_modulated_value(name, "resonance", frames, modulators)
        vcf_type_val = self._get_modulated_value(name, "vcf_type", frames, modulators)
        mix_val = self._get_modulated_value(name, "mix", frames, modulators)
        
        # Use scalar mean if possible
        avg_cutoff = np.clip(np.mean(cutoff) if not np.isscalar(cutoff) else cutoff, 0.01, 0.99)
        avg_res = np.clip(np.mean(res) if not np.isscalar(res) else res, 0.0, 0.99)
        avg_type = np.mean(vcf_type_val) if not np.isscalar(vcf_type_val) else vcf_type_val
        avg_mix = np.clip(np.mean(mix_val) if not np.isscalar(mix_val) else mix_val, 0.0, 1.0)
        
        # Frequency scale: 50Hz to 8kHz (Logarithmic)
        f0 = 50.0 * (160.0 ** avg_cutoff)
        f0 = np.clip(f0, 50.0, 8000.0) 
        
        w0 = 2.0 * np.pi * f0 / self.engine.sample_rate
        cos_w0 = np.cos(w0)
        sin_w0 = np.sin(w0)
        
        q = 0.707 + (avg_res ** 2) * 24.3
        alpha = sin_w0 / (2.0 * q)
        
        # LPF coeffs (12dB/octave)
        b0_lp = (1.0 - cos_w0) / 2.0
        b1_lp = 1.0 - cos_w0
        b2_lp = (1.0 - cos_w0) / 2.0
        
        # BPF coeffs (constant peak gain)
        b0_bp = alpha
        b1_bp = 0
        b2_bp = -alpha
        
        # HPF coeffs (12dB/octave)
        b0_hp = (1.0 + cos_w0) / 2.0
        b1_hp = -(1.0 + cos_w0)
        b2_hp = (1.0 + cos_w0) / 2.0
        
        # Morphing logic (0.0 to 1.0 range maps to LPF -> BPF -> HPF)
        t = np.clip(avg_type, 0.0, 1.0) * 2.0
        if t <= 1.0:
            # LPF to BPF
            b0 = (1.0 - t) * b0_lp + t * b0_bp
            b1 = (1.0 - t) * b1_lp + t * b1_bp
            b2 = (1.0 - t) * b2_lp + t * b2_bp
        else:
            # BPF to HPF
            t2 = t - 1.0
            b0 = (1.0 - t2) * b0_bp + t2 * b0_hp
            b1 = (1.0 - t2) * b1_bp + t2 * b1_hp
            b2 = (1.0 - t2) * b2_bp + t2 * b2_hp
            
        a0 = 1.0 + alpha
        a1 = -2.0 * cos_w0
        a2 = 1.0 - alpha
        
        b = np.array([b0, b1, b2]) / a0
        a = np.array([a0, a1, a2]) / a0
        
        if np.any(np.isnan(b)) or np.any(np.isinf(b)):
            return signal

        filtered, self.filter_state[name] = scipy.signal.lfilter(b, a, signal, zi=self.filter_state[name])
        
        # Soft-clip the filtered signal to keep resonance under control
        # We use a slightly modified tanh to preserve some volume at high resonance
        filtered = np.tanh(filtered * (1.0 + avg_res)) 
        
        return signal * (1.0 - avg_mix) + filtered * avg_mix

    def process(self, frames, lfo1, lfo2, noise_raw):
        if not self.active:
            return np.zeros(frames)
            
        # Glide Update (Per Block)
        if abs(self.current_freq - self.target_freq) > 0.01:
            glide_time = self.engine.params["GLIDE"]["time"] * 2.0 # 0-2s
            slope = self.engine.params["GLIDE"]["slope"] # 0=Lin, 1=Exp
            
            diff = self.target_freq - self.current_freq
            dt = frames / self.engine.sample_rate
            
            # Linear Step
            step_lin = diff * (dt / max(0.01, glide_time))
            if abs(step_lin) > abs(diff): step_lin = diff
            
            # Exp Step
            alpha = 1.0 - np.exp(-dt / max(0.01, glide_time))
            step_exp = diff * alpha
            
            # Mix
            step = step_lin * (1.0 - slope) + step_exp * slope
            self.current_freq += step

        eg1 = self._get_envelope("EG1", frames)
        eg2 = self._get_envelope("EG2", frames)
        
        # Modulators dictionary
        modulators = {
            "LFO1": lfo1,
            "LFO2": lfo2,
            "EG1": eg1,
            "EG2": eg2
        }
        
        # Gate Envelope (Smoothed) for default VCA
        # 5ms smoothing (approx 220 samples at 44.1k)
        target_gate = 1.0 if self.gate else 0.0
        step = 1.0 / (0.005 * self.engine.sample_rate)
        
        # Linear approach for block?
        # If gate changed, we ramp.
        # Vectorized ramp
        start_gate = self.gate_val
        if start_gate < target_gate:
            gate_env = np.linspace(start_gate, min(start_gate + step * frames, target_gate), frames)
            self.gate_val = gate_env[-1]
            # Fix undershoot/overshoot in next block if needed
            if self.gate_val >= target_gate: self.gate_val = target_gate
        elif start_gate > target_gate:
            gate_env = np.linspace(start_gate, max(start_gate - step * frames, target_gate), frames)
            self.gate_val = gate_env[-1]
            if self.gate_val <= target_gate: self.gate_val = target_gate
        else:
            gate_env = np.full(frames, target_gate)
            self.gate_val = target_gate

        vco_balance = self._get_modulated_value("MIXER", "vco1_vol", frames, modulators)
        master_vol = self._get_modulated_value("MIXER", "vco2_vol", frames, modulators)
        vco_noise_balance = self._get_modulated_value("MIXER", "noise_vol", frames, modulators)
        phase2 = self._get_modulated_value("MIXER", "phase2", frames, modulators)
        
        if ("MIXER", "vco2_vol") not in self.engine.mod_assignments:
            master_vol *= gate_env
        
        if np.isscalar(vco_balance): vco_balance = np.full(frames, vco_balance)
        if np.isscalar(master_vol): master_vol = np.full(frames, master_vol)
        if np.isscalar(vco_noise_balance): vco_noise_balance = np.full(frames, vco_noise_balance)
        if np.isscalar(phase2): phase2 = np.full(frames, phase2)

        # MOD logic
        m1_val = self.engine.params["MOD"]["mode1"]
        d1 = self.engine.params["MOD"]["depth1"]
        m2_val = self.engine.params["MOD"]["mode2"]
        d2 = self.engine.params["MOD"]["depth2"]
        
        m1 = int(m1_val * 8 + 0.5)
        m2 = int(m2_val * 8 + 0.5)

        # 1. Get Oscillator outputs and phases
        # Optimize: if an oscillator is a slave, we don't need its first pass output
        if m2 == 1: # Sync 2>1 (VCO1 is Slave)
            # We still need VCO2 first to get its phases
            vco2, p2_sync = self._oscillator("VCO2", self.current_freq, frames, modulators, phase_offset=phase2)
            vco1, p1_sync = self._oscillator("VCO1", self.current_freq, frames, modulators, master_phases=p2_sync)
        elif m1 == 1: # Sync 1>2 (VCO2 is Slave)
            vco1, p1_sync = self._oscillator("VCO1", self.current_freq, frames, modulators)
            vco2, p2_sync = self._oscillator("VCO2", self.current_freq, frames, modulators, 
                                           phase_offset=phase2, master_phases=p1_sync)
        else:
            # Normal pass
            vco1, p1_sync = self._oscillator("VCO1", self.current_freq, frames, modulators)
            vco2, p2_sync = self._oscillator("VCO2", self.current_freq, frames, modulators, phase_offset=phase2)
            
        noise = noise_raw # Local alias
        
        # 2. Apply Mod 1 (Source 1 modulating 2 or Noise)
        # Sync 1>2 already handled in first pass optimization
        if m1 == 3: # AM 1>2
            vco2 = vco2 * (1.0 + vco1 * d1)
        elif m1 == 4: # AM 1>Noise
            noise = noise * (1.0 + vco1 * d1)
        elif m1 == 5: # FM 1>2
            vco2, _ = self._oscillator("VCO2", self.current_freq, frames, modulators, 
                                   phase_offset=phase2 + vco1 * d1 * 0.2)
        elif m1 == 7: # RM 1>2
            vco2 = vco2 * vco1 * d1 + vco2 * (1.0 - d1)
        elif m1 == 8: # RM 1>Noise
            noise = noise * vco1 * d1 + noise * (1.0 - d1)

        # 3. Apply Mod 2 (Source 2 modulating 1 or Noise)
        # Sync 2>1 already handled in first pass optimization
        if m2 == 3: # AM 2>1
            vco1 = vco1 * (1.0 + vco2 * d2)
        elif m2 == 4: # AM 2>Noise
            noise = noise * (1.0 + vco2 * d2)
        elif m2 == 5: # FM 2>1
            vco1, _ = self._oscillator("VCO1", self.current_freq, frames, modulators, 
                                   phase_offset=vco2 * d2 * 0.2)
        elif m2 == 7: # RM 2>1
            vco1 = vco1 * vco2 * d2 + vco1 * (1.0 - d2)
        elif m2 == 8: # RM 2>Noise
            noise = noise * vco2 * d2 + noise * (1.0 - d2)

        # Update noise_raw with modulated noise for routing
        noise_raw = noise
        
        if np.isscalar(vco1): vco1 = np.full(frames, vco1)
        if np.isscalar(vco2): vco2 = np.full(frames, vco2)
        if np.isscalar(noise_raw): noise_raw = np.full(frames, noise_raw)

        vco_balance_clipped = np.clip(vco_balance, 0.0, 1.0)
        vco_noise_clipped = np.clip(vco_noise_balance, 0.0, 1.0)
        master_clipped = np.clip(master_vol, 0.0, 1.0)
        
        vco_gain1 = np.sqrt(1.0 - vco_balance_clipped)
        vco_gain2 = np.sqrt(vco_balance_clipped)
        
        k_vco = 1.0 - vco_noise_clipped
        k_noise = vco_noise_clipped
        
        base_gain = master_clipped * 0.5
        
        gain_vco1 = base_gain * k_vco * vco_gain1
        gain_vco2 = base_gain * k_vco * vco_gain2
        gain_noise = base_gain * k_noise

        vcf1_in = np.zeros(frames)
        vcf2_in = np.zeros(frames)
        
        dest = self.engine.signal_routing.get("VCO1", None)
        vco1_scaled = vco1 * gain_vco1
        if dest == "VCF1": vcf1_in += vco1_scaled
        elif dest == "VCF2": vcf2_in += vco1_scaled
        else: pass # VCO1 is direct
        
        dest = self.engine.signal_routing.get("VCO2", None)
        vco2_scaled = vco2 * gain_vco2
        if dest == "VCF1": vcf1_in += vco2_scaled
        elif dest == "VCF2": vcf2_in += vco2_scaled
        else: pass # VCO2 is direct

        dest = self.engine.signal_routing.get("NOISE", None)
        noise_scaled = noise_raw * gain_noise
        if dest == "VCF1": vcf1_in += noise_scaled
        elif dest == "VCF2": vcf2_in += noise_scaled
        else: pass # Noise is direct

        # VCF1
        vcf1_out = self._filter("VCF1", vcf1_in, modulators)
        
        # VCF2
        vcf2_out = self._filter("VCF2", vcf2_in, modulators)
        
        # Mix for output
        mix_out = vcf1_out + vcf2_out
        
        direct_mix = np.zeros(frames)
        
        # Add direct signals (if not routed to filters)
        dest1 = self.engine.signal_routing.get("VCO1", None)
        if dest1 not in ["VCF1", "VCF2"]: direct_mix += vco1_scaled
        
        dest2 = self.engine.signal_routing.get("VCO2", None)
        if dest2 not in ["VCF1", "VCF2"]: direct_mix += vco2_scaled
        
        destN = self.engine.signal_routing.get("NOISE", None)
        if destN not in ["VCF1", "VCF2"]: direct_mix += noise_scaled
        
        mix_out += direct_mix
        
        # Auto-deactivate check
        if (self.env_state["EG1"] == 0 and self.env_val["EG1"] < 0.001 and 
            self.env_state["EG2"] == 0 and self.env_val["EG2"] < 0.001 and
            self.gate_val < 0.001 and not self.gate):
            self.active = False
            
        return mix_out


class AudioEngine:
    def __init__(self, sample_rate=44100, block_size=512, debug=False):
        self.sample_rate = sample_rate
        self.block_size = block_size
        self.active = False
        self.stream = None
        self.debug_logging = debug

        # Global State
        self.params = {
            "VCO1": {"transpose": 0.5, "detune": 0.5, "wave": 0.0, "shape": 0.5},
            "VCO2": {"transpose": 0.5, "detune": 0.5, "wave": 0.0, "shape": 0.5},
            
            "VCF1": {"cutoff": 1.0, "resonance": 0.0, "vcf_type": 0.0, "mix": 1.0},
            "VCF2": {"cutoff": 1.0, "resonance": 0.0, "vcf_type": 0.0, "mix": 1.0},
            
            "LFO1": {"rate": 0.2, "smooth": 0.0, "wave": 0.0, "shape": 0.5},
            "LFO2": {"rate": 0.2, "smooth": 0.0, "wave": 0.0, "shape": 0.5},
            
            "EG1":  {"attack": 0.1, "decay": 0.3, "sustain": 0.5, "release": 0.5},
            "EG2":  {"attack": 0.1, "decay": 0.3, "sustain": 0.5, "release": 0.5},
            
            "MIXER": {"vco1_vol": 0.5, "vco2_vol": 0.5, "phase2": 0.0, "noise_vol": 0.0},
            
            "NOISE": {"color": 0.5, "unused1": 0.0, "p3": 0.0, "p4": 0.0},
            "FX":    {"time": 0.0, "feedback": 0.0, "mix": 0.0, "tone": 0.5},
            "ARP":   {"rate": 0.5, "mode": 0.0, "swing": 0.0, "oct_range": 0.0},
            "MOD":   {"mode1": 0.0, "depth1": 0.0, "mode2": 0.0, "depth2": 0.0},
            "GLIDE": {"polyphony": 1.0, "time": 0.0, "slope": 0.0, "mode": 0.0},
        }
        
        self.mod_assignments = {} # {(module, param): (source, depth)}
        
        # Signal Routing
        self.signal_routing = {
            "VCO1": None,
            "VCO2": None,
            "NOISE": None
        }

        # Global LFO/Noise state
        self.phase = {"LFO1": 0.0, "LFO2": 0.0}
        self.lfo_val = {"LFO1": 0.0, "LFO2": 0.0} 
        self.last_lfo_sample = {"LFO1": 0.0, "LFO2": 0.0} 
        self.last_mod_values = {} # Store latest modulation values for UI
        self.filter_state = {"noise_color": np.zeros(2)}
        
        # ARP State
        self.arp_held_notes = [] # List of freqs held by user
        self.arp_active_notes = [] # Sorted/Processed notes for pattern
        self.arp_timer = 0.0
        self.arp_index = 0
        self.arp_direction = 1 # 1 or -1 for PingPong/UpDown
        self.last_arp_note = None
        
        # Polyphony
        self.MAX_VOICES = 8
        self.voices = [Voice(self) for _ in range(self.MAX_VOICES)]
        
        self.scope_size = 2048
        self.scope_buffer = np.zeros(self.scope_size) 
        self.delay_buffer = np.zeros(self.sample_rate * 2)
        self.delay_ptr = 0
        
        # Pre-calculate 100 unique PAM4 patterns (32 bits = 16 symbols)
        self.pam4_table = []
        rng = np.random.default_rng(1234) # Stable seed for pattern collection
        while len(self.pam4_table) < 100:
            levels = rng.integers(0, 4, size=16)
            zero_count = np.sum(levels == 0)
            if zero_count <= 2:
                pattern = np.array([-1.0, -0.33, 0.33, 1.0])[levels]
                # Check uniqueness (simplified)
                is_unique = True
                for p in self.pam4_table:
                    if np.array_equal(p, pattern):
                        is_unique = False
                        break
                if is_unique:
                    self.pam4_table.append(pattern)
        self.pam4_table = np.array(self.pam4_table)
        
        # Arp state
        self.arp_step_count = 0
        
    def get_state_dict(self):
        return {
            "params": self.params,
            "mod_assignments": {str(k): v for k, v in self.mod_assignments.items()}, # Convert tuple keys to str for JSON
            "signal_routing": self.signal_routing
        }
        
    def load_state_dict(self, state):
        if "params" in state:
            # Update params deeply
            for module, p in state["params"].items():
                if module in self.params:
                    # Migration: res -> resonance
                    if "res" in p and "resonance" in self.params[module]:
                        p["resonance"] = p.pop("res")
                    self.params[module].update(p)
                    
        if "signal_routing" in state:
            self.signal_routing.update(state["signal_routing"])
            
        if "mod_assignments" in state:
            self.mod_assignments = {}
            for k_str, v in state["mod_assignments"].items():
                # k_str is "('LFO1', 'rate')"
                # Eval is dangerous? Use parsing.
                # Expected format: "('Module', 'param')"
                try:
                    # Remove parens and split
                    clean = k_str.replace("(", "").replace(")", "").replace("'", "").replace('"', "")
                    parts = [p.strip() for p in clean.split(",")]
                    if len(parts) == 2:
                        key = (parts[0], parts[1])
                        self.mod_assignments[key] = (v[0], v[1])
                except:
                    print(f"Failed to load assignment: {k_str}")

    def start(self):
        if self.active: return
        self.stream = sd.OutputStream(
            channels=1, 
            samplerate=self.sample_rate, 
            blocksize=self.block_size,
            callback=self.audio_callback
        )
        self.stream.start()
        self.active = True

    def stop(self):
        if self.stream:
            self.stream.stop()
            self.stream.close()
        self.active = False

    def _trigger_voice(self, freq, from_freq=None):
        # Polyphony Limit (from Glide params as requested)
        poly_param = self.params["GLIDE"]["polyphony"]
        limit = int(poly_param * 3.9) + 1 # 1 to 4
        
        allowed_voices = self.voices[:limit]
        
        # 1. Check if note already playing (Retrigger)
        for v in allowed_voices:
            if v.active and abs(v.target_freq - freq) < 0.1:
                v.note_on(freq, from_freq=from_freq)
                return
        
        # 2. Find free voice
        for v in allowed_voices:
            if not v.active:
                v.note_on(freq, from_freq=from_freq)
                return
                
        # 3. Steal voice (oldest/released? Simplest: first one)
        for v in allowed_voices:
            if v.env_state["EG1"] == 3: # Release
                v.note_on(freq, from_freq=from_freq)
                return
                
        # 4. Hard steal
        allowed_voices[0].note_on(freq, from_freq=from_freq)
        # Move to end of allowed set to rotate priority?
        # We can't easily modify the main list order if we slice.
        # But since we always pick [0], we effectively steal the "first" one.
        # To implement LRU, we'd need to track usage time.
        # For now, simple steal is fine.

    def note_on(self, freq):
        self._trigger_voice(freq)

    def note_off(self, freq):
        # Find voice playing this note
        for v in self.voices:
            if v.active and abs(v.target_freq - freq) < 0.1:
                v.note_off()
                # Don't break, in case multiple voices have same note (unlikely but safe)

    def arp_press(self, freq):
        if freq in self.arp_held_notes:
            self.arp_held_notes.remove(freq)
        self.arp_held_notes.append(freq)
        # Do NOT sort here. Keep insertion order (FIFO) for "first N notes" logic.

    def arp_release(self, freq):
        if freq in self.arp_held_notes:
            self.arp_held_notes.remove(freq)

    def _fold(self, x, shape):
        # gain from 1.0 to 10.0
        gain = 1.0 + shape * 4.5
        y = x * gain
        
        T = 1.0 # Main threshold
        L = 0.35
        
        # Positive peaks
        mask_pos = y > T
        if np.any(mask_pos):
            y_rel = y[mask_pos] - (T - L)
            y[mask_pos] = (T - L) + (L - np.abs((y_rel + L) % (4 * L) - 2 * L))
            
        # Negative peaks
        mask_neg = y < -T
        if np.any(mask_neg):
            y_rel = y[mask_neg] - (-T + L)
            y[mask_neg] = (-T + L) - (L - np.abs((y_rel + L) % (4 * L) - 2 * L))
            
        return y

    def _generate_waveform(self, phases, wave_val, shape_val):
        """
        Unified waveform generator with 6-stage morphing:
        0.0: Sin
        0.2: /| (Saw)
        0.4: /\\ (Triangle)
        0.6: |\\ (Rev Saw)
        0.8: Π_ (Square)
        1.0: PAM4
        """
        # 1. Determine which segments are needed
        s = wave_val * 5.0
        seg = np.floor(np.clip(s, 0, 4.99)).astype(int)
        frac = s - seg
        
        unique_segs = np.unique(seg)
        # We might need seg and seg+1
        needed_waves = set(unique_segs)
        needed_waves.update([min(5, i + 1) for i in unique_segs])
        
        # 2. Generate only needed waveforms
        waves = {}
        
        if 0 in needed_waves: # Sine
            waves[0] = self._fold(np.sin(2.0 * np.pi * phases), shape_val)
        if 1 in needed_waves: # Saw
            waves[1] = self._fold(2.0 * phases - 1.0, shape_val)
        if 2 in needed_waves: # Tri
            waves[2] = 1.0 - np.abs(phases - 0.5) * 4.0
            waves[2] = self._fold(waves[2], shape_val)
        if 3 in needed_waves: # Rev Saw
            waves[3] = self._fold(1.0 - 2.0 * phases, shape_val)
        if 4 in needed_waves: # Square
            pw = 0.5 - (shape_val * 0.45)
            waves[4] = np.where(phases < pw, 1.0, -1.0)
        if 5 in needed_waves: # PAM4
            avg_shape = np.mean(shape_val)
            idx = int(avg_shape * 99)
            idx = np.clip(idx, 0, 99)
            pam_levels = self.pam4_table[idx]
            sym_idx = (phases * 16).astype(int)
            sym_idx = np.clip(sym_idx, 0, 15)
            waves[5] = pam_levels[sym_idx]

        # 3. Morphing
        out = np.zeros_like(phases)
        for i in unique_segs:
            mask = (seg == i)
            if np.any(mask):
                if i < 5:
                    f = frac[mask]
                    out[mask] = (1.0 - f) * waves[i][mask] + f * waves[i+1][mask]
                else:
                    out[mask] = waves[5][mask]
            
        return out

    def _lfo(self, name, frames, modulators):
        rate_val = self._get_modulated_value(name, "rate", frames, modulators)
        # Check if rate is zero or very low (Optimized path)
        avg_rate = np.mean(rate_val) if not np.isscalar(rate_val) else rate_val
        
        if avg_rate < 0.0001:
            # Static LFO value
            val = self._generate_waveform(self.phase[name], 
                                        self.params[name]["wave"], 
                                        self.params[name]["shape"])
            if np.isscalar(val): return np.full(frames, val)
            return np.full(frames, val[0]) if len(val) > 0 else np.zeros(frames)

        rate = rate_val * 20.0
        wave_val = self._get_modulated_value(name, "wave", frames, modulators)
        shape_val = self._get_modulated_value(name, "shape", frames, modulators)
        smooth_val = self._get_modulated_value(name, "smooth", frames, modulators)
        
        phase_inc = rate / self.sample_rate
        if np.isscalar(phase_inc):
            phases = self.phase[name] + (np.arange(1, frames + 1) * phase_inc)
        else:
            phases = self.phase[name] + np.cumsum(phase_inc)
            
        self.phase[name] = phases[-1] % 1.0
        phases = phases % 1.0
        
        raw = self._generate_waveform(phases, wave_val, shape_val)
            
        avg_smooth = np.mean(smooth_val) if not np.isscalar(smooth_val) else smooth_val
        if avg_smooth > 0.001:
            alpha = 1.0 - (avg_smooth ** 0.5) * 0.99
            b = np.array([alpha])
            a = np.array([1.0, -(1.0 - alpha)])
            
            if not isinstance(self.lfo_val[name], np.ndarray):
                 self.lfo_val[name] = np.zeros(1)
                 
            raw, self.lfo_val[name] = scipy.signal.lfilter(b, a, raw, zi=self.lfo_val[name])
        
        return raw

    def _biquad(self, signal, freq, q, f_type, state_name):
        w0 = 2 * np.pi * freq / self.sample_rate
        cos_w0 = np.cos(w0)
        sin_w0 = np.sin(w0)
        alpha = sin_w0 / (2 * q)
        
        if f_type == "lp":
            b0 = (1 - cos_w0) / 2
            b1 = 1 - cos_w0
            b2 = (1 - cos_w0) / 2
            a0 = 1 + alpha
            a1 = -2 * cos_w0
            a2 = 1 - alpha
        elif f_type == "hp":
            b0 = (1 + cos_w0) / 2
            b1 = -(1 + cos_w0)
            b2 = (1 + cos_w0) / 2
            a0 = 1 + alpha
            a1 = -2 * cos_w0
            a2 = 1 - alpha
        else:
            return signal
            
        b = np.array([b0, b1, b2]) / a0
        a = np.array([a0, a1, a2]) / a0
        
        zi = self.filter_state.get(state_name, np.zeros(2))
        out, zi = scipy.signal.lfilter(b, a, signal, zi=zi)
        self.filter_state[state_name] = zi
        return out

    def _noise_gen(self, frames):
        p = self.params["NOISE"]
        noise = (np.random.random(frames) * 2.0 - 1.0)
        f_val = p["color"]
        
        # User Request: Pink (Low) -> White -> Blue (High)
        # 0.0 -> Pink/Brown (LPF)
        # 0.5 -> White
        # 1.0 -> Blue (HPF)
        
        if f_val < 0.45: 
            # Low Pass (Pink/Brown)
            # f_val 0.0 (Deep) -> 0.45 (White)
            norm = (0.45 - f_val) / 0.45
            # fc: 100Hz (at 1.0 norm) to 20kHz (at 0.0 norm)
            fc = 20000.0 * (0.005 ** norm)
            noise = self._biquad(noise, fc, 0.707, "lp", "noise_color")
            
        elif f_val > 0.55: 
            # High Pass (Blue)
            # f_val 0.55 (White) -> 1.0 (Thin)
            norm = (f_val - 0.55) / 0.45
            # fc: 20Hz (at 0.0 norm) to 10kHz (at 1.0 norm)
            fc = 20.0 * (500.0 ** norm)
            noise = self._biquad(noise, fc, 0.707, "hp", "noise_color")
            
        return noise

    def _get_modulated_value(self, module, param, frames, modulators):
        """Centralized modulation handler for both AudioEngine and Voice"""
        base_val = self.params[module][param]
        
        # Check assignment
        key = (module, param)
        if key in self.mod_assignments:
            source, depth = self.mod_assignments[key]
            if depth != 0 and source in modulators:
                mod_signal = modulators[source]
                
                # Special logic for volume parameters (Exponential-ish feeling for EG)
                if "vol" in param and "EG" in source:
                    return base_val * (1.0 - depth * (1.0 - mod_signal))
                    
                return base_val + mod_signal * depth
        
        return base_val

    def audio_callback(self, outdata, frames, time, status):
        if status:
            print(status)
            
        # 1. ARP Logic - Skip if no notes
        arp_mode = self.params["ARP"]["mode"]
        if arp_mode >= 0.166 and self.arp_held_notes:
            rate_param = self.params["ARP"]["rate"]
            freq = rate_param * 5.0
            step_samples = self.sample_rate / max(0.01, freq)
            
            if self.last_arp_note is None:
                self.arp_timer = step_samples
                self.arp_index = -1 
                self.arp_direction = 1 

            self.arp_timer += frames
            if self.arp_timer >= step_samples:
                self.arp_timer = 0
                
                oct_range = 1 + int(self.params["ARP"]["oct_range"] * 3.99)
                sorted_notes = sorted(self.arp_held_notes)
                
                notes = []
                for i in range(oct_range):
                    factor = 2**i
                    for note in sorted_notes:
                        notes.append(note * factor)
                
                num_notes = len(notes)
                if num_notes > 0:
                    if arp_mode < 0.333: # UP
                        self.arp_index = (self.arp_index + 1) % num_notes
                    elif arp_mode < 0.5: # DOWN
                        self.arp_index = (self.arp_index - 1) % num_notes
                    elif arp_mode < 0.666: # UP-DN
                        if self.arp_index >= num_notes - 1: self.arp_direction = -1
                        elif self.arp_index <= 0: self.arp_direction = 1
                        self.arp_index = (self.arp_index + self.arp_direction) % num_notes
                    elif arp_mode < 0.833: # RND
                        self.arp_index = np.random.randint(0, num_notes)
                    else: # CHORD
                        self.arp_index = 0 
                    
                    note_to_play = notes[self.arp_index]

                    if self.last_arp_note is not None:
                        if isinstance(self.last_arp_note, list):
                            for n in self.last_arp_note: self.note_off(n)
                        else:
                            self.note_off(self.last_arp_note)
                    
                    self._trigger_voice(note_to_play, from_freq=self.last_arp_note)
                    self.last_arp_note = note_to_play
        else:
            self.arp_timer = 0
            if self.last_arp_note is not None:
                if isinstance(self.last_arp_note, list):
                    for n in self.last_arp_note: self.note_off(n)
                else:
                    self.note_off(self.last_arp_note)
                self.last_arp_note = None
        
        # 2. Prepare Output
        outdata.fill(0)
        active_voices = [v for v in self.voices if v.active]
        active_count = len(active_voices)
        
        # 3. Global Modulators (LFOs)
        # We always calculate LFOs because they are used for UI feedback and global FX
        pre_modulators = {
            "LFO1": np.full(frames, self.last_lfo_sample.get("LFO1", 0.0)),
            "LFO2": np.full(frames, self.last_lfo_sample.get("LFO2", 0.0)),
            "EG1": np.zeros(frames),
            "EG2": np.zeros(frames)
        }
        
        lfo1 = self._lfo("LFO1", frames, pre_modulators)
        lfo2 = self._lfo("LFO2", frames, pre_modulators)
        
        # Update UI/State
        self.last_lfo_sample["LFO1"] = lfo1[-1]
        self.last_lfo_sample["LFO2"] = lfo2[-1]
        self.last_mod_values["LFO1"] = lfo1[-1]
        self.last_mod_values["LFO2"] = lfo2[-1]
        
        # 4. Noise Generation Optimization
        noise_vol_param = self.params["MIXER"]["noise_vol"]
        noise_raw = None
        
        # Check if noise is needed
        noise_needed = False
        if noise_vol_param > 0.001:
            noise_needed = True
        else:
            # Check if MOD uses noise as target or source
            m1 = int(self.params["MOD"]["mode1"] * 8 + 0.5)
            m2 = int(self.params["MOD"]["mode2"] * 8 + 0.5)
            if m1 in [2, 4, 6, 8] or m2 in [2, 4, 6, 8]:
                noise_needed = True
        
        if noise_needed and active_count > 0:
            noise_raw = self._noise_gen(frames)
        else:
            # Return None if not needed to avoid array creation, 
            # but Voice.process expects an array or something it can use.
            # Let's use a shared zero array if not needed.
            noise_raw = np.zeros(frames)

        # 5. Process Voices
        mix = np.zeros(frames)
        max_eg1 = 0.0
        max_eg2 = 0.0
        
        for voice in active_voices:
            mix += voice.process(frames, lfo1, lfo2, noise_raw)
            if "EG1" in voice.env_val: max_eg1 = max(max_eg1, voice.env_val["EG1"])
            if "EG2" in voice.env_val: max_eg2 = max(max_eg2, voice.env_val["EG2"])
                
        self.last_mod_values["EG1"] = max_eg1
        self.last_mod_values["EG2"] = max_eg2

        # 6. Master Scaling & Clipping
        if active_count > 0:
            scale_factor = 0.5 / np.sqrt(active_count)
            mix = np.tanh(mix * scale_factor)
        
        mix_before_fx = mix.copy()
        
        # 7. FX Processing Optimization
        mix_param = self.params["FX"]["mix"]
        # Process if mix is on OR if we have active voices (to fill delay buffer)
        if mix_param > 0.001 or active_count > 0:
            fx_modulators = {"LFO1": lfo1, "LFO2": lfo2}
            
            # Additional check: if mix is 0 and no voices, skip
            if not (mix_param < 0.001 and active_count == 0):
                time_param = self._get_modulated_value("FX", "time", frames, fx_modulators)
                fdbk_param = self._get_modulated_value("FX", "feedback", frames, fx_modulators)
                tone_param = self._get_modulated_value("FX", "tone", frames, fx_modulators)

                d_ptr = self.delay_ptr
                buf_len = len(self.delay_buffer)
                
                if mix_param > 0.001:
                    # Full Processing
                    delay_samples = int(np.mean(time_param) * (self.sample_rate - 1))
                    delay_samples = max(1, delay_samples)
                    
                    read_ptr = (d_ptr - delay_samples + buf_len) % buf_len
                    indices = (np.arange(frames) + read_ptr) % buf_len
                    delayed_sig = self.delay_buffer[indices]
                    
                    # Tone Filter
                    tone = np.mean(tone_param)
                    if tone < 0.45 or tone > 0.55:
                         if tone < 0.45:
                             lp_cut = max(0.01, tone / 0.45)
                             freq = min(200.0 * (100.0 ** lp_cut), 18000.0)
                             p = np.exp(-2.0 * np.pi * freq / self.sample_rate)
                             b, a = [1.0 - p], [1.0, -p]
                         else:
                             hp_amount = (tone - 0.55) / 0.45
                             freq = 20.0 * (250.0 ** hp_amount) 
                             p = np.exp(-2.0 * np.pi * freq / self.sample_rate)
                             b, a = [(1.0+p)/2.0, -(1.0+p)/2.0], [1.0, -p]
                         
                         if "FX_Tone" not in self.filter_state: self.filter_state["FX_Tone"] = np.zeros(1)
                         delayed_sig, self.filter_state["FX_Tone"] = scipy.signal.lfilter(b, a, delayed_sig, zi=self.filter_state["FX_Tone"])
                    
                    fdbk = np.clip(np.mean(fdbk_param), 0.0, 1.0)
                    mix = mix * (1.0 - mix_param) + delayed_sig * mix_param
                    
                    # Store to buffer (use original input signal for feedback path, not processed mix)
                    input_sig = mix_before_fx if 'mix_before_fx' in locals() else mix
                    to_store = np.tanh(input_sig + delayed_sig * fdbk)
                    
                    # Denormal protection
                    to_store[np.abs(to_store) < 1e-10] = 0
                    
                    w_indices = (np.arange(frames) + d_ptr) % buf_len
                    self.delay_buffer[w_indices] = to_store
                else:
                    # Just write input to buffer for future echo
                    w_indices = (np.arange(frames) + d_ptr) % buf_len
                    self.delay_buffer[w_indices] = mix 
                
                self.delay_ptr = (d_ptr + frames) % buf_len

        # 8. UI Scope Update
        self.scope_buffer = np.roll(self.scope_buffer, -frames)
        self.scope_buffer[-frames:] = mix
        
        outdata[:] = mix.reshape(-1, 1)
