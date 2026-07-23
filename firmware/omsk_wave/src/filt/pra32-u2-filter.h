#pragma once
#include <stdint.h>
#include "../sw_config.h"
#include "../synth/pra32-u2-common.h"
#if CFG_FILTER_MODE == FILTER_MODE_PRA32_TABLES
#include "../tables/pra32-u2-filter-table.h"
#endif
#include "moog-ladder-filter.h"
#include "../tables/vcf_lut_data.h"

static INLINE int32_t soft_clip(int32_t value) {
  int32_t one = (1 << 23);
  int32_t two_three = one * 2 / 3;
  volatile int32_t clamped =
      (value > (+one)) * (+two_three) + (value < (-one)) * (-two_three) +
      ((value <= (+one)) && (value >= (-one))) *
          (value - (multiply_shift_right(
                        multiply_shift_right(value << 5, value << 4, 32) << 5,
                        value << 4, 32) /
                    3));
  return clamped;
}

class PRA32_U2_Filter {
  int32_t m_b_0_over_a_0;
  int32_t m_b_1_over_a_0;
  int32_t m_b_2_over_a_0;
  int32_t m_a_1_over_a_0;
  int32_t m_a_2_over_a_0;
  int32_t m_z_1;
  int32_t m_z_2;
  uint8_t m_resonance_target;
  uint8_t m_resonance_current;
  int16_t m_cutoff_current;
  int16_t m_cutoff_control;
  int16_t m_mix;
  uint8_t m_instance_id; 
  uint16_t m_osc_pitch;
  int16_t m_eg_input;
  int16_t m_lfo_input;
  uint8_t m_filter_type; // 0=LPF, 1=BPF, 2=HPF

public:
  PRA32_U2_Filter()
      : m_b_0_over_a_0(0), m_b_1_over_a_0(0), m_b_2_over_a_0(0), m_a_1_over_a_0(0), m_a_2_over_a_0(0), m_z_1(0), m_z_2(0),
        m_resonance_target(0), m_resonance_current(0), m_cutoff_current(254),
        m_cutoff_control(127), m_mix(127), m_instance_id(1), m_osc_pitch(60 << 8),
        m_eg_input(0), m_lfo_input(0), m_filter_type(0) {
    update_coefs();
  }

  INLINE void set_cutoff(uint8_t controller_value) { m_cutoff_control = controller_value; }
  INLINE void set_resonance(uint8_t controller_value) { m_resonance_target = controller_value; }
  INLINE void set_mix(uint8_t controller_value) { m_mix = controller_value; }
  INLINE void set_instance_id(uint8_t id) { m_instance_id = id; }
  INLINE void set_filter_mode(uint8_t type) { m_filter_type = type; }
  INLINE void reset_state() { m_z_1 = 0; m_z_2 = 0; }

  INLINE void process_at_low_rate(uint8_t count, int16_t eg_input, int16_t lfo_input, uint16_t osc_pitch) {
    (void)count;
    m_eg_input = eg_input;
    m_lfo_input = lfo_input;
    m_osc_pitch = osc_pitch;
    update_coefs();
  }

  INLINE int32_t process(int32_t audio_input_int24) {
    int32_t x_0 = audio_input_int24;
    int32_t y_0 = m_z_1 + (multiply_shift_right(m_b_0_over_a_0, x_0, 32) << (32 - FILTER_TABLE_FRACTION_BITS));
    m_z_1 = soft_clip(m_z_2 + (multiply_shift_right(m_b_1_over_a_0, x_0, 32) << (32 - FILTER_TABLE_FRACTION_BITS)) -
                      (multiply_shift_right(m_a_1_over_a_0, y_0, 32) << (32 - FILTER_TABLE_FRACTION_BITS)));
    m_z_2 = soft_clip((multiply_shift_right(m_b_2_over_a_0, x_0, 32) << (32 - FILTER_TABLE_FRACTION_BITS)) -
                      (multiply_shift_right(m_a_2_over_a_0, y_0, 32) << (32 - FILTER_TABLE_FRACTION_BITS)));
    return y_0;
  }

private:
  INLINE void update_coefs() {
    int16_t cutoff_candidate = m_cutoff_control << 3; // Shift to match scale
    // Simple modulation for now, scale if needed
    int16_t cutoff_target = clamp(cutoff_candidate, 0, 1023);

    for (uint32_t i = 0; i < 8; ++i) {
      if (m_cutoff_current < cutoff_target) m_cutoff_current++;
      else if (m_cutoff_current > cutoff_target) m_cutoff_current--;
    }
    if (m_resonance_current < m_resonance_target) m_resonance_current++;
    else if (m_resonance_current > m_resonance_target) m_resonance_current--;

    uint8_t res_idx = m_resonance_current >> 2;
    if (res_idx > 31) res_idx = 31;
    size_t cut_idx = m_cutoff_current >> 3; // 1024 / 128 steps = 8
    if (cut_idx > 127) cut_idx = 127;

#if CFG_FILTER_MODE == FILTER_MODE_3_TABLES
    const int32_t (*table)[128][5];
    if (m_filter_type == 1) table = g_vcf_bpf_lut;
    else if (m_filter_type == 2) table = g_vcf_hpf_lut;
    else if (m_filter_type == 3) table = g_vcf_bsf_lut;
    else if (m_filter_type == 4) table = g_vcf_apf_lut;
    else table = g_vcf_lpf_lut;
    
    const int32_t *coefs = table[res_idx][cut_idx];
    
    m_b_0_over_a_0 = coefs[0];
    m_b_1_over_a_0 = coefs[1];
    m_b_2_over_a_0 = coefs[2];
    m_a_1_over_a_0 = coefs[3];
    m_a_2_over_a_0 = coefs[4];
#else
    const int32_t *filter_table = g_filter_tables[res_idx];
    size_t final_idx = (m_cutoff_current >> 1) * 3;
    int32_t b2_lpf = filter_table[final_idx + 0];
    m_a_1_over_a_0 = filter_table[final_idx + 1];
    m_a_2_over_a_0 = filter_table[final_idx + 2];

    if (m_filter_type == 1) { // BPF (Constant 0 dB peak gain)
      m_b_0_over_a_0 = ((1 << 30) - m_a_2_over_a_0) >> 1;
      m_b_1_over_a_0 = 0;
      m_b_2_over_a_0 = -m_b_0_over_a_0;
    } else if (m_filter_type == 2) { // HPF
      m_b_0_over_a_0 = ((1 << 30) + m_a_2_over_a_0 - m_a_1_over_a_0) >> 2;
      m_b_1_over_a_0 = -2 * m_b_0_over_a_0;
      m_b_2_over_a_0 = m_b_0_over_a_0;
    } else { // LPF
      m_b_0_over_a_0 = b2_lpf;
      m_b_1_over_a_0 = b2_lpf << 1;
      m_b_2_over_a_0 = b2_lpf;
    }
#endif
  }
};
