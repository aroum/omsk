#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "hardware/i2c.h"
#include "tusb.h"
#include "usb/usb_midi.h"
#include "midi_uart.h"

#include "sw_config.h"
#include "synth/synth.h"
#include "ui/ui_state.h"
#include "ui/ui_oled.h"
#include "leds/leds.h"
#include "midi/midi_map.h"
#include "midi_helpers.h"
#include "tables/audio_data.h"
#include "matrix.h"
#include "encoders.h"


// PIO Headers
#include "audio.pio.h"
#include "i2s_tx.pio.h"

// =============================================================================
// GLOBAL STATE
// =============================================================================

Synth synth;
UIState ui;
u8g2_t u8g2;

static uint64_t last_activity_ms = 0;
bool is_sleeping = false;

void update_activity() {
    last_activity_ms = to_ms_since_boot(get_absolute_time());
    if (is_sleeping) {
        is_sleeping = false;
        u8g2_SetPowerSave(&u8g2, 0); // Wake up OLED
    }
}

#define AUDIO_BLOCK_SIZE 64
float audio_out_l[AUDIO_BLOCK_SIZE];
float audio_out_r[AUDIO_BLOCK_SIZE];

// Ring buffer for audio (Core 1 -> Timer Interrupt)
#define RING_BUF_SIZE 1024
float ring_l[RING_BUF_SIZE];
float ring_r[RING_BUF_SIZE];
volatile uint32_t ring_write = 0;
volatile uint32_t ring_read = 0;

#ifdef AUDIO_OUT_DAC
static PIO i2s_pio = pio0;
static uint i2s_sm = 0;
#endif

// =============================================================================
// U8G2 PICO I2C CALLBACKS
// =============================================================================

uint8_t u8x8_byte_pico_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    static uint8_t buffer[32];
    static uint8_t buf_idx;
    switch (msg) {
        case U8X8_MSG_BYTE_SEND:
            if (buf_idx + arg_int < 32) {
                memcpy(buffer + buf_idx, arg_ptr, arg_int);
                buf_idx += arg_int;
            }
            break;
        case U8X8_MSG_BYTE_INIT:
            break;
        case U8X8_MSG_BYTE_SET_DC:
            break;
        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_idx = 0;
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:
            i2c_write_blocking(OLED_I2C, u8x8_GetI2CAddress(u8x8) >> 1, buffer, buf_idx, false);
            break;
        default:
            return 0;
    }
    return 1;
}

uint8_t u8x8_gpio_and_delay_pico(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch (msg) {
        case U8X8_MSG_DELAY_100NANO:
            __asm volatile("nop");
            break;
        case U8X8_MSG_DELAY_10MICRO:
            sleep_us(10);
            break;
        case U8X8_MSG_DELAY_MILLI:
            sleep_ms(arg_int);
            break;
        default:
            return 0;
    }
    return 1;
}

// =============================================================================
// AUDIO OUTPUT: PWM with Oversampling & Wrap ISR / DAC
// =============================================================================

#ifdef AUDIO_OUT_PWM
static uint pwm_slice_l;
static uint pwm_slice_r;
static volatile uint8_t pwm8_current_l = 128;
static volatile uint8_t pwm8_current_r = 128;
static volatile uint8_t pwm8_repeat = 0;
static const uint8_t PWM8_REPEAT_DIV = 8;

static void __not_in_flash_func(pwm_wrap_isr)(void) {
    pwm_clear_irq(pwm_slice_l);
    pwm_set_gpio_level(PIN_AUDIO_PWM_L, pwm8_current_l);
    pwm_set_gpio_level(PIN_AUDIO_PWM_R, pwm8_current_r);
    pwm8_repeat++;
    if (pwm8_repeat >= PWM8_REPEAT_DIV) {
        pwm8_repeat = 0;
        if (ring_read != ring_write) {
            float fl = ring_l[ring_read];
            float fr = ring_r[ring_read];
            ring_read = (ring_read + 1) % RING_BUF_SIZE;
            
            // Map floating point [-1.0, 1.0] to 8-bit [0, 255]
            int32_t il = (int32_t)((fl + 1.0f) * 127.5f);
            int32_t ir = (int32_t)((fr + 1.0f) * 127.5f);
            if (il < 0) il = 0; if (il > 255) il = 255;
            if (ir < 0) ir = 0; if (ir > 255) ir = 255;
            pwm8_current_l = (uint8_t)il;
            pwm8_current_r = (uint8_t)ir;
        } else {
            pwm8_current_l = 128;
            pwm8_current_r = 128;
        }
    }
}
#endif

#ifdef AUDIO_OUT_DAC
bool audio_timer_callback(repeating_timer_t *rt) {
    if (ring_read != ring_write) {
        float fl = ring_l[ring_read];
        float fr = ring_r[ring_read];
        ring_read = (ring_read + 1) % RING_BUF_SIZE;
        
        // 16-bit signed I2S: map -1..1 to -32768..32767
        int16_t sl = (int16_t)(fl * 32767.0f);
        int16_t sr = (int16_t)(fr * 32767.0f);
        
        // Pack into 32-bit: Left in upper 16, Right in lower 16
        uint32_t val = ((uint32_t)(uint16_t)sl << 16) | (uint16_t)sr;
        if (!pio_sm_is_tx_fifo_full(i2s_pio, i2s_sm)) {
            pio_sm_put(i2s_pio, i2s_sm, val);
        }
    } else {
        if (!pio_sm_is_tx_fifo_full(i2s_pio, i2s_sm)) {
            pio_sm_put(i2s_pio, i2s_sm, 0);
        }
    }
    return true;
}
#endif

static void init_audio_output() {
#ifdef AUDIO_OUT_PWM
    // Initialize PWM pins
    gpio_set_function(PIN_AUDIO_PWM_L, GPIO_FUNC_PWM);
    gpio_set_function(PIN_AUDIO_PWM_R, GPIO_FUNC_PWM);
    
    pwm_slice_l = pwm_gpio_to_slice_num(PIN_AUDIO_PWM_L);
    pwm_slice_r = pwm_gpio_to_slice_num(PIN_AUDIO_PWM_R);
    
    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, 255); // 8-bit resolution
    
    // Set clock divisor for 8x oversampling
    uint32_t sys_hz = clock_get_hz(clk_sys);
    float clkdiv = (float)sys_hz / (256.0f * ((float)SAMPLE_RATE * (float)PWM8_REPEAT_DIV));
    if (clkdiv < 1.0f) clkdiv = 1.0f;
    pwm_config_set_clkdiv(&config, clkdiv);
    
    pwm_init(pwm_slice_l, &config, true);
    pwm_init(pwm_slice_r, &config, true);
    
    // Set midpoint initially
    pwm_set_gpio_level(PIN_AUDIO_PWM_L, 128);
    pwm_set_gpio_level(PIN_AUDIO_PWM_R, 128);
    
    // Setup Wrap Interrupt for slice L (it drives both channels)
    pwm_clear_irq(pwm_slice_l);
    pwm_set_irq_enabled(pwm_slice_l, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_wrap_isr);
    irq_set_enabled(PWM_IRQ_WRAP, true);
#endif

#ifdef AUDIO_OUT_DAC
    // Initialize I2S PIO
    int sm = pio_claim_unused_sm(i2s_pio, false);
    if (sm < 0) {
        i2s_pio = pio1;
        sm = pio_claim_unused_sm(i2s_pio, true);
    }
    i2s_sm = (uint)sm;

    uint offset = pio_add_program(i2s_pio, &i2s_tx_program);
    i2s_tx_program_init(i2s_pio, i2s_sm, offset, PIN_DAC_I2S_DATA, PIN_DAC_I2S_BCK, SAMPLE_RATE);

    // Start audio timer interrupt (DAC only)
    static repeating_timer_t timer;
    add_repeating_timer_us(-(1000000 / SAMPLE_RATE), audio_timer_callback, NULL, &timer);
#endif
}

// =============================================================================
// HARDWARE INITIALIZATION
// =============================================================================

static void init_clocks_overclock() {
    set_sys_clock_khz(CFG_OVERCLOCK_KHZ, true);
}

static void init_i2c_oled() {
    i2c_init(OLED_I2C, 400 * 1000);
    gpio_set_function(PIN_OLED_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_OLED_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_OLED_SDA);
    gpio_pull_up(PIN_OLED_SCL);

#ifdef OLED_FLIP
    u8g2_Setup_ssd1312_i2c_128x64_noname_f(&u8g2, U8G2_R2, u8x8_byte_pico_i2c, u8x8_gpio_and_delay_pico);
#else
    u8g2_Setup_ssd1312_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8x8_byte_pico_i2c, u8x8_gpio_and_delay_pico);
#endif
    oled_init(&u8g2);
}



// =============================================================================
// USB MIDI & INPUT SCANNING
static int current_midi_channel() {
    return synth.get_voice(0)->p.midi_ch;
}
// =============================================================================

static uint8_t midi_note_to_voice[128]; 

static void handle_midi_message(uint8_t status, uint8_t data1, uint8_t data2) {
    update_activity();
    uint8_t type = status & 0xF0;
    
    // Apply channel filtering
    if (type >= 0x80 && type <= 0xEF) {
        int ch = (status & 0x0F) + 1;
        int active_ch = current_midi_channel();
        if (active_ch != 0 && active_ch != ch) return;
    }

    if (type == 0xB0) {
        uint8_t cc  = data1;
        uint8_t val = data2;
        
        // Handle Modwheel and Sustain directly
        if (cc == 1) {
            for (int v = 0; v < NUM_VOICES; v++) {
                synth.get_voice(v)->modwheel_val = (float)val / 127.0f;
            }
        } else if (cc == 64) {
    bool sustain = (val >= 64);
    for (int v = 0; v < NUM_VOICES; v++) {
        synth.get_voice(v)->sustain_active = sustain;
        if (!sustain && synth.get_voice(v)->is_sustained) {
            synth.get_voice(v)->is_sustained = false;
            if (synth.get_voice(v)->current_note == -1) {
                ui.voices_triggered[v] = false;
                synth.get_voice(v)->set_trigger(false);
            }
        }
    }
} else if (cc == 32) {
            synth.preset_load(val);
            return;
        } else if (cc == 33) {
            synth.preset_save(val);
            return;
        }

        // Check if it's a trigger button CC (BTN_TRIG1..4)
        if (cc >= MIDI_CC_BTN_ROW4_COL1 && cc <= MIDI_CC_BTN_ROW4_COL4) {
            if (val >= 64) oled_notify_trigger_activity();
        } else {
            oled_notify_encoder_activity();
        }

        if (midi_map_process(&ui, synth.get_voice(0), cc, val)) {
            for (int v = 0; v < NUM_VOICES; v++) {
                synth.get_voice(v)->set_trigger(ui.voices_triggered[v]);
            }
        }
    } else if (type == 0x90 && data2 > 0) {
        uint8_t note = data1;
        int voice_mode = synth.get_voice(0)->p.midi_mode; // 0=V1, 1=V2, 2=V3, 3=V4, 4=RR, 5=RND, 6=OCT
        int voice_idx = -1;

        if (voice_mode >= 0 && voice_mode <= 3) {
            // Monophonic modes V1-V4: Route note strictly to specific voice
            voice_idx = voice_mode;
        } else if (voice_mode == 4) {
            // RR mode: Round Robin cyclic allocation across 4 voices
            static int rr_counter = 0;
            voice_idx = rr_counter;
            rr_counter = (rr_counter + 1) % NUM_VOICES;
        } else if (voice_mode == 5) {
            // RND mode: Randomly select among currently free voices
            int free_voices[NUM_VOICES];
            int free_count = 0;
            for (int v = 0; v < NUM_VOICES; v++) {
                if (!ui.voices_triggered[v]) {
                    free_voices[free_count++] = v;
                }
            }
            if (free_count > 0) {
                voice_idx = free_voices[rand() % free_count];
            } else {
                voice_idx = rand() % NUM_VOICES;
            }
        } else if (voice_mode == 6) {
            // OCT mode: Voice assigned by MIDI note octave number
            int oct = note / 12; // 1=oct1, 2=oct2, 3=oct3, 4=oct4
            if (oct >= 1 && oct <= 4) {
                voice_idx = oct - 1;
            } else {
                voice_idx = -1; // Ignore notes outside octaves 1-4
            }
        }

        if (voice_idx >= 0 && voice_idx < NUM_VOICES) {
            // Clear any previous mapping to this voice to avoid note-off leaks
            for (int i = 0; i < 128; i++) {
                if (midi_note_to_voice[i] == voice_idx) {
                    midi_note_to_voice[i] = 0xFF;
                }
            }
            midi_note_to_voice[note & 127] = (uint8_t)voice_idx;
            ui.voices_triggered[voice_idx] = true;
            synth.get_voice(voice_idx)->set_trigger(true);
            synth.get_voice(voice_idx)->current_note = note;
            synth.get_voice(voice_idx)->is_sustained = false; // Reset sustain state on Note On
            oled_notify_trigger_activity(); // Note On switches to GRAIN
        }
    } else if (type == 0x80 || (type == 0x90 && data2 == 0)) {
        uint8_t note = data1;
        uint8_t v = midi_note_to_voice[note & 127];
        if (v < NUM_VOICES) {
            // Only release the voice if the released note is the active note on that voice
            if (synth.get_voice(v)->current_note == note) {
                if (synth.get_voice(v)->sustain_active) {
                    synth.get_voice(v)->is_sustained = true;
                    synth.get_voice(v)->current_note = -1;
                } else {
                    ui.voices_triggered[v] = false;
                    synth.get_voice(v)->set_trigger(false);
                    synth.get_voice(v)->current_note = -1;
                }
            }
            midi_note_to_voice[note & 127] = 0xFF;
        }
    } else if (type == 0xE0) {
        // Pitch Bend (data1 is LSB, data2 is MSB)
        float semitones = midi_pitch_bend_to_semitones(data1, data2, 2.0f); // Default range +/- 2 semitones
        for (int v = 0; v < NUM_VOICES; v++) {
            synth.get_voice(v)->pitch_bend_semitones = semitones;
        }
    } else if (type == 0xD0) {
        // Channel Pressure / Aftertouch (data1 is pressure)
        float aftertouch = (float)(data1 & 0x7F) / 127.0f;
        for (int v = 0; v < NUM_VOICES; v++) {
            synth.get_voice(v)->aftertouch_val = aftertouch;
        }
    }

}

static void send_usb_midi(uint8_t status, uint8_t data1, uint8_t data2) {
    if (tud_midi_mounted()) {
        uint8_t packet[4];
        packet[0] = (status >> 4); // Cable 0, CIN matching status high nibble
        packet[1] = status;
        packet[2] = data1;
        packet[3] = data2;
        tud_midi_packet_write(packet);
    }
}

static void process_usb_midi() {
    while (tud_midi_available()) {
        uint8_t packet[4];
        if (!tud_midi_packet_read(packet)) break;
        handle_midi_message(packet[1], packet[2], packet[3]);
#ifdef MIDI_THRU
        // Forward USB MIDI to UART MIDI Out
        midi_uart_write_byte(packet[1]);
        uint8_t cmd = packet[1] & 0xF0;
        if (cmd == 0xC0 || cmd == 0xD0) {
            midi_uart_write_byte(packet[2]);
        } else if (cmd >= 0x80 && cmd <= 0xEF) {
            midi_uart_write_byte(packet[2]);
            midi_uart_write_byte(packet[3]);
        }
#endif
    }
}

static void init_uart_midi() {
    midi_uart_init();
}

static void process_uart_midi() {
    static uint8_t midi_state = 0; // 0: Idle, 1: Expecting 1 byte, 2: Expecting 2 bytes
    static uint8_t midi_buf[3];
    static uint8_t midi_buf_idx = 0;
    static uint8_t current_status = 0;

    uint8_t byte;
    while (midi_uart_read_byte(&byte)) {
#ifdef MIDI_THRU
        // Hardware MIDI Thru: echo RX byte to TX
        midi_uart_write_byte(byte);
#endif
        
        if (byte >= 0x80) {
            if (byte >= 0xF0) {
                // System common/realtime messages (we can ignore for simplicity or reset)
                midi_state = 0;
                continue;
            }
            current_status = byte;
            midi_buf[0] = byte;
            midi_buf_idx = 1;
            uint8_t cmd = byte & 0xF0;
            if (cmd == 0xC0 || cmd == 0xD0) {
                midi_state = 1;
            } else {
                midi_state = 2;
            }
        } else {
            if (midi_state > 0 && current_status != 0) {
                midi_buf[midi_buf_idx++] = byte;
                if (midi_buf_idx == (midi_state + 1)) {
                    uint8_t data2 = (midi_state == 2) ? midi_buf[2] : 0;
                    handle_midi_message(midi_buf[0], midi_buf[1], data2);
#ifdef MIDI_THRU
                    // Duplicate incoming Jack MIDI to USB MIDI Out
                    send_usb_midi(midi_buf[0], midi_buf[1], data2);
#endif
                    
                    // Support running status (re-use status byte)
                    midi_buf_idx = 1;
                }
            }
        }
    }
}

static bool matrix_prev[16] = {false};

static void process_matrix() {
    scan_matrix();

    for (int i = 0; i < 16; i++) {
        if (matrix_debounced[i] != matrix_prev[i]) {
            update_activity();
            int ch = current_midi_channel();
            if (ch == 0) ch = 1;
            uint8_t cc_status = 0xB0 | ((ch - 1) & 0x0F);
            uint8_t note_status_on = 0x90 | ((ch - 1) & 0x0F);
            uint8_t note_status_off = 0x80 | ((ch - 1) & 0x0F);

            if (matrix_debounced[i]) {
                ui_on_button_press(&ui, (ButtonId)i);

                // Trig buttons (BTN_TRIG1=12 to BTN_TRIG4=15)
                if (i >= 12 && i <= 15) {
                    int v = i - 12;
                    int note = 24 + v * 12; // C1=24, C2=36, C3=48, C4=60
                    synth.get_voice(v)->current_note = note;
                    send_usb_midi(note_status_on, note, 127);
                    midi_uart_write_byte(note_status_on);
                    midi_uart_write_byte(note);
                    midi_uart_write_byte(127);
                    oled_notify_trigger_activity();
                } else {
                    send_usb_midi(cc_status, 40 + i, 127);
                    midi_uart_write_byte(cc_status);
                    midi_uart_write_byte(40 + i);
                    midi_uart_write_byte(127);
                    oled_notify_encoder_activity();
                }
            } else {
                ui_on_button_release(&ui, (ButtonId)i);

                if (i >= 12 && i <= 15) {
                    int v = i - 12;
                    int note = 24 + v * 12;
                    synth.get_voice(v)->current_note = -1;
                    send_usb_midi(note_status_off, note, 0);
                    midi_uart_write_byte(note_status_off);
                    midi_uart_write_byte(note);
                    midi_uart_write_byte(0);
                } else {
                    send_usb_midi(cc_status, 40 + i, 0);
                    midi_uart_write_byte(cc_status);
                    midi_uart_write_byte(40 + i);
                    midi_uart_write_byte(0);
                }
            }
            matrix_prev[i] = matrix_debounced[i];
            for (int v = 0; v < NUM_VOICES; v++) {
                synth.get_voice(v)->set_trigger(ui.voices_triggered[v]);
            }
        }
    }
}

static void process_encoders() {
    scan_encoders();
    for (int e = 0; e < 4; e++) {
        int delta = encoders_get_delta(e);
        if (delta != 0) {
            update_activity();
            oled_notify_encoder_activity();
            midi_map_process(&ui, synth.get_voice(0), ENC_CC_MAP[e], delta > 0 ? 65 : 63);
        }
    }
}

bool encoder_timer_callback(repeating_timer_t *rt) {
    process_encoders();
    return true;
}

// =============================================================================
// CORE 1: Audio processing loop
// =============================================================================

void core1_entry() {
    while (true) {
        synth.process(AUDIO_BLOCK_SIZE, audio_out_l, audio_out_r);
        for (int i = 0; i < AUDIO_BLOCK_SIZE; i++) {
            // Push to ring buffer, wait if full
            uint32_t next_write = (ring_write + 1) % RING_BUF_SIZE;
            while (next_write == ring_read) tight_loop_contents();
            
            ring_l[ring_write] = audio_out_l[i];
            ring_r[ring_write] = audio_out_r[i];
            ring_write = next_write;
        }
    }
}

int main() {
    init_clocks_overclock();
    usb_midi_init();
    stdio_init_all();

    init_matrix();
    init_encoders();
    init_i2c_oled();
    init_audio_output();
    init_uart_midi();
    leds_init();

    synth.init();
    synth.preset_load(0);
    ui_state_init(&ui);
    memset(midi_note_to_voice, 0xFF, sizeof(midi_note_to_voice));

    for (int v = 0; v < NUM_VOICES; v++) {
        synth.get_voice(v)->set_trigger(ui.voices_triggered[v]);
    }

    multicore_launch_core1(core1_entry);

    // Start encoder timer interrupt (every 1 ms / 1000 Hz)
    static repeating_timer_t enc_timer;
    add_repeating_timer_ms(-1, encoder_timer_callback, NULL, &enc_timer);

    last_activity_ms = to_ms_since_boot(get_absolute_time());

    uint32_t last_oled_refresh = 0;
    uint32_t last_led_update = 0;
    while (true) {
        usb_midi_task();
        process_usb_midi();
        process_uart_midi();
        process_matrix();

        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
#if defined(CFG_SLEEP_TIMEOUT_MIN) && CFG_SLEEP_TIMEOUT_MIN > 0
        if (!is_sleeping && (now_ms - last_activity_ms) > (uint32_t)CFG_SLEEP_TIMEOUT_MIN * 60 * 1000) {
            is_sleeping = true;
            u8g2_SetPowerSave(&u8g2, 1); // Turn off OLED
        }
#endif

        uint32_t now = time_us_32();
        if (!is_sleeping && (now - last_oled_refresh > 33333)) {
            oled_render(&u8g2, &ui, synth.get_voice(0), NUM_VOICES);
            last_oled_refresh = now;
        } else if (is_sleeping) {
            last_oled_refresh = now;
        }

        if (now - last_led_update > 16666) {
            leds_show();
            last_led_update = now;
        }
    }
    return 0;
}

