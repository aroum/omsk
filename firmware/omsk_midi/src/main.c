#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "ws2812.pio.h"
#include "tusb.h"
#include "sw_config.h"
#include "matrix.h"
#include "encoders.h"
#include "midi_uart.h"

//--------------------------------------------------------------------+
// MIDI Jack Helpers
//--------------------------------------------------------------------+
void send_midi_message(uint8_t const* msg, uint8_t length) {
#if CFG_ENABLE_USB_MIDI
    tud_midi_stream_write(0, msg, length);
#endif

#if CFG_ENABLE_JACK_MIDI
    for (uint8_t i = 0; i < length; i++) {
        midi_uart_write_byte(msg[i]);
    }
#endif
}

void midi_init(void) {
#if CFG_ENABLE_JACK_MIDI
    midi_uart_init();
#endif
}

void midi_poll(void) {
#if CFG_ENABLE_JACK_MIDI
    uint8_t b;
    while (midi_uart_read_byte(&b)) {
        
#if CFG_ENABLE_MIDI_THRU
        midi_uart_write_byte(b);
#endif

#if CFG_ENABLE_USB_MIDI
        tud_midi_stream_write(0, &b, 1);
#endif
    }
#endif

#if CFG_ENABLE_USB_MIDI
    uint8_t usb_rx_buf[64];
    int n;
    while ((n = tud_midi_stream_read(usb_rx_buf, sizeof(usb_rx_buf))) > 0) {
#if CFG_ENABLE_JACK_MIDI
        for (int i = 0; i < n; i++) {
            midi_uart_write_byte(usb_rx_buf[i]);
        }
#endif
    }
#endif
}

//--------------------------------------------------------------------+
// LED Helpers
//--------------------------------------------------------------------+
static inline void put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)(r) << 8) | ((uint32_t)(g) << 16) | (uint32_t)(b);
}

static inline uint32_t hex_to_grb(uint32_t hex) {
    uint8_t r = ((hex >> 16) & 0xFF) * RGB_BRIGHTNESS_PERCENT / 100;
    uint8_t g = ((hex >> 8) & 0xFF) * RGB_BRIGHTNESS_PERCENT / 100;
    uint8_t b = (hex & 0xFF) * RGB_BRIGHTNESS_PERCENT / 100;
    return urgb_u32(r, g, b);
}

void update_leds(void) {
    for (int i = 0; i < LED_COUNT; i++) {
        put_pixel(hex_to_grb(LED_COLORS[i]));
    }
}

//--------------------------------------------------------------------+
// Matrix Button Handling
//--------------------------------------------------------------------+
static bool button_states[BTN_COUNT] = {false};

void poll_matrix(bool* any_pressed) {
    uint8_t channel = MIDI_CHANNEL - 1;

    scan_matrix();

    for (int btn_idx = 0; btn_idx < BTN_COUNT; btn_idx++) {
        bool debounced_state = matrix_debounced[btn_idx];

        if (debounced_state && !button_states[btn_idx]) {
            button_states[btn_idx] = true;
            // Send MIDI Press
            if (BUTTON_CONFIGS[btn_idx].type == MSG_NOTE) {
                uint8_t msg[] = { 0x90 | channel, BUTTON_CONFIGS[btn_idx].index, 100 };
                send_midi_message(msg, 3);
            } else {
                uint8_t msg[] = { 0xB0 | channel, BUTTON_CONFIGS[btn_idx].index, 127 };
                send_midi_message(msg, 3);
            }
        } 
        else if (!debounced_state && button_states[btn_idx]) {
            button_states[btn_idx] = false;
            // Send MIDI Release
            if (BUTTON_CONFIGS[btn_idx].type == MSG_NOTE) {
                uint8_t msg[] = { 0x80 | channel, BUTTON_CONFIGS[btn_idx].index, 0 };
                send_midi_message(msg, 3);
            } else {
                uint8_t msg[] = { 0xB0 | channel, BUTTON_CONFIGS[btn_idx].index, 0 };
                send_midi_message(msg, 3);
            }
        }

        if (button_states[btn_idx]) *any_pressed = true;
    }
}

static int16_t enc_values[ENCODER_COUNT] = {64, 64, 64, 64}; // Start at 64 (center) for absolute
static bool enc_note_states[ENCODER_COUNT] = {false};

void poll_encoders(void) {
    uint8_t channel = MIDI_CHANNEL - 1;
    scan_encoders();

    for (int i = 0; i < ENCODER_COUNT; i++) {
        int delta = encoders_get_delta(i);
        if (delta != 0) {
            // Determine direction: >0 right, <0 left
            if (ENCODER_CONFIGS[i].mode == ENC_MODE_RELATIVE) {
                uint8_t val = (delta > 0) ? 65 : 63;
                if (ENCODER_CONFIGS[i].type == MSG_CC) {
                    uint8_t msg[] = { 0xB0 | channel, ENCODER_CONFIGS[i].index, val };
                    send_midi_message(msg, 3);
                } else {
                    // Relative Note? Send NoteOn then NoteOff to simulate a trigger
                    uint8_t msg_on[] = { 0x90 | channel, ENCODER_CONFIGS[i].index, val };
                    send_midi_message(msg_on, 3);
                    uint8_t msg_off[] = { 0x80 | channel, ENCODER_CONFIGS[i].index, 0 };
                    send_midi_message(msg_off, 3);
                }
            } 
            else { // ENC_MODE_ABSOLUTE
                enc_values[i] += delta;
                if (enc_values[i] > 127) enc_values[i] = 127;
                if (enc_values[i] < 0) enc_values[i] = 0;

                uint8_t midi_val = (uint8_t)enc_values[i];

                if (ENCODER_CONFIGS[i].type == MSG_CC) {
                    uint8_t msg[] = { 0xB0 | channel, ENCODER_CONFIGS[i].index, midi_val };
                    send_midi_message(msg, 3);
                } else {
                    bool state_high = midi_val > 63;
                    if (state_high && !enc_note_states[i]) {
                        enc_note_states[i] = true;
                        uint8_t msg[] = { 0x90 | channel, ENCODER_CONFIGS[i].index, 100 };
                        send_midi_message(msg, 3);
                    } else if (!state_high && enc_note_states[i]) {
                        enc_note_states[i] = false;
                        uint8_t msg[] = { 0x80 | channel, ENCODER_CONFIGS[i].index, 0 };
                        send_midi_message(msg, 3);
                    }
                }
            }
        }
    }
}

//--------------------------------------------------------------------+
// Main
//--------------------------------------------------------------------+
int main(void) {
    stdio_init_all();
#if CFG_ENABLE_USB_MIDI
    tusb_init();
#endif

    // PIO WS2812 Init
    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, PIN_RGB_LED, 800000, false);

    // GPIO Init: Matrix (Shared)
    init_matrix();

    // Encoders Init (Shared)
    init_encoders();

    // Initialize MIDI Jacks (Shared)
    midi_init();

    // GPIO Init: Status LED
#ifdef PIN_STATUS_LED
    gpio_init(PIN_STATUS_LED);
    gpio_set_dir(PIN_STATUS_LED, GPIO_OUT);
    gpio_put(PIN_STATUS_LED, 0);
#endif

    while (1) {
#if CFG_ENABLE_USB_MIDI
        tud_task();
#endif

        // Poll MIDI Jacks and bridge/process MIDI
        midi_poll();
        
        // Use a timer for matrix polling to prevent checking too fast (e.g. 1ms interval)
        static uint32_t last_matrix_poll = 0;
        uint32_t now = to_ms_since_boot(get_absolute_time());
        
        bool any_pressed = false;
        
        if (now - last_matrix_poll >= 1) {
            poll_matrix(&any_pressed);
            last_matrix_poll = now;
        }

        // Poll encoders as fast as possible to catch edges
        poll_encoders();
        
#ifdef PIN_STATUS_LED
        gpio_put(PIN_STATUS_LED, any_pressed);
#endif

        // Update LEDs periodically (50Hz)
        static uint32_t last_led_update = 0;
        if (now - last_led_update > 20) { 
            update_leds();
            last_led_update = now;
        }
    }

    return 0;
}
