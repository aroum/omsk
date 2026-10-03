#include "audio.h"
#include "../sw_config.h"
// Hardware SDK includes MUST come before PIO generated headers
// (PIO headers contain inline functions using clock_get_hz, etc.)
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
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

// Exactly one physical backend must be enabled, UNLESS we are in USB-only mode
#if !CFG_ENABLE_USB_AUDIO
#if ((CFG_ENABLE_PWM8_AUDIO ? 1 : 0) + (CFG_ENABLE_DAC ? 1 : 0)) != 1
#error                                                                         \
    "Enable exactly one physical backend: CFG_ENABLE_PWM8_AUDIO or CFG_ENABLE_DAC. Or enable CFG_ENABLE_USB_AUDIO."
#endif
#endif
PIO audio_pio = pio0;

#if CFG_ENABLE_DAC
#define I2S_BUFFER_COUNT 16
#define I2S_BUFFER_SAMPLES 32
#define I2S_PREFILL_BUFFERS 8

uint audio_sm_i2s;
static int audio_dma_chan = -1;

static uint32_t audio_ring_buf[I2S_BUFFER_COUNT][I2S_BUFFER_SAMPLES];
static uint32_t audio_silence_buf[I2S_BUFFER_SAMPLES]; // zero-initialized
static volatile uint8_t audio_write_idx = 0;
static volatile uint8_t audio_play_idx = 0;
static volatile uint8_t audio_buffers_ready = 0;

static void __not_in_flash_func(audio_dma_irq_handler)(void) {
  dma_channel_acknowledge_irq0(audio_dma_chan);
  if (audio_buffers_ready > 0) {
    dma_channel_transfer_from_buffer_now(audio_dma_chan, audio_ring_buf[audio_play_idx], I2S_BUFFER_SAMPLES);
    audio_play_idx = (audio_play_idx + 1) % I2S_BUFFER_COUNT;
    audio_buffers_ready--;
  } else {
    // Underflow protection: keep I2S clocks active and output silence
    dma_channel_transfer_from_buffer_now(audio_dma_chan, audio_silence_buf, I2S_BUFFER_SAMPLES);
  }
}

void __not_in_flash_func(audio_core_entry)(void) {
  multicore_lockout_victim_init();

  // Pre-render initial audio blocks to provide headroom
  for (int b = 0; b < I2S_PREFILL_BUFFERS; b++) {
    for (int i = 0; i < I2S_BUFFER_SAMPLES; i++) {
      int16_t left = 0;
      int16_t right = 0;
      pra_synth_get_stereo(&left, &right);
      left  = (int16_t)((int32_t)left  * CFG_MASTER_VOLUME_PERCENT / 100);
      right = (int16_t)((int32_t)right * CFG_MASTER_VOLUME_PERCENT / 100);

#if CFG_ENABLE_USB_AUDIO
      usb_audio_push_sample(left, right);
#endif

      audio_ring_buf[audio_write_idx][i] = ((uint32_t)(uint16_t)left << 16) | (uint16_t)right;
    }
    audio_write_idx = (audio_write_idx + 1) % I2S_BUFFER_COUNT;
    audio_buffers_ready++;
  }

  // Setup DMA interrupt exclusively on Core 1
  irq_set_exclusive_handler(DMA_IRQ_0, audio_dma_irq_handler);
  irq_set_enabled(DMA_IRQ_0, true);

  // Kick off DMA playback
  audio_dma_irq_handler();

  while (1) {
    while (audio_buffers_ready >= I2S_BUFFER_COUNT - 1) {
      tight_loop_contents();
    }

    for (int i = 0; i < I2S_BUFFER_SAMPLES; i++) {
      int16_t left = 0;
      int16_t right = 0;
      pra_synth_get_stereo(&left, &right);
      left  = (int16_t)((int32_t)left  * CFG_MASTER_VOLUME_PERCENT / 100);
      right = (int16_t)((int32_t)right * CFG_MASTER_VOLUME_PERCENT / 100);

#if CFG_ENABLE_USB_AUDIO
      usb_audio_push_sample(left, right);
#endif

      audio_ring_buf[audio_write_idx][i] = ((uint32_t)(uint16_t)left << 16) | (uint16_t)right;
    }
    audio_write_idx = (audio_write_idx + 1) % I2S_BUFFER_COUNT;

    uint32_t save = save_and_disable_interrupts();
    audio_buffers_ready++;
    restore_interrupts(save);
  }
}

void audio_init(void) {
#if CFG_ENABLE_USB_AUDIO
  usb_audio_init();
#endif
  uint offset = pio_add_program(audio_pio, &i2s_tx_program);
  audio_sm_i2s = pio_claim_unused_sm(audio_pio, true);
  i2s_tx_program_init(audio_pio, audio_sm_i2s, offset, PIN_DAC_I2S_DATA,
                      PIN_DAC_I2S_BCK, AUDIO_SAMPLE_RATE);

  audio_dma_chan = dma_claim_unused_channel(true);
  dma_channel_config c = dma_channel_get_default_config(audio_dma_chan);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, pio_get_dreq(audio_pio, audio_sm_i2s, true));

  dma_channel_configure(
      audio_dma_chan,
      &c,
      &audio_pio->txf[audio_sm_i2s],
      NULL,
      I2S_BUFFER_SAMPLES,
      false
  );

  dma_channel_set_irq0_enabled(audio_dma_chan, true);

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
