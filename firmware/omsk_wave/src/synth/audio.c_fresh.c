#include "audio.h"
#include "../sw_config.h"
#include "audio.pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "i2s_tx.pio.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "pra_synth.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>


#if CFG_ENABLE_USB_AUDIO
bool usb_audio_push_sample(int16_t left, int16_t right);
#endif

void audio_usb_task(void) {}

// Exactly one physical backend must be enabled, UNLESS we are in USB-only mode
#if !CFG_ENABLE_USB_AUDIO
#if ((CFG_ENABLE_PWM8_AUDIO ? 1 : 0) + (CFG_ENABLE_PWM_AUDIO_MONO ? 1 : 0) +   \
     (CFG_ENABLE_PWM_AUDIO_STEREO ? 1 : 0) + (CFG_ENABLE_DAC ? 1 : 0)) != 1
#error                                                                         \
    "Enable exactly one physical backend: CFG_ENABLE_PWM8_AUDIO, CFG_ENABLE_PWM_AUDIO_MONO, CFG_ENABLE_PWM_AUDIO_STEREO, or CFG_ENABLE_DAC. Or enable CFG_ENABLE_USB_AUDIO."
#endif
#endif
PIO audio_pio = pio0;
