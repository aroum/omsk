#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include "grain.h"
#include "filter.h"
#include "fx.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_GRAINS_PER_VOICE 64

struct GrainParams {
    float pos;          // 0.0 - 1.0
    float size;         // 0.01 - 2.0 (seconds)
    float dens;         // 1.0 - 150.0 (Hz)
    float pitch;        // 0.1 - 4.0
    int pitch_mode;     // 0=Free, 1=Semi, 2=Oct
    int sample_idx;     // 0 - MAX
    int max_grains;     // 1 - 64
    float grain_amp;    // 0.0 - 1.0
    float keytrack;     // 0.0 - 1.0
    float scan;         // -2.0 - 2.0
    float direction;    // 0.0 - 1.0 (Probability)
    float spread;       // 0.0 - 1.0
    float shape;        // 0.0 - 9.0
    float cutoff;       // 20 - 20000 Hz
    float res;          // 0.0 - 0.99
    int filt_type;      // 0=LP, 1=HP, 2=BP, 3=Off
    float filt_key;     // 0.0 - 1.0
    float atk;          // 0.001 - 5.0 (seconds)
    float atk_curve;    // -1.0 to 1.0
    float rel;          // 0.001 - 5.0 (seconds)
    float rel_curve;    // -1.0 to 1.0
    float lfo1_rate;    // 0.1 - 50.0 Hz
    float lfo1_wave;    // 0=Sine, 1=Tri, 2=Saw, 3=S&H
    float lfo1_phase;
    float lfo1_sync;
    float lfo2_rate;    
    float lfo2_wave;    
    float lfo2_phase;
    float lfo2_sync;
    float vol;          // 0.0 - 1.0
    int mod1_src;       // Routing config
    float mod1_amt;
    int mod2_src;
    float mod2_amt;
    
    // FX (Global, but mapped here for UI convenience)
    float fx_wf;
    float fx_ds;
    float fx_bc;
    float fx_mix;

    // Sys (Global)
    float viz_scale;
    int midi_mode;
    int midi_ch;
    float master_vol;
};

class GranularEngine {
public:
    GranularEngine(int voice_id);

    // out_l and out_r must be pre-allocated buffers of size `frames`.
    // fm_signal and rm_signal can be nullptr if no modulation.
    void process(int frames, float* out_l, float* out_r, const float* fm_signal, const float* rm_signal, float rm_amt);
    
    void set_trigger(bool triggered);
    bool is_triggered_now() const { return is_triggered; }
    float get_mod_val(int mod_src_id); // 0=None, 1=Jit, 2=LFO1, 3=LFO2, 4=EG
    float get_modulated_param(int param_id, float base_val, float lo, float hi);
    float get_last_env() const { return master_env; }
    float get_playback_pos() const { return playback_pos; }

    // Raw parameters (0.0 to 1.0 or real values as per UI)
    GrainParams p;

    int current_note;
    int last_note;

    float pitch_bend_semitones;
    float modwheel_val;
    float aftertouch_val;
    bool sustain_active;
    bool is_sustained;


    // Per-parameter modulation (indexed by ParamId)
    uint8_t mod_src[64];
    float   mod_amt[64];

    // Visualization helpers
    int num_active_grains();
    float grain_pos_ratio(int grain_idx);
    float grain_pitch(int grain_idx);
    float grain_pan(int grain_idx);
    bool is_grain_active(int grain_idx) const {
        if (grain_idx < 0 || grain_idx >= MAX_GRAINS_PER_VOICE) return false;
        return active_grains[grain_idx].active;
    }

    // Last output buffer (used for cross-voice FM/RM)
    // We store only the mono mixdown of the last block for simplicity, 
    // or just a single sample if we process per-sample.
    // For block processing, we need a buffer. We assume max 128 frames for memory safety.
    float last_buffer[128];

private:
    int voice_id;
    Grain active_grains[MAX_GRAINS_PER_VOICE];
    float next_grain_time; // in frames
    float playback_pos;
    bool is_triggered;
    float lin_env;
    float master_env;
    float lfo_phases[2];
    
    StateVariableFilter filter_l;
    StateVariableFilter filter_r;
    
    void spawn_grain();
    float get_lfo_val(int lfo_idx);
    bool is_lfo_used(int lfo_idx);
    
    DigitalFX fx_processor;
};

#endif // AUDIO_ENGINE_H
