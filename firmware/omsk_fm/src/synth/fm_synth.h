#ifndef FM_SYNTH_H
#define FM_SYNTH_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define FM_SYNTH_VOICE_COUNT 8
#define FM_SYNTH_OPERATOR_COUNT 6

typedef struct {
  uint8_t rates[4];
  uint8_t levels[4];
  uint8_t break_point;
  uint8_t left_depth;
  uint8_t right_depth;
  uint8_t left_curve;
  uint8_t right_curve;
  uint8_t rate_scaling;
  uint8_t key_velocity_sensitivity;
  uint8_t amp_mod_sens;
  uint8_t output_level;
  uint8_t ratio_num;
  uint8_t ratio_den;
  float ratio;
  uint8_t freq_coarse;
  uint8_t freq_fine;
  int8_t detune;
  bool fixed_freq;
  bool carrier;
  bool active; // Custom operator bypass (Double tap toggle)
} FmOperatorPatch;

typedef struct {
  uint8_t algorithm;
  uint8_t feedback;
  int8_t transpose;
  uint8_t lfo_speed;
  uint8_t lfo_delay;
  uint8_t lfo_pitch_mod_depth;
  uint8_t lfo_amp_mod_depth;
  uint8_t lfo_sync;
  uint8_t lfo_waveform;
  uint8_t pitch_mod_sensitivity;
  uint8_t pitch_eg_rates[4];
  uint8_t pitch_eg_levels[4];
  char name[11];
  FmOperatorPatch op[FM_SYNTH_OPERATOR_COUNT];
} FmPatch;

typedef struct {
  uint32_t voice_steal_count;
  uint32_t same_note_retrigger_count;
  uint32_t output_clip_count;
  uint8_t active_voice_count;
  uint8_t peak_active_voice_count;
} FmSynthStats;

extern FmPatch g_active_patch;
extern FmPatch g_presets[32];

void fm_synth_init(void);
void fm_synth_set_master_level(uint8_t level);
uint8_t fm_synth_master_level(void);
void fm_synth_set_voice_level(uint8_t level);
uint8_t fm_synth_voice_level(void);
void fm_synth_set_patch(uint8_t patch_id);
void fm_synth_note_on(uint8_t note, uint8_t velocity);
void fm_synth_note_off(uint8_t note);
void fm_synth_all_notes_off(void);
void fm_synth_panic(void);
void fm_synth_render_block(int16_t *buffer, size_t samples);
void fm_synth_set_pitch_bend(uint16_t pb);
void fm_synth_get_stats(FmSynthStats *stats);

// Converts 128 bytes packed DX7 format to FmPatch
void dx7_unpack_voice(const uint8_t *dx7_128_bytes, FmPatch *patch);
// Converts 156 bytes unpacked DX7 format to FmPatch
void dx7_unpack_unpacked_voice(const uint8_t *dx7_unpacked_bytes, FmPatch *patch);
// Packs FmPatch to 128 bytes DX7 format
void dx7_pack_voice(const FmPatch *patch, uint8_t *dx7_128_bytes);
extern bool g_portamento_enable;
extern uint8_t g_portamento_time;
extern uint8_t g_mod_wheel_val;
// Checks if an operator has feedback output in a DX7 algorithm
bool fm_synth_algo_has_feedback(int algo, int op);

#endif // FM_SYNTH_H
