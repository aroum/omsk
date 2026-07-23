#ifndef SYNTH_H
#define SYNTH_H

#include "audio_engine.h"
#include "fx.h"

#define NUM_VOICES 4

struct PresetData {
    GrainParams params;
    uint8_t mod_src[64];
    float mod_amt[64];
};

struct PresetSlot {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t checksum;
    PresetData data;
};

class Synth {
public:
    Synth();

    void init();

    // Generate 'frames' of audio. 'out_l' and 'out_r' must be large enough.
    void process(int frames, float* out_l, float* out_r);

    GranularEngine* get_voice(int v) { return &voices[v]; }
    DigitalFX* get_fx() { return &global_fx; }

    bool preset_save(uint8_t slot);
    bool preset_load(uint8_t slot);

    // Global FX Parameters
    float fx_wf_gain;
    float fx_ds_factor;
    float fx_bc_bits;
    float fx_mix;

private:
    GranularEngine voices[NUM_VOICES] = {
        GranularEngine(0),
        GranularEngine(1),
        GranularEngine(2),
        GranularEngine(3)
    };
    
    DigitalFX global_fx;

    // Temporary buffers for modulation routing
    float mod_buffer[NUM_VOICES][128]; 
    float mix_l_buffer[128];
    float mix_r_buffer[128];
};

#endif // SYNTH_H
