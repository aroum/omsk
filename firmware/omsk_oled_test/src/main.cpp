#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"

extern "C" {
#include "u8g2.h"
}

#include "hw_config.h"

// OLED display pins & configuration (as defined in shared/hw_config.h)
#define PIN_OLED_SCL 21
#define PIN_OLED_SDA 20 
#define OLED_I2C i2c0

#ifdef PICO_RP2040
#define CFG_OVERCLOCK_KHZ 150000
#else
#define CFG_OVERCLOCK_KHZ 240000
#endif

u8g2_t u8g2;

// =============================================================================
// U8G2 PICO I2C CALLBACKS
// =============================================================================

uint8_t u8x8_byte_pico_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    static uint8_t buffer[1100];
    static uint16_t buf_idx;
    switch (msg) {
        case U8X8_MSG_BYTE_SEND:
            if (buf_idx + arg_int < sizeof(buffer)) {
                memcpy(buffer + buf_idx, arg_ptr, arg_int);
                buf_idx += arg_int;
            }
            break;
        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_idx = 0;
            break;
        case U8X8_MSG_BYTE_END_TRANSFER: {
            uint8_t addr = u8x8_GetI2CAddress(u8x8) >> 1;
            i2c_write_timeout_us(OLED_I2C, addr, buffer, buf_idx, false, 50000);
            break;
        }
    }
    return 1;
}

uint8_t u8x8_gpio_and_delay_pico(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch (msg) {
        case U8X8_MSG_DELAY_MILLI:
            sleep_ms(arg_int);
            break;
        case U8X8_MSG_DELAY_10MICRO:
            sleep_us(arg_int * 10);
            break;
        case U8X8_MSG_DELAY_I2C:
            sleep_us(2);
            break;
    }
    return 1;
}

// =============================================================================
// INITIALIZATION
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

    // Setup U8g2 using SSD1312 or SH1107 config
#if CFG_OLED_TYPE == 1
    u8g2_Setup_sh1107_i2c_64x128_f(&u8g2, U8G2_R1, u8x8_byte_pico_i2c, u8x8_gpio_and_delay_pico);
#else
    u8g2_Setup_ssd1312_i2c_128x64_noname_f(&u8g2, U8G2_R2, u8x8_byte_pico_i2c, u8x8_gpio_and_delay_pico);
#endif
    
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
    u8x8_SetI2CAddress(&u8g2.u8x8, 0x3C << 1);
    u8g2_ClearDisplay(&u8g2);
}

int main() {
    init_clocks_overclock();
    stdio_init_all();
    init_i2c_oled();

    while (true) {
        u8g2_ClearBuffer(&u8g2);

        // Draw perimeter frame: 1 pixel wide at the very edge of the 128x64 display
        u8g2_DrawFrame(&u8g2, 0, 0, 128, 64);

        // Draw center cross: 2 pixels wide
        // Horizontal bar centered vertically (height 2px, occupies y=31 and y=32)
        u8g2_DrawBox(&u8g2, 0, 31, 128, 2);
        // Vertical bar centered horizontally (width 2px, occupies x=63 and x=64)
        u8g2_DrawBox(&u8g2, 63, 0, 2, 64);

        u8g2_SendBuffer(&u8g2);

        sleep_ms(100);
    }

    return 0;
}
