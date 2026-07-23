#ifndef SHARED_COLORS_H
#define SHARED_COLORS_H

#include <stdint.h>

/* Unified RGB colors for identical modules/pages across firmwares */

/* VCO / Grains
 * - omsk_synth: VCO1, VCO2, NOISE (default parameter indicator)
 * - omsk_grain: PAGE_GRAIN1, PAGE_GRAIN2, PAGE_GRAIN3
 */
#define COLOR_VCO_R 0
#define COLOR_VCO_G 255
#define COLOR_VCO_B 0

/* Filters (VCF)
 * - omsk_synth: MOD_VCF1, MOD_VCF2
 * - omsk_grain: PAGE_FILT
 */
#define COLOR_FILTER_R 255
#define COLOR_FILTER_G 0
#define COLOR_FILTER_B 128

/* LFO
 * - omsk_synth: MOD_LFO1, MOD_LFO2
 * - omsk_grain: PAGE_LFO1, PAGE_LFO2
 */
#define COLOR_LFO_R 0
#define COLOR_LFO_G 191
#define COLOR_LFO_B 255

/* Envelope Generator (EG)
 * - omsk_synth: MOD_EG1, MOD_EG2
 * - omsk_grain: PAGE_EG
 */
#define COLOR_EG_R 255
#define COLOR_EG_G 127
#define COLOR_EG_B 0

/* Mixer
 * - omsk_synth: MOD_MIXER
 * - omsk_grain: PAGE_MIX
 */
#define COLOR_MIXER_R 0
#define COLOR_MIXER_G 0
#define COLOR_MIXER_B 255

/* Modulation (MOD)
 * - omsk_synth: MOD_MOD
 * - omsk_grain: PAGE_MOD
 */
#define COLOR_MOD_R 180
#define COLOR_MOD_G 0
#define COLOR_MOD_B 255

/* Effects (FX)
 * - omsk_synth: MOD_FX1, MOD_FX2
 * - omsk_grain: PAGE_FX
 */
#define COLOR_FX_R 255
#define COLOR_FX_G 215
#define COLOR_FX_B 0

/* Glide
 * - omsk_synth: MOD_GLIDE
 * - omsk_grain: Not used
 */
#define COLOR_GLIDE_R 0
#define COLOR_GLIDE_G 255
#define COLOR_GLIDE_B 0

/* Arpeggiator / Jitter
 * - omsk_synth: MOD_ARP
 * - omsk_grain: PAGE_JIT
 */
#define COLOR_ARP_JIT_R 173
#define COLOR_ARP_JIT_G 255
#define COLOR_ARP_JIT_B 47

/* Settings Mode (SET)
 * - omsk_synth: MOD_SET
 * - omsk_grain: Not used
 */
#define COLOR_SET_R 255
#define COLOR_SET_G 0
#define COLOR_SET_B 0

/* Function Key (FN)
 * - omsk_synth: MOD_FN
 * - omsk_grain: Not used
 */
#define COLOR_FN_R 255
#define COLOR_FN_G 80
#define COLOR_FN_B 0

/* System Mode / Settings
 * - omsk_synth: Default fallback for unrouted VCO/NOISE
 * - omsk_grain: PAGE_SYS
 */
#define COLOR_SYS_R 255
#define COLOR_SYS_G 255
#define COLOR_SYS_B 255

/* Sequencer General Colors */
#define COLOR_SEQ_PLAY_R 0
#define COLOR_SEQ_PLAY_G 255
#define COLOR_SEQ_PLAY_B 0

#define COLOR_SEQ_STOP_R 255
#define COLOR_SEQ_STOP_G 0
#define COLOR_SEQ_STOP_B 0

/* Sequencer Navigation Pages */
#define COLOR_SEQ_PAGE0_R 255
#define COLOR_SEQ_PAGE0_G 0
#define COLOR_SEQ_PAGE0_B 0

#define COLOR_SEQ_PAGE1_R 0
#define COLOR_SEQ_PAGE1_G 255
#define COLOR_SEQ_PAGE1_B 0

#define COLOR_SEQ_PAGE2_R 0
#define COLOR_SEQ_PAGE2_G 0
#define COLOR_SEQ_PAGE2_B 255

#define COLOR_SEQ_PAGE3_R 128
#define COLOR_SEQ_PAGE3_G 0
#define COLOR_SEQ_PAGE3_B 128

/* Sequencer Step States */
#define COLOR_SEQ_STEP_STOP_R 255
#define COLOR_SEQ_STEP_STOP_G 0
#define COLOR_SEQ_STEP_STOP_B 0

#define COLOR_SEQ_STEP_PLAYHEAD_R 255
#define COLOR_SEQ_STEP_PLAYHEAD_G 255
#define COLOR_SEQ_STEP_PLAYHEAD_B 255

/* Piano Keyboard Colors */
#define COLOR_PIANO_ACTIVE_R 180
#define COLOR_PIANO_ACTIVE_G 180
#define COLOR_PIANO_ACTIVE_B 180

#define COLOR_PIANO_INACTIVE_R 30
#define COLOR_PIANO_INACTIVE_G 30
#define COLOR_PIANO_INACTIVE_B 30

#define COLOR_PIANO_BLACK_ACTIVE_R 0
#define COLOR_PIANO_BLACK_ACTIVE_G 0
#define COLOR_PIANO_BLACK_ACTIVE_B 180

#define COLOR_PIANO_BLACK_INACTIVE_R 0
#define COLOR_PIANO_BLACK_INACTIVE_G 0
#define COLOR_PIANO_BLACK_INACTIVE_B 30

/* UI Presets Mode Overlay */
#define COLOR_PRESET_BLINK_R 150
#define COLOR_PRESET_BLINK_G 150
#define COLOR_PRESET_BLINK_B 0

/* Helper to get cold-to-hot color gradient for encoders/parameters (v = 0.0f to 1.0f) */
static inline void get_cold_hot_color(float v, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (v < 0.5f) {
        float t = v * 2.0f;
        *r = (uint8_t)(t * 128.0f);
        *g = 0;
        *b = (uint8_t)(255.0f - t * 127.0f);
    } else {
        float t = (v - 0.5f) * 2.0f;
        *r = (uint8_t)(128.0f + t * 127.0f);
        *g = 0;
        *b = (uint8_t)(128.0f - t * 128.0f);
    }
}

#endif /* SHARED_COLORS_H */
