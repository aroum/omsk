#ifndef SYNTH_DEFS_H
#define SYNTH_DEFS_H

#include <stdint.h>

// Enums for Modulation Matrix
typedef enum {
    SRC_LFO1 = 0,
    SRC_LFO2,
    SRC_EG1,
    SRC_EG2,
    SRC_MODWHEEL,
    SRC_AFTERTOUCH,
    SRC_BREATH,
    SRC_COUNT
} ModSource;

typedef enum {
    // VCO1
    PARAM_VCO1_TRANSPOSE = 0,
    PARAM_VCO1_DETUNE,
    PARAM_VCO1_WAVE,
    PARAM_VCO1_SHAPE,
    
    // VCO2
    PARAM_VCO2_TRANSPOSE,
    PARAM_VCO2_DETUNE,
    PARAM_VCO2_WAVE,
    PARAM_VCO2_SHAPE,
    
    // VCF1
    PARAM_VCF1_CUTOFF,
    PARAM_VCF1_RES,
    PARAM_VCF1_DRIVE,
    PARAM_VCF1_MIX,
    
    // VCF2
    PARAM_VCF2_CUTOFF,
    PARAM_VCF2_RES,
    PARAM_VCF2_DRIVE,
    PARAM_VCF2_MIX,
    
    // LFO1
    PARAM_LFO1_RATE,
    PARAM_LFO1_SMOOTH,
    PARAM_LFO1_WAVE,
    PARAM_LFO1_SHAPE,
    
    // LFO2
    PARAM_LFO2_RATE,
    PARAM_LFO2_SMOOTH,
    PARAM_LFO2_WAVE,
    PARAM_LFO2_SHAPE,
    
    // EG1
    PARAM_EG1_ATTACK,
    PARAM_EG1_DECAY,
    PARAM_EG1_SUSTAIN,
    PARAM_EG1_RELEASE,
    
    // EG2
    PARAM_EG2_ATTACK,
    PARAM_EG2_DECAY,
    PARAM_EG2_SUSTAIN,
    PARAM_EG2_RELEASE,
    
    // MIXER
    PARAM_MIX_VCO1_VOL,
    PARAM_MIX_VCO2_VOL,
    PARAM_MIX_PHASE2,
    PARAM_MIX_NOISE_VOL,
    
    // NOISE
    PARAM_NOISE_COLOR,
    PARAM_CHORD_MODE,
    
    // ARP
    PARAM_ARP_RATE,
    PARAM_ARP_MODE,
    PARAM_ARP_SWING,
    PARAM_ARP_OCT,

    // GLIDE
    PARAM_GLIDE_POLY,
    PARAM_GLIDE_TIME,
    PARAM_GLIDE_SLOPE,
    PARAM_GLIDE_MODE,

    // FX1 (Chorus)
    PARAM_FX1_TIME,
    PARAM_FX1_FEEDBACK,
    PARAM_FX1_SPREAD,
    PARAM_FX1_MIX,

    // FX2 (Delay)
    PARAM_FX2_TIME,
    PARAM_FX2_FEEDBACK,
    PARAM_FX2_TONE,
    PARAM_FX2_MIX,

    // MOD
    PARAM_MOD_ROUTING1,
    PARAM_MOD_DEPTH1,
    PARAM_MOD_ROUTING2,
    PARAM_MOD_DEPTH2,

    // Global
    PARAM_VCF_KEY_TRACK,
    PARAM_VCF_EG_AMT,
    PARAM_VCF_MODE,
    PARAM_LFO_DEPTH,
    PARAM_LFO_FILTER_AMT,
    PARAM_LFO_OSC_AMT,
    PARAM_LFO_OSC_DST,
    PARAM_EG_OSC_AMT,
    PARAM_EG_OSC_DST,
    PARAM_SUB_OSC,
    PARAM_AMP_GAIN,
    PARAM_EG_AMP_MOD,
    PARAM_PITCH_BEND_RANGE,
    PARAM_VOICE_MODE,
    PARAM_VOICE_ASSIGN_MODE,
    PARAM_PAN,
    PARAM_EG_VEL_SENS,
    PARAM_AMP_VEL_SENS,

    // ADV Mode (Global Tempo/Sync/Scale)
    PARAM_ADV_TEMPO,     // 0=Off, 1=ExtSync, 2..=BPM 30..300
    PARAM_ADV_SCALE,     // 0=Off/Chromatic, 1=Major, ...
    PARAM_ADV_MIDI_CH,   // 0-15
    PARAM_ADV_SYNC_MODE, // 0-17, sync division index
    PARAM_ADV_SCALE_KEY, // 0=C, 1=C#, ..., 11=B
    
    PARAM_EG1_ATTACK_CURVE,
    PARAM_EG1_DECAY_CURVE,
    PARAM_EG1_RELEASE_CURVE,
    PARAM_EG2_ATTACK_CURVE,
    PARAM_EG2_DECAY_CURVE,
    PARAM_EG2_RELEASE_CURVE,

    // Interface Emulation (Pads 0-15, Encoders 0-7)
    PARAM_PAD_0, PARAM_PAD_1, PARAM_PAD_2, PARAM_PAD_3,
    PARAM_PAD_4, PARAM_PAD_5, PARAM_PAD_6, PARAM_PAD_7,
    PARAM_PAD_8, PARAM_PAD_9, PARAM_PAD_10, PARAM_PAD_11,
    PARAM_PAD_12, PARAM_PAD_13, PARAM_PAD_14, PARAM_PAD_15,

    PARAM_ENC_0, PARAM_ENC_1, PARAM_ENC_2, PARAM_ENC_3,
    
    PARAM_COUNT
} ParamID;

// Signal Routing Destinations
typedef enum {
    ROUTE_NONE = 0,
    ROUTE_VCF1,
    ROUTE_VCF2
} RouteDest;

typedef enum {
    CHORD_OFF = 0,
    CHORD_MIN2,
    CHORD_MAJ2,
    CHORD_MIN3,
    CHORD_MAJ3,
    CHORD_P4,
    CHORD_TRITONE,
    CHORD_P5,
    CHORD_MIN6,
    CHORD_MAJ6,
    CHORD_INT_MIN7,
    CHORD_INT_MAJ7,
    CHORD_OCT,
    CHORD_MAJ,
    CHORD_MIN,
    CHORD_DIM,
    CHORD_AUG,
    CHORD_SUS2,
    CHORD_SUS4,
    CHORD_MAJ7,
    CHORD_DOM7,
    CHORD_MIN7,
    CHORD_M7B5,
    CHORD_DIM7,
    CHORD_MIN_MAJ7,
    CHORD_AUG_MAJ7,
    CHORD_AUG7,
    CHORD_ADD9,
    CHORD_6,
    CHORD_M6,
    CHORD_69,
    CHORD_7SUS4,
    CHORD_MAJ7SUS4,
    CHORD_MODE_COUNT
} ChordMode;

typedef struct {
    // VCO1
    uint8_t vco1_transpose; // 0-255, map to +/- 5 oct
    uint8_t vco1_detune;    // 0-255, map to +/- 100 cents
    uint8_t vco1_wave;      // 0-255, Morph
    uint8_t vco1_shape;     // 0-255, PWM/Fold
    
    // VCO2
    uint8_t vco2_transpose;
    uint8_t vco2_detune;
    uint8_t vco2_wave;
    uint8_t vco2_shape;
    
    // VCF1
    uint8_t vcf1_cutoff;
    uint8_t vcf1_res;
    uint8_t vcf1_drive;
    uint8_t vcf1_mix;
    uint8_t vcf1_type;    // 0=LPF, 1=BPF, 2=HPF
    
    // VCF2
    uint8_t vcf2_cutoff;
    uint8_t vcf2_res;
    uint8_t vcf2_drive;
    uint8_t vcf2_mix;
    uint8_t vcf2_type;    // 0=LPF, 1=BPF, 2=HPF
    
    // LFO1
    uint8_t lfo1_rate;
    uint8_t lfo1_smooth;
    uint8_t lfo1_wave;
    uint8_t lfo1_shape;

    // LFO2
    uint8_t lfo2_rate;
    uint8_t lfo2_smooth;
    uint8_t lfo2_wave;
    uint8_t lfo2_shape;
    
    // EG1 (ADSR)
    uint8_t eg1_attack;
    uint8_t eg1_decay;
    uint8_t eg1_sustain;
    uint8_t eg1_release;

    // EG2 (ADSR)
    uint8_t eg2_attack;
    uint8_t eg2_decay;
    uint8_t eg2_sustain;
    uint8_t eg2_release;
    
    // Mixer
    uint8_t mix_vco_balance;
    uint8_t mix_osc_noise;
    uint8_t mix_phase2;
    uint8_t mix_master;
    
    // Noise
    uint8_t noise_color;
    uint8_t chord_mode;
    
    // ARP & GLIDE
    uint8_t arp_rate;
    uint8_t arp_mode;
    uint8_t arp_swing;
    uint8_t arp_oct;
    
    uint8_t glide_time;
    uint8_t glide_mode;
    uint8_t glide_slope;
    uint8_t glide_poly;

    // FX1 (Chorus)
    uint8_t fx1_time;
    uint8_t fx1_feedback;
    uint8_t fx1_spread;
    uint8_t fx1_mix;

    // FX2 (Delay)
    uint8_t fx2_time;
    uint8_t fx2_feedback;
    uint8_t fx2_tone;
    uint8_t fx2_mix;

    // Audio-rate Mod
    uint8_t mod_routing1;
    uint8_t mod_depth1;
    uint8_t mod_routing2;
    uint8_t mod_depth2;

    // Global
    uint8_t vcf_key_track;
    uint8_t vcf_eg_amt;
    uint8_t vcf_mode;
    uint8_t lfo_depth;
    uint8_t lfo_filter_amt;
    uint8_t lfo_osc_amt;
    uint8_t lfo_osc_dst;
    uint8_t eg_osc_amt;
    uint8_t eg_osc_dst;
    uint8_t sub_osc;
    uint8_t amp_gain;
    uint8_t eg_amp_mod;
    uint8_t pitch_bend_range;
    uint8_t voice_mode;
    uint8_t voice_assign_mode;
    uint8_t pan;
    uint8_t eg_vel_sens;
    uint8_t amp_vel_sens;
    
    // Modulation Matrix (Depth 0-255)
    // [ParamID][ModSource]
    uint8_t mod_matrix[PARAM_COUNT][SRC_COUNT];
    
    // Explicitly assigned modulation source (0-6 index, 0xFF = none)
    uint8_t mod_source_assigned[PARAM_COUNT];
    
    // Signal Routing
    uint8_t route_vco1; // RouteDest
    uint8_t route_vco2; // RouteDest
    uint8_t route_noise; // RouteDest

    // Sync Flags
    uint8_t lfo1_sync;
    uint8_t lfo2_sync;
    uint8_t eg1_sync; // Syncs A, D, R
    uint8_t eg2_sync; // Syncs A, D, R
    uint8_t arp_sync;
    uint8_t glide_sync;
    uint8_t delay_sync;
    uint8_t chorus_sync;

    // ADV Mode
    uint16_t adv_tempo;    // 0=Off, 1=ExtSync, 2+=BPM offset (30+val-2=BPM)
    uint8_t adv_scale;     // 0=Off, 1=Major, ..., 11=Augmented
    uint8_t midi_channel;  // 0-15
    uint8_t adv_sync_mode; // 0-17 division index
    uint8_t adv_scale_key; // 0=C, 1=C#, ..., 11=B

    // EG Curves (0: Exp, 64: Lin, 127: Log)
    uint8_t eg1_attack_curve;
    uint8_t eg1_decay_curve;
    uint8_t eg1_release_curve;
    uint8_t eg2_attack_curve;
    uint8_t eg2_decay_curve;
    uint8_t eg2_release_curve;

} SynthParams;

// ADV Tempo modes
#define ADV_TEMPO_OFF    0
#define ADV_TEMPO_EXT    1
#define ADV_TEMPO_BPM_MIN 2  // adv_tempo == 2 -> BPM=30, adv_tempo==272 -> BPM=300

// ADV Scale modes (match manifest order)
typedef enum {
    ADV_SCALE_OFF = 0,         // Chromatic / Thru
    ADV_SCALE_MAJOR,
    ADV_SCALE_MINOR,
    ADV_SCALE_HARMONIC_MINOR,
    ADV_SCALE_MELODIC_MINOR,
    ADV_SCALE_DORIAN,
    ADV_SCALE_LOCRIAN,
    ADV_SCALE_LYDIAN,
    ADV_SCALE_BLUES,
    ADV_SCALE_MAJOR_PENT,
    ADV_SCALE_MINOR_PENT,
    ADV_SCALE_AUGMENTED,
    ADV_SCALE_COUNT
} AdvScaleMode;

// Sync MODE divisions (index -> fraction numerator/denominator)
// 0=8/1, 1=8/1t, 2=4/1, 3=4/1t, 4=1/1, 5=1/1t, 6=1/2, 7=1/2t,
// 8=1/4, 9=1/4t, 10=1/8, 11=1/8t, 12=1/16, 13=1/16t, 14=1/32, 15=1/32t, 16=1/64, 17=1/64t
#define ADV_SYNC_MODE_COUNT 18

// Returns beat multiplier relative to quarter note
// e.g. 1/4 division = 1.0, 1/8 = 0.5, 8/1 = 32.0
static inline float adv_sync_mode_to_beat_mult(uint8_t mode) {
    // Each pair: straight, triplet
    // 8/1=32, 4/1=16, 1/1=4, 1/2=2, 1/4=1, 1/8=0.5, 1/16=0.25, 1/32=0.125, 1/64=0.0625
    static const float beat_mults[ADV_SYNC_MODE_COUNT] = {
        32.0f,       // 8/1
        32.0f*2/3,   // 8/1t
        16.0f,       // 4/1
        16.0f*2/3,   // 4/1t
        4.0f,        // 1/1
        4.0f*2/3,    // 1/1t
        2.0f,        // 1/2
        2.0f*2/3,    // 1/2t
        1.0f,        // 1/4
        1.0f*2/3,    // 1/4t
        0.5f,        // 1/8
        0.5f*2/3,    // 1/8t
        0.25f,       // 1/16
        0.25f*2/3,   // 1/16t
        0.125f,      // 1/32
        0.125f*2/3,  // 1/32t
        0.0625f,     // 1/64
        0.0625f*2/3  // 1/64t
    };
    if (mode >= ADV_SYNC_MODE_COUNT) mode = 8; // default 1/4
    return beat_mults[mode];
}

// Returns the time in ms for a given sync division at a given BPM
static inline float adv_sync_mode_to_ms(uint8_t mode, float bpm) {
    if (bpm <= 0.0f) bpm = 120.0f;
    float quarter_ms = 60000.0f / bpm; // ms per quarter note
    return quarter_ms * adv_sync_mode_to_beat_mult(mode);
}

// Global BPM from external MIDI clock (updated by sequencer)
extern volatile float g_midi_bpm;

typedef enum {
    ENV_IDLE = 0,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
} EnvStage;

typedef struct {
    EnvStage stage;
    float value;
    float current_level; // For release/retrigger
} Envelope;

#endif
