#include "ui_oled.h"
#include <cstdio>
#include <string.h>
#include <math.h>
#include "pico/time.h"
#include "../sw_config.h"
#include "../../../shared/image_logo.h"
#include "../tables/audio_data.h"
#include "../synth/grain_env.h"

// ---- OLED is 128x64 px ---- //
// Two rendering modes:
//   GRAIN_VIEW  — shown when any trigger is held (envelope + grains + range cursors)
//   PARAM_VIEW  — default param bars + waveform zones
//
// Switches automatically:
//   Trig pressed  → GRAIN_VIEW
//   Encoder moved → PARAM_VIEW  (with 2s auto-return timer if trigger still held)

static const char* WAVE_NAMES[] = {"SINE", "TRI ", "SAW ", "RSAW", "S&H "};
static const char* FILT_NAMES[] = {"LP  ", "HP  ", "BP  ", "OFF "};

// Baked LFO waveform Y-offsets (range 3 to 15, centered inside 18px height box)
static const uint8_t LFO_SINE_Y[24] = { 9, 8, 6, 5, 4, 3, 3, 4, 5, 6, 8, 9, 9, 10, 12, 13, 14, 15, 15, 14, 13, 12, 10, 9 };
static const uint8_t LFO_TRI_Y[24]  = { 9, 8, 7, 6, 5, 4, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 14, 13, 12, 11, 9 };
static const uint8_t LFO_SAW_Y[24]  = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 3, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 3 };
static const uint8_t LFO_RSAW_Y[24] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15 };
static const uint8_t LFO_SH_Y[24]   = { 12, 12, 12, 12, 12, 4, 4, 4, 4, 4, 4, 14, 14, 14, 14, 14, 14, 8, 8, 8, 8, 8, 8, 8 };

// Cached dynamic morph shape values (28 shapes * 24 points)
static uint8_t shape_icons_lut[28][24];

// ---- Display mode ----
typedef enum {
    DISP_GRAIN = 0,
    DISP_PARAM
} DispMode;

static DispMode g_mode = DISP_PARAM;
static uint32_t g_last_activity_ms = 0;
#define GRAIN_VIEW_RETURN_MS 5000   // Return to grain view 5s after encoder idle

void oled_init(u8g2_t* u8g2) {
    u8g2_InitDisplay(u8g2);
    u8g2_SetPowerSave(u8g2, 0);
    u8g2_ClearDisplay(u8g2);

    u8g2_ClearBuffer(u8g2);
    u8g2_SetBitmapMode(u8g2, 1);
    u8g2_DrawXBM(u8g2, 2, 7, 125, 51, image_logo_bits);
#ifdef CFG_LOGO_SUBTITLE
    if (CFG_LOGO_SUBTITLE[0] != '\0') {
        u8g2_SetFont(u8g2, u8g2_font_4x6_tr);
        u8g2_uint_t width = u8g2_GetStrWidth(u8g2, CFG_LOGO_SUBTITLE);
        u8g2_DrawStr(u8g2, 128 - width, 63, CFG_LOGO_SUBTITLE);
    }
#endif
    u8g2_SendBuffer(u8g2);
    sleep_ms(1500);

// Pre-bake grain shape envelope icons into a LUT to save CPU resources
    for (int s = 0; s < 28; s++) {
        // First pass: find max amplitude for this shape
        float max_amp = 0.0f;
        for (int i = 0; i < 24; i++) {
            float x = (float)i / 23.0f;
            float amp = GrainEnvelope::get_morphed_envelope((float)s, x);
            if (amp < 0.0f) amp = 0.0f;
            if (amp > 1.0f) amp = 1.0f;
            if (amp > max_amp) max_amp = amp;
        }
        if (max_amp < 1e-6f) max_amp = 1.0f; // avoid division by zero
        // Second pass: store scaled, rounded pixel heights (0‑12)
        for (int i = 0; i < 24; i++) {
            float x = (float)i / 23.0f;
            float amp = GrainEnvelope::get_morphed_envelope((float)s, x);
            if (amp < 0.0f) amp = 0.0f;
            if (amp > 1.0f) amp = 1.0f;
            // Scale to full height based on max_amp of this shape
            uint8_t pix = (uint8_t)lroundf((amp / max_amp) * 12.0f);
            shape_icons_lut[s][i] = pix;
        }
    }
}

void oled_notify_encoder_activity() {
    g_mode = DISP_PARAM;
    g_last_activity_ms = to_ms_since_boot(get_absolute_time());
}

void oled_notify_trigger_activity() {
    g_mode = DISP_GRAIN;
    g_last_activity_ms = to_ms_since_boot(get_absolute_time());
}

static void draw_header(u8g2_t* u, const UIState* s, GranularEngine* voices, const char* name_override = nullptr) {
    u8g2_SetFont(u, u8g2_font_5x7_tf);
    char name_buf[32];
    const char* name = name_override;
    if (!name) {
        const char* base_name = PAGE_LAYOUTS[s->active_page].name;
        if (s->active_page == PAGE_MOD || s->active_page == PAGE_FX || 
            s->active_page == PAGE_MIX || s->active_page == PAGE_SYS) {
            snprintf(name_buf, sizeof(name_buf), "%s GLOBAL", base_name);
            name = name_buf;
        } else {
            name = base_name;
        }
    }
    u8g2_DrawStr(u, 0, 8, name);

    for (int v = 0; v < NUM_VOICES; v++) {
        int x = 80 + v * 12;
        char vbuf[3];
        snprintf(vbuf, sizeof(vbuf), "V%d", v + 1);
        if (v == s->active_voice) {
            u8g2_DrawBox(u, x - 1, 0, 11, 9);
            u8g2_SetDrawColor(u, 0);
            u8g2_DrawStr(u, x, 8, vbuf);
            u8g2_SetDrawColor(u, 1);
        } else if (s->voices_triggered[v]) {
            u8g2_DrawFrame(u, x - 1, 0, 11, 9);
            u8g2_DrawStr(u, x, 8, vbuf);
        } else {
            u8g2_DrawStr(u, x, 8, vbuf);
        }
    }
    u8g2_DrawHLine(u, 0, 10, 128);
}

// ------------------------------------------------------------------ //
//  GRAIN VIEW
//  Mirrors old.py OLED.paintEvent():
//   - Top/bottom envelope drawn around center
//   - Two vertical cursors at pos±spread (grain birth range)
//   - Animated grain dots
// ------------------------------------------------------------------ //
static void draw_grain_view(u8g2_t* u, const UIState* state, GranularEngine* voices) {
    GranularEngine* eng = &voices[state->active_voice];
    int sid = eng->p.sample_idx;

    int center_y = 32;
    int max_h = 28;

#ifdef SHOW_STATUS_BAR
    char sid_buf[20];
    snprintf(sid_buf, sizeof(sid_buf), "SMPL %d/%d", sid + 1, NUM_SAMPLES);
    draw_header(u, state, voices, sid_buf);
    center_y = 37;
    max_h = 24;
#endif

    // -- Envelope (top + bottom mirror) --
    const uint8_t* env_pos = _env_pos_ptr_for(sid);
    const uint8_t* env_neg = _env_neg_ptr_for(sid);
    if (env_pos && env_neg) {
        for (int x = 0; x < 127; x++) {
            int p0 = (int)env_pos[x] - 128;
            int p1 = (int)env_pos[x+1] - 128;
            int n0 = (int)env_neg[x] - 128;
            int n1 = (int)env_neg[x+1] - 128;
            
            int yp0 = center_y - (p0 * max_h / 127);
            int yp1 = center_y - (p1 * max_h / 127);
            int yn0 = center_y - (n0 * max_h / 127);
            int yn1 = center_y - (n1 * max_h / 127);
            
            u8g2_DrawLine(u, x, yp0, x + 1, yp1);
            u8g2_DrawLine(u, x, yn0, x + 1, yn1);
        }
    }

    // -- Range cursors (pos ± mod_amt) --
    float pos     = eng->p.pos;
    float mod_amt = eng->mod_amt[0]; // PARAM_POS = 0
    float center_pos = fmodf(pos + eng->get_playback_pos(), 1.0f);
    if (center_pos < 0.0f) center_pos += 1.0f;

    int cx = (int)(center_pos * 127.0f);
    if (cx < 0) cx = 0;
    if (cx > 127) cx = 127;

    int cursor_y_start = 10;
    int cursor_h = 64 - cursor_y_start;

    if (mod_amt < 0.01f) {
        // Single solid line at center pos
        u8g2_DrawVLine(u, cx, cursor_y_start, cursor_h);
    } else {
        float l_pos = center_pos - mod_amt;
        float r_pos = center_pos + mod_amt;
        if (l_pos < 0) l_pos += 1.0f;
        if (r_pos > 1.0f) r_pos -= 1.0f;
        
        int lx = (int)(l_pos * 127.0f);
        int rx = (int)(r_pos * 127.0f);
        if (lx < 0) lx = 0; if (lx > 127) lx = 127;
        if (rx < 0) rx = 0; if (rx > 127) rx = 127;
        
        u8g2_DrawVLine(u, lx, cursor_y_start, cursor_h);
        u8g2_DrawVLine(u, rx, cursor_y_start, cursor_h);
    }

    // -- Active grains dots --
    uint32_t slen = raw_len(sid);
    if (slen > 0) {
        bool show_pan = (eng->p.viz_scale != 0.0f);
        for (int gi = 0; gi < MAX_GRAINS_PER_VOICE; gi++) {
            if (eng->is_grain_active(gi)) {
                float gpos  = eng->grain_pos_ratio(gi);
                int gx = (int)(gpos * 127.0f);
                
                int gy;
                if (show_pan) {
                    float gpan = eng->grain_pan(gi);
                    gy = cursor_y_start + (int)(gpan * (63 - cursor_y_start));
                } else {
                    float gpitch = eng->grain_pitch(gi);
                    gy = 63 - (int)((gpitch / 4.0f) * (63 - cursor_y_start));
                }

                if (gy < cursor_y_start + 1) gy = cursor_y_start + 1;
                if (gy > 62) gy = 62;
                u8g2_DrawPixel(u, gx, gy);
            }
        }
    }

#ifndef SHOW_STATUS_BAR
    u8g2_SetFont(u, u8g2_font_4x6_tf);
    char hdr[24];
    snprintf(hdr, sizeof(hdr), "V%d  %d/%d", state->active_voice + 1,
             eng->p.sample_idx + 1, NUM_SAMPLES > 0 ? NUM_SAMPLES : 1);
    u8g2_DrawStr(u, 0, 6, hdr);
#endif
}

// ------------------------------------------------------------------ //
//  PARAM VIEW  (unchanged from before)
// ------------------------------------------------------------------ //
static float get_param_ratio(GranularEngine* eng, ParamId pid, int enc_idx_for_mix, Page page) {
    switch(pid) {
        case PARAM_POS: {
            // Compute position in seconds based on current sample length
            float sample_len_sec = (float)raw_len(eng->p.sample_idx) / (float)CONFIG_SAMPLE_RATE;
            float pos_sec = eng->p.pos * sample_len_sec;
            // Quantize to nearest 0.001 s (1 ms)
            float quantized = lroundf(pos_sec * 1000.0f) / 1000.0f;
            // Return ratio (0..1) based on quantized time
            return quantized / sample_len_sec;
        }
        case PARAM_SIZE:       return eng->p.size;
        case PARAM_DENS:       return (eng->p.dens - 0.1f) / (100.0f - 0.1f);
        case PARAM_PITCH:      
            if (eng->p.pitch_mode == 0) return (eng->p.pitch - 0.1f) / 3.9f;
            if (eng->p.pitch_mode == 1) return (eng->p.pitch + 24.0f) / 48.0f;
            return (eng->p.pitch + 3.0f) / 6.0f;
        case PARAM_PITCH_MODE: return (float)eng->p.pitch_mode / 2.0f;
        case PARAM_SAMPLE_IDX: return (float)eng->p.sample_idx / (float)(NUM_SAMPLES - 1);
        case PARAM_MAX_GRAINS: return (float)(eng->p.max_grains - 1) / 31.0f; // 1-32
        case PARAM_GRAIN_AMP:  return eng->p.grain_amp;
        case PARAM_KEYTRACK:   return eng->p.keytrack;
        case PARAM_SCAN:       return (eng->p.scan + 2.0f) / 4.0f;
        case PARAM_DIRECTION:  return eng->p.direction;
        case PARAM_SPREAD:     return eng->p.spread;
        case PARAM_SHAPE:      return eng->p.shape / 27.0f;
        case PARAM_CUTOFF:     return (eng->p.cutoff - 20.0f) / (20000.0f - 20.0f);
        case PARAM_RES:        return (eng->p.res - 0.5f) / (13.0f - 0.5f);
        case PARAM_FILT_TYPE:  return (float)eng->p.filt_type / 3.0f;
        case PARAM_FILT_KEY:   return eng->p.filt_key;
        case PARAM_ATK:        return (eng->p.atk - 0.001f) / 4.999f;
        case PARAM_ATK_CURVE:  return (eng->p.atk_curve + 1.0f) / 2.0f;
        case PARAM_REL:        return (eng->p.rel - 0.001f) / 4.999f;
        case PARAM_REL_CURVE:  return (eng->p.rel_curve + 1.0f) / 2.0f;
        case PARAM_LFO_RATE:   return (((page == PAGE_LFO2) ? eng->p.lfo2_rate : eng->p.lfo1_rate) - 0.1f) / 49.9f;
        case PARAM_LFO_WAVE:   return ((page == PAGE_LFO2) ? eng->p.lfo2_wave : eng->p.lfo1_wave) / 4.0f;
        case PARAM_LFO_PHASE:  return ((page == PAGE_LFO2) ? eng->p.lfo2_phase : eng->p.lfo1_phase) / 180.0f;
        case PARAM_LFO_SYNC:   return ((page == PAGE_LFO2) ? eng->p.lfo2_sync : eng->p.lfo1_sync) / 5.0f;
        case PARAM_VOL:        return eng->p.vol;
        case PARAM_MOD1_SRC:   return (float)eng->p.mod1_src / 24.0f;
        case PARAM_MOD1_AMT:   return eng->p.mod1_amt;
        case PARAM_MOD2_SRC:   return (float)eng->p.mod2_src / 24.0f;
        case PARAM_MOD2_AMT:   return eng->p.mod2_amt;
        case PARAM_FX_WF:      return eng->p.fx_wf;
        case PARAM_FX_DS:      return (eng->p.fx_ds - 1.0f) / 79.0f;
        case PARAM_FX_BC:      return (eng->p.fx_bc - 1.0f) / 15.0f;
        case PARAM_FX_MIX:     return eng->p.fx_mix;
        case PARAM_VIZ_SCALE:  return eng->p.viz_scale;
        case PARAM_MIDI_MODE:  return (float)eng->p.midi_mode / 6.0f;
        case PARAM_MIDI_CH:    return (float)(eng->p.midi_ch - 1) / 15.0f;
        case PARAM_MASTER_VOL: return eng->p.master_vol;
        default:               return 0.0f;
    }
}

static const char* get_param_display(GranularEngine* eng, ParamId pid, int enc_idx, char* buf, int bufsz, Page page) {
    switch(pid) {
        case PARAM_POS: {
            // Compute position in seconds based on current sample length
            float sample_len_sec = (float)raw_len(eng->p.sample_idx) / (float)CONFIG_SAMPLE_RATE;
            float pos_sec = eng->p.pos * sample_len_sec;
            snprintf(buf, bufsz, "%.3fs", pos_sec);
            break;
        }
        case PARAM_SIZE:       snprintf(buf, bufsz, "%.2fs", eng->p.size); break;
        case PARAM_DENS:
            if (eng->p.dens < 10.0f) {
                snprintf(buf, bufsz, "%.1fHz", eng->p.dens);
            } else {
                snprintf(buf, bufsz, "%.0fHz", eng->p.dens);
            }
            break;
        case PARAM_PITCH:      
            if (eng->p.pitch_mode == 0) snprintf(buf, bufsz, "%.2fx", eng->p.pitch);
            else if (eng->p.pitch_mode == 1) snprintf(buf, bufsz, "%dst", (int)eng->p.pitch);
            else                             snprintf(buf, bufsz, "%doct", (int)eng->p.pitch);
            break;
        case PARAM_PITCH_MODE: {
            static const char* pmodes[] = {"SPEED", "SEMI", "OCT "};
            return pmodes[eng->p.pitch_mode % 3];
        }
        case PARAM_SAMPLE_IDX: snprintf(buf, bufsz, "#%d",   eng->p.sample_idx + 1); break;
        case PARAM_MAX_GRAINS: snprintf(buf, bufsz, "%d",    eng->p.max_grains); break;
        case PARAM_GRAIN_AMP:  snprintf(buf, bufsz, "%d%%",  (int)(eng->p.grain_amp * 100)); break;
        case PARAM_KEYTRACK:   snprintf(buf, bufsz, "%d%%",  (int)(eng->p.keytrack * 100)); break;
        case PARAM_SCAN:       snprintf(buf, bufsz, "%.2f",  eng->p.scan); break;
        case PARAM_DIRECTION:  snprintf(buf, bufsz, "%d%%",  (int)(eng->p.direction * 100)); break;
        case PARAM_SPREAD:     snprintf(buf, bufsz, "%d%%",  (int)(eng->p.spread * 100)); break;
        case PARAM_SHAPE:      snprintf(buf, bufsz, "%d",  (int)eng->p.shape + 1); break;
        case PARAM_CUTOFF:
            if (eng->p.cutoff > 999.0f) {
                snprintf(buf, bufsz, "%.2fKHZ", eng->p.cutoff / 1000.0f);
            } else {
                snprintf(buf, bufsz, "%dHZ", (int)eng->p.cutoff);
            }
            break;
        case PARAM_RES:        snprintf(buf, bufsz, "%.1f",  eng->p.res); break;
        case PARAM_FILT_TYPE:  return FILT_NAMES[eng->p.filt_type & 3];
        case PARAM_FILT_KEY:   snprintf(buf, bufsz, "%d%%",  (int)(eng->p.filt_key * 100)); break;
        case PARAM_ATK:        snprintf(buf, bufsz, "%.2fs", eng->p.atk); break;
        case PARAM_ATK_CURVE:  snprintf(buf, bufsz, "%.1f",  eng->p.atk_curve); break;
        case PARAM_REL:        snprintf(buf, bufsz, "%.2fs", eng->p.rel); break;
        case PARAM_REL_CURVE:  snprintf(buf, bufsz, "%.1f",  eng->p.rel_curve); break;
        case PARAM_LFO_RATE:   snprintf(buf, bufsz, "%.1fHz", page == PAGE_LFO2 ? eng->p.lfo2_rate : eng->p.lfo1_rate); break;
        case PARAM_LFO_WAVE:   return WAVE_NAMES[(int)(page == PAGE_LFO2 ? eng->p.lfo2_wave : eng->p.lfo1_wave) & 3];
        case PARAM_LFO_PHASE:  snprintf(buf, bufsz, "%d",   (int)(page == PAGE_LFO2 ? eng->p.lfo2_phase : eng->p.lfo1_phase)); break;
        case PARAM_LFO_SYNC: {
            static const char* smodes[] = {"V1", "V2", "V3", "V4", "ANY", "FREE"};
            float sync = page == PAGE_LFO2 ? eng->p.lfo2_sync : eng->p.lfo1_sync;
            return smodes[(int)sync % 6];
        }
        case PARAM_VOL:        snprintf(buf, bufsz, "%d%%",  (int)(eng->p.vol * 100)); break;
        case PARAM_MOD1_SRC:
        case PARAM_MOD2_SRC: {
            int r = (pid == PARAM_MOD1_SRC) ? (int)eng->p.mod1_src : (int)eng->p.mod2_src;
            if (r == 0) return "OFF";
            if (r >= 1 && r <= 12) {
                int idx = 1;
                for (int s=0; s<4; s++) {
                    for (int d=0; d<4; d++) {
                        if (s != d) {
                            if (idx == r) {
                                snprintf(buf, bufsz, "F%d>%d", s+1, d+1);
                                return buf;
                            }
                            idx++;
                        }
                    }
                }
            } else if (r >= 13 && r <= 24) {
                int idx = 13;
                for (int s=0; s<4; s++) {
                    for (int d=0; d<4; d++) {
                        if (s != d) {
                            if (idx == r) {
                                snprintf(buf, bufsz, "R%d*%d", s+1, d+1);
                                return buf;
                            }
                            idx++;
                        }
                    }
                }
            }
            return "???";
        }
        case PARAM_MOD1_AMT:
        case PARAM_MOD2_AMT:   snprintf(buf, bufsz, "%d%%",  (int)((pid==PARAM_MOD1_AMT?eng->p.mod1_amt:eng->p.mod2_amt)*100)); break;
        case PARAM_FX_WF:      snprintf(buf, bufsz, "%d%%",  (int)(eng->p.fx_wf * 100)); break;
        case PARAM_FX_DS:      snprintf(buf, bufsz, "x%d",   (int)(eng->p.fx_ds)); break;
        case PARAM_FX_BC:      snprintf(buf, bufsz, "%dbit", (int)(eng->p.fx_bc)); break;
        case PARAM_FX_MIX:     snprintf(buf, bufsz, "%d%%",  (int)(eng->p.fx_mix * 100)); break;
        case PARAM_VIZ_SCALE:  return eng->p.viz_scale == 0 ? "PITCH" : "PAN";
        case PARAM_MIDI_MODE: {
            static const char* mmodes[] = {"V1", "V2", "V3", "V4", "RR", "RND", "OCT"};
            return mmodes[eng->p.midi_mode % 7];
        }
        case PARAM_MIDI_CH:    snprintf(buf, bufsz, "%d",    eng->p.midi_ch); break;
        case PARAM_MASTER_VOL: snprintf(buf, bufsz, "%d%%",  (int)(eng->p.master_vol * 100)); break;
        default:               snprintf(buf, bufsz, "---"); break;
    }
    return buf;
}


static void draw_param_view(u8g2_t* u, const UIState* state, GranularEngine* voices) {
    GranularEngine* current_eng = &voices[state->active_voice];
    const PageLayout& layout = PAGE_LAYOUTS[state->active_page];

    int y_start = 12; // Below status bar line
#ifndef SHOW_STATUS_BAR
    y_start = 2;
#else
    draw_header(u, state, voices);
#endif

    u8g2_SetFont(u, u8g2_font_4x6_tf);

    for (int e = 0; e < 4; e++) {
        int col_x = e * 32;
        int col_w = 32;
        int box_w = 10;
        int box_x = col_x + (col_w - box_w) / 2;

        ParamId pid = layout.enc[e];
        if (pid == PARAM_NONE) {
            int tw = u8g2_GetStrWidth(u, "---");
            u8g2_DrawStr(u, col_x + (col_w - tw) / 2, y_start + 6, "---");
            continue;
        }

        GranularEngine* eng = (state->active_page == PAGE_MIX) ? &voices[e] : current_eng;
        
        // 1. Parameter Name (Centered)
        const char* name = PARAM_NAMES[(int)pid];
        if (state->active_page == PAGE_MIX) {
            static char vname[4];
            snprintf(vname, sizeof(vname), "V%d", e+1);
            name = vname;
        }
        int name_w = u8g2_GetStrWidth(u, name);
        u8g2_DrawStr(u, col_x + (col_w - name_w) / 2, y_start + 6, name);

        // 2. Status Indicator (Vertical fill box or Graphical Icon)
        int y_box = y_start + 8;
        int h_box = 18;
        
#ifdef SHOW_ICONS
        bool is_custom_icon = (pid == PARAM_LFO_WAVE || pid == PARAM_SHAPE || pid == PARAM_ATK_CURVE || pid == PARAM_REL_CURVE ||
                               (state->active_page == PAGE_FILT && (pid == PARAM_CUTOFF || pid == PARAM_RES || pid == PARAM_FILT_TYPE)));
        if (is_custom_icon) {
            int icon_w = 28;
            int icon_x = col_x + (col_w - icon_w) / 2;
            
            if (state->active_page == PAGE_FILT && (pid == PARAM_CUTOFF || pid == PARAM_RES || pid == PARAM_FILT_TYPE)) {
                if (e == 0) {
                    // Draw unified filter graph spanning columns 0, 1, 2
                    u8g2_DrawFrame(u, 2, y_box, 92, h_box);
                    
                    int filt_type = eng->p.filt_type;
                    float xc_f = 6.0f + get_param_ratio(eng, PARAM_CUTOFF, 0, PAGE_FILT) * 80.0f;
                    float Q = get_param_ratio(eng, PARAM_RES, 0, PAGE_FILT);
                    int prev_y = -1;
                    for (int i = 0; i < 92; i++) {
                        float dx = (float)i - xc_f;
                        float val = 0.0f;
                        if (filt_type == 0) { // LP
                            if (dx < 0.0f) {
                                val = 6.0f + Q * 6.0f * expf(dx / 8.0f);
                            } else {
                                val = (6.0f + Q * 6.0f) * expf(-dx / (5.0f + (1.0f - Q) * 8.0f));
                            }
                        } else if (filt_type == 1) { // HP
                            if (dx > 0.0f) {
                                val = 6.0f + Q * 6.0f * expf(-dx / 8.0f);
                            } else {
                                val = (6.0f + Q * 6.0f) * expf(dx / (5.0f + (1.0f - Q) * 8.0f));
                            }
                        } else if (filt_type == 2) { // BP
                            val = (6.0f + Q * 6.0f) * expf(-fabsf(dx) / (4.0f + (1.0f - Q) * 10.0f));
                        } else { // OFF
                            val = 6.0f;
                        }
                        if (val > 15.0f) val = 15.0f;
                        int ly = y_box + h_box - 2 - (int)val;
                        int lx = 2 + i;
                        if (prev_y != -1) {
                            u8g2_DrawLine(u, lx - 1, prev_y, lx, ly);
                        }
                        prev_y = ly;
                    }
                }
            } else {
                // Draw single column icon frame
                u8g2_DrawFrame(u, icon_x, y_box, icon_w, h_box);
                
                if (pid == PARAM_LFO_WAVE) {
                    float wave = (state->active_page == PAGE_LFO2) ? eng->p.lfo2_wave : eng->p.lfo1_wave;
                    int wave_idx = (int)wave;
                    if (wave_idx < 0) wave_idx = 0;
                    if (wave_idx > 4) wave_idx = 4;
                    
                    const uint8_t* lut_ptr = nullptr;
                    if (wave_idx == 0)      lut_ptr = LFO_SINE_Y;
                    else if (wave_idx == 1) lut_ptr = LFO_TRI_Y;
                    else if (wave_idx == 2) lut_ptr = LFO_SAW_Y;
                    else if (wave_idx == 3) lut_ptr = LFO_RSAW_Y;
                    else                    lut_ptr = LFO_SH_Y;

                    int prev_y = -1;
                    for (int i = 0; i < 24; i++) {
                        int ly = y_box + lut_ptr[i];
                        int lx = icon_x + 2 + i;
                        if (prev_y != -1) {
                            u8g2_DrawLine(u, lx - 1, prev_y, lx, ly);
                        }
                        prev_y = ly;
                    }
                } else if (pid == PARAM_SHAPE) {
                    // Draw Morphing Grain Envelope from cached shape_icons_lut
                    float shape_val = eng->p.shape;
                    if (shape_val < 0.0f) shape_val = 0.0f;
                    if (shape_val > 27.0f) shape_val = 27.0f;
                    
                    int idx_low = (int)floorf(shape_val);
                    int idx_high = (int)ceilf(shape_val);
                    if (idx_high > 27) idx_high = 27;
                    float t = shape_val - (float)idx_low;

                    int prev_y = -1;
                    for (int i = 0; i < 24; i++) {
                        float val = (1.0f - t) * (float)shape_icons_lut[idx_low][i] + t * (float)shape_icons_lut[idx_high][i];
                        int ly = y_box + h_box - 3 - (int)lroundf(val);
                        int lx = icon_x + 2 + i;
                        if (prev_y != -1) {
                            u8g2_DrawLine(u, lx - 1, prev_y, lx, ly);
                        }
                        prev_y = ly;
                    }
                } else if (pid == PARAM_ATK_CURVE || pid == PARAM_REL_CURVE) {
                    float curve_val = (pid == PARAM_ATK_CURVE) ? eng->p.atk_curve : eng->p.rel_curve;
                    if (curve_val < -1.0f) curve_val = -1.0f;
                    if (curve_val > 1.0f) curve_val = 1.0f;
                    
                    int prev_y = -1;
                    for (int i = 0; i < 24; i++) {
                        float t = (float)i / 23.0f;
                        if (pid == PARAM_REL_CURVE) {
                            t = 1.0f - t;
                        }
                        float y_norm = 0.0f;
                        if (curve_val >= 0.0f) {
                            y_norm = (1.0f - curve_val) * t + curve_val * t * t;
                        } else {
                            y_norm = (1.0f + curve_val) * t - curve_val * (1.0f - (1.0f - t) * (1.0f - t));
                        }
                        int ly = y_box + h_box - 3 - (int)(y_norm * 12.0f);
                        int lx = icon_x + 2 + i;
                        if (prev_y != -1) {
                            u8g2_DrawLine(u, lx - 1, prev_y, lx, ly);
                        }
                        prev_y = ly;
                    }
                }
            }
        } else {
#endif
            float ratio = get_param_ratio(eng, pid, e, state->active_page);
            int fill_h = (int)(ratio * (h_box - 2));
            u8g2_DrawFrame(u, box_x, y_box, box_w, h_box);
            if (fill_h > 0) {
                u8g2_DrawBox(u, box_x + 1, y_box + h_box - 1 - fill_h, box_w - 2, fill_h);
            }
#ifdef SHOW_ICONS
        }
#endif
        
        // 3. Parameter Value Text (Centered)
        char vbuf[12];
        const char* disp = get_param_display(eng, pid, e, vbuf, sizeof(vbuf), state->active_page);
        int val_w = u8g2_GetStrWidth(u, disp);
        u8g2_DrawStr(u, col_x + (col_w - val_w) / 2, y_box + h_box + 7, disp);

        // 4. Modulation Bar (Horizontal, centered, width 30)
        int y_mod = y_box + h_box + 10;
        int w_mod = 30;
        int h_mod = 4;
        int mod_x = col_x + (col_w - w_mod) / 2;
        uint8_t src_idx = eng->mod_src[(int)pid];
        u8g2_DrawFrame(u, mod_x, y_mod, w_mod, h_mod);
        
        // Determine modulation amount for display
        float mod_amt = (float)eng->mod_amt[(int)pid];

        // Default label
        const char* label = "---";

        if (src_idx == 4) {
            // EG source – map 0..1 to signed where 0 => +1 (EG+), 1 => -1 (EG-)
            float signed_mod = (mod_amt - 0.5f) * 2.0f;
            // Choose label based on sign
            label = (signed_mod >= 0.0f) ? "EG+" : "EG-";

            // Draw bar with inverted direction for EG
            if (signed_mod > 0.01f) {
                // EG+: fill from left edge moving right
                int fill_w = (int)(signed_mod * (w_mod - 2));
                if (fill_w < 1) fill_w = 1;
                if (fill_w > w_mod - 2) fill_w = w_mod - 2;
                u8g2_DrawBox(u, mod_x + 1, y_mod + 1, fill_w, h_mod - 2);
            } else if (signed_mod < -0.01f) {
                // EG-: fill from right edge moving left
                int fill_w = (int)(-signed_mod * (w_mod - 2));
                if (fill_w < 1) fill_w = 1;
                if (fill_w > w_mod - 2) fill_w = w_mod - 2;
                int right_edge = mod_x + w_mod - 1;
                u8g2_DrawBox(u, right_edge - fill_w, y_mod + 1, fill_w, h_mod - 2);
            }
        } else {
            // Other sources: unipolar bar left‑to‑right
            if (mod_amt > 0.01f) {
                int fill_w = (int)(mod_amt * (w_mod - 2));
                if (fill_w < 1) fill_w = 1;
                if (fill_w > w_mod - 2) fill_w = w_mod - 2;
                u8g2_DrawBox(u, mod_x + 1, y_mod + 1, fill_w, h_mod - 2);
            }
            // Labels for non‑EG sources
            if (src_idx == 0) label = "---";
            else if (src_idx == 1) label = "JIT";
            else if (src_idx == 2) label = "LF1";
            else if (src_idx == 3) label = "LF2";
            else label = "---";
        }

        // Render label
        int src_w = u8g2_GetStrWidth(u, label);
        u8g2_DrawStr(u, col_x + (col_w - src_w) / 2, y_mod + h_mod + 7, label);
    }
}

// ------------------------------------------------------------------ //
//  MAIN RENDER ENTRY
// ------------------------------------------------------------------ //
void oled_render(u8g2_t* u8g2, const UIState* state, GranularEngine* voices, int num_voices) {
    // Check if any voice is triggered
    bool any_trig = false;
    for (int v = 0; v < NUM_VOICES; v++) {
        if (state->voices_triggered[v]) { any_trig = true; break; }
    }

    // Mode switching logic:
    if (any_trig) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        // Auto-return to GRAIN view after 5s of idle if playing
        if (g_mode == DISP_PARAM && (now - g_last_activity_ms) > GRAIN_VIEW_RETURN_MS) {
            g_mode = DISP_GRAIN;
        }
    }

    u8g2_ClearBuffer(u8g2);

    if (g_mode == DISP_GRAIN) {
        draw_grain_view(u8g2, state, voices);
    } else {
        draw_param_view(u8g2, state, voices);
    }

    u8g2_SendBuffer(u8g2);
}
