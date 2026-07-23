#include "ui_state.h"
#include <string.h>

// Mirrors Python pages dict
const PageLayout PAGE_LAYOUTS[PAGE_COUNT] = {
    {"GRAIN1", {PARAM_SAMPLE_IDX, PARAM_POS,       PARAM_SIZE,      PARAM_DENS}},
    {"GRAIN2", {PARAM_PITCH,      PARAM_PITCH_MODE,PARAM_MAX_GRAINS,PARAM_GRAIN_AMP}},
    {"GRAIN3", {PARAM_SCAN,       PARAM_DIRECTION, PARAM_SPREAD,    PARAM_SHAPE}},
    {"FILT",   {PARAM_CUTOFF,     PARAM_RES,       PARAM_FILT_TYPE, PARAM_FILT_KEY}},
    {"JIT",    {PARAM_NONE,       PARAM_NONE,      PARAM_NONE,      PARAM_NONE}},
    {"LFO1",   {PARAM_LFO_RATE,   PARAM_LFO_WAVE,  PARAM_LFO_PHASE, PARAM_LFO_SYNC}},
    {"LFO2",   {PARAM_LFO_RATE,   PARAM_LFO_WAVE,  PARAM_LFO_PHASE, PARAM_LFO_SYNC}},
    {"EG",     {PARAM_ATK,        PARAM_ATK_CURVE, PARAM_REL,       PARAM_REL_CURVE}},
    {"MOD",    {PARAM_MOD1_SRC,   PARAM_MOD1_AMT,  PARAM_MOD2_SRC,  PARAM_MOD2_AMT}},
    {"FX",     {PARAM_FX_WF,      PARAM_FX_DS,     PARAM_FX_BC,     PARAM_FX_MIX}},
    {"MIX",    {PARAM_VOL,        PARAM_VOL,       PARAM_VOL,       PARAM_VOL}},
    {"SYS",    {PARAM_VIZ_SCALE,  PARAM_MIDI_MODE, PARAM_MIDI_CH,   PARAM_MASTER_VOL}},
};

const char* PARAM_NAMES[] = {
    "POSIT", "SIZE", "DENS", "PITCH",
    "SAMPLE", "GRAINS", "VOL_G", "KEYTRK",
    "P.MODE",
    "SCAN", "REV", "SPREAD", "SHAPE",
    "CUT", "RES", "TYPE", "KTRK",
    "ATK", "A.CURV", "REL", "R.CURV",
    "RATE", "WAVE", "PHASE", "SYNC",
    "VOL",
    "M1.S>D", "M1.DPH", "M2.S>D", "M2.DPH",
    "FOLD", "DNSMPL", "BCRSH", "MIX",
    "YSCALE", "V_MODE", "CH", "VOL_M",
};

void ui_state_init(UIState* s) {
    memset(s, 0, sizeof(UIState));
    s->active_page = PAGE_GRAIN1;
    s->active_voice = 0;
    s->first_trig_voice = -1;
    s->held_mod = BTN_COUNT; // none
}

static bool is_mod_btn(ButtonId btn) {
    return btn == BTN_JIT || btn == BTN_LFO1 || btn == BTN_LFO2 || btn == BTN_EG;
}

static bool is_trig_btn(ButtonId btn) {
    return btn >= BTN_TRIG1 && btn <= BTN_TRIG4;
}

// Resolve ButtonId -> Page
static Page resolve_page(UIState* s, ButtonId btn) {
    (void)s;
    switch(btn) {
        case BTN_GRAIN1: return PAGE_GRAIN1;
        case BTN_GRAIN2: return PAGE_GRAIN2;
        case BTN_GRAIN3: return PAGE_GRAIN3;
        case BTN_FILT:   return PAGE_FILT;
        case BTN_JIT:    return PAGE_JIT;
        case BTN_LFO1:   return PAGE_LFO1;
        case BTN_LFO2:   return PAGE_LFO2;
        case BTN_EG:     return PAGE_EG;
        case BTN_MOD:    return PAGE_MOD;
        case BTN_FX:     return PAGE_FX;
        case BTN_MIX:    return PAGE_MIX;
        case BTN_SYS:    return PAGE_SYS;
        default:         return s->active_page;
    }
}

void ui_on_button_press(UIState* s, ButtonId btn) {
    if (is_trig_btn(btn)) {
        int v = (int)(btn - BTN_TRIG1);
        if (s->fn_held) {
            // Latch toggle (Sys + Trig)
            s->voices_triggered[v] = !s->voices_triggered[v];
        } else {
            s->voices_triggered[v] = true;
        }
        
        if (s->voices_triggered[v]) {
            if (s->first_trig_voice == -1) {
                s->first_trig_voice = v;
                s->active_voice = v;
            }
        }
    } else if (btn == BTN_SYS) {
        s->fn_held = true;
        s->active_page = PAGE_SYS;
    } else if (is_mod_btn(btn)) {
        s->held_mod = btn;
        s->mod_interaction_happened = false;
        s->active_page = resolve_page(s, btn);
    } else {
        s->active_page = resolve_page(s, btn);
    }
}

void ui_on_button_release(UIState* s, ButtonId btn) {
    if (is_trig_btn(btn)) {
        int v = (int)(btn - BTN_TRIG1);
        if (!s->fn_held) {
            // Normal release (non-latched)
            s->voices_triggered[v] = false;
        }
        
        // Update first_trig_voice if the "display" voice was released
        if (!s->voices_triggered[v] && v == s->first_trig_voice) {
            s->first_trig_voice = -1;
            for (int i = 0; i < NUM_VOICES; i++) {
                if (s->voices_triggered[i]) {
                    s->first_trig_voice = i;
                    s->active_voice = i;
                    break;
                }
            }
        }
    } else if (btn == BTN_SYS) {
        s->fn_held = false;
    } else if (is_mod_btn(btn)) {
        // If no encoder was moved while holding, switch to that page
        if (!s->mod_interaction_happened) {
            s->active_page = resolve_page(s, btn);
        }
        s->held_mod = BTN_COUNT;
        s->mod_interaction_happened = false;
    }
}

int ui_on_encoder(UIState* s, int enc_idx, int midi_value) {
    // Relative encoder: <=63 = step -, >=65 = step +
    if (midi_value <= 63) return -1;
    if (midi_value >= 65) return +1;
    return 0;
}
