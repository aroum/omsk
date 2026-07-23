#ifndef UI_STATE_H
#define UI_STATE_H

#include <stdint.h>
#include <stdbool.h>

// Pages matching Python main.py
typedef enum {
    PAGE_GRAIN1 = 0,
    PAGE_GRAIN2,
    PAGE_GRAIN3,
    PAGE_FILT,
    PAGE_JIT,
    PAGE_LFO1,
    PAGE_LFO2,
    PAGE_EG,
    PAGE_MOD,
    PAGE_FX,
    PAGE_MIX,
    PAGE_SYS,
    PAGE_COUNT
} Page;

// Button matrix layout (4x4 = 16 buttons)
// Row 0: GRAIN1, GRAIN2, GRAIN3, FILT
// Row 1: JIT,    LFO1,   LFO2,   EG
// Row 2: SYS,    MOD,    MIX,    FN
// Row 3: TRIG1,  TRIG2,  TRIG3,  TRIG4
typedef enum {
    BTN_GRAIN1 = 0, BTN_GRAIN2, BTN_GRAIN3, BTN_FILT,
    BTN_JIT,        BTN_LFO1,   BTN_LFO2,   BTN_EG,
    BTN_MOD,        BTN_FX,     BTN_MIX,    BTN_SYS,
    BTN_TRIG1,      BTN_TRIG2,  BTN_TRIG3,  BTN_TRIG4,
    BTN_COUNT
} ButtonId;

#define BTN_FN BTN_SYS // Sys button acts as FN modifier

#define NUM_VOICES 4
#define NUM_ENCODERS 4

// Which parameter each encoder controls per page (index into voice params)
// Mirroring Python pages dict
typedef enum {
    PARAM_POS = 0, PARAM_SIZE, PARAM_DENS, PARAM_PITCH,
    PARAM_SAMPLE_IDX, PARAM_MAX_GRAINS, PARAM_GRAIN_AMP, PARAM_KEYTRACK,
    PARAM_PITCH_MODE,
    PARAM_SCAN, PARAM_DIRECTION, PARAM_SPREAD, PARAM_SHAPE,
    PARAM_CUTOFF, PARAM_RES, PARAM_FILT_TYPE, PARAM_FILT_KEY,
    PARAM_ATK, PARAM_ATK_CURVE, PARAM_REL, PARAM_REL_CURVE,
    PARAM_LFO_RATE, PARAM_LFO_WAVE, PARAM_LFO_PHASE, PARAM_LFO_SYNC,
    PARAM_VOL,
    PARAM_MOD1_SRC, PARAM_MOD1_AMT, PARAM_MOD2_SRC, PARAM_MOD2_AMT,
    PARAM_FX_WF, PARAM_FX_DS, PARAM_FX_BC, PARAM_FX_MIX,
    PARAM_VIZ_SCALE, PARAM_MIDI_MODE, PARAM_MIDI_CH, PARAM_MASTER_VOL,
    PARAM_NONE = 0xFF
} ParamId;

static inline bool is_param_modifiable(ParamId pid) {
    if (pid == PARAM_VIZ_SCALE || pid == PARAM_MIDI_MODE || pid == PARAM_MIDI_CH) {
        return false;
    }
    if (pid == PARAM_MOD1_SRC || pid == PARAM_MOD2_SRC) {
        return false;
    }
    return true;
}

struct PageLayout {
    const char* name;
    ParamId enc[4]; // Enc1..Enc4
};

extern const PageLayout PAGE_LAYOUTS[PAGE_COUNT];
extern const char* PARAM_NAMES[];

struct UIState {
    Page     active_page;
    int      active_voice;              // Voice shown on OLED (= first trig pressed)
    int      first_trig_voice;          // -1 if none held; set to first trig pressed
    bool     fn_held;
    ButtonId held_mod;                  // JIT/LFO1/LFO2/EG if held
    bool     mod_interaction_happened;
    bool     voices_triggered[NUM_VOICES];  // Which voices are currently playing
    float    encoder_accum[NUM_ENCODERS];
};

void ui_state_init(UIState* s);
void ui_on_button_press(UIState* s, ButtonId btn);
void ui_on_button_release(UIState* s, ButtonId btn);
// Returns signed delta (1 or -1), or 0 if no action
int  ui_on_encoder(UIState* s, int enc_idx, int midi_value);

#endif // UI_STATE_H
