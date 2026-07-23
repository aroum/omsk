#include "tusb.h"
#include "tusb_config.h"
#include "pico/stdlib.h"
#include "audio.h"
#include "sw_config.h"
#include "hardware/sync.h"
#include <string.h>

#if CFG_TUD_AUDIO

void usb_audio_init(void);

#define UAC2_ENTITY_CLOCK               0x04
#define UAC2_ENTITY_MIC_INPUT_TERMINAL  0x11

static uint32_t current_sample_rate;
static uint8_t current_bit_rate;
static uint8_t clk_valid;
static const uint32_t supported_sample_rates[] = { AUDIO_SAMPLE_RATE };
#define N_SAMPLE_RATES TU_ARRAY_SIZE(supported_sample_rates)
static uint64_t tx_frac_acc;
#define USB_AUDIO_MAX_FRAMES ((CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE / 1000) + 1)
#define USB_AUDIO_TX_SAMPLES (USB_AUDIO_MAX_FRAMES * CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX)
#define USB_AUDIO_TX_RING_SAMPLES 2048
// static int16_t usb_audio_tx_ring[USB_AUDIO_TX_RING_SAMPLES];
// static volatile uint16_t usb_audio_tx_widx;
// static volatile uint16_t usb_audio_tx_ridx;
#include "pico/util/queue.h"

static queue_t usb_audio_tx_queue;
static int16_t usb_audio_tx_scratch[USB_AUDIO_TX_SAMPLES];
static bool usb_audio_queue_ready;

void usb_audio_init(void) {
    current_sample_rate = AUDIO_SAMPLE_RATE;
    current_bit_rate = CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_TX;
    clk_valid = 1;
    // usb_audio_tx_widx = 0;
    // usb_audio_tx_ridx = 0;
    tx_frac_acc = 0;
    queue_init(&usb_audio_tx_queue, sizeof(uint32_t), USB_AUDIO_TX_RING_SAMPLES / 2);
    usb_audio_queue_ready = (usb_audio_tx_queue.data != NULL);
}

// Called from audio.c (e.g. PWM ISR or I2S callback)
bool usb_audio_push_sample(int16_t left, int16_t right) {
    if (!tud_audio_mounted()) return true; // Drop sample if not mounted
    if (!usb_audio_queue_ready) return false;

    // Non-blocking push. If full, we drop samples.
    // Pack L+R into uint32_t to ensure channel sync
    uint32_t sample = ((uint16_t)left) | (((uint32_t)(uint16_t)right) << 16);
    if (!queue_try_add(&usb_audio_tx_queue, &sample)) return false;
    
    return true;
}

#if CFG_TUD_AUDIO_ENABLE_EP_OUT
bool tud_audio_rx_done_pre_read_cb(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id,
                                   uint8_t ep_out, uint8_t cur_alt_setting) {
    (void)rhport;
    (void)func_id;
    (void)ep_out;
    (void)cur_alt_setting;
    static uint8_t rx_drop[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX];
    if (n_bytes_received == 0 || !tud_audio_mounted()) {
        return true;
    }
    uint16_t remaining = n_bytes_received;
    while (remaining > 0) {
        uint16_t chunk = remaining > sizeof(rx_drop) ? sizeof(rx_drop) : remaining;
        uint16_t got = tud_audio_read(rx_drop, chunk);
        if (got == 0) {
            break;
        }
        remaining = (uint16_t)(remaining - got);
    }
    return true;
}
#endif

//--------------------------------------------------------------------+
// Application Callback API Implementations
//--------------------------------------------------------------------+



// Invoked when audio class specific set request received for an entity
static bool tud_audio_clock_set_request(uint8_t rhport, audio_control_request_t const *request,
                                        uint8_t const *buf) {
  (void)rhport;
  TU_ASSERT(request->bEntityID == UAC2_ENTITY_CLOCK);
  TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);
  if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ) {
    TU_VERIFY(request->wLength == sizeof(audio_control_cur_4_t));
    uint32_t requested = (uint32_t)((audio_control_cur_4_t const *)buf)->bCur;
    for (uint8_t i = 0; i < N_SAMPLE_RATES; i++) {
      if (requested == supported_sample_rates[i]) {
        current_sample_rate = requested;
        tx_frac_acc = 0;
        return true;
      }
    }
    return false;
  }
  return false;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const * p_request, uint8_t *pBuff)
{
  audio_control_request_t const *request = (audio_control_request_t const *)p_request;
  if (request->bEntityID == UAC2_ENTITY_CLOCK) {
    return tud_audio_clock_set_request(rhport, request, pBuff);
  }
  return false;
}

// Invoked when audio class specific get request received for an EP
bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
  (void) rhport;
  (void) p_request;
  return false;
}

// Invoked when audio class specific get request received for an entity
static bool tud_audio_clock_get_request(uint8_t rhport, audio_control_request_t const *request) {
  if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ) {
    if (request->bRequest == AUDIO_CS_REQ_CUR) {
      audio_control_cur_4_t curf = {(int32_t)tu_htole32(current_sample_rate)};
      return tud_audio_buffer_and_schedule_control_xfer(
          rhport, (tusb_control_request_t const *)request, &curf, sizeof(curf));
    } else if (request->bRequest == AUDIO_CS_REQ_RANGE) {
      audio_control_range_4_n_t(N_SAMPLE_RATES)
          rangef = {.wNumSubRanges = tu_htole16(N_SAMPLE_RATES)};
      for (uint8_t i = 0; i < N_SAMPLE_RATES; i++) {
        rangef.subrange[i].bMin = (int32_t)supported_sample_rates[i];
        rangef.subrange[i].bMax = (int32_t)supported_sample_rates[i];
        rangef.subrange[i].bRes = 0;
      }
      return tud_audio_buffer_and_schedule_control_xfer(
          rhport, (tusb_control_request_t const *)request, &rangef, sizeof(rangef));
    }
  } else if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID &&
             request->bRequest == AUDIO_CS_REQ_CUR) {
    audio_control_cur_1_t cur_valid = {.bCur = clk_valid};
    return tud_audio_buffer_and_schedule_control_xfer(
        rhport, (tusb_control_request_t const *)request, &cur_valid, sizeof(cur_valid));
  }
  return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
  audio_control_request_t const *request = (audio_control_request_t const *)p_request;

  if (request->bEntityID == UAC2_ENTITY_CLOCK) {
    return tud_audio_clock_get_request(rhport, request);
  }

  return false;
}

bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting)
{
  (void) rhport;
  (void) itf;
  (void) ep_in;
  if (cur_alt_setting == 0) {
    return true;
  }
  if (!usb_audio_queue_ready) {
    return true;
  }

  // For 48kHz on full-speed USB, keep packet size deterministic (48 frames/ms).
  // This avoids host-side jitter/buffering artifacts (notably on Windows/macOS).
  uint32_t frames;
  if ((current_sample_rate % 1000u) == 0u) {
    frames = current_sample_rate / 1000u;
  } else {
    // Fallback for non-integer rates.
    const uint32_t usb_sof_hz = 1000u;
    tx_frac_acc += (uint64_t)current_sample_rate;
    frames = (uint32_t)(tx_frac_acc / usb_sof_hz);
    tx_frac_acc %= usb_sof_hz;
  }

  uint32_t samples_needed = frames * CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX;
  if (samples_needed == 0 || samples_needed > USB_AUDIO_TX_SAMPLES) {
    return true;
  }

  // uint32_t irq = save_and_disable_interrupts();
  // uint16_t w = usb_audio_tx_widx;
  // uint16_t r = usb_audio_tx_ridx;
  // uint16_t available = (w >= r) ? (w - r) : (USB_AUDIO_TX_RING_SAMPLES - r + w);
  // uint16_t to_copy = (available < samples_needed) ? available : samples_needed;
  // for (uint16_t i = 0; i < to_copy; i++)
  // {
  //   usb_audio_tx_scratch[i] = usb_audio_tx_ring[r];
  //   r = (uint16_t)((r + 1) % USB_AUDIO_TX_RING_SAMPLES);
  // }
  // usb_audio_tx_ridx = r;
  // restore_interrupts(irq);
  uint16_t to_copy = 0;
  while (to_copy < samples_needed) {
      uint32_t sample_pair;
      if (queue_try_remove(&usb_audio_tx_queue, &sample_pair)) {
          usb_audio_tx_scratch[to_copy++] = (int16_t)(sample_pair & 0xFFFF);
          if (to_copy < samples_needed) {
              usb_audio_tx_scratch[to_copy++] = (int16_t)(sample_pair >> 16);
          }
      } else {
          break;
      }
  }

  if (to_copy < samples_needed) {
    // if (to_copy >= CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX && to_copy > 0)
    // {
    //   uint32_t last_frame_start = to_copy - CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX;
    //   for (uint32_t i = to_copy; i < samples_needed; i += CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX)
    //   {
    //     for (uint8_t ch = 0; ch < CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX; ch++)
    //     {
    //       if (i + ch < samples_needed)
    //       {
    //         usb_audio_tx_scratch[i + ch] = usb_audio_tx_scratch[last_frame_start + ch];
    //       }
    //     }
    //   }
    // }
    // else
    // {
    //   memset(usb_audio_tx_scratch + to_copy, 0, (samples_needed - to_copy) * sizeof(int16_t));

      // Fill with the last valid sample instead of zero to avoid 1kHz buzzing/metallic sound
      int16_t last_l = (to_copy >= 2) ? usb_audio_tx_scratch[to_copy - 2] : 0;
      int16_t last_r = (to_copy >= 1) ? usb_audio_tx_scratch[to_copy - 1] : 0;
      for (uint32_t i = to_copy; i < samples_needed; i += 2)
      {
        usb_audio_tx_scratch[i] = last_l;
        if (i + 1 < samples_needed) usb_audio_tx_scratch[i + 1] = last_r;
    }
    }

  tud_audio_write(usb_audio_tx_scratch, (uint16_t)(samples_needed * sizeof(int16_t)));
  return true;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
  (void)rhport;
  (void)tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));
  if (alt == 0) {
    tx_frac_acc = 0;
  } else {
    current_bit_rate = CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_TX;
  }
  return true;
}

bool tud_audio_tx_done_post_load_cb(uint8_t rhport, uint16_t n_bytes_copied, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting)
{
  (void) rhport;
  (void) n_bytes_copied;
  (void) itf;
  (void) ep_in;
  (void) cur_alt_setting;

  // if (usb_microphone_tx_post_load_handler)
  // {
  //   usb_microphone_tx_post_load_handler(rhport, n_bytes_copied, itf, ep_in, cur_alt_setting);
  // }

  return true;
}

// Callback invoked when a new value is set for a control
bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const * p_request) {
    (void) rhport;
    (void) p_request;
    return true;
}

#else // CFG_TUD_AUDIO

void usb_audio_init(void) {
    // Stub
}
bool usb_audio_push_sample(int16_t left, int16_t right) {
    (void)left;
    (void)right;
    return false;
}

#endif // CFG_TUD_AUDIO
