#include "audio.h"
#include "../sw_config.h"
// Hardware SDK includes MUST come before PIO generated headers
// (PIO headers contain inline functions using clock_get_hz, etc.)
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
// PIO generated headers (depend on hardware/clocks.h above)
#include "audio.pio.h"
#include "i2s_tx.pio.h"
#include "pra_synth.h"
#include "../gen/omsk_core.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#if CFG_ENABLE_USB_AUDIO
bool usb_audio_push_sample(int16_t left, int16_t right);
#endif

void audio_usb_task(void){}

// Exactly one physical backend must be enabled, UNLESS we are in USB-only mode
#if !CFG_ENABLE_USB_AUDIO
#if ((CFG_ENABLE_PWM8_AUDIO ? 1 : 0) + (CFG_ENABLE_DAC ? 1 : 0)) != 1
#error                                                                         \
    "Enable exactly one physical backend: CFG_ENABLE_PWM8_AUDIO or CFG_ENABLE_DAC. Or enable CFG_ENABLE_USB_AUDIO."
#endif
#endif
PIO audio_pio = pio0;

#if CFG_ENABLE_DAC
uint audio_sm_i2s;
#endif

#if CFG_ENABLE_DAC

void audio_core_entry(void) {
  multicore_lockout_victim_init();
  int16_t left = 0;
  int16_t right = 0;
  while (1) {
    pra_synth_get_stereo(&left, &right);
    left  = (int16_t)((int32_t)left  * CFG_MASTER_VOLUME_PERCENT / 100);
    right = (int16_t)((int32_t)right * CFG_MASTER_VOLUME_PERCENT / 100);

#if CFG_ENABLE_USB_AUDIO
    usb_audio_push_sample(left, right);
#endif

    // I2S PIO expects one 32-bit word: left channel in upper 16 bits,
    // right channel in lower 16 bits. PIO side-set controls LRCK automatically.
    // DO NOT call gpio_put(PIN_DAC_I2S_LRCK) here — it conflicts with PIO.
    uint32_t sample = ((uint32_t)(uint16_t)left << 16) | (uint16_t)right;
    pio_sm_put_blocking(audio_pio, audio_sm_i2s, sample);


  }
}
// removed i2s_tx_program_init
void audio_init(void) {
#if CFG_ENABLE_USB_AUDIO
  usb_audio_init();
#endif
  // NOTE: Do NOT manually init/control PIN_DAC_I2S_LRCK here.
  // The PIO program drives LRCK via side-set. Manual gpio_init would conflict.
  uint offset = pio_add_program(audio_pio, &i2s_tx_program);
  audio_sm_i2s = pio_claim_unused_sm(audio_pio, true);
  i2s_tx_program_init(audio_pio, audio_sm_i2s, offset, PIN_DAC_I2S_DATA,
                      PIN_DAC_I2S_BCK, AUDIO_SAMPLE_RATE);

  pra_synth_init();
}


void audio_start(void) { multicore_launch_core1(audio_core_entry); }

#elif CFG_ENABLE_PWM8_AUDIO

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
  int16_t l, r;
  uint32_t sample;
  while (1) {
    pra_synth_get_stereo(&l, &r);
    sample = ((uint16_t)l) | ((uint32_t)((uint16_t)r) << 16);
    while (!queue_try_add(&audio_queue, &sample)) {
      __asm volatile("nop");
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
    uint32_t sample;
    if (queue_try_remove(&audio_queue, &sample)) {
      int16_t l = (int16_t)(sample & 0xFFFF);
      int16_t r = (int16_t)(sample >> 16);
      int16_t ls = (int16_t)((int32_t)l * CFG_MASTER_VOLUME_PERCENT / 100);

      // First-order sigma-delta noise shaping (error diffusion)
      int32_t sample_with_error = (int32_t)ls + error_accumulator;
      if (sample_with_error > 32767) sample_with_error = 32767;
      else if (sample_with_error < -32768) sample_with_error = -32768;

      uint32_t u = (uint32_t)(sample_with_error + 32768);
      uint16_t quantized = (uint16_t)(u >> 6); // Quantize to 10 bits
      pwm_current = quantized;
      error_accumulator = (int32_t)u - (int32_t)(quantized << 6);

#if CFG_ENABLE_USB_AUDIO
      usb_audio_push_sample(l, r);
#else
      (void)r;
#endif
    } else {
#if CFG_ENABLE_USB_AUDIO
      usb_audio_push_sample(0, 0);
#endif
    }
  }
#else
  pwm_set_gpio_level(PIN_AUDIO_PWM_L, pwm8_current);
  pwm8_repeat++;
  if (pwm8_repeat >= PWM8_REPEAT_DIV) {
    pwm8_repeat = 0;
    uint32_t sample;
    if (queue_try_remove(&audio_queue, &sample)) {
      int16_t l = (int16_t)(sample & 0xFFFF);
      int16_t r = (int16_t)(sample >> 16);
      int16_t ls = (int16_t)((int32_t)l * CFG_MASTER_VOLUME_PERCENT / 100);
      uint16_t u = (uint16_t)(ls + 32768);
      pwm8_current = (uint8_t)(u >> 8);
#if CFG_ENABLE_USB_AUDIO
      usb_audio_push_sample(
          l,
          r); // Use raw samples for USB to avoid double scaling and 8-bit noise
#else
      (void)r;
#endif
    } else {
#if CFG_ENABLE_USB_AUDIO
      // Underrun handling
      usb_audio_push_sample(0, 0);
#endif
    }
  }
#endif
}

void audio_init(void) {
  usb_audio_init();
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
  float clkdiv =
      (float)sys_hz / (1024.0f * (AUDIO_SAMPLE_RATE * (float)PWM_REPEAT_DIV));
#else
  float clkdiv =
      (float)sys_hz / (256.0f * (AUDIO_SAMPLE_RATE * (float)PWM8_REPEAT_DIV));
#endif
  if (clkdiv < 1.0f)
    clkdiv = 1.0f;
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
  pra_synth_init();
}

void audio_start(void) { multicore_launch_core1(audio_core_entry); }

#else

#if CFG_ENABLE_USB_AUDIO
void __not_in_flash_func(audio_core_entry)(void) {
  multicore_lockout_victim_init();
  int16_t l, r;
  while (1) {
    pra_synth_get_stereo(&l, &r);
    usb_audio_push_sample(l, r);
    // Approximately 48kHz pacing to prevent CPU saturation on Core 1
    // and allow some time for other background tasks if needed.
    // 20us = 50kHz.
    sleep_us(20);
  }
}

void audio_init(void) {
  usb_audio_init();
  pra_synth_init();
}

void audio_start(void) { multicore_launch_core1(audio_core_entry); }
#else
#error "No audio backend enabled in config.h"
#endif

#endif
