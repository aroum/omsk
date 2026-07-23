// refs https://www.g200kg.com/archives/2012/10/adsr.html
// refs https://www.g200kg.com/archives/2012/10/adsr2.html
// refs https://www.g200kg.com/archives/2020/07/adsr-1.html

#pragma once

#include <stdint.h>
#include <math.h>
#include "../sw_config.h"
#include "../synth/pra32-u2-common.h"
#include "../tables/vcf_lut_data.h"

class PRA32_U2_EG {
  static const uint8_t STATE_ATTACK = 0;
  static const uint8_t STATE_DECAY = 1;
  static const uint8_t STATE_SUSTAIN = 2;
  static const uint8_t STATE_RELEASE = 3;
  static const uint8_t STATE_IDLE = 4;

  uint8_t m_state;
  int32_t m_level;
  int16_t m_level_out;
  int32_t m_attack_step;
  int32_t m_decay_step;
  int32_t m_sustain_level_target;
  int32_t m_release_step;
  uint8_t m_velocity_sensitivity;
  uint8_t m_note_on_velocity;
  int32_t m_attack_level;
  uint8_t m_sustain_param; // 0-127

public:
  PRA32_U2_EG()
      : m_state(STATE_IDLE), m_level(0), m_level_out(0), m_attack_step(0),
        m_decay_step(0), m_sustain_level_target(0), m_release_step(0),
        m_velocity_sensitivity(0), m_note_on_velocity(0), m_attack_level(0),
        m_sustain_param(127) {
    set_attack(0);
    set_decay(0);
    set_sustain(127);
    set_release(0);
  }

  INLINE int32_t calculate_step(uint8_t time_param) {
    // Logarithmic: ms = 2000 * (100^(p/127) - 1) / 99
    float eg_ms = 2000.0f * (powf(100.0f, (float)time_param / 127.0f) - 1.0f) / 99.0f;
    float T = eg_ms / 1000.0f;
    float N = T * (float)SAMPLING_RATE;
    if (N < 1.0f) N = 1.0f;
    return (int32_t)((float)EG_LEVEL_MAX / N);
  }

  INLINE void set_attack(uint8_t controller_value) {
    m_attack_step = calculate_step(controller_value);
  }

  INLINE void set_decay(uint8_t controller_value) {
    m_decay_step = calculate_step(controller_value);
  }

  INLINE void set_sustain(uint8_t controller_value) {
    m_sustain_param = controller_value;
    m_sustain_level_target = (int32_t)(((int64_t)m_attack_level * controller_value) / 127);
  }

  INLINE void set_release(uint8_t controller_value) {
    m_release_step = calculate_step(controller_value);
  }

  INLINE void set_velocity_sensitivity(uint8_t controller_value) {
    m_velocity_sensitivity = (controller_value + 1) >> 1;
  }

  INLINE void note_on(uint8_t velocity) {
    if (velocity <= 127) {
      m_note_on_velocity = velocity;
    }
    m_attack_level = ((((m_note_on_velocity * m_velocity_sensitivity) +
                        (127 * (64 - m_velocity_sensitivity))) *
                       16384) /
                      127)
                     << (EG_LEVEL_MAX_BITS - 20);
    
    m_sustain_level_target = (int32_t)(((int64_t)m_attack_level * m_sustain_param) / 127);
    m_state = STATE_ATTACK;
    // Current level stays where it was for retriggering
  }

  INLINE void note_off() {
    if (m_state != STATE_IDLE) {
        m_state = STATE_RELEASE;
    }
  }

  INLINE int16_t get_output() { return m_level_out; }

  INLINE void process_at_low_rate(uint8_t a_cur, uint8_t d_cur, uint8_t r_cur) {
    uint8_t curve = 64;

    switch (m_state) {
    case STATE_ATTACK:
      curve = a_cur;
      m_level += m_attack_step;
      if (m_level >= m_attack_level) {
        m_level = m_attack_level;
        m_state = STATE_DECAY;
      }
      break;

    case STATE_DECAY:
      curve = d_cur;
      m_level -= m_decay_step;
      if (m_level <= m_sustain_level_target) {
        m_level = m_sustain_level_target;
        m_state = STATE_SUSTAIN;
      }
      break;

    case STATE_SUSTAIN:
      curve = d_cur; // Use decay curve style for sustain levels? Or usually sust is flat
      m_level = m_sustain_level_target;
      break;

    case STATE_RELEASE:
      curve = r_cur;
      m_level -= m_release_step;
      if (m_level <= 0) {
        m_level = 0;
        m_state = STATE_IDLE;
      }
      break;

    case STATE_IDLE:
      m_level = 0;
      break;
    }

    if (curve > 127) curve = 127;

    // Shaping via LUT
    // Map m_level (0..EG_LEVEL_MAX) to 0..255
    uint32_t idx = (uint32_t)m_level >> (EG_LEVEL_MAX_BITS - 8);
    if (idx > 255) idx = 255;
    
    // Result from LUT is 16-bit
    uint32_t shaped = g_eg_curve_lut[curve][idx];
    
    // Convert to m_level_out (16-bit signed, but levels are positive)
    // EG_LEVEL_MAX is usually 1<<30. m_level_out is level >> 16.
    // shaped is 0..65535 (16 bit).
    // Original m_level_out was m_level >> 14? Let's check common.h or constants.
    // EG_LEVEL_MAX = 1 << 30.
    // m_level_out = m_level >> 16 = 2^14 max.
    // shaped = 2^16 max. 
    // We need m_level_out to match original scale.
    m_level_out = (int16_t)(shaped >> 2); // 65535/4 = 16383 (max 14-bit)
  }
};

