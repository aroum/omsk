#pragma once

#include "../synth/pra32-u2-common.h"

class PRA32_U2_ChorusFx {
  static const uint32_t DELAY_BUFF_SIZE = 4096;

  int16_t m_delay_buff[2][DELAY_BUFF_SIZE];
  uint32_t m_delay_wp[2];

  uint16_t m_chorus_level_control;
  uint16_t m_chorus_level_control_effective;
  uint32_t m_chorus_depth_control;
  uint32_t m_chorus_depth_control_effective;
  uint32_t m_chorus_rate_control;
  uint32_t m_chorus_delay_time_control;
  uint32_t m_chorus_delay_time_control_effective;
  uint32_t m_chorus_depth_control_actual;
  uint32_t m_chorus_lfo_phase;
  uint8_t m_chorus_spread;
  uint8_t m_chorus_feedback;
  uint8_t m_chorus_feedback_effective;
  uint32_t m_chorus_delay_time[2];

public:
  PRA32_U2_ChorusFx()
      : m_delay_buff(), m_delay_wp()

        ,
        m_chorus_level_control(), m_chorus_level_control_effective(),
        m_chorus_depth_control(), m_chorus_depth_control_effective(),
        m_chorus_rate_control(), m_chorus_delay_time_control(),
        m_chorus_delay_time_control_effective(), m_chorus_lfo_phase(),
        m_chorus_spread(64), m_chorus_feedback(0),
        m_chorus_feedback_effective(0), m_chorus_delay_time() {
    m_delay_wp[0] = DELAY_BUFF_SIZE - 1;
    m_delay_wp[1] = DELAY_BUFF_SIZE - 1;

    set_chorus_depth(64);
    set_chorus_rate_fixed(0.6f); // Default rate ~0.6Hz
    set_chorus_delay_time(64);
    set_chorus_spread(64);
    set_chorus_feedback(0);

    m_chorus_depth_control_effective = 64 * 15360 / 127;
    m_chorus_delay_time_control_effective = 64 * 15360 / 127;
  }

  INLINE void set_chorus_spread(uint8_t controller_value) {
    m_chorus_spread = controller_value;
  }

  INLINE void set_chorus_feedback(uint8_t controller_value) {
    m_chorus_feedback = controller_value;
  }

  INLINE void set_chorus_depth(uint8_t controller_value) {
    m_chorus_depth_control = (uint32_t)controller_value * 15360 / 127;
  }

  INLINE void set_chorus_rate_fixed(float hz) {
    // 0x01000000 = Sample Rate (48000Hz)
    // phase_inc = hz / 48000 * 2^24
    m_chorus_rate_control = (uint32_t)(hz * 16777216.0f / 48000.0f);
  }

  INLINE void set_chorus_rate_bpm(float bpm) {
    // Stub for BPM sync: e.g. 1/4 note
    // hz = bpm / 60
    set_chorus_rate_fixed(bpm / 60.0f);
  }

  INLINE void set_chorus_rate(uint8_t controller_value) {
    uint32_t rate = 10 + (controller_value * controller_value) / 16;
    m_chorus_rate_control = rate;
  }

  INLINE void set_chorus_delay_time(uint8_t controller_value) {
    // 0-20ms -> 0-960 samples -> 0-15360 units
    m_chorus_delay_time_control = (uint32_t)controller_value * 15360 / 127;
  }

  INLINE void set_chorus_level(uint8_t controller_value) {
    m_chorus_level_control = (controller_value + 1) >> 1;
  }

  template <uint8_t N> INLINE uint32_t get_chorus_delay_time() {
    return m_chorus_delay_time[N];
  }

  INLINE void process_at_low_rate(uint8_t count) {
#if 1
    static_cast<void>(count);

    // Fast response (1/16th per loop)
    m_chorus_level_control_effective += (int32_t)(m_chorus_level_control - m_chorus_level_control_effective) >> 4;
    m_chorus_depth_control_effective += (int32_t)(m_chorus_depth_control - m_chorus_depth_control_effective) >> 4;
    m_chorus_delay_time_control_effective += (int32_t)(m_chorus_delay_time_control - m_chorus_delay_time_control_effective) >> 4;
    m_chorus_feedback_effective += (int32_t)(m_chorus_feedback - m_chorus_feedback_effective) >> 4;

    uint32_t chorus_depth_control_effective_limited;
    // Limit depth to avoid crossing 0 or the maximum mapped range (15360)
    if (m_chorus_delay_time_control_effective < 7680) {
      if (m_chorus_depth_control_effective >
          (m_chorus_delay_time_control_effective << 1)) {
        chorus_depth_control_effective_limited =
            (m_chorus_delay_time_control_effective << 1);
      } else {
        chorus_depth_control_effective_limited =
            m_chorus_depth_control_effective;
      }
    } else {
      uint32_t dist_to_max = (15360 > m_chorus_delay_time_control_effective)
                                 ? (15360 - m_chorus_delay_time_control_effective)
                                 : 0;
      if (m_chorus_depth_control_effective > (dist_to_max << 1)) {
        chorus_depth_control_effective_limited = (dist_to_max << 1);
      } else {
        chorus_depth_control_effective_limited =
            m_chorus_depth_control_effective;
      }
    }

    m_chorus_lfo_phase += m_chorus_rate_control;
    m_chorus_lfo_phase &= 0x00FFFFFF;

    m_chorus_lfo_phase = (m_chorus_lfo_phase + m_chorus_rate_control) & 0x00FFFFFF;

    uint32_t lfo_phase_0 = m_chorus_lfo_phase;
    uint32_t lfo_phase_1 =
        (m_chorus_lfo_phase + (static_cast<uint32_t>(m_chorus_spread) << 17)) &
        0x00FFFFFF;

    int16_t chorus_lfo_wave_level_0 = get_chorus_lfo_wave_level(lfo_phase_0);
    int16_t chorus_lfo_wave_level_1 = get_chorus_lfo_wave_level(lfo_phase_1);

    int16_t chorus_lfo_level_0 =
        (int16_t)((int32_t)chorus_lfo_wave_level_0 *
                  (int32_t)chorus_depth_control_effective_limited >>
                  14);
    int16_t chorus_lfo_level_1 =
        (int16_t)((int32_t)chorus_lfo_wave_level_1 *
                  (int32_t)chorus_depth_control_effective_limited >>
                  14);

    m_chorus_delay_time[0] =
        m_chorus_delay_time_control_effective - chorus_lfo_level_0;
    m_chorus_delay_time[1] =
        m_chorus_delay_time_control_effective + chorus_lfo_level_1;
#endif
  }

  INLINE int32_t process(int32_t left_input_int24, int32_t right_input_int24,
                         int32_t &right_output_int24) {
    int32_t eff_sample_0 = delay_buff_get(0, m_chorus_delay_time[0]);
    int32_t eff_sample_1 = delay_buff_get(1, m_chorus_delay_time[1]);

    int32_t feedback_0 = multiply_shift_right(
        eff_sample_0, (uint32_t)m_chorus_feedback_effective * 65536 / 127, 16);
    int32_t feedback_1 = multiply_shift_right(
        eff_sample_1, (uint32_t)m_chorus_feedback_effective * 65536 / 127, 16);

    delay_buff_push(0, (clamp(((left_input_int24 * m_chorus_level_control_effective) >>
                        6) +
                           feedback_0, -8388607, 8388607)));
    delay_buff_push(1, (clamp(((right_input_int24 * m_chorus_level_control_effective) >>
                        6) +
                           feedback_1, -8388607, 8388607)));

    right_output_int24 = right_input_int24 + eff_sample_1;
    return left_input_int24 + eff_sample_0;
  }

private:
  INLINE void delay_buff_push(uint32_t lr, int32_t audio_input_int24) {
    m_delay_wp[lr] = (m_delay_wp[lr] + 1) & (DELAY_BUFF_SIZE - 1);
    m_delay_buff[lr][m_delay_wp[lr]] = (int16_t)(audio_input_int24 >> 8);
  }

  INLINE int32_t delay_buff_get(uint32_t lr, uint32_t sample_delay) {
    uint32_t curr_index =
        (m_delay_wp[lr] - (sample_delay >> 4)) & (DELAY_BUFF_SIZE - 1);
    uint32_t next_index = (curr_index - 1) & (DELAY_BUFF_SIZE - 1);
    uint32_t next_weight = (sample_delay & 0xF);
    int32_t curr_data = ((int32_t)m_delay_buff[lr][curr_index]) << 8;
    int32_t next_data = ((int32_t)m_delay_buff[lr][next_index]) << 8;

    // lerp
    int32_t result = curr_data + multiply_shift_right(next_data - curr_data,
                                                       next_weight << 12, 16);

    return result;
  }

  INLINE void delay_buff_attenuate() {
    for (uint32_t i = 0; i < DELAY_BUFF_SIZE; ++i) {
      m_delay_buff[0][i] = m_delay_buff[0][i] >> 1;
      m_delay_buff[1][i] = m_delay_buff[1][i] >> 1;
    }
  }

  INLINE int16_t get_chorus_lfo_wave_level(uint32_t phase) {
    int16_t triangle_wave_level = 0;
    phase = (phase >> 9);

    if (phase < 0x00004000) {
      triangle_wave_level = phase - (512 << 4);
    } else {
      triangle_wave_level = (1535 << 4) - phase;
    }

    return triangle_wave_level;
  }
};
