
#if CFG_ENABLE_PWM_AUDIO_MONO
uint audio_sm;
static int32_t sdm_err0 = 0;
#elif CFG_ENABLE_PWM_AUDIO_STEREO
uint audio_sm_l;
uint audio_sm_r;
static int32_t sdm_err_l = 0;
static int32_t sdm_err_r = 0;
#elif CFG_ENABLE_DAC
uint audio_sm_i2s;
#endif

#if CFG_ENABLE_PWM_AUDIO_MONO

static inline uint32_t sdm_o1_os32(int16_t sig) {
  uint32_t out = 0;
  int32_t d = -32767 - sig;
  int32_t etmp;
  for (int j = 0; j < 32; j++) {
    etmp = d + sdm_err0;
    sdm_err0 = etmp;
    if (etmp < 0) {
      sdm_err0 += 65534;
      out |= (1u << j);
    }
  }
  return out;
}

void audio_core_entry(void) {
  int16_t left = 0;
  int16_t right = 0;
  while (1) {
    while (!pio_sm_is_tx_fifo_full(audio_pio, audio_sm)) {
      pra_synth_get_stereo(&left, &right);
      int16_t s = (int16_t)((int32_t)left * CFG_MASTER_VOLUME_PERCENT / 100);
      uint32_t bits = sdm_o1_os32(s);
      pio_sm_put(audio_pio, audio_sm, bits);
    }
  }
}

void audio_init(void) {
  uint offset = pio_add_program(audio_pio, &audio_sdm_program);
  audio_sm = pio_claim_unused_sm(audio_pio, true);
  float bit_rate = (float)AUDIO_SAMPLE_RATE * 32.0f;
  audio_sdm_program_init(audio_pio, audio_sm, offset, PIN_AUDIO_PWM, bit_rate);
  pra_synth_init();
}

void audio_start(void) { multicore_launch_core1(audio_core_entry); }

#elif CFG_ENABLE_PWM_AUDIO_STEREO

static inline uint32_t sdm_o1_os32_l(int16_t sig) {
  uint32_t out = 0;
  int32_t d = -32767 - sig;
  int32_t etmp;
  for (int j = 0; j < 32; j++) {
    etmp = d + sdm_err_l;
    sdm_err_l = etmp;
    if (etmp < 0) {
      sdm_err_l += 65534;
      out |= (1u << j);
    }
  }
  return out;
}

static inline uint32_t sdm_o1_os32_r(int16_t sig) {
  uint32_t out = 0;
  int32_t d = -32767 - sig;
  int32_t etmp;
  for (int j = 0; j < 32; j++) {
    etmp = d + sdm_err_r;
    sdm_err_r = etmp;
    if (etmp < 0) {
      sdm_err_r += 65534;
      out |= (1u << j);
    }
  }
  return out;
}

void audio_core_entry(void) {
  int16_t left = 0;
  int16_t right = 0;
  while (1) {
    while (!pio_sm_is_tx_fifo_full(audio_pio, audio_sm_l) &&
           !pio_sm_is_tx_fifo_full(audio_pio, audio_sm_r)) {
      pra_synth_get_stereo(&left, &right);
      int16_t sl = (int16_t)((int32_t)left * CFG_MASTER_VOLUME_PERCENT / 100);
      int16_t sr = (int16_t)((int32_t)right * CFG_MASTER_VOLUME_PERCENT / 100);
      uint32_t bits_l = sdm_o1_os32_l(sl);
      uint32_t bits_r = sdm_o1_os32_r(sr);
      pio_sm_put(audio_pio, audio_sm_l, bits_l);
      pio_sm_put(audio_pio, audio_sm_r, bits_r);
    }
  }
}

void audio_init(void) {
  uint offset = pio_add_program(audio_pio, &audio_sdm_program);
  audio_sm_l = pio_claim_unused_sm(audio_pio, true);
  audio_sm_r = pio_claim_unused_sm(audio_pio, true);
  float bit_rate = (float)AUDIO_SAMPLE_RATE * 32.0f;
  audio_sdm_program_init(audio_pio, audio_sm_l, offset, PIN_AUDIO_PWM_L,
                         bit_rate);
  audio_sdm_program_init(audio_pio, audio_sm_r, offset, PIN_AUDIO_PWM_R,
                         bit_rate);
  pra_synth_init();
}

void audio_start(void) { multicore_launch_core1(audio_core_entry); }

#elif CFG_ENABLE_DAC

void audio_core_entry(void)
{
  int16_t left = 0;
  int16_t right = 0;

  while (1)
  {
    pra_synth_get_stereo(&left, &right);

    // Применяем громкость
    int16_t out_l = (int16_t)((int32_t)left * CFG_MASTER_VOLUME_PERCENT / 100);
    int16_t out_r = (int16_t)((int32_t)right * CFG_MASTER_VOLUME_PERCENT / 100);

    // Упаковываем два 16-битных канала в одно 32-битное слово
    // (Зависит от того, как написан ваш .pio файл, но это стандарт для I2S)
    uint32_t sample = ((uint16_t)out_l << 16) | (uint16_t)out_r;

    // Отправляем в PIO. Он сам разберется с BCK, LRCK и DATA
    pio_sm_put_blocking(audio_pio, audio_sm_i2s, sample);
  }
}
static inline void i2s_tx_program_init(PIO pio, uint sm, uint offset, uint data_pin, uint bck_pin, float sample_rate)
{
  // bck_pin = 26 (BCK), следующий пин 27 автоматически станет LRCK
  uint lrck_pin = bck_pin + 1;

  pio_gpio_init(pio, data_pin);
  pio_gpio_init(pio, bck_pin);
  pio_gpio_init(pio, lrck_pin);

  pio_sm_set_consecutive_pindirs(pio, sm, data_pin, 1, true);
  pio_sm_set_consecutive_pindirs(pio, sm, bck_pin, 2, true);

  pio_sm_config c = i2s_tx_program_get_default_config(offset);
  sm_config_set_out_pins(&c, data_pin, 1);

  // Указываем базовый пин для side-set (26), программа сама займет 26 и 27
  sm_config_set_sideset_pins(&c, bck_pin);

  // false = сдвиг влево (MSB first), true = автопуш при 32 битах (16 L + 16 R)
  sm_config_set_out_shift(&c, false, true, 32);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

  // Частота: SampleRate * 2 канала * 16 бит * 2 (на полупериоды BCK)
  float div = (float)clock_get_hz(clk_sys) / (sample_rate * 2.0f * 16.0f * 4.0f);
  sm_config_set_clkdiv(&c, div);

  pio_sm_init(pio, sm, offset, &c);
  pio_sm_set_enabled(pio, sm, true);
}
void audio_init(void)
{
  uint offset = pio_add_program(audio_pio, &i2s_tx_program);
  audio_sm_i2s = pio_claim_unused_sm(audio_pio, true);

  i2s_tx_program_init(audio_pio, audio_sm_i2s, offset,
                      PIN_DAC_I2S_DATA, PIN_DAC_I2S_BCK, AUDIO_SAMPLE_RATE);

  pra_synth_init();
}

void audio_start(void)
{
  multicore_launch_core1(audio_core_entry);
}

#elif CFG_ENABLE_PWM8_AUDIO

static uint pwm_slice;
static volatile uint8_t pwm8_current = 128;
static volatile uint8_t pwm8_repeat = 0;
// Reduced oversampling to 4x to lower CPU load on 150MHz clock.
// 8x (384kHz) was too heavy for synthesis + USB.
static const uint8_t PWM8_REPEAT_DIV = 4;
static queue_t audio_queue;

void __not_in_flash_func(audio_core_entry)(void)
{
  int16_t l, r;
  uint32_t sample;
  while (1)
  {
    pra_synth_get_stereo(&l, &r);
    sample = ((uint16_t)l) | ((uint32_t)((uint16_t)r) << 16);
    queue_add_blocking(&audio_queue, &sample);
  }
}

static void __not_in_flash_func(pwm_wrap_isr)(void)
{
  pwm_clear_irq(pwm_slice);
  pwm_set_gpio_level(PIN_AUDIO_PWM, pwm8_current);
  pwm8_repeat++;
  if (pwm8_repeat >= PWM8_REPEAT_DIV)
  {
    pwm8_repeat = 0;
    uint32_t sample;
    if (queue_try_remove(&audio_queue, &sample))
    {
      int16_t l = (int16_t)(sample & 0xFFFF);
      int16_t r = (int16_t)(sample >> 16);
      int16_t ls = (int16_t)((int32_t)l * CFG_MASTER_VOLUME_PERCENT / 100);
      uint16_t u = (uint16_t)(ls + 32768);
      pwm8_current = (uint8_t)(u >> 8);
#if CFG_ENABLE_USB_AUDIO
      usb_audio_push_sample(l, r); // Use raw samples for USB to avoid double scaling and 8-bit noise
#else
      (void)r;
#endif
    }
    else
    {
#if CFG_ENABLE_USB_AUDIO
      // Underrun handling
      usb_audio_push_sample(0, 0);
#endif
    }
  }
}

void audio_init(void)
{
  usb_audio_init();
  queue_init(&audio_queue, sizeof(uint32_t), 1024);

  gpio_set_function(PIN_AUDIO_PWM, GPIO_FUNC_PWM);
  pwm_slice = pwm_gpio_to_slice_num(PIN_AUDIO_PWM);
  pwm_config cfg = pwm_get_default_config();
  pwm_config_set_wrap(&cfg, 255);
  uint32_t sys_hz = clock_get_hz(clk_sys);
  float clkdiv = (float)sys_hz / (256.0f * (AUDIO_SAMPLE_RATE * (float)PWM8_REPEAT_DIV));
  if (clkdiv < 1.0f)
    clkdiv = 1.0f;
  pwm_config_set_clkdiv(&cfg, clkdiv);
  pwm_init(pwm_slice, &cfg, true);
  pwm_set_gpio_level(PIN_AUDIO_PWM, 128);
  pwm_clear_irq(pwm_slice);
  pwm_set_irq_enabled(pwm_slice, true);
  irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_wrap_isr);
  irq_set_enabled(PWM_IRQ_WRAP, true);
  pra_synth_init();
}

void audio_start(void)
{
  multicore_launch_core1(audio_core_entry);
}

#elif CFG_ENABLE_PWM8_AUDIO

static uint pwm_slice;
static volatile uint8_t pwm8_current = 128;
static volatile uint8_t pwm8_repeat = 0;
// Reduced oversampling to 4x to lower CPU load on 150MHz clock.
// 8x (384kHz) was too heavy for synthesis + USB.
static const uint8_t PWM8_REPEAT_DIV = 4;
static queue_t audio_queue;

void __not_in_flash_func(audio_core_entry)(void) {
  int16_t l, r;
  uint32_t sample;
  while (1) {
    pra_synth_get_stereo(&l, &r);
    sample = ((uint16_t)l) | ((uint32_t)((uint16_t)r) << 16);
    queue_add_blocking(&audio_queue, &sample);
  }
}

static void __not_in_flash_func(pwm_wrap_isr)(void) {
  pwm_clear_irq(pwm_slice);
  pwm_set_gpio_level(PIN_AUDIO_PWM, pwm8_current);
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
}

void audio_init(void) {
  usb_audio_init();
  queue_init(&audio_queue, sizeof(uint32_t), 1024);

  gpio_set_function(PIN_AUDIO_PWM, GPIO_FUNC_PWM);
  pwm_slice = pwm_gpio_to_slice_num(PIN_AUDIO_PWM);
  pwm_config cfg = pwm_get_default_config();
  pwm_config_set_wrap(&cfg, 255);
  uint32_t sys_hz = clock_get_hz(clk_sys);
  float clkdiv =
      (float)sys_hz / (256.0f * (AUDIO_SAMPLE_RATE * (float)PWM8_REPEAT_DIV));
  if (clkdiv < 1.0f)
    clkdiv = 1.0f;
  pwm_config_set_clkdiv(&cfg, clkdiv);
  pwm_init(pwm_slice, &cfg, true);
  pwm_set_gpio_level(PIN_AUDIO_PWM, 128);
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
