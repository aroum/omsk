#include "encoders.h"
#include "pico/stdlib.h"
#include "../hw_config.h"

static const uint ENC_A_PINS[4] = {PIN_ENCODER1_A, PIN_ENCODER2_A, PIN_ENCODER3_A, PIN_ENCODER4_A};
static const uint ENC_B_PINS[4] = {PIN_ENCODER1_B, PIN_ENCODER2_B, PIN_ENCODER3_B, PIN_ENCODER4_B};
static uint8_t enc_prev_states[4] = {0};

int8_t enc_positions[4] = {0};
static const int8_t encoder_states[] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

void init_encoders(void) {
    for (int e = 0; e < 4; e++) {
        gpio_init(ENC_A_PINS[e]);
        gpio_set_dir(ENC_A_PINS[e], GPIO_IN);
        gpio_pull_up(ENC_A_PINS[e]);

        gpio_init(ENC_B_PINS[e]);
        gpio_set_dir(ENC_B_PINS[e], GPIO_IN);
        gpio_pull_up(ENC_B_PINS[e]);

        enc_prev_states[e] = (gpio_get(ENC_A_PINS[e]) << 1) | gpio_get(ENC_B_PINS[e]);
        enc_positions[e] = 0;
    }
}

void scan_encoders(void) {
    for (int e = 0; e < 4; e++) {
        uint8_t current_state = (gpio_get(ENC_A_PINS[e]) << 1) | gpio_get(ENC_B_PINS[e]);
        if (current_state != enc_prev_states[e]) {
            uint8_t state_idx = (enc_prev_states[e] << 2) | current_state;
            int8_t movement = encoder_states[state_idx & 0x0F];
            enc_prev_states[e] = current_state;

            if (movement != 0) {
                enc_positions[e] += movement;
            }
        }
    }
}

int encoders_get_delta(int index) {
    if (index < 0 || index >= 4) return 0;
    int delta = 0;
    if (enc_positions[index] >= ENCODER_RESOLUTION) {
        delta = 1;
        enc_positions[index] -= ENCODER_RESOLUTION;
    } else if (enc_positions[index] <= -ENCODER_RESOLUTION) {
        delta = -1;
        enc_positions[index] += ENCODER_RESOLUTION;
    }
    return delta;
}
