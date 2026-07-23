#include "sw_config.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "synth/audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tables/omsk_wavetables.h"
#if CFG_ENABLE_USB_MIDI
#include "tusb.h"
#endif
void usb_midi_init(void);
void usb_midi_task(void);
#include "../../shared/hardware/matrix.h"
#include "../../shared/hardware/encoders.h"
#include "../../shared/hardware/colors.h"
#include "leds/leds.h"
#include "midi/midi_map.h"
#include "synth/pra_synth.h"
#include "synth/synth.h"
#include "ui/ui_oled.h"
#include "ui/ui_state.h"

extern volatile uint32_t pra_synth_sample_count;

static void system_overclock_if_enabled(void) {
#if CFG_ENABLE_OVERCLOCK
#if PICO_RP2040
  set_sys_clock_khz(CFG_OVERCLOCK_RP2040_KHZ, true);
#elif PICO_RP2350
  set_sys_clock_khz(CFG_OVERCLOCK_RP2350_KHZ, true);
#endif
#endif
}

#include "midi/midi_handler.h"

// --- Globals ---
#include "ui/ui_logic.h"

#if CFG_SLEEP_TIMEOUT_MIN > 0
static uint32_t last_activity_time_ms = 0;
static bool is_sleeping = false;

void update_activity(void) {
  if (is_sleeping) {
    is_sleeping = false;
    ui_oled_set_power(true);
  }
  last_activity_time_ms = to_ms_since_boot(get_absolute_time());
}
#endif

int main() {
  system_overclock_if_enabled();
  stdio_init_all();

  init_matrix();
  init_encoders();
  leds_init();
  audio_init();
  synth_init();
  audio_start();
  midi_init();
  midi_map_init();
#if CFG_ENABLE_SEQUENCER
#include "sequencer/sequencer.h"
  seq_init();
#endif
#if CFG_ENABLE_USB
  usb_midi_init();
#endif

  // Send initial modulation matrix state for emulator sync
  for (int p = 0; p < PARAM_COUNT; p++) {
    for (int s = 0; s < SRC_COUNT; s++) {
      if (params.mod_matrix[p][s] != 0) {
        // Determine target module and index
        // This is a bit tedious, but we can just loop through common modules
        // Or simplified: just log that source S is active for P
        int8_t depth = (int8_t)params.mod_matrix[p][s] - 64;
        if (depth != 0) {
          DBG_PRINTF("MOD_INIT %d %d %d\n", p, s, (int)params.mod_matrix[p][s]);
        }
      }
    }
  }

  // Init Active Notes
  for (int i = 0; i < 16; i++)
    active_notes[i] = -1;
  for (int i = 0; i < 16; i++) {
    preset_hold_start[i] = 0;
    preset_hold_used[i] = false;
  }

  ui_mode = UI_MODE_PIANO;
  selected_module = MOD_VCO1;
  target_module = MOD_VCO1;
  
  // Initialize critical parameters to safe defaults
  params.vco1_transpose = 5; // 0 oct
  params.vco2_transpose = 5; // 0 oct
  params.vco1_wave = 0;
  params.vco2_wave = 0;
  params.arp_mode = 0;
  params.arp_rate = 12; // 1/16
  params.arp_oct = 3;   // 0 oct shift
  params.amp_gain = 100; // Default master volume
  
  // Push initial values to synth engine
  pra_synth_param_change(PARAM_VCO1_TRANSPOSE, params.vco1_transpose);
  pra_synth_param_change(PARAM_VCO2_TRANSPOSE, params.vco2_transpose);
  pra_synth_param_change(PARAM_VCO1_WAVE, params.vco1_wave);
  pra_synth_param_change(PARAM_VCO2_WAVE, params.vco2_wave);
  pra_synth_param_change(PARAM_ARP_MODE, params.arp_mode);
  pra_synth_param_change(PARAM_ARP_RATE, params.arp_rate);
  pra_synth_param_change(PARAM_ARP_OCT, params.arp_oct);
  pra_synth_param_change(PARAM_AMP_GAIN, params.amp_gain);

  ui_set_status("VCO1", 2000);

  uint32_t last_scan = 0;
  uint32_t last_debug = 0;

  // Send welcome packet for emulator
  printf("STEPATENO_OLED_READY\n");

  // Start OLED after other systems
  // (Moved to loop for delayed init)

  uint32_t startup_time = time_us_32();
  bool oled_initialized = false;

  while (1) {
    uint32_t now = time_us_32();

#if CFG_SLEEP_TIMEOUT_MIN > 0
    if (!is_sleeping) {
      uint32_t now_ms = to_ms_since_boot(get_absolute_time());
      if (now_ms - last_activity_time_ms > (CFG_SLEEP_TIMEOUT_MIN * 60 * 1000)) {
        is_sleeping = true;
        ui_oled_set_power(false);
      }
    }
#endif

    // Delayed OLED Init: wait 0.5 seconds after boot
#if CFG_ENABLE_OLED
    if (!oled_initialized && (now - startup_time > 500000)) {
#if 0
      // Turn board cyan to indicate OLED Init attempt
      leds_set_all(0, 255, 255);
      leds_show();

      ui_oled_init();
      oled_initialized = true;

      // Turn board green to indicate OLED Init success
      leds_set_all(0, 255, 0);
      leds_show();
#else
      ui_oled_init();
      oled_initialized = true;
#endif
    }
#endif

    if (now - last_debug > 1000000) {
      last_debug = now;
    }

    midi_poll();
    
    // Update control at ~1kHz (every 1ms) to reduce mutex contention with Core 1
    static uint32_t last_control_update = 0;
    if (now - last_control_update > 1000) {
      last_control_update = now;
      pra_synth_update_control(); // Core 0 handle envelopes, LFOs, and MIDI commands
    }

#if CFG_ENABLE_SEQUENCER
    seq_poll();
#endif
#if CFG_ENABLE_USB
    usb_midi_task();
    audio_usb_task(); // Push buffered audio to USB
#endif
#if CFG_ENABLE_USB_MIDI
    uint8_t buf[64];
    int n;
    while ((n = tud_midi_stream_read(buf, sizeof(buf))) > 0) {
      for (int i = 0; i < n; i++) {
        midi_process_byte(buf[i]);
      }
    }
#endif

    // 1. Encoders
    scan_encoders();
    int d1 = encoders_get_delta(0);
    int d2 = encoders_get_delta(1);
    int d3 = encoders_get_delta(2);
    int d4 = encoders_get_delta(3);
    int8_t ext_enc[8];
    midi_map_consume_encoder_deltas(ext_enc);
    d1 += ext_enc[0];
    d2 += ext_enc[1];
    d3 += ext_enc[2];
    d4 += ext_enc[3];

#if CFG_SLEEP_TIMEOUT_MIN > 0
    if (d1 || d2 || d3 || d4 || ext_enc[4] || ext_enc[5] || ext_enc[6] ||
        ext_enc[7]) {
      update_activity();
    }
#endif

    uint8_t modwheel = 0;
    if ((set_mode_active || set_button_held) &&
        midi_map_consume_modwheel(&modwheel)) {
      if (set_context_src_override == SRC_MODWHEEL) {
          for (int p = 0; p < PARAM_COUNT; p++) {
              params.mod_matrix[p][SRC_MODWHEEL] = 64;
              if (params.mod_source_assigned[p] == SRC_MODWHEEL) {
                  params.mod_source_assigned[p] = 0xFF;
              }
          }
          ui_set_status("MODWH CLEARED", 1500);
          DBG_PRINTF("ModWheel assignments CLEARED via SET+Move\n");
      } else {
          set_context_module = MOD_MOD;
          last_mod_source = MOD_MOD;
          set_context_src_override = SRC_MODWHEEL;
          DBG_PRINTF("ModWheel selected as Source\n");
      }
    }

    if (fn_button_held && midi_map_consume_modwheel(&modwheel)) {
      for (int p = 0; p < PARAM_COUNT; p++) {
        params.mod_matrix[p][SRC_MODWHEEL] = 64;
      }
      DBG_PRINTF("ModWheel assignments CLEARED\n");
    }

    uint8_t aftertouch = 0;
    if (set_mode_active && midi_map_consume_aftertouch(&aftertouch)) {
      set_context_module = MOD_MOD;
      last_mod_source = MOD_MOD;
      set_context_src_override = SRC_AFTERTOUCH;
    }

    uint8_t breath = 0;
    if (set_mode_active && midi_map_consume_breath(&breath)) {
      set_context_module = MOD_MOD;
      last_mod_source = MOD_MOD;
      set_context_src_override = SRC_BREATH;
    }

    // Encoders always control the currently selected module's parameters
    handle_params_encoders(d1, d2, d3, d4);

    handle_params_encoders_lower_row(ext_enc[4], ext_enc[5], ext_enc[6],
                                     ext_enc[7]);

#if CFG_ENABLE_DEBUG
    log_panel_changes_for_module(selected_module, d1, d2, d3, d4);
    ModuleID m = get_module_below(selected_module);
    if (m != MOD_NONE) {
      log_panel_changes_for_module(m, ext_enc[4], ext_enc[5], ext_enc[6],
                                   ext_enc[7]);
    }
#endif

    // MIDI pad events are now merged into current_keys below

    // 2. Matrix
    if (now - last_scan > 10000) {
      last_scan = now;
      scan_matrix();
      uint16_t current_keys = 0;
      for(int i=0; i<16; i++) {
        if (matrix_debounced[i]) {
          current_keys |= (1 << i);
        }
      }
      current_keys |= midi_pad_state;

      // Toggle Logic
      static bool prev_combo = false;
      bool combo = (current_keys & (1 << 12)) && (current_keys & (1 << 13));
      // Handled inside ui_handle_pad_pressed
      prev_combo = combo;

      uint16_t pressed = current_keys & ~held_keys;
      uint16_t released = held_keys & ~current_keys;

#if CFG_SLEEP_TIMEOUT_MIN > 0
      if (pressed || released) {
        update_activity();
      }
#endif

      // Handle logical Pad events for Params Mode / Mirroring
      for (int i = 0; i < 16; i++) {
        if (pressed & (1 << i))
          ui_handle_pad_pressed(i);
        if (released & (1 << i))
          ui_handle_pad_released(i);
      }

      // Handle Preset Mode Activation is now in ui_logic.c via LFO1+EG1 release

      // Preset long-press saving logic - use milliseconds
      ui_process_preset_longpress(to_ms_since_boot(get_absolute_time()));

      if (ui_mode == UI_MODE_PIANO) {
        // --- PIANO LOGIC handled via ui_handle_pad_pressed ---
      } else {
        // --- PARAMS LOGIC ---
      }

      held_keys = current_keys;
    }

    // Run Arp
    process_arp(now);

    // 3. LEDs
    static uint32_t last_led_update = 0;
    if (now - last_led_update > 16666) { // ~60Hz
      last_led_update = now;
      leds_set_all(0, 0, 0);

#if CFG_SLEEP_TIMEOUT_MIN > 0
      if (!is_sleeping) {
#endif
        // Encoders
    uint8_t r, g, b;
    get_encoder_led(0, &r, &g, &b);
    leds_set_pixel(LED_ENCODER_1, r, g, b);
    get_encoder_led(1, &r, &g, &b);
    leds_set_pixel(LED_ENCODER_2, r, g, b);
    get_encoder_led(2, &r, &g, &b);
    leds_set_pixel(LED_ENCODER_3, r, g, b);
    get_encoder_led(3, &r, &g, &b);
    leds_set_pixel(LED_ENCODER_4, r, g, b);

#if CFG_ENABLE_SEQUENCER
    if (ui_mode == UI_MODE_SEQ) {
        // Transport state
        if (seq_state.is_playing) leds_set_pixel(LED_MODE_1, COLOR_SEQ_PLAY_R, COLOR_SEQ_PLAY_G, COLOR_SEQ_PLAY_B);
        else leds_set_pixel(LED_MODE_1, COLOR_SEQ_STOP_R, COLOR_SEQ_STOP_G, COLOR_SEQ_STOP_B);
        
        // Navigation
        if (seq_state.current_page == 0) leds_set_pixel(LED_MODE_2, COLOR_SEQ_PAGE0_R, COLOR_SEQ_PAGE0_G, COLOR_SEQ_PAGE0_B);
        else if (seq_state.current_page == 1) leds_set_pixel(LED_MODE_2, COLOR_SEQ_PAGE1_R, COLOR_SEQ_PAGE1_G, COLOR_SEQ_PAGE1_B);
        else if (seq_state.current_page == 2) leds_set_pixel(LED_MODE_2, COLOR_SEQ_PAGE2_R, COLOR_SEQ_PAGE2_G, COLOR_SEQ_PAGE2_B);
        else if (seq_state.current_page == 3) leds_set_pixel(LED_MODE_2, COLOR_SEQ_PAGE3_R, COLOR_SEQ_PAGE3_G, COLOR_SEQ_PAGE3_B);
    }
#endif

    // Matrix LEDs
    if (ui_mode == UI_MODE_PIANO) {
      for (int i = 0; i < 16; i++) {
        uint8_t r = 0, g = 0, b = 0;
        bool is_active = (held_keys | latched_keys) & (1 << i);
        bool is_midi = (midi_note_mask & (1 << i));
        const char *label = get_piano_key_label_from_index(i);

        if (strcmp(label, "OCT-") == 0) {
          // White at center, increasingly saturated blue when octave < 0
          if (octave < 0) {
            int off = -octave; // 1..4
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
          // White at center, increasingly saturated blue when octave > 0
          if (octave > 0) {
            int off = octave; // 1..4
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
          // Orange, brighter when arp mode or layer is active
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
          // Orange, brighter when ADV layer is active
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
          // Note keys: detect black/white from label
          bool is_black = (strchr(label, '#') != NULL);
          bool note_on = (active_notes[i] != -1 || is_active || is_midi);
          if (is_black) {
            // Black key → dark blue, brighter when active
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
            // White key → white, brighter when active
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
    } else if (ui_mode == UI_MODE_SEQ) {
#if CFG_ENABLE_SEQUENCER
      for (int i = 0; i < 16; i++) {
        uint8_t step_idx = seq_state.current_page * 16 + i;
        SeqStep *step = &current_seq.steps[step_idx];
        
        uint8_t r = 0, g = 0, b = 0;
        
        bool has_enabled_note = false;
        uint8_t prob = step->notes[0].probability;
        for (int j = 0; j < SEQ_MAX_NOTES_PER_STEP; j++) {
            if (step->notes[j].enabled) {
                has_enabled_note = true;
                prob = step->notes[j].probability;
                break;
            }
        }
        
        uint8_t br = 0;
        if (prob >= 100) br = 255;
        else if (prob >= 85) br = 210;
        else if (prob >= 75) br = 190;
        else if (prob >= 50) br = 130;
        else if (prob >= 40) br = 110;
        else if (prob >= 25) br = 70;
        else if (prob >= 10) br = 40;
        else br = 0;

        if (step->stop_flag) {
            // Stop-step: Red
            r = COLOR_SEQ_STEP_STOP_R;
            g = COLOR_SEQ_STEP_STOP_G;
            b = COLOR_SEQ_STEP_STOP_B;
        } else if (!has_enabled_note) {
            if (step->notes[0].note > 0) {
                // Mute: Pink
                r = br; g = br / 5; b = br / 2;
            } else {
                // Empty: Black
                r = 0; g = 0; b = 0;
            }
        } else {
            bool condition_met = true;
            if (step->loop_every > 1) {
                if (step->loop_count < (step->loop_every - 1)) {
                    condition_met = false;
                }
            }
            
            if (condition_met) {
                // Active/Trig: Green
                r = 0; g = br; b = 0;
            } else {
                // Wait: Yellow
                r = br; g = br; b = 0;
            }
        }

        if (step_idx == seq_state.current_step) {
            // Playhead: White
            r = COLOR_SEQ_STEP_PLAYHEAD_R;
            g = COLOR_SEQ_STEP_PLAYHEAD_G;
            b = COLOR_SEQ_STEP_PLAYHEAD_B;
        }
        leds_set_pixel(MATRIX_LED_MAP[i], r, g, b);
      }
#endif
    } else {
      // Params Mode: show module colors
      for (int i = 0; i < 16; i++) {
        ModuleID m = btn_to_mod[i];
        bool is_held = held_keys & (1 << i);
        
        // Base colors at 20% brightness, 70% when held/selected
        uint8_t r = 0, g = 0, b = 0;
        float br = (is_held || m == selected_module) ? 0.70f : 0.20f;
        
        switch (m) {
          case MOD_VCO1:
          case MOD_VCO2:
          case MOD_NOISE: {
            // White 5% default, or filter color if routed
            uint8_t route = 0;
            if (m == MOD_VCO1) route = params.route_vco1;
            else if (m == MOD_VCO2) route = params.route_vco2;
            else if (m == MOD_NOISE) route = params.route_noise;
            
            if (route == 1 || route == 2) { // VCF1 or VCF2 = Filter Color
              r = (uint8_t)(COLOR_FILTER_R * br);
              g = (uint8_t)(COLOR_FILTER_G * br);
              b = (uint8_t)(COLOR_FILTER_B * br);
            } else {
              float dim = (is_held || m == selected_module) ? 0.70f : 0.05f;
              r = (uint8_t)(COLOR_SYS_R * dim);
              g = (uint8_t)(COLOR_SYS_G * dim);
              b = (uint8_t)(COLOR_SYS_B * dim);
            }
            break;
          }
          case MOD_VCF1:
          case MOD_VCF2:
            r = (uint8_t)(COLOR_FILTER_R * br);
            g = (uint8_t)(COLOR_FILTER_G * br);
            b = (uint8_t)(COLOR_FILTER_B * br);
            break;
          case MOD_LFO1:
          case MOD_LFO2:
            r = (uint8_t)(COLOR_LFO_R * br);
            g = (uint8_t)(COLOR_LFO_G * br);
            b = (uint8_t)(COLOR_LFO_B * br);
            break;
          case MOD_EG1:
          case MOD_EG2:
            r = (uint8_t)(COLOR_EG_R * br);
            g = (uint8_t)(COLOR_EG_G * br);
            b = (uint8_t)(COLOR_EG_B * br);
            break;
          case MOD_SET:
            r = (uint8_t)(COLOR_SET_R * br);
            g = (uint8_t)(COLOR_SET_G * br);
            b = (uint8_t)(COLOR_SET_B * br);
            break;
          case MOD_FX1:
          case MOD_FX2:
            r = (uint8_t)(COLOR_FX_R * br);
            g = (uint8_t)(COLOR_FX_G * br);
            b = (uint8_t)(COLOR_FX_B * br);
            break;
          case MOD_GLIDE:
            r = (uint8_t)(COLOR_GLIDE_R * br);
            g = (uint8_t)(COLOR_GLIDE_G * br);
            b = (uint8_t)(COLOR_GLIDE_B * br);
            break;
          case MOD_MOD:
            r = (uint8_t)(COLOR_MOD_R * br);
            g = (uint8_t)(COLOR_MOD_G * br);
            b = (uint8_t)(COLOR_MOD_B * br);
            break;
          case MOD_FN:
            r = (uint8_t)(COLOR_FN_R * br);
            g = (uint8_t)(COLOR_FN_G * br);
            b = (uint8_t)(COLOR_FN_B * br);
            break;
          case MOD_MIXER:
            r = (uint8_t)(COLOR_MIXER_R * br);
            g = (uint8_t)(COLOR_MIXER_G * br);
            b = (uint8_t)(COLOR_MIXER_B * br);
            break;
          default:
            r = (uint8_t)(12 * br); g = (uint8_t)(12 * br); b = (uint8_t)(12 * br); break;
        }
        
        leds_set_pixel(MATRIX_LED_MAP[i], r, g, b);
      }
    }

    // Special Preset Mode LED Feedback (Overlays current state)
    if (preset_mode_active) {
        // Blink all pads in a soft yellow/white to indicate preset selection mode
        static uint32_t last_blink = 0;
        static bool blink_state = false;
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - last_blink > 200) {
            last_blink = now_ms;
            blink_state = !blink_state;
        }
        if (blink_state) {
            for (int i = 0; i < 16; i++) {
                leds_set_pixel(MATRIX_LED_MAP[i], COLOR_PRESET_BLINK_R, COLOR_PRESET_BLINK_G, COLOR_PRESET_BLINK_B);
            }
        }
    }
#if CFG_SLEEP_TIMEOUT_MIN > 0
      }
#endif

      leds_show();
    }

    // OLED Display Update
    // Static text or page-based UI, updated at ~25Hz to prevent blocking the main loop
    static uint32_t last_oled_update = 0;
    if (now - last_oled_update > 40000) { // 40ms = 25Hz
      last_oled_update = now;
#if CFG_SLEEP_TIMEOUT_MIN > 0
      if (!is_sleeping) {
#endif
        OledPage page;
        build_oled_page(&page);
        ui_oled_draw(&page);
#if CFG_SLEEP_TIMEOUT_MIN > 0
      }
#endif
    }

    // Audio process handled by IRQ usually, but if main loop needs to do
    // anything: nothing.

    // sleep_ms(1);
  }

  return 0;
}