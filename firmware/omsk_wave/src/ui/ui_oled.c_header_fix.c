#include "ui_oled.h"
#include "../sw_config.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "u8g2.h"
#include <stdio.h>
#include <string.h>

#define OLED_I2C_INST i2c1

static u8g2_t g_u8g2;
static int g_oled_ready = 0;
