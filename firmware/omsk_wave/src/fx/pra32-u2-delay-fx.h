#pragma once

#include "../synth/pra32-u2-common.h"

class PRA32_U2_DelayFx {
  static const uint32_t DELAY_BUFF_SIZE = 16384;
  int16_t m_delay_buff[DELAY_BUFF_SIZE];
  uint32_t m_delay_wp;

  uint16_t m_delay_level;
  uint16_t m_delay_level_effective;
  uint8_t m_delay_feedback;
  uint8_t m_delay_feedback_effective;
  uint32_t m_delay_time;
  uint32_t m_delay_time_effective;
  uint8_t m_delay_tone;
  int32_t m_lpf_state;

public:
  PRA32_U2_DelayFx()
      : m_delay_buff(), m_delay_wp()

        ,
        m_delay_level(), m_delay_level_effective(), m_delay_feedback(),
        m_delay_feedback_effective(), m_delay_time(), m_delay_time_effective(),
        m_delay_tone(), m_lpf_state(0) {
    m_delay_wp = DELAY_BUFF_SIZE - 1;

    set_delay_level(0);
    set_delay_feedback(64);
    set_delay_time(64);
    m_delay_tone = 127;

    m_delay_time_effective = m_delay_time;
  }

  INLINE void set_delay_level(uint8_t controller_value) {
    m_delay_level = ((controller_value + 1) >> 1) << 2;
  }

  INLINE void set_delay_feedback(uint8_t controller_value) {
    m_delay_feedback = controller_value;
  }

  INLINE void set_delay_time(uint8_t controller_value) {
    m_delay_time = (uint32_t)controller_value * (DELAY_BUFF_SIZE - 1) / 127;
  }

  INLINE void set_delay_time_samples(uint32_t samples) {
    if (samples >= DELAY_BUFF_SIZE)
      samples = DELAY_BUFF_SIZE - 1;
    m_delay_time = samples;
  }

  INLINE void set_delay_mode(uint8_t controller_value) {
    m_delay_tone = controller_value;
  }

  INLINE void process_at_low_rate(uint8_t count) {
    (void)count;
    // Faster response (1/16th per loop)
    m_delay_level_effective += (int32_t)(m_delay_level - m_delay_level_effective) >> 4;
    m_delay_feedback_effective += (int32_t)(m_delay_feedback - m_delay_feedback_effective) >> 4;
    m_delay_time_effective += (int32_t)(m_delay_time - m_delay_time_effective) >> 4;
  }

  INLINE int32_t process(int32_t input_int24) {
    int32_t delayed = delay_buff_get(m_delay_time_effective);

    // Tone (Simple LPF)
    // m_delay_tone: 0-127
    uint32_t alpha = (m_delay_tone << 9) + 4096; // 0..65536+4096
    if (alpha > 65536)
      alpha = 65536;

    m_lpf_state =
        m_lpf_state + multiply_shift_right(delayed - m_lpf_state, alpha, 16);
    int32_t feedback_in = m_lpf_state;

    int32_t send = multiply_shift_right(input_int24, m_delay_level_effective, 8);

    int32_t feedback_out = multiply_shift_right(
        send + feedback_in, (uint32_t)m_delay_feedback_effective * 65536 / 127, 16);

    delay_buff_push(clamp(feedback_out, -8388607, 8388607));

    return input_int24 + delayed;
  }

private:
  INLINE void delay_buff_push(int32_t audio_input) {
    m_delay_wp = (m_delay_wp + 1) & (DELAY_BUFF_SIZE - 1);
    m_delay_buff[m_delay_wp] = (int16_t)(audio_input >> 8);
  }

  INLINE int32_t delay_buff_get(uint32_t sample_delay) {
    uint32_t delay_rp = (m_delay_wp - sample_delay) & (DELAY_BUFF_SIZE - 1);
    return ((int32_t)m_delay_buff[delay_rp]) << 8;
  }
};
