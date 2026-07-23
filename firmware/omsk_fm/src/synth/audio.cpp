#include "audio.h"
#include "../sw_config.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "fm_synth.h"
#include "synth.h"

static uint pwm_slice;
#if CFG_ENABLE_BETTER_PWM
static volatile uint16_t pwm_current = 512;
static volatile uint8_t pwm_repeat = 0;
static const uint8_t PWM_REPEAT_DIV = 2; // 2x oversampling (96kHz interrupt rate)
static int32_t error_accumulator = 0;
#else
static volatile uint8_t pwm8_current = 128;
static volatile uint8_t pwm8_repeat = 0;
static const uint8_t PWM8_REPEAT_DIV = 4;
#endif
static queue_t audio_queue;

void __not_in_flash_func(audio_core_entry)(void) {
  multicore_lockout_victim_init();
  int16_t render_buf[32];
  while (1) {
    fm_synth_render_block(render_buf, 32);
    for (int i = 0; i < 32; i++) {
      uint32_t packed_sample = (uint16_t)render_buf[i];
      while (!queue_try_add(&audio_queue, &packed_sample)) {
        __asm volatile("nop");
      }
    }
  }
}

static void __not_in_flash_func(pwm_wrap_isr)(void) {
  pwm_clear_irq(pwm_slice);
#if CFG_ENABLE_BETTER_PWM
  pwm_set_gpio_level(PIN_AUDIO_PWM_L, pwm_current);
  pwm_repeat++;
  if (pwm_repeat >= PWM_REPEAT_DIV) {
    pwm_repeat = 0;
    uint32_t packed_sample;
    if (queue_try_remove(&audio_queue, &packed_sample)) {
      int16_t sample = (int16_t)(packed_sample & 0xFFFF);
      int16_t ls = (int16_t)((int32_t)sample * CFG_MASTER_VOLUME_PERCENT / 100);

      // Sigma-delta noise shaper
      int32_t sample_with_error = (int32_t)ls + error_accumulator;
      if (sample_with_error > 32767) sample_with_error = 32767;
      else if (sample_with_error < -32768) sample_with_error = -32768;

      uint32_t u = (uint32_t)(sample_with_error + 32768);
      uint16_t quantized = (uint16_t)(u >> 6); // 10 bits
      pwm_current = quantized;
      error_accumulator = (int32_t)u - (int32_t)(quantized << 6);
    }
  }
#else
  pwm_set_gpio_level(PIN_AUDIO_PWM_L, pwm8_current);
  pwm8_repeat++;
  if (pwm8_repeat >= PWM8_REPEAT_DIV) {
    pwm8_repeat = 0;
    uint32_t packed_sample;
    if (queue_try_remove(&audio_queue, &packed_sample)) {
      int16_t sample = (int16_t)(packed_sample & 0xFFFF);
      int16_t ls = (int16_t)((int32_t)sample * CFG_MASTER_VOLUME_PERCENT / 100);
      uint32_t u = (uint32_t)(ls + 32768);
      pwm8_current = (uint8_t)(u >> 8);
    }
  }
#endif
}

void audio_init(void) {
  queue_init(&audio_queue, sizeof(uint32_t), 1024);

  gpio_set_function(PIN_AUDIO_PWM_L, GPIO_FUNC_PWM);
  pwm_slice = pwm_gpio_to_slice_num(PIN_AUDIO_PWM_L);
  pwm_config cfg = pwm_get_default_config();
#if CFG_ENABLE_BETTER_PWM
  pwm_config_set_wrap(&cfg, 1023); // 10-bit wrap
#else
  pwm_config_set_wrap(&cfg, 255);
#endif

  uint32_t sys_hz = clock_get_hz(clk_sys);
#if CFG_ENABLE_BETTER_PWM
  float clkdiv = (float)sys_hz / (1024.0f * (AUDIO_SAMPLE_RATE * (float)PWM_REPEAT_DIV));
#else
  float clkdiv = (float)sys_hz / (256.0f * (AUDIO_SAMPLE_RATE * (float)PWM8_REPEAT_DIV));
#endif
  if (clkdiv < 1.0f) clkdiv = 1.0f;
  pwm_config_set_clkdiv(&cfg, clkdiv);
  pwm_init(pwm_slice, &cfg, true);
#if CFG_ENABLE_BETTER_PWM
  pwm_set_gpio_level(PIN_AUDIO_PWM_L, 512);
#else
  pwm_set_gpio_level(PIN_AUDIO_PWM_L, 128);
#endif

  pwm_clear_irq(pwm_slice);
  pwm_set_irq_enabled(pwm_slice, true);
  irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_wrap_isr);
  irq_set_enabled(PWM_IRQ_WRAP, true);

  // Initialize synth engine inside audio_init
  synth_init();
}

void audio_start(void) {
  multicore_launch_core1(audio_core_entry);
}
