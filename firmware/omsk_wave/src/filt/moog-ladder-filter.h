#pragma once

#include "../sw_config.h"
#include "../synth/pra32-u2-common.h"
#include "../tables/moog-filter-table.h"

class MoogLadderFilter {
  int32_t m_y1, m_y2, m_y3, m_y4;
  uint16_t m_f_q15;
  uint32_t m_k_q15;
  uint8_t m_mix;

public:
  MoogLadderFilter() : m_y1(0), m_y2(0), m_y3(0), m_y4(0), m_f_q15(0), m_k_q15(0), m_mix(127) {}

  INLINE void set_f(uint16_t f_q15) { m_f_q15 = f_q15; }

  INLINE void set_resonance(uint8_t resonance_127) {
    if (resonance_127 > 127) resonance_127 = 127;
    m_k_q15 = g_moog_res_table[resonance_127];
  }

  INLINE void set_mix(uint8_t mix_127) { m_mix = mix_127; }

  INLINE int32_t process(int32_t x0) {
    if (m_mix == 0) return x0;

    // Gain compensation (Moog style)
    // x = x0 * (1 + 0.5 * k)
    // 0.5 * k in Q15 is m_k_q15 >> 1
    int32_t compensated_x = x0 + multiply_shift_right(x0, m_k_q15 >> 1, 15);

    // Feedback
    // x = compensated_x - k * y4
    int32_t x = compensated_x - multiply_shift_right(m_k_q15, m_y4, 15);

    // 4 stages of one-pole filters
    // y = y + f * (x - y)
    m_y1 += multiply_shift_right(m_f_q15, x - m_y1, 15);
    m_y2 += multiply_shift_right(m_f_q15, m_y1 - m_y2, 15);
    m_y3 += multiply_shift_right(m_f_q15, m_y2 - m_y3, 15);
    m_y4 += multiply_shift_right(m_f_q15, m_y3 - m_y4, 15);

    // m_y4 is the LPF output
    if (m_mix >= 127) return m_y4;

    // Dry/Wet Mix
    // result = x0 + (y4 - x0) * mix / 127
    return x0 + ((m_y4 - x0) * m_mix / 127);
  }
};
