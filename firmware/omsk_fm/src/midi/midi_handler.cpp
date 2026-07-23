#include "midi_handler.h"
#include "../sw_config.h"
#include "../ui/ui_state.h"
#include "../ui/ui_logic.h"
#include "../synth/synth.h"
#include "../synth/fm_synth.h"
#include "midi_uart.h"
#include "midi_map.h"
#include <string.h>

#if CFG_ENABLE_USB_MIDI
#include "tusb.h"
#endif

static uint8_t midi_status = 0;
static uint8_t midi_data_bytes[2] = {0};
static uint8_t midi_data_count = 0;
static bool midi_in_sysex = false;

// SysEx accumulation buffer (max 4104 bytes for 32-voice bank)
static uint8_t sysex_buffer[4200];
static uint16_t sysex_count = 0;

static void parse_sysex(const uint8_t *buf, uint16_t len) {
  if (len < 6) return;

  // Yamaha ID = 0x43
  if (buf[1] != 0x43) return;

  uint8_t substatus = (buf[2] >> 4) & 0x07;
  uint8_t channel = buf[2] & 0x0F;

  if (channel != params.midi_channel) return;

  // Bulk presets import
  if (substatus == 0) { // Substatus = 0 (bulk dump)
    uint8_t format = buf[3];
    if (format == 9 && len >= 4104) { // 32 voice bank
      // Parse 32 voices
      for (int i = 0; i < 32; i++) {
        dx7_unpack_voice(&buf[6 + i * 128], &g_presets[i]);
      }
      g_active_patch = g_presets[0];
      fm_synth_set_patch(0);
      ui_set_status("BANK LOADED", 2000);
      g_oled_dirty = true;
    } else if (format == 0 && len >= 161) { // Single voice dump
      const uint8_t *data = (len >= 163) ? &buf[6] : &buf[4];
      dx7_unpack_unpacked_voice(data, &g_active_patch);
      ui_set_status("PATCH LOADED", 2000);
      g_oled_dirty = true;
    }
  } else if (substatus == 1) { // Parameter change
    // F0 43 1n 12 [Param Group] [Value] F7
    if (len >= 6 && buf[3] == 18) { // Group 18 = Voice parameter
      uint8_t param_idx = buf[4];
      uint8_t value = buf[5];
      // Map to FmPatch fields
      // For simplicity, we can unpack/pack or write parameter change directly.
      g_oled_dirty = true;
    }
  }
}

void midi_process_byte(uint8_t b) {
  if (b >= 0xF8) { // Real-time clock messages
    return;
  }

  if (b & 0x80) {
    if (b == 0xF0) {
      midi_in_sysex = true;
      sysex_count = 0;
      sysex_buffer[sysex_count++] = b;
      return;
    }
    if (b == 0xF7) {
      if (midi_in_sysex && sysex_count < sizeof(sysex_buffer)) {
        sysex_buffer[sysex_count++] = b;
        parse_sysex(sysex_buffer, sysex_count);
      }
      midi_in_sysex = false;
      return;
    }
    midi_status = b;
    midi_data_count = 0;
    return;
  }

  if (midi_in_sysex) {
    if (sysex_count < sizeof(sysex_buffer)) {
      sysex_buffer[sysex_count++] = b;
    }
    return;
  }

  if (!midi_status) return;

  uint8_t type = midi_status & 0xF0;
  if (type == 0xC0 || type == 0xD0) {
    midi_data_bytes[midi_data_count++] = b & 0x7F;
    if (midi_data_count >= 1) {
      midi_map_process(midi_status, midi_data_bytes[0], 0);
      midi_data_count = 0;
    }
    return;
  }

  midi_data_bytes[midi_data_count++] = b & 0x7F;
  if (midi_data_count >= 2) {
    midi_map_process(midi_status, midi_data_bytes[0], midi_data_bytes[1]);
    midi_data_count = 0;
  }
}

#if CFG_ENABLE_JACK_MIDI
void midi_init(void) {
  midi_uart_init();
  midi_status = 0;
  midi_data_count = 0;
}

void midi_poll(void) {
  uint8_t b;
  while (midi_uart_read_byte(&b)) {
    midi_process_byte(b);
  }
}
#else
void midi_init(void) {}
void midi_poll(void) {}
#endif

void midi_send_message(const uint8_t *msg, uint8_t length) {
#if CFG_ENABLE_USB_MIDI
  if (tud_midi_mounted()) {
    tud_midi_stream_write(0, msg, length);
  }
#endif
#if CFG_ENABLE_JACK_MIDI
  for (uint8_t i = 0; i < length; i++) {
    midi_uart_write_byte(msg[i]);
  }
#endif
}
