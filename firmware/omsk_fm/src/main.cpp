#include "sw_config.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../shared/hardware/matrix.h"
#include "../../shared/hardware/encoders.h"
#include "../../shared/hardware/colors.h"
#include "leds/leds.h"
#include "midi/midi_map.h"
#include "midi/midi_handler.h"
#include "synth/audio.h"
#include "synth/synth.h"
#include "synth/fm_synth.h"
#include "ui/ui_oled.h"
#include "ui/ui_state.h"
#include "ui/ui_logic.h"
#include "sequencer/sequencer.h"

#if CFG_ENABLE_USB_MIDI
#include "tusb.h"
void usb_midi_init(void);
void usb_midi_task(void);
#endif

bool encoder_timer_callback(repeating_timer_t *rt) {
  scan_encoders();
  return true;
}

static void system_overclock_if_enabled(void) {
#if CFG_ENABLE_OVERCLOCK
  set_sys_clock_khz(CFG_OVERCLOCK_RP2350_KHZ, true);
#endif
}

int main() {
  system_overclock_if_enabled();
  stdio_init_all();
  printf("\n=== OMSK FM STARTUP ===\n");
  printf("SYS CLOCK: %lu kHz\n", (unsigned long)(clock_get_hz(clk_sys) / 1000));

  init_matrix();
  init_encoders();
  leds_init();


  audio_init();
  midi_init();
  midi_map_init();
  seq_init();
  ui_init();

#if CFG_ENABLE_USB_MIDI
  usb_midi_init();
#endif

  audio_start();

  // Start encoder timer interrupt (every 1 ms / 1000 Hz)
  static repeating_timer_t enc_timer;
  add_repeating_timer_ms(-1, encoder_timer_callback, NULL, &enc_timer);

  uint32_t last_scan = 0;
  uint32_t startup_time = time_us_32();
  bool oled_initialized = false;

  while (1) {
    uint32_t now = time_us_32();

    if (!oled_initialized && (now - startup_time > 500000)) {
      ui_oled_init();
      oled_initialized = true;
    }

    midi_poll();

#if CFG_ENABLE_USB_MIDI
    usb_midi_task();
    uint8_t buf[64];
    int n;
    while ((n = tud_midi_stream_read(buf, sizeof(buf))) > 0) {
      for (int i = 0; i < n; i++) {
        midi_process_byte(buf[i]);
      }
    }
#endif

    // Encoders read delta (scanned asynchronously in timer interrupt)
    int d1 = encoders_get_delta(0);
    int d2 = encoders_get_delta(1);
    int d3 = encoders_get_delta(2);
    int d4 = encoders_get_delta(3);
    if (d1 || d2 || d3 || d4) {
      handle_params_encoders(d1, d2, d3, d4);
    }

    // Matrix scan
    if (now - last_scan > 10000) {
      last_scan = now;
      scan_matrix();
      uint16_t current_keys = 0;
      for (int i = 0; i < 16; i++) {
        if (matrix_debounced[i]) {
          current_keys |= (1 << i);
        }
      }
      uint16_t pressed = current_keys & ~held_keys;
      uint16_t released = held_keys & ~current_keys;

      for (int i = 0; i < 16; i++) {
        if (pressed & (1 << i)) ui_handle_pad_pressed(i);
        if (released & (1 << i)) ui_handle_pad_released(i);
      }
      held_keys = current_keys;
    }

    // LED indicators
    static uint32_t last_led_update = 0;
    if (now - last_led_update > 16666) {
      last_led_update = now;
      leds_set_all(0, 0, 0);

      OledPage temp_page;
      build_oled_page(&temp_page);

      // Encoders LEDs
      uint8_t r, g, b;
      get_encoder_led(&temp_page, 0, &r, &g, &b);
      leds_set_pixel(LED_ENCODER_1, r, g, b);
      get_encoder_led(&temp_page, 1, &r, &g, &b);
      leds_set_pixel(LED_ENCODER_2, r, g, b);
      get_encoder_led(&temp_page, 2, &r, &g, &b);
      leds_set_pixel(LED_ENCODER_3, r, g, b);
      get_encoder_led(&temp_page, 3, &r, &g, &b);
      leds_set_pixel(LED_ENCODER_4, r, g, b);

#if CFG_ENABLE_SEQUENCER
      if (ui_mode == UI_MODE_SEQ) {
        if (seq_state.is_playing) leds_set_pixel(LED_MODE_1, COLOR_SEQ_PLAY_R, COLOR_SEQ_PLAY_G, COLOR_SEQ_PLAY_B);
        else leds_set_pixel(LED_MODE_1, COLOR_SEQ_STOP_R, COLOR_SEQ_STOP_G, COLOR_SEQ_STOP_B);
        
        if (seq_state.current_page == 0) leds_set_pixel(LED_MODE_2, COLOR_SEQ_PAGE0_R, COLOR_SEQ_PAGE0_G, COLOR_SEQ_PAGE0_B);
        else if (seq_state.current_page == 1) leds_set_pixel(LED_MODE_2, COLOR_SEQ_PAGE1_R, COLOR_SEQ_PAGE1_G, COLOR_SEQ_PAGE1_B);
        else if (seq_state.current_page == 2) leds_set_pixel(LED_MODE_2, COLOR_SEQ_PAGE2_R, COLOR_SEQ_PAGE2_G, COLOR_SEQ_PAGE2_B);
        else if (seq_state.current_page == 3) leds_set_pixel(LED_MODE_2, COLOR_SEQ_PAGE3_R, COLOR_SEQ_PAGE3_G, COLOR_SEQ_PAGE3_B);
      }
#endif

      if (ui_mode == UI_MODE_PIANO) {
        for (int i = 0; i < 16; i++) {
          uint8_t r = 0, g = 0, b = 0;
          bool is_active = (held_keys | latched_keys) & (1 << i);
          bool is_midi = (midi_note_mask & (1 << i));
          const char *label = get_piano_key_label_from_index(i);

          if (strcmp(label, "OCT-") == 0) {
            if (octave < 0) {
              int off = -octave;
              r = (uint8_t)(off * 10 < 40 ? 40 - off * 10 : 0);
              g = r;
              b = (uint8_t)(40 + off * 50 > 255 ? 255 : 40 + off * 50);
            } else {
              r = 40; g = 40; b = 40;
            }
            if (is_active) {
              int nr = r + 40, ng = g + 40, nb = b + 40;
              r = nr > 255 ? 255 : (uint8_t)nr;
              g = ng > 255 ? 255 : (uint8_t)ng;
              b = nb > 255 ? 255 : (uint8_t)nb;
            }
          } else if (strcmp(label, "OCT+") == 0) {
            if (octave > 0) {
              int off = octave;
              r = (uint8_t)(off * 10 < 40 ? 40 - off * 10 : 0);
              g = r;
              b = (uint8_t)(40 + off * 50 > 255 ? 255 : 40 + off * 50);
            } else {
              r = 40; g = 40; b = 40;
            }
            if (is_active) {
              int nr = r + 40, ng = g + 40, nb = b + 40;
              r = nr > 255 ? 255 : (uint8_t)nr;
              g = ng > 255 ? 255 : (uint8_t)ng;
              b = nb > 255 ? 255 : (uint8_t)nb;
            }
          } else if (strcmp(label, "ARP") == 0) {
            bool layer_on = (params.arp_mode > 0) || (selected_module == MOD_ARP);
            if (layer_on || is_active) {
              r = (uint8_t)(COLOR_EG_R * 0.8f);
              g = (uint8_t)(COLOR_EG_G * 0.6f);
              b = 0;
            } else {
              r = (uint8_t)(COLOR_EG_R * 0.15f);
              g = (uint8_t)(COLOR_EG_G * 0.12f);
              b = 0;
            }
          } else if (strcmp(label, "ADV") == 0) {
            bool layer_on = (selected_module == MOD_ADV);
            if (layer_on || is_active) {
              r = (uint8_t)(COLOR_EG_R * 0.8f);
              g = (uint8_t)(COLOR_EG_G * 0.6f);
              b = 0;
            } else {
              r = (uint8_t)(COLOR_EG_R * 0.15f);
              g = (uint8_t)(COLOR_EG_G * 0.12f);
              b = 0;
            }
          } else {
            bool is_black = (strchr(label, '#') != NULL);
            bool note_on = (active_notes[i] != -1 || is_active || is_midi);
            if (is_black) {
              if (note_on) {
                r = COLOR_PIANO_BLACK_ACTIVE_R;
                g = COLOR_PIANO_BLACK_ACTIVE_G;
                b = COLOR_PIANO_BLACK_ACTIVE_B;
              } else {
                r = COLOR_PIANO_BLACK_INACTIVE_R;
                g = COLOR_PIANO_BLACK_INACTIVE_G;
                b = COLOR_PIANO_BLACK_INACTIVE_B;
              }
            } else {
              if (note_on) {
                r = COLOR_PIANO_ACTIVE_R;
                g = COLOR_PIANO_ACTIVE_G;
                b = COLOR_PIANO_ACTIVE_B;
              } else {
                r = COLOR_PIANO_INACTIVE_R;
                g = COLOR_PIANO_INACTIVE_G;
                b = COLOR_PIANO_INACTIVE_B;
              }
            }
          }
          leds_set_pixel(MATRIX_LED_MAP[i], r, g, b);
        }
      } else {
        for (int i = 0; i < 16; i++) {
          ModuleID m = btn_to_mod[i];
          uint8_t r = 0, g = 0, b = 0;
          float br = (m == selected_module) ? 0.70f : 0.20f;
          
          if (m >= MOD_OP1 && m <= MOD_OP6) {
            uint8_t op_idx = m - MOD_OP1;
            
            bool op_active = g_active_patch.op[op_idx].active;
            bool is_selected = (active_op == op_idx);
            
            if (op_active) {
              r = 0;
              g = is_selected ? 255 : 50;
              b = 0;
            } else {
              r = is_selected ? 255 : 50;
              g = 0;
              b = 0;
            }
          } else {
            switch (m) {
              case MOD_FREQ:
                r = (uint8_t)(COLOR_VCO_R * br); g = (uint8_t)(COLOR_VCO_G * br); b = (uint8_t)(COLOR_VCO_B * br);
                break;
              case MOD_LVL_MOD:
                r = (uint8_t)(COLOR_MOD_R * br); g = (uint8_t)(COLOR_MOD_G * br); b = (uint8_t)(COLOR_MOD_B * br);
                break;
              case MOD_LFO:
                r = (uint8_t)(COLOR_LFO_R * br); g = (uint8_t)(COLOR_LFO_G * br); b = (uint8_t)(COLOR_LFO_B * br);
                break;
              case MOD_EG:
                r = (uint8_t)(COLOR_EG_R * br); g = (uint8_t)(COLOR_EG_G * br); b = (uint8_t)(COLOR_EG_B * br);
                break;
              case MOD_KBDSCALE:
                r = (uint8_t)(COLOR_GLIDE_R * br); g = (uint8_t)(COLOR_GLIDE_G * br); b = (uint8_t)(COLOR_GLIDE_B * br);
                break;
              case MOD_FILT:
                r = (uint8_t)(COLOR_FILTER_R * br); g = (uint8_t)(COLOR_FILTER_G * br); b = (uint8_t)(COLOR_FILTER_B * br);
                break;
              case MOD_ALGO_FB:
                r = (uint8_t)(COLOR_MIXER_R * br); g = (uint8_t)(COLOR_MIXER_G * br); b = (uint8_t)(COLOR_MIXER_B * br);
                break;
              case MOD_PITCH_EG:
                r = (uint8_t)(COLOR_EG_R * br); g = (uint8_t)(COLOR_EG_G * br); b = (uint8_t)(COLOR_EG_B * br);
                break;
              case MOD_MEM:
                r = (uint8_t)(COLOR_FX_R * br); g = (uint8_t)(COLOR_FX_G * br); b = (uint8_t)(COLOR_FX_B * br);
                break;
              case MOD_SYS:
                r = (uint8_t)(COLOR_SYS_R * br); g = (uint8_t)(COLOR_SYS_G * br); b = (uint8_t)(COLOR_SYS_B * br);
                break;
              case MOD_ARP:
                r = (uint8_t)(COLOR_ARP_JIT_R * br); g = (uint8_t)(COLOR_ARP_JIT_G * br); b = (uint8_t)(COLOR_ARP_JIT_B * br);
                break;
              case MOD_ADV:
                r = (uint8_t)(COLOR_SYS_R * br); g = (uint8_t)(COLOR_SYS_G * br); b = (uint8_t)(COLOR_SYS_B * br);
                break;
              default:
                break;
            }
          }
          leds_set_pixel(MATRIX_LED_MAP[i], r, g, b);
        }
      }
      leds_show();
    }

    // Status message expiration check
    uint32_t now_ms = now / 1000;
    if (ui_status_msg[0] != '\0' && now_ms >= ui_status_msg_timeout_ms) {
      ui_status_msg[0] = '\0';
      g_oled_dirty = true;
    }

    // OLED display update
    static uint32_t last_oled_update = 0;
    if (g_oled_dirty && (now - last_oled_update > 40000)) {
      last_oled_update = now;
      if (oled_initialized) {
        OledPage page;
        build_oled_page(&page);
        ui_oled_draw(&page);
      }
    }
  }

  return 0;
}
