#include "midi_map.h"
#include "../synth/audio_engine.h"
#include "../tables/audio_data.h"
#include <math.h>

// Parameter step sizes (per one encoder click)
struct ParamMeta {
  float min_val;
  float max_val;
  float step;
  bool is_int;
};

// Index matches ParamId enum
static const ParamMeta PARAM_META[] = {
    {0.0f, 1.0f, 0.005f, false},                   // POS
    {0.001f, 1.0f, 0.001f, false},                 // SIZE
    {0.1f, 100.0f, 0.1f, false},                   // DENS
    {0.1f, 4.0f, 0.01f, false},                    // PITCH
    {0.0f, (float)NUM_SAMPLES - 1.0f, 1.0f, true}, // SAMPLE_IDX
    {1.0f, 32.0f, 1.0f, true},                     // MAX_GRAINS
    {0.0f, 1.0f, 0.01f, false},                    // GRAIN_AMP
    {0.0f, 1.0f, 0.01f, false},                    // KEYTRACK
    {0.0f, 2.0f, 1.0f, true},         // PITCH_MODE (Speed, Semi, Oct)
    {-2.0f, 2.0f, 0.01f, false},      // SCAN
    {0.0f, 1.0f, 0.01f, false},       // DIRECTION
    {0.0f, 1.0f, 0.01f, false},       // SPREAD
    {0.0f, 27.0f, 1.0f, true},        // SHAPE
    {20.0f, 20000.0f, 100.0f, false}, // CUTOFF
    {0.5f, 13.0f, 0.1f, false},       // RES
    {0.0f, 3.0f, 1.0f, true},         // FILT_TYPE
    {0.0f, 1.0f, 0.01f, false},       // FILT_KEY
    {0.0f, 5.0f, 0.01f, false},       // ATK
    {-1.0f, 1.0f, 0.1f, false},       // ACRV
    {0.0f, 5.0f, 0.01f, false},       // REL
    {-1.0f, 1.0f, 0.1f, false},       // RCRV
    {0.1f, 40.0f, 0.1f, false},       // LFO_RATE
    {0.0f, 4.0f, 1.0f, true},         // LFO_WAVE
    {0.0f, 180.0f, 1.0f, true},       // LFO_PHASE
    {0.0f, 5.0f, 1.0f, true},         // LFO_SYNC
    {0.0f, 1.0f, 0.01f, false},       // VOL (Voice)
    {0.0f, 24.0f, 1.0f, true},        // MOD1_SRC
    {0.0f, 1.0f, 0.01f, false},       // MOD1_AMT
    {0.0f, 24.0f, 1.0f, true},        // MOD2_SRC
    {0.0f, 1.0f, 0.01f, false},       // MOD2_AMT
    {0.0f, 1.0f, 0.01f, false},       // FX_WF
    {1.0f, 80.0f, 1.0f, true},        // FX_DS
    {1.0f, 16.0f, 1.0f, true},        // FX_BC
    {0.0f, 1.0f, 0.01f, false},       // FX_MIX
    {0.0f, 1.0f, 1.0f, true},         // VIZ_SCALE
    {0.0f, 6.0f, 1.0f, true},         // MIDI_MODE
    {0.0f, 16.0f, 1.0f, true},        // MIDI_CH
    {0.0f, 1.0f, 0.01f, false},       // MASTER_VOL
};

static float clamp(float v, float lo, float hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static float *get_param_ptr(GranularEngine *eng, ParamId pid, Page page) {
  switch (pid) {
  case PARAM_POS:
    return &eng->p.pos;
  case PARAM_SIZE:
    return &eng->p.size;
  case PARAM_DENS:
    return &eng->p.dens;
  case PARAM_PITCH:
    return &eng->p.pitch;
  case PARAM_GRAIN_AMP:
    return &eng->p.grain_amp;
  case PARAM_KEYTRACK:
    return &eng->p.keytrack;
  case PARAM_SCAN:
    return &eng->p.scan;
  case PARAM_DIRECTION:
    return &eng->p.direction;
  case PARAM_SPREAD:
    return &eng->p.spread;
  case PARAM_SHAPE:
    return &eng->p.shape;
  case PARAM_CUTOFF:
    return &eng->p.cutoff;
  case PARAM_RES:
    return &eng->p.res;
  case PARAM_FILT_KEY:
    return &eng->p.filt_key;
  case PARAM_ATK:
    return &eng->p.atk;
  case PARAM_ATK_CURVE:
    return &eng->p.atk_curve;
  case PARAM_REL:
    return &eng->p.rel;
  case PARAM_REL_CURVE:
    return &eng->p.rel_curve;
  case PARAM_LFO_RATE:
    return (page == PAGE_LFO2) ? &eng->p.lfo2_rate : &eng->p.lfo1_rate;
  case PARAM_LFO_WAVE:
    return (page == PAGE_LFO2) ? &eng->p.lfo2_wave : &eng->p.lfo1_wave;
  case PARAM_LFO_PHASE:
    return (page == PAGE_LFO2) ? &eng->p.lfo2_phase : &eng->p.lfo1_phase;
  case PARAM_LFO_SYNC:
    return (page == PAGE_LFO2) ? &eng->p.lfo2_sync : &eng->p.lfo1_sync;
  case PARAM_VOL:
    return &eng->p.vol;
  case PARAM_MOD1_AMT:
    return &eng->p.mod1_amt;
  case PARAM_MOD2_AMT:
    return &eng->p.mod2_amt;
  case PARAM_FX_WF:
    return &eng->p.fx_wf;
  case PARAM_FX_DS:
    return &eng->p.fx_ds;
  case PARAM_FX_BC:
    return &eng->p.fx_bc;
  case PARAM_FX_MIX:
    return &eng->p.fx_mix;
  case PARAM_VIZ_SCALE:
    return &eng->p.viz_scale;
  case PARAM_MASTER_VOL:
    return &eng->p.master_vol;
  default:
    return nullptr;
  }
}

static void apply_encoder_delta(UIState *state, GranularEngine *voices,
                                int enc_idx, int delta) {
  const PageLayout &layout = PAGE_LAYOUTS[state->active_page];
  ParamId pid = layout.enc[enc_idx];
  if (pid == PARAM_NONE)
    return;

  // Global pages affect all voices
  bool is_global =
      (state->active_page == PAGE_LFO1 || state->active_page == PAGE_LFO2 ||
       state->active_page == PAGE_MOD || state->active_page == PAGE_FX ||
       state->active_page == PAGE_SYS);

  int voice_start = is_global ? 0 : state->active_voice;
  int voice_end = is_global ? NUM_VOICES : state->active_voice + 1;

  // MIX page: each encoder = its own voice volume
  if (state->active_page == PAGE_MIX) {
    voice_start = enc_idx;
    voice_end = enc_idx + 1;
  }

  for (int v = voice_start; v < voice_end; v++) {
    GranularEngine *eng = &voices[v];
    const ParamMeta &meta = PARAM_META[(int)pid];

    if (pid == PARAM_SAMPLE_IDX) {
      eng->p.sample_idx += delta;
      if (eng->p.sample_idx < 0)
        eng->p.sample_idx = 0;
      if (eng->p.sample_idx >= NUM_SAMPLES)
        eng->p.sample_idx = NUM_SAMPLES - 1;
    } else if (pid == PARAM_PITCH) {
      float step = 0.01f;
      float lo = 0.1f, hi = 4.0f;
      if (eng->p.pitch_mode == 1) {
        step = 1.0f;
        lo = -24.0f;
        hi = 24.0f;
      } else if (eng->p.pitch_mode == 2) {
        step = 1.0f;
        lo = -3.0f;
        hi = 3.0f;
      }
      eng->p.pitch = clamp(eng->p.pitch + (float)delta * step, lo, hi);
    } else if (pid == PARAM_MAX_GRAINS) {
      eng->p.max_grains += delta;
      eng->p.max_grains =
          (int)clamp((float)eng->p.max_grains, meta.min_val, meta.max_val);
    } else if (pid == PARAM_PITCH_MODE) {
      int old_mode = eng->p.pitch_mode;
      eng->p.pitch_mode = (int)clamp((float)eng->p.pitch_mode + delta, 0, 2);
      if (eng->p.pitch_mode != old_mode) {
        if (eng->p.pitch_mode == 0)
          eng->p.pitch = 1.0f;
        else
          eng->p.pitch = 0.0f;
      }
    } else if (pid == PARAM_FILT_TYPE) {
      eng->p.filt_type += delta;
      eng->p.filt_type =
          (int)clamp((float)eng->p.filt_type, meta.min_val, meta.max_val);
    } else if (pid == PARAM_MOD1_SRC) {
      eng->p.mod1_src += delta;
      if (eng->p.mod1_src < 0)
        eng->p.mod1_src = 0;
    } else if (pid == PARAM_MOD2_SRC) {
      eng->p.mod2_src += delta;
      if (eng->p.mod2_src < 0)
        eng->p.mod2_src = 0;
    } else if (pid == PARAM_MIDI_MODE) {
      eng->p.midi_mode += delta;
      eng->p.midi_mode =
          (int)clamp((float)eng->p.midi_mode, meta.min_val, meta.max_val);
    } else if (pid == PARAM_MIDI_CH) {
      eng->p.midi_ch += delta;
      eng->p.midi_ch =
          (int)clamp((float)eng->p.midi_ch, meta.min_val, meta.max_val);
    } else if (pid == PARAM_DENS) {
      float step = 0.1f;
      if (delta > 0) {
        step = (eng->p.dens >= 9.95f) ? 1.0f : 0.1f;
      } else {
        step = (eng->p.dens > 10.05f) ? 1.0f : 0.1f;
      }
      float next_val = eng->p.dens + (float)delta * step;
      if (next_val < 10.0f) {
        next_val = roundf(next_val * 10.0f) / 10.0f;
      } else {
        next_val = roundf(next_val);
      }
      eng->p.dens = clamp(next_val, 0.1f, 100.0f);
    } else {
      float *ptr = get_param_ptr(eng, pid, state->active_page);
      if (ptr) {
        *ptr =
            clamp(*ptr + (float)delta * meta.step, meta.min_val, meta.max_val);
      }
    }
  }
}

bool midi_map_process(UIState *state, void *voices_ptr, uint8_t cc,
                     uint8_t value) {
  GranularEngine *voices = (GranularEngine *)voices_ptr;

  // Check encoders
  for (int e = 0; e < 4; e++) {
    if (cc == ENC_CC_MAP[e]) {
      int delta = ui_on_encoder(state, e, (int)value);
      if (delta != 0) {
        if (state->held_mod != BTN_COUNT) {
          // Mod-hold: set mod amount for active param
          const PageLayout &layout = PAGE_LAYOUTS[state->active_page];
          ParamId pid = layout.enc[e];
          if (pid != PARAM_NONE) {
            if (!is_param_modifiable(pid))
              return true;
            GranularEngine *eng = &voices[state->active_voice];
            uint8_t src = 0;
            if (state->held_mod == BTN_JIT)
              src = 1;
            else if (state->held_mod == BTN_LFO1)
              src = 2;
            else if (state->held_mod == BTN_LFO2)
              src = 3;
            else if (state->held_mod == BTN_EG)
              src = 4;

            // Preserve previous source to detect change
            uint8_t prev_src = eng->mod_src[(int)pid];
            eng->mod_src[(int)pid] = src;
            if (src == 4 && prev_src != 4) {
                // EG just assigned – start from neutral centre (mod_amt = 0.5)
                eng->mod_amt[(int)pid] = 0.5f;
            } else {
                // Adjust amount based on encoder delta
                float min_mod = (src == 4) ? -1.0f : 0.0f;
                eng->mod_amt[(int)pid] = clamp(
                    eng->mod_amt[(int)pid] + (float)delta * 0.01f, min_mod, 1.0f);
            }
            state->mod_interaction_happened = true;
          }
        } else {
          apply_encoder_delta(state, voices, e, delta);
        }
        return true;
      }
    }
  }

  // Check buttons
  for (int b = 0; b < BTN_COUNT; b++) {
    if (cc == BTN_CC_MAP[b]) {
      if (value >= 64) {
        ui_on_button_press(state, (ButtonId)b);
      } else {
        ui_on_button_release(state, (ButtonId)b);
      }
      return true;
    }
  }

  // Check direct parameter CC mappings
  ParamId pid = PARAM_NONE;
  switch (cc) {
    case 7:   pid = PARAM_MASTER_VOL; break;
    case 14:  pid = PARAM_LFO_RATE; break;
    case 15:  pid = PARAM_SPREAD; break;
    case 16:  pid = PARAM_SHAPE; break;
    case 20:  pid = PARAM_POS; break;
    case 21:  pid = PARAM_SIZE; break;
    case 22:  pid = PARAM_DENS; break;
    case 23:  pid = PARAM_PITCH; break;
    case 24:  pid = PARAM_SAMPLE_IDX; break;
    case 25:  pid = PARAM_MAX_GRAINS; break;
    case 26:  pid = PARAM_GRAIN_AMP; break;
    case 27:  pid = PARAM_KEYTRACK; break;
    case 28:  pid = PARAM_LFO_WAVE; break;
    case 29:  pid = PARAM_PITCH_MODE; break;
    case 30:  pid = PARAM_SCAN; break;
    case 31:  pid = PARAM_DIRECTION; break;
    case 71:  pid = PARAM_RES; break;
    case 72:  pid = PARAM_REL; break;
    case 73:  pid = PARAM_ATK; break;
    case 74:  pid = PARAM_CUTOFF; break;
    case 75:  pid = PARAM_FILT_KEY; break;
    case 85:  pid = PARAM_ATK_CURVE; break;
    case 86:  pid = PARAM_REL_CURVE; break;
    case 87:  pid = PARAM_LFO_PHASE; break;
    case 89:  pid = PARAM_LFO_SYNC; break;
    case 90:  pid = PARAM_VOL; break;
    case 91:  pid = PARAM_FX_MIX; break;
    case 92:  pid = PARAM_MOD1_SRC; break;
    case 93:  pid = PARAM_MOD1_AMT; break;
    case 94:  pid = PARAM_MOD2_SRC; break;
    case 95:  pid = PARAM_MOD2_AMT; break;
    case 96:  pid = PARAM_FX_WF; break;
    case 97:  pid = PARAM_FX_DS; break;
    case 98:  pid = PARAM_FX_BC; break;
    case 107: pid = PARAM_FILT_TYPE; break;
    case 109: pid = PARAM_MIDI_MODE; break;
    case 118: pid = PARAM_MIDI_CH; break;
    default:  pid = PARAM_NONE; break;
  }

  if (pid != PARAM_NONE) {
    const ParamMeta &meta = PARAM_META[(int)pid];
    float norm = (float)value / 127.0f;

    bool is_global =
        (state->active_page == PAGE_LFO1 || state->active_page == PAGE_LFO2 ||
         state->active_page == PAGE_MOD || state->active_page == PAGE_FX ||
         state->active_page == PAGE_SYS ||
         pid == PARAM_MASTER_VOL || pid == PARAM_MIDI_MODE || pid == PARAM_MIDI_CH);

    int voice_start = is_global ? 0 : state->active_voice;
    int voice_end = is_global ? NUM_VOICES : state->active_voice + 1;

    for (int v = voice_start; v < voice_end; v++) {
      GranularEngine *eng = &voices[v];

      // Custom pitch limits based on pitch mode
      float lo = meta.min_val;
      float hi = meta.max_val;
      bool is_int = meta.is_int;
      if (pid == PARAM_PITCH) {
        if (eng->p.pitch_mode == 1) {
          lo = -24.0f; hi = 24.0f; is_int = true;
        } else if (eng->p.pitch_mode == 2) {
          lo = -3.0f; hi = 3.0f; is_int = true;
        }
      }

      float target_val = lo + norm * (hi - lo);
      if (is_int) {
        target_val = roundf(target_val);
      }

      float *ptr = get_param_ptr(eng, pid, state->active_page);
      if (ptr) {
        *ptr = target_val;
      } else {
        if (pid == PARAM_SAMPLE_IDX) {
          eng->p.sample_idx = (int)target_val;
        } else if (pid == PARAM_MAX_GRAINS) {
          eng->p.max_grains = (int)target_val;
        } else if (pid == PARAM_PITCH_MODE) {
          eng->p.pitch_mode = (int)target_val;
        } else if (pid == PARAM_FILT_TYPE) {
          eng->p.filt_type = (int)target_val;
        } else if (pid == PARAM_MOD1_SRC) {
          eng->p.mod1_src = (int)target_val;
        } else if (pid == PARAM_MOD2_SRC) {
          eng->p.mod2_src = (int)target_val;
        } else if (pid == PARAM_MIDI_MODE) {
          eng->p.midi_mode = (int)target_val;
        } else if (pid == PARAM_MIDI_CH) {
          eng->p.midi_ch = (int)target_val;
        }
      }
    }
    return true;
  }

  return false;
}

float get_normalized_param_value(void* voices_ptr, int voice_idx, int param_id, int page_id) {
    if (!voices_ptr || voice_idx < 0 || voice_idx >= NUM_VOICES) return 0.0f;
    GranularEngine *voices = (GranularEngine *)voices_ptr;
    GranularEngine *eng = &voices[voice_idx];
    ParamId pid = (ParamId)param_id;
    Page page = (Page)page_id;
    
    float *ptr = get_param_ptr(eng, pid, page);
    if (!ptr) return 0.0f;
    
    const ParamMeta &meta = PARAM_META[param_id];
    float val = *ptr;
    
    if (meta.max_val == meta.min_val) return 0.0f;
    
    float normalized = (val - meta.min_val) / (meta.max_val - meta.min_val);
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 1.0f) normalized = 1.0f;
    return normalized;
}
