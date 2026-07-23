#include "midi_map.h"
#include "../sw_config.h"
#include "../synth/synth.h"
#include "../synth/fm_synth.h"
#include "../ui/ui_logic.h"
#include "../ui/ui_state.h"

void midi_map_init(void) {
  // Init map structures if needed
}

void midi_map_process(uint8_t status, uint8_t d1, uint8_t d2) {
  uint8_t type = status & 0xF0;
  uint8_t ch = status & 0x0F;

  if (ch != params.midi_channel) return;

  if (type == 0x90) { // Note On
    if (d2 > 0) {
      ui_midi_note_on(d1, d2);
    } else {
      ui_midi_note_off(d1);
    }
  } else if (type == 0x80) { // Note Off
    ui_midi_note_off(d1);
  } else if (type == 0xB0) { // CC
    uint8_t cc = d1 & 0x7F;
    uint8_t val = d2 & 0x7F;

    if (cc == 7) { // Volume
      fm_synth_set_master_level(val);
      g_oled_dirty = true;
    } else if (cc == 1) { // Mod Wheel
      g_mod_wheel_val = val;
      g_oled_dirty = true;
    } else if (cc == 5) { // Portamento Time
      g_portamento_time = val;
      g_oled_dirty = true;
    } else if (cc == 65) { // Portamento Enable
      g_portamento_enable = (val >= 64);
      g_oled_dirty = true;
    }
  } else if (type == 0xC0) { // Program Change
    uint8_t program = d1 & 0x7F;
    if (program < 32) {
      fm_synth_set_patch(program);
      g_oled_dirty = true;
    }
  } else if (type == 0xE0) { // Pitch Bend
    uint16_t pb = d1 | (d2 << 7);
    fm_synth_set_pitch_bend(pb);
  }
}
