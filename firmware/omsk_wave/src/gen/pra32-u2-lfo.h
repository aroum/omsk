#pragma once

#include "../sw_config.h"
#include "../synth/pra32-u2-common.h"

#if CFG_LFO_MODE == LFO_MODE_TABLE
#include "../tables/omsk_custom_wave_table.h"
#include "../tables/pra32-u2-lfo-table.h"
#include "../tables/pra32-u2-osc-table.h"

#else
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#endif

class PRA32_U2_LFO {
  static const uint8_t LFO_WAVEFORM_TRIANGLE = 0;

  static const uint8_t LFO_WAVEFORM_SINE = 1;
  static const uint8_t LFO_WAVEFORM_SAW_DOWN = 2;
  static const uint8_t LFO_WAVEFORM_RANDOM = 3;
  static const uint8_t LFO_WAVEFORM_SQUARE = 4;
  static const uint8_t LFO_WAVEFORM_RED_NOISE = 5;

  static const uint8_t LFO_FADE_COEF_OFF = 1;

  static const uint8_t LFO_FADE_LEVEL_MAX = 128;

  uint32_t m_lfo_phase;
  int16_t m_lfo_wave_level;
  uint32_t m_lfo_rate;
  uint8_t m_lfo_depth[2];
  uint8_t m_lfo_waveform;
  uint16_t m_lfo_fade_coef;
  uint16_t m_lfo_fade_cnt;
  uint8_t m_lfo_fade_level;
  int16_t m_noise_int15;
  int16_t m_prev_noise_int15;
  int16_t m_sampled_noise_int15;
  uint8_t m_pressure_amt;
  uint8_t m_pressure[4];

public:
  PRA32_U2_LFO()
      : m_lfo_phase(), m_lfo_wave_level(), m_lfo_rate(), m_lfo_depth(),
        m_lfo_waveform(), m_lfo_fade_coef(), m_lfo_fade_cnt(),
        m_lfo_fade_level(), m_noise_int15(), m_prev_noise_int15(),
        m_sampled_noise_int15(), m_pressure_amt(), m_pressure() {
    m_lfo_waveform = LFO_WAVEFORM_TRIANGLE;
    m_lfo_fade_coef = LFO_FADE_COEF_OFF;
    m_lfo_fade_cnt = m_lfo_fade_coef;
    m_lfo_fade_level = LFO_FADE_LEVEL_MAX;
    m_noise_int15 = 0;
    m_prev_noise_int15 = m_noise_int15;
    m_sampled_noise_int15 = m_noise_int15;
  }

  INLINE void set_lfo_waveform(uint8_t controller_value) {
    m_lfo_waveform = controller_value;
  }

  INLINE void set_lfo_rate(uint8_t controller_value) {
#if CFG_LFO_MODE == LFO_MODE_TABLE
    m_lfo_rate = g_lfo_rate_table[controller_value];
#else
    if (controller_value == 0) {
      m_lfo_rate = 0;
    } else {
      // Formula: rate = 2^((val-64)/12) * Base * 2^24 / Fs_control
      float base_freq = A4_FREQ * powf(2.0f, -88.0f / 12.0f); // approx 0.006 Hz
      float freq =
          base_freq * powf(2.0f, (float)(controller_value - 64) / 12.0f);
      m_lfo_rate = (uint32_t)(freq * 16777216.0f / (SAMPLING_RATE / 4.0f));
    }
#endif
  }

  INLINE void set_lfo_rate_direct(uint32_t rate) { m_lfo_rate = rate; }

  template <uint8_t N> INLINE void set_lfo_depth(uint8_t controller_value) {
    if (controller_value == 127) {
      controller_value = 128;
    }

    m_lfo_depth[N] = controller_value;
  }

  INLINE void set_lfo_fade_time(uint8_t controller_value) {
#if CFG_LFO_MODE == LFO_MODE_TABLE
    m_lfo_fade_coef = g_lfo_fade_coef_table[controller_value];
#else
    if (controller_value == 0) {
      m_lfo_fade_coef = 1;
    } else {
      float coef = 10.0f *
                   powf(10.0f, ((float)controller_value - 128.0f) / 64.0f) *
                   (SAMPLING_RATE / 4.0f) / 128.0f;
      m_lfo_fade_coef = (uint16_t)coef;
    }
#endif
  }

  INLINE void set_pressure_amt(uint8_t controller_value) {
    if (controller_value == 127) {
      controller_value = 128;
    }

    m_pressure_amt = controller_value;
  }

  template <uint8_t N> INLINE void set_pressure(uint8_t pressure) {
    if (pressure == 127) {
      pressure = 128;
    }

    m_pressure[N] = pressure;
  }

  INLINE void trigger_lfo() {
    if ((m_lfo_waveform == LFO_WAVEFORM_SAW_DOWN) ||
        (m_lfo_waveform == LFO_WAVEFORM_RANDOM) ||
        (m_lfo_waveform == LFO_WAVEFORM_SQUARE)) {
      m_lfo_phase = 0x00000000;
      m_sampled_noise_int15 = m_noise_int15;
    }

    if (m_lfo_fade_coef > LFO_FADE_COEF_OFF) {
      m_lfo_fade_level = 0;
    }
  }

  template <uint8_t N> INLINE int16_t get_output() {
    uint8_t lfo_depth = high_byte((m_lfo_depth[0] << 1) * m_lfo_fade_level) +
                        m_lfo_depth[1] +
                        ((m_pressure_amt * m_pressure[N]) >> 7);
    if (lfo_depth > 128) {
      lfo_depth = 128;
    }

    int16_t lfo_level = (lfo_depth * m_lfo_wave_level) >> 7;

    return lfo_level;
  }

  INLINE void process_at_low_rate(uint8_t count, int16_t noise_int15) {
    static_cast<void>(count);

    m_prev_noise_int15 = m_noise_int15;
    m_noise_int15 = noise_int15;
    update_lfo_wave_level();
  }

private:
  INLINE int16_t get_lfo_wave_level(uint32_t phase) {
#if CFG_LFO_MODE == LFO_MODE_TABLE
    // Morphing LFO using g_lfo_custom_wave_tables (SIN, SAW, TRI, RSAW, SQR,
    // PAM) m_lfo_waveform (0-127) maps to 0..5 float

    float t = m_lfo_waveform / 127.0f;
    if (t > 1.0f)
      t = 1.0f;
    float seg_pos = t * 5.0f;
    int seg = (int)seg_pos;
    if (seg > 4)
      seg = 4;
    float frac = seg_pos - (float)seg;

    int type0 = seg;
    int type1 = seg + 1;

    // Table lookup
    // g_lfo_custom_wave_tables[] contains pointers to 512-sample tables
    // phase is 32-bit.
    // Index = phase >> (32 - 9) = phase >> 23

    uint32_t idx = phase >> (32 - 9);

    const int16_t *t0 = g_lfo_custom_wave_tables[type0];
    const int16_t *t1 = g_lfo_custom_wave_tables[type1];

    int16_t v0 = t0[idx];
    int16_t v1 = t1[idx];

    // Morph interpolation
    float val = (float)v0 * (1.0f - frac) + (float)v1 * frac;

    return (int16_t)val;

#else
    // Legacy / Calc mode (if needed, but user wants morphing)
    // We can implement morphing via calculation too, but for now fallback to
    // Sine or implement the full calc logic
    return 0;
#endif
  }

  INLINE void update_lfo_wave_level() {
    --m_lfo_fade_cnt;
    if (m_lfo_fade_cnt == 0) {
      m_lfo_fade_cnt = m_lfo_fade_coef;
      if (m_lfo_fade_level < LFO_FADE_LEVEL_MAX) {
        m_lfo_fade_level += 1;
      }
    }

    m_lfo_phase += m_lfo_rate;
    m_lfo_phase &= 0x00FFFFFF;
    m_lfo_wave_level = get_lfo_wave_level(m_lfo_phase);
  }
};
