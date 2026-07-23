#include "matrix.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "../hw_config.h"

bool matrix_curr[16] = {false};
bool matrix_debounced[16] = {false};
static uint32_t last_change_time[16] = {0};

void init_matrix(void) {
    const uint rows[] = {PIN_MATRIX_ROW1, PIN_MATRIX_ROW2, PIN_MATRIX_ROW3, PIN_MATRIX_ROW4};
    for (int i = 0; i < 4; i++) {
        gpio_init(rows[i]);
        gpio_set_dir(rows[i], GPIO_OUT);
        gpio_put(rows[i], 1);
    }
    const uint cols[] = {PIN_MATRIX_COL1, PIN_MATRIX_COL2, PIN_MATRIX_COL3, PIN_MATRIX_COL4};
    for (int i = 0; i < 4; i++) {
        gpio_init(cols[i]);
        gpio_set_dir(cols[i], GPIO_IN);
        gpio_pull_up(cols[i]);
    }
}

void scan_matrix(void) {
    const uint rows[] = {PIN_MATRIX_ROW1, PIN_MATRIX_ROW2, PIN_MATRIX_ROW3, PIN_MATRIX_ROW4};
    const uint cols[] = {PIN_MATRIX_COL1, PIN_MATRIX_COL2, PIN_MATRIX_COL3, PIN_MATRIX_COL4};

    for (int r = 0; r < 4; r++) {
        gpio_put(rows[r], 0);
        sleep_us(5);
        for (int c = 0; c < 4; c++) {
            int idx = r * 4 + c;
            matrix_curr[idx] = !gpio_get(cols[c]);
        }
        gpio_put(rows[r], 1);
    }

    uint32_t now = to_ms_since_boot(get_absolute_time());
    for (int i = 0; i < 16; i++) {
        bool raw = matrix_curr[i];
        if (raw != matrix_debounced[i]) {
            if (now - last_change_time[i] >= DEBOUNCE_TIME_MS) {
                matrix_debounced[i] = raw;
                last_change_time[i] = now;
            }
        }
    }
}
