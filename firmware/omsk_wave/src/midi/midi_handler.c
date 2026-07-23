#include "midi_handler.h"
#include "../sw_config.h"
#include "../ui/ui_state.h"
#include "midi_uart.h"
#include "midi_map.h"
#include <stdbool.h>

#if CFG_ENABLE_USB_MIDI
#include "tusb.h"
#endif

#if CFG_ENABLE_SEQUENCER
#include "../sequencer/sequencer.h"
#endif

extern void update_activity(void);

static uint8_t midi_status;
static uint8_t midi_data_bytes[2];
static uint8_t midi_data_count;
static bool midi_in_sysex;

static void midi_handle_message(uint8_t status, uint8_t d1, uint8_t d2) {
  uint8_t type = status & 0xF0;
  if ((type == 0x90 || type == 0x80) && (ui_mode == UI_MODE_SEQ || ui_mode == UI_MODE_SEQ_EDIT)) {
    return;
  }
  midi_map_process(status, d1, d2);
}

void midi_process_byte(uint8_t b) {
#if CFG_ENABLE_MIDI_THRU
#if CFG_ENABLE_JACK_MIDI
  midi_uart_write_byte(b);
#endif
#endif

  // Real-Time Messages (0xF8 - 0xFF) - pass to sequencer immediately
  // Do NOT call update_activity() for these — DAW sends 24+ clock ticks/beat,
  // triggering get_absolute_time() on every byte would stall the MIDI parser.
  if (b >= 0xF8) {
#if CFG_ENABLE_SEQUENCER
    seq_process_midi_byte(b);
#endif
    return;
  }

#if CFG_SLEEP_TIMEOUT_MIN > 0
  // Only real MIDI events (notes, CCs, etc.) count as user activity
  update_activity();
#endif

  if (b & 0x80) {
    if (b == 0xF0) {
      midi_in_sysex = true;
      midi_status = 0;
      midi_data_count = 0;
      return;
    }
    if (b == 0xF7) {
      midi_in_sysex = false;
      return;
    }
    if (b >= 0xF1) {
      return;
    }
    midi_status = b;
    midi_data_count = 0;
    return;
  }
  if (midi_in_sysex) {
    return;
  }
  if (!midi_status) {
    return;
  }
  uint8_t type = midi_status & 0xF0;
  if (type == 0xC0 || type == 0xD0) {
    midi_data_bytes[midi_data_count++] = b & 0x7F;
    if (midi_data_count >= 1) {
      midi_handle_message(midi_status, midi_data_bytes[0], 0);
      midi_data_count = 0;
    }
    return;
  }
  midi_data_bytes[midi_data_count++] = b & 0x7F;
  if (midi_data_count >= 2) {
    midi_handle_message(midi_status, midi_data_bytes[0], midi_data_bytes[1]);
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
