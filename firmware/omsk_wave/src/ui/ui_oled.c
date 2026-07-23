#include "ui_oled.h"
#include "../sw_config.h"
#include "../tables/vcf_lut_data.h"
#include "ui_logic.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "u8g2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../tables/vco_lut_data.h"
#include "../tables/noise_lut_data.h"
#define OLED_I2C_INST OLED_I2C
static u8g2_t g_u8g2;
static int g_oled_ready = 0;
extern bool set_button_held;
extern ModuleID selected_module;
extern SynthParams params;

// --- VCF Graph Precalculations (128 pixels) ---
static float g_vcf_graph_cw[128];
static float g_vcf_graph_sw[128];
static float g_vcf_graph_c2w[128];
static float g_vcf_graph_s2w[128];

bool ui_is_fn_held(void);

// --- 1. LUT Таблица ---
static const int8_t knob_lut[128][2] = {
    {-5, 9},   {-5, 8},   {-6, 8},   {-6, 8},   {-6, 8},   {-7, 7},   {-7, 7},
    {-7, 7},   {-8, 7},   {-8, 6},   {-8, 6},   {-8, 6},   {-9, 5},   {-9, 5},
    {-9, 5},   {-9, 4},   {-9, 4},   {-9, 3},   {-10, 3},  {-10, 3},  {-10, 2},
    {-10, 2},  {-10, 1},  {-10, 1},  {-10, 1},  {-10, 0},  {-10, 0},  {-10, -1},
    {-10, -1}, {-10, -1}, {-10, -2}, {-10, -2}, {-10, -3}, {-10, -3}, {-9, -3},
    {-9, -4},  {-9, -4},  {-9, -5},  {-9, -5},  {-8, -5},  {-8, -6},  {-8, -6},
    {-8, -6},  {-7, -7},  {-7, -7},  {-7, -7},  {-7, -8},  {-6, -8},  {-6, -8},
    {-6, -8},  {-5, -8},  {-5, -9},  {-5, -9},  {-4, -9},  {-4, -9},  {-3, -9},
    {-3, -10}, {-3, -10}, {-2, -10}, {-2, -10}, {-1, -10}, {-1, -10}, {-1, -10},
    {0, -10},  {0, -10},  {1, -10},  {1, -10},  {1, -10},  {2, -10},  {2, -10},
    {3, -10},  {3, -10},  {3, -9},   {4, -9},   {4, -9},   {5, -9},   {5, -9},
    {5, -8},   {6, -8},   {6, -8},   {6, -8},   {7, -8},   {7, -7},   {7, -7},
    {7, -7},   {8, -6},   {8, -6},   {8, -6},   {8, -5},   {9, -5},   {9, -5},
    {9, -4},   {9, -4},   {9, -3},   {10, -3},  {10, -3},  {10, -2},  {10, -2},
    {10, -1},  {10, -1},  {10, -1},  {10, 0},   {10, 0},   {10, 1},   {10, 1},
    {10, 1},   {10, 2},   {10, 2},   {10, 3},   {10, 3},   {9, 3},    {9, 4},
    {9, 4},    {9, 5},    {9, 5},    {9, 5},    {8, 6},    {8, 6},    {8, 6},
    {8, 7},    {7, 7},    {7, 7},    {7, 7},    {6, 8},    {6, 8},    {6, 8},
    {5, 8},    {5, 9}};

static const uint8_t image_bar1_bits[] = {
    0x00, 0x20, 0x00, 0x00, 0xfe, 0xff, 0xff, 0x03, 0x01, 0x20,
    0x00, 0x04, 0x01, 0x20, 0x00, 0x04, 0x01, 0x20, 0x00, 0x04,
    0xfe, 0xff, 0xff, 0x03, 0x00, 0x20, 0x00, 0x00};

// --- Подключаем общий заголовочный файл с логотипом ---
#include "../../../shared/image_logo.h"

// --- 2. Коллбэки (теперь они ВЫШЕ инициализации) ---

static uint8_t u8x8_byte_pico_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int,
                                     void *arg_ptr) {
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
    i2c_write_timeout_us(OLED_I2C_INST, addr, buffer, buf_idx, false, 50000);
    break;
  }
  }
  return 1;
}

static uint8_t u8x8_gpio_and_delay_pico(u8x8_t *u8x8, uint8_t msg,
                                        uint8_t arg_int, void *arg_ptr) {
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

void ui_oled_set_brightness(uint8_t percentage) {
  if (!g_oled_ready)
    return;

  if (percentage > 100)
    percentage = 100;

  // Преобразуем 0-100 в 0-255
  uint8_t contrast_value = (uint8_t)((percentage * 255) / 100);

  // u8g2_SetContrast — стандартная функция библиотеки для SSD1306/SSD1312
  u8g2_SetContrast(&g_u8g2, contrast_value);
}

void ui_oled_set_power(bool on) {
  if (!g_oled_ready)
    return;
  u8g2_SetPowerSave(&g_u8g2, on ? 0 : 1);
}

// --- 3. Функции API ---

void ui_oled_init(void) {
  i2c_init(OLED_I2C_INST, 400 * 1000);
  gpio_set_function(PIN_OLED_SCL, GPIO_FUNC_I2C);
  gpio_set_function(PIN_OLED_SDA, GPIO_FUNC_I2C);
  gpio_pull_up(PIN_OLED_SCL);
  gpio_pull_up(PIN_OLED_SDA);

#if CFG_OLED_TYPE == 1
  u8g2_Setup_sh1107_i2c_64x128_f(
      &g_u8g2, U8G2_R1, u8x8_byte_pico_hw_i2c, u8x8_gpio_and_delay_pico);
#else
#ifdef OLED_FLIP
  u8g2_Setup_ssd1312_i2c_128x64_noname_f(
      &g_u8g2, U8G2_R2, u8x8_byte_pico_hw_i2c, u8x8_gpio_and_delay_pico);
#else
  u8g2_Setup_ssd1312_i2c_128x64_noname_f(
      &g_u8g2, U8G2_R0, u8x8_byte_pico_hw_i2c, u8x8_gpio_and_delay_pico);
#endif
#endif
  u8g2_InitDisplay(&g_u8g2);
  u8g2_SetPowerSave(&g_u8g2, 0);
  u8x8_SetI2CAddress(&g_u8g2.u8x8, 0x3C << 1);

  void ui_oled_precompute_vcf_math();
  ui_oled_precompute_vcf_math();

  u8g2_ClearBuffer(&g_u8g2);
  u8g2_SetBitmapMode(&g_u8g2, 1);
  u8g2_DrawXBM(&g_u8g2, 2, 7, 125, 51, image_logo_bits);
#ifdef CFG_LOGO_SUBTITLE
  if (CFG_LOGO_SUBTITLE[0] != '\0') {
    u8g2_SetFont(&g_u8g2, u8g2_font_4x6_tr);
    u8g2_uint_t width = u8g2_GetStrWidth(&g_u8g2, CFG_LOGO_SUBTITLE);
    u8g2_DrawStr(&g_u8g2, 128 - width, 63, CFG_LOGO_SUBTITLE);
  }
#endif
  u8g2_SendBuffer(&g_u8g2);

  u8g2_SendBuffer(&g_u8g2);
  sleep_ms(1500);

  g_oled_ready = 1;

  // Устанавливаем яркость из конфига сразу после инициализации
  ui_oled_set_brightness(OLED_BRIGHTNESS_PERCENT);
}

void ui_oled_precompute_vcf_math() {
    const float fs = 48000.0f;
    const float f_min = 20.0f;
    const float f_max = 20000.0f;
    for (int x = 0; x < 128; x++) {
        float f = f_min * powf(1000.0f, x / 127.0f);
        if (f > 22000.0f) f = 22000.0f;
        float w = 2.0f * 3.14159265f * f / fs;
        g_vcf_graph_cw[x] = cosf(w);
        g_vcf_graph_sw[x] = sinf(w);
        g_vcf_graph_c2w[x] = cosf(2.0f * w);
        g_vcf_graph_s2w[x] = sinf(2.0f * w);
    }
}

extern char ui_status_msg[32];
extern uint32_t ui_status_msg_timeout_ms;

#include "../sequencer/sequencer.h"

static void draw_dashed_vline(int x, int y1, int y2) {
  for (int y = y1; y <= y2; y += 4) {
    u8g2_DrawPixel(&g_u8g2, x, y);
    if (y + 1 <= y2)
      u8g2_DrawPixel(&g_u8g2, x, y + 1);
  }
}

static void draw_dashed_hline(int y, int x1, int x2) {
  for (int x = x1; x <= x2; x += 4) {
    u8g2_DrawPixel(&g_u8g2, x, y);
    if (x + 1 <= x2)
      u8g2_DrawPixel(&g_u8g2, x + 1, y);
  }
}

static void draw_eg_graph(ModuleID mod) {
  uint8_t a_v, d_v, s_v, r_v, ac, dc, rc;
  if (mod == MOD_EG1) {
    a_v = params.eg1_attack; d_v = params.eg1_decay; s_v = params.eg1_sustain; r_v = params.eg1_release;
    ac = params.eg1_attack_curve; dc = params.eg1_decay_curve; rc = params.eg1_release_curve;
  } else {
    a_v = params.eg2_attack; d_v = params.eg2_decay; s_v = params.eg2_sustain; r_v = params.eg2_release;
    ac = params.eg2_attack_curve; dc = params.eg2_decay_curve; rc = params.eg2_release_curve;
  }

  const int baseline_y = 47;
  const uint32_t max_h = 45; 
  uint32_t sust_h = (uint32_t)s_v * max_h / 127;
  int sust_y = baseline_y - (int)sust_h;

  int wa = 2 + (a_v * 36 / 127);
  int wd = 2 + (d_v * 36 / 127);
  int wr = 2 + (r_v * 36 / 127);
  int ws = 14; 

  int x0 = 0, x1 = x0 + wa, x2 = x1 + wd, x3 = x2 + ws, x4 = x3 + wr;
  if (x4 > 127) x4 = 127;

  u8g2_SetDrawColor(&g_u8g2, 1);

  // 1. Attack
  int prev_y = baseline_y;
  for (int x = x0; x < x1; x++) {
    int idx = (wa > 1) ? ((x - x0) * 255) / (wa - 1) : 255;
    uint32_t h_val = (uint32_t)g_eg_curve_lut[ac & 127][idx] * max_h / 65535;
    int y = baseline_y - (int)h_val;
    int ymin = (y < prev_y) ? y : prev_y;
    int h = abs(y - prev_y) + 1;
    u8g2_DrawVLine(&g_u8g2, x, ymin, h);
    u8g2_DrawVLine(&g_u8g2, x, ymin - 1, h);
    prev_y = y;
  }

  // 2. Decay
  prev_y = baseline_y - (int)max_h; // Peak
  for (int x = x1; x < x2; x++) {
    int pos = (x2 - 1) - x; 
    int idx = (wd > 1) ? (pos * 255) / (wd - 1) : 0;
    uint32_t cv = (uint32_t)g_eg_curve_lut[dc & 127][idx];
    uint32_t h_val = sust_h + ((max_h - sust_h) * cv / 65535);
    int y = baseline_y - (int)h_val;
    int ymin = (y < prev_y) ? y : prev_y;
    int h = abs(y - prev_y) + 1;
    u8g2_DrawVLine(&g_u8g2, x, ymin, h);
    u8g2_DrawVLine(&g_u8g2, x, ymin - 1, h);
    prev_y = y;
  }

  // 3. Sustain
  u8g2_DrawHLine(&g_u8g2, x2, sust_y, x3 - x2);
  u8g2_DrawHLine(&g_u8g2, x2, sust_y - 1, x3 - x2);

  // 4. Release
  prev_y = sust_y;
  for (int x = x3; x < x4; x++) {
    int pos = (x4 - 1) - x; 
    int idx = (wr > 1) ? (pos * 255) / (wr - 1) : 0;
    uint32_t cv = (uint32_t)g_eg_curve_lut[rc & 127][idx];
    uint32_t h_val = sust_h * cv / 65535;
    int y = baseline_y - (int)h_val;
    int ymin = (y < prev_y) ? y : prev_y;
    int h = abs(y - prev_y) + 1;
    u8g2_DrawVLine(&g_u8g2, x, ymin, h);
    u8g2_DrawVLine(&g_u8g2, x, ymin - 1, h);
    prev_y = y;
  }

  // Vertical Dashed Lines at points
  draw_dashed_vline(x1, 2, baseline_y);      // Peak
  draw_dashed_vline(x2, sust_y, baseline_y); // Decay end
  draw_dashed_vline(x3, sust_y, baseline_y); // Release start

  // Vertex boxes (Inflection points)
  u8g2_DrawBox(&g_u8g2, x1 - 1, 1, 3, 3);
  u8g2_DrawBox(&g_u8g2, x2 - 1, sust_y - 1, 3, 3);
  u8g2_DrawBox(&g_u8g2, x3 - 1, sust_y - 1, 3, 3);
  u8g2_DrawBox(&g_u8g2, x4 - 1, baseline_y - 1, 3, 3);
}

#if CFG_FILTER_MODE == FILTER_MODE_3_TABLES
static void draw_vcf_graph(ModuleID mod) {
    uint8_t cutoff, res, type;
    const int32_t (*lut)[128][5];
    
    if (mod == MOD_VCF1) {
        cutoff = params.vcf1_cutoff;
        res = params.vcf1_res;
        type = params.vcf1_type;
    } else {
        cutoff = params.vcf2_cutoff;
        res = params.vcf2_res;
        type = params.vcf2_type;
    }

    switch (type % 5) {
        case 0: lut = g_vcf_lpf_lut; break;
        case 1: lut = g_vcf_bpf_lut; break;
        case 2: lut = g_vcf_hpf_lut; break;
        case 3: lut = g_vcf_bsf_lut; break;
        case 4: lut = g_vcf_apf_lut; break;
        default: lut = g_vcf_lpf_lut; break;
    }

    // Coeffs are at [res >> 2][cutoff]
    const int32_t *c = lut[res >> 2][cutoff];
    float b0 = c[0] / (float)(1 << 30);
    float b1 = c[1] / (float)(1 << 30);
    float b2 = c[2] / (float)(1 << 30);
    float a1 = c[3] / (float)(1 << 30);
    float a2 = c[4] / (float)(1 << 30);

    const int baseline_y = 47;
    const float fs = 48000.0f;
    const float f_min = 20.0f;
    const float f_max = 20000.0f;

    u8g2_SetDrawColor(&g_u8g2, 1);
    
    // 1. Draw Vertical Cutoff Indicator (at exactly the cutoff index x=cutoff)
    // In our log scale, the index 'cutoff' maps to 20 * 1000^(cutoff/127)
    // The horizontal axis also follows this mapping 0..127
    draw_dashed_vline(cutoff, 2, 47);

    int prev_y = -1;
    bool is_apf = ((type % 5) == 4);

    for (int x = 0; x < 128; x++) {
        // Use precalculated values
        float cos_w = g_vcf_graph_cw[x];
        float sin_w = g_vcf_graph_sw[x];
        float cos_2w = g_vcf_graph_c2w[x];
        float sin_2w = g_vcf_graph_s2w[x];

        // Numerator Real/Imag
        float nr = b0 + b1 * cos_w + b2 * cos_2w;
        float ni = -(b1 * sin_w + b2 * sin_2w);
        // Denominator Real/Imag
        float dr = 1.0f + a1 * cos_w + a2 * cos_2w;
        float di = -(a1 * sin_w + a2 * sin_2w);

        int y;
        if (!is_apf) {
            float mag_sq = (nr * nr + ni * ni) / (dr * dr + di * di + 1e-12f);
            float mag_db = 10.0f * log10f(mag_sq + 1e-12f);
            y = 20 - (int)(mag_db * 25.0f / 40.0f);
        } else {
            float phi_nr = atan2f(ni, nr);
            float phi_dr = atan2f(di, dr);
            float phi = phi_nr - phi_dr;
            while (phi > 3.14159f) phi -= 2.0f * 3.14159f;
            while (phi < -3.14159f) phi += 2.0f * 3.14159f;
            y = 24 - (int)(phi * 180.0f / 3.14159f * 20.0f / 180.0f);
        }

        if (y < 2) y = 2;
        if (y > 47) y = 47;
        if (prev_y == -1) prev_y = y;
        
        int ymin = (y < prev_y) ? y : prev_y;
        int h = abs(y - prev_y) + 1;
        u8g2_DrawVLine(&g_u8g2, x, ymin, h);
        u8g2_DrawVLine(&g_u8g2, x, ymin - 1, h);
        prev_y = y;
    }
    
    // 2. Cutoff dot (uses LUT at index 'cutoff')
    float cos_wc = g_vcf_graph_cw[cutoff];
    float sin_wc = g_vcf_graph_sw[cutoff];
    float cos_2wc = g_vcf_graph_c2w[cutoff];
    float sin_2wc = g_vcf_graph_s2w[cutoff];
    
    float nr_c = b0 + b1 * cos_wc + b2 * cos_2wc, ni_c = -(b1 * sin_wc + b2 * sin_2wc);
    float dr_c = 1.0f + a1 * cos_wc + a2 * cos_2wc, di_c = -(a1 * sin_wc + a2 * sin_2wc);
    
    int dot_y;
    if (!is_apf) {
        float m_sq = (nr_c * nr_c + ni_c * ni_c) / (dr_c * dr_c + di_c * di_c + 1e-12f);
        dot_y = 20 - (int)(10.0f * log10f(m_sq + 1e-12f) * 25.0f / 40.0f);
    } else {
        float phi_c = (atan2f(ni_c, nr_c) - atan2f(di_c, dr_c));
        while (phi_c > 3.14159f) phi_c -= 2.0f * 3.14159f;
        while (phi_c < -3.14159f) phi_c += 2.0f * 3.14159f;
        dot_y = 24 - (int)(phi_c * 180.0f / 3.14159f * 20.0f / 180.0f);
    }
    if (dot_y < 2) dot_y = 2;
    if (dot_y > 47) dot_y = 47;
    u8g2_DrawBox(&g_u8g2, cutoff - 1, dot_y - 1, 3, 3);
}
#else
static void draw_vcf_graph(ModuleID mod) {
    uint8_t cutoff, res, type;
    if (mod == MOD_VCF1) {
        cutoff = params.vcf1_cutoff;
        res = params.vcf1_res;
        type = params.vcf1_type;
    } else {
        cutoff = params.vcf2_cutoff;
        res = params.vcf2_res;
        type = params.vcf2_type;
    }

    u8g2_SetDrawColor(&g_u8g2, 1);
    draw_dashed_vline(cutoff, 2, 47);

    int prev_y = -1;
    int peak = (res * 15) / 127;
    int filter_type = type % 5;

    for (int x = 0; x < 128; x++) {
        int y = 20;

        if (filter_type == 0) { // LPF
            if (x < cutoff - 4) {
                y = 24;
            } else if (x < cutoff) {
                y = 24 - (peak * (x - (cutoff - 4))) / 4;
            } else if (x < cutoff + 16) {
                y = (24 - peak) + ((47 - (24 - peak)) * (x - cutoff)) / 16;
            } else {
                y = 47;
            }
        } else if (filter_type == 1) { // BPF
            if (x < cutoff - 16) {
                y = 47;
            } else if (x < cutoff) {
                y = 47 - ((47 - (24 - peak)) * (x - (cutoff - 16))) / 16;
            } else if (x < cutoff + 16) {
                y = (24 - peak) + ((47 - (24 - peak)) * (x - cutoff)) / 16;
            } else {
                y = 47;
            }
        } else if (filter_type == 2) { // HPF
            if (x < cutoff - 16) {
                y = 47;
            } else if (x < cutoff) {
                y = 47 - ((47 - (24 - peak)) * (x - (cutoff - 16))) / 16;
            } else if (x < cutoff + 4) {
                y = (24 - peak) + (peak * (x - cutoff)) / 4;
            } else {
                y = 24;
            }
        } else if (filter_type == 3) { // Notch (BSF)
            if (x < cutoff - 8) {
                y = 24;
            } else if (x < cutoff) {
                y = 24 + ((47 - 24) * (x - (cutoff - 8))) / 8;
            } else if (x < cutoff + 8) {
                y = 47 - ((47 - 24) * (x - cutoff)) / 8;
            } else {
                y = 24;
            }
        } else { // APF (All Pass)
            y = 24 + ((x - 64) * 20) / 64;
        }

        if (y < 2) y = 2;
        if (y > 47) y = 47;
        if (prev_y == -1) prev_y = y;
        
        int ymin = (y < prev_y) ? y : prev_y;
        int h = abs(y - prev_y) + 1;
        u8g2_DrawVLine(&g_u8g2, x, ymin, h);
        u8g2_DrawVLine(&g_u8g2, x, ymin - 1, h);
        prev_y = y;
    }

    // Cutoff dot on peak
    int dot_y = 24;
    if (filter_type == 0 || filter_type == 2) {
        dot_y = 24 - peak;
    } else if (filter_type == 1) {
        dot_y = 24 - peak;
    } else if (filter_type == 3) {
        dot_y = 47;
    } else {
        dot_y = 24 + ((cutoff - 64) * 20) / 64;
    }
    if (dot_y < 2) dot_y = 2;
    if (dot_y > 47) dot_y = 47;
    u8g2_DrawBox(&g_u8g2, cutoff - 1, dot_y - 1, 3, 3);
}
#endif

static float ui_get_lut_val(int w, uint32_t ph, float pwm, uint8_t sh, uint8_t pam_idx) {
  uint8_t p_idx = (ph >> 24) & 255;
  if (w <= 3) { // W_RSAW
      float s_b = (float)sh * (63.0f / 127.0f);
      int sb0 = (int)s_b;
      int sb1 = (sb0 < 63) ? sb0 + 1 : 63;
      float f = s_b - (float)sb0;
      float v0 = (float)lut_basic[0][w][sb0][p_idx] / 32767.0f;
      float v1 = (float)lut_basic[0][w][sb1][p_idx] / 32767.0f;
      return v0 + (v1 - v0) * f;
  } else if (w == 6) { // W_PAM
      int idx = (pam_idx + sh) % 128;
      return lut_pam4[idx][(ph >> 28) & 15];
  } else {
      float s_f = (float)sh * (15.0f / 127.0f);
      int s0 = (int)s_f, s1 = (s0 < 15) ? s0 + 1 : 15;
      float f = s_f - (float)s0;
      float b_pwm = (w == 5) ? pwm : 0.5f; // W_PULSE=5
      float f_idx = (0.5f - b_pwm) * (15.0f / 0.49f);
      int pw_idx = (int)f_idx;
      if (pw_idx < 0) pw_idx = 0; else if (pw_idx > 15) pw_idx = 15;
      
      float v0 = (float)lut_pulse[0][pw_idx][s0][p_idx] / 32767.0f;
      float v1 = (float)lut_pulse[0][pw_idx][s1][p_idx] / 32767.0f;
      return v0 + (v1 - v0) * f;
  }
}

typedef enum { W_SIN = 0, W_TRI = 1, W_SAW = 2, W_RSAW = 3, W_SQR = 4, W_PULSE = 5, W_PAM = 6 } WaveType;

static WaveType ui_get_hybrid_type(int is_b, int idx) {
    static const WaveType pairs[22][2] = {
        {W_SIN, W_TRI},   {W_SIN, W_SAW},    {W_SIN, W_RSAW},  {W_SIN, W_SQR},
        {W_SIN, W_PULSE}, {W_SIN, W_PAM},    {W_TRI, W_SAW},   {W_TRI, W_RSAW},
        {W_TRI, W_SQR},   {W_TRI, W_PULSE},  {W_TRI, W_PAM},   {W_SAW, W_RSAW},
        {W_RSAW, W_SAW},  {W_SAW, W_SQR},    {W_SAW, W_PULSE}, {W_SAW, W_PAM},
        {W_RSAW, W_SQR},  {W_RSAW, W_PULSE}, {W_RSAW, W_PAM},  {W_SQR, W_PULSE},
        {W_SQR, W_PAM},   {W_PULSE, W_PAM}
    };
    return pairs[idx][is_b];
}

static float ui_sample_wave(uint32_t phase, uint8_t wave_param, uint8_t shape_param) {
  float final_val = 0.0f;
  if (wave_param < 32) {
    int seg = wave_param / 8;
    float t = (float)(wave_param % 8) / 7.0f;
    if (seg == 2) {
        // Special Saw-Tri-RSaw morph
        float s_saw = ui_get_lut_val(W_SAW, phase, 0.5f, shape_param, 0);
        float s_rsaw = ui_get_lut_val(W_RSAW, phase + 0x80000000, 0.5f, shape_param, 0);
        float s_tri = ui_get_lut_val(W_TRI, phase, 0.5f, shape_param, 0);
        float ct = t * 2.0f;
        if (t < 0.5f) final_val = (1.0f - ct) * s_saw + ct * s_tri;
        else final_val = (2.0f - ct) * s_tri + (ct - 1.0f) * s_rsaw;
    } else {
        WaveType types[4][2] = {{W_SIN, W_TRI}, {W_TRI, W_SAW}, {W_SAW, W_RSAW}, {W_RSAW, W_PULSE}};
        WaveType a = types[seg][0], b = types[seg][1];
        float s1 = ui_get_lut_val(a, phase, 0.5f, shape_param, 0);
        float s2 = ui_get_lut_val(b, phase, 0.5f, shape_param, 0);
        final_val = (1.0f - t) * s1 + t * s2;
    }
  } else if (wave_param < 64) {
    float pw = 0.5f - ((float)(wave_param - 32) / 31.0f) * 0.49f;
    final_val = ui_get_lut_val(W_PULSE, phase, pw, shape_param, 0); 
  } else if (wave_param < 80) {
    uint8_t base_idx = (uint8_t)((wave_param - 64) * (127.0f / 15.0f));
    final_val = ui_get_lut_val(W_PAM, phase, 0.5f, shape_param, base_idx);
  } else {
    float pos = (wave_param - 80) / 47.0f;
    float segf = pos * 21.0f;
    int idx = (int)segf; if (idx > 21) idx = 21;
    float t = segf - (float)idx;
    
    WaveType a = ui_get_hybrid_type(0, idx);
    WaveType b = ui_get_hybrid_type(1, idx);
    
    uint32_t phase_a = phase;
    if (a == W_PULSE || a == W_SQR) phase_a += 0x40000000;
    // Invert phase for waves 85-93
    if (wave_param >= 85 && wave_param <= 93 && a == W_SIN) phase_a += 0x80000000;
    uint32_t phase_b = phase + 0x80000000;
    
    float pwm_val = 0.5f - ((t > 1.0f ? 1.0f : (t < 0.0f ? 0.0f : t)) * 0.49f);
    uint8_t pam_val = (uint8_t)(t * 127.0f);
    
    float s_a = ui_get_lut_val(a, phase_a, pwm_val, shape_param, pam_val);
    float s_b = ui_get_lut_val(b, phase_b, pwm_val, shape_param, pam_val);
    final_val = (phase < 0x80000000) ? s_a : s_b;
  }
  
  return (final_val - lut_norm_bias[wave_param][shape_param]) * lut_norm_gain[wave_param][shape_param];
}

static void draw_noise_graph() {
  uint8_t color = params.noise_color;
  uint8_t mode = g_noise_mode_lut[color];

  u8g2_SetDrawColor(&g_u8g2, 1);
  if (mode == 0) {
    u8g2_DrawHLine(&g_u8g2, 0, 24, 128);
    u8g2_DrawHLine(&g_u8g2, 0, 23, 128);
    return;
  }

  float b0 = g_noise_filter_lut[color][0];
  float b1 = g_noise_filter_lut[color][1];
  float b2 = g_noise_filter_lut[color][2];
  float a1 = g_noise_filter_lut[color][3];
  float a2 = g_noise_filter_lut[color][4];

  int prev_y = -1;
  for (int x = 0; x < 128; x++) {
    float cw = g_vcf_graph_cw[x], sw = g_vcf_graph_sw[x];
    float c2w = g_vcf_graph_c2w[x], s2w = g_vcf_graph_s2w[x];
    
    float nr = b0 + b1 * cw + b2 * c2w, ni = -(b1 * sw + b2 * s2w);
    float dr = 1.0f + a1 * cw + a2 * c2w, di = -(a1 * sw + a2 * s2w);
    float m_sq = (nr * nr + ni * ni) / (dr * dr + di * di + 1e-12f);
    
    int y = 24 - (int)(10.0f * log10f(m_sq + 1e-12f) * 25.0f / 40.0f);
    if (y < 2) y = 2; if (y > 47) y = 47;

    if (prev_y == -1) prev_y = y;
    int ymin = (y < prev_y) ? y : prev_y;
    int h = abs(y - prev_y) + 1;
    u8g2_DrawVLine(&g_u8g2, x, ymin, h);
    u8g2_DrawVLine(&g_u8g2, x, ymin - 1, h);
    prev_y = y;
  }
}

static void draw_vco_lfo_graph(ModuleID mod) {
  uint8_t wave, shape;
  if (mod == MOD_VCO1) { wave = params.vco1_wave; shape = params.vco1_shape; }
  else if (mod == MOD_VCO2) { wave = params.vco2_wave; shape = params.vco2_shape; }
  else if (mod == MOD_LFO1) { wave = params.lfo1_wave; shape = params.lfo1_shape; }
  else { wave = params.lfo2_wave; shape = params.lfo2_shape; }

  u8g2_SetDrawColor(&g_u8g2, 1);
  draw_dashed_hline(24, 0, 127); // Zero baseline

  int prev_y = -1;
  for (int x = 0; x < 128; x++) {
    uint32_t phase = (uint32_t)x * (0xFFFFFFFF / 127);
    float sample = ui_sample_wave(phase, wave, shape);
    
    // Map -1.0..1.0 to 4..44 (centered at 24)
    int y = 24 - (int)(sample * 18.0f);
    if (y < 4) y = 4; if (y > 44) y = 44;

    if (prev_y == -1) prev_y = y;
    int ymin = (y < prev_y) ? y : prev_y;
    int h = abs(y - prev_y) + 1;
    u8g2_DrawVLine(&g_u8g2, x, ymin, h);
    u8g2_DrawVLine(&g_u8g2, x, ymin - 1, h);
    prev_y = y;
  }
}
void ui_oled_draw_graph(const OledPage *page) {
    // 1. Divider between graph and info
    u8g2_DrawHLine(&g_u8g2, 0, 48, 128);

    // 2. Info Panel (Bottom 16px)
    u8g2_SetFont(&g_u8g2, u8g2_font_4x6_tr);
    bool is_eg = (selected_module == MOD_EG1 || selected_module == MOD_EG2);
    bool fn = ui_is_fn_held();

    for (int i = 0; i < 4; i++) {
        int x_off = i * 32;
        int cx = 16 + x_off;
        
        // "Пропуск" for Sustain column (idx 2) when FN is held in EG mode
        if (is_eg && fn && i == 2) {
            if (i > 0) u8g2_DrawVLine(&g_u8g2, x_off, 48, 16);
            continue; 
        }

        // Label (A, D, S, R or Curve)
        if (page->knobs[i].title) {
            int tw = u8g2_GetStrWidth(&g_u8g2, page->knobs[i].title);
            u8g2_DrawStr(&g_u8g2, cx - (tw / 2), 55, page->knobs[i].title);
        }
        
        // Value string
        char buf[16];
        if (page->knobs[i].value_str[0] != '\0') {
            strncpy(buf, page->knobs[i].value_str, sizeof(buf));
        } else {
            snprintf(buf, sizeof(buf), "%d", page->knobs[i].value);
        }
        int vw = u8g2_GetStrWidth(&g_u8g2, buf);
        u8g2_DrawStr(&g_u8g2, cx - (vw / 2), 63, buf);
        
        if (i > 0) u8g2_DrawVLine(&g_u8g2, x_off, 48, 16);
    }

    // 3. Graph Area (Top 48px)
    if (is_eg) {
        draw_eg_graph(selected_module);
    } else if (selected_module == MOD_VCF1 || selected_module == MOD_VCF2) {
        draw_vcf_graph(selected_module);
    } else if (selected_module == MOD_VCO1 || selected_module == MOD_VCO2 || 
               selected_module == MOD_LFO1 || selected_module == MOD_LFO2) {
        draw_vco_lfo_graph(selected_module);
    } else if (selected_module == MOD_NOISE) {
        draw_noise_graph();
    } else {
        // Aesthetic Placeholder for other modules
        u8g2_SetFont(&g_u8g2, u8g2_font_5x7_tr);
        u8g2_DrawFrame(&g_u8g2, 4, 4, 120, 40);
        u8g2_DrawHLine(&g_u8g2, 4, 24, 120);
        
        char buf[32];
        snprintf(buf, sizeof(buf), "%s GRAPH", module_name(selected_module));
        int tw = u8g2_GetStrWidth(&g_u8g2, buf);
        u8g2_DrawStr(&g_u8g2, 64 - (tw/2), 20, buf);
        
        u8g2_SetFont(&g_u8g2, u8g2_font_4x6_tr);
        u8g2_DrawStr(&g_u8g2, 10, 36, "Visualizer coming soon...");
    }
    
    // 4. Baseline and selection indicators
    u8g2_DrawHLine(&g_u8g2, 0, 47, 128);
    for (int i = 0; i < 4; i++) {
        if (is_eg && fn && i == 2) continue;
        uint8_t val = page->knobs[i].value;
        int tri_x = i * 32 + (val * 31 / 127);
        u8g2_DrawTriangle(&g_u8g2, tri_x, 46, tri_x - 2, 44, tri_x + 2, 44);
    }
}

static const uint8_t image_play_bits[] = {
  0x1c, 0x00, 0x3c, 0x00, 0x7c, 0x00, 0xfc, 0x00, 0xfc, 0x01, 0xfc, 0x03, 
  0xfc, 0x07, 0xfc, 0x0f, 0xfc, 0x0f, 0xfc, 0x07, 0xfc, 0x03, 0xfc, 0x01, 
  0xfc, 0x00, 0x7c, 0x00, 0x3c, 0x00, 0x1c, 0x00
};

static const uint8_t image_pause_bits[] = {
  0x60, 0x06, 0x60, 0x06, 0x60, 0x06, 0x60, 0x06, 
  0x60, 0x06, 0x60, 0x06, 0x60, 0x06, 0x60, 0x06, 
  0x60, 0x06, 0x60, 0x06, 0x60, 0x06, 0x60, 0x06, 
  0x60, 0x06, 0x60, 0x06, 0x60, 0x06, 0x60, 0x06
};

static bool ui_oled_has_graph(ModuleID m) {
    return (m == MOD_VCO1 || m == MOD_VCO2 || m == MOD_NOISE ||
            m == MOD_VCF1 || m == MOD_VCF2 || m == MOD_LFO1 ||
            m == MOD_LFO2 || m == MOD_EG1 || m == MOD_EG2);
}

void ui_oled_draw(const OledPage *page) {
  if (!g_oled_ready || !page)
    return;

  u8g2_ClearBuffer(&g_u8g2);
  u8g2_SetFont(&g_u8g2, u8g2_font_5x7_tr);

  extern bool ui_oled_view_graph;
  if (ui_oled_view_graph && ui_oled_has_graph(selected_module)) {
      ui_oled_draw_graph(page);
      goto finalize;
  }


  for (int i = 0; i < 4; i++) {
    int x_off = i * 32;
    int cx = 16 + x_off;
    int cy = 23;
    uint8_t val =
        set_button_held ? page->knobs[i].mod_amount : page->knobs[i].value;
    if (val > 127)
      val = 127;

    u8g2_SetDrawColor(&g_u8g2, 1);
    if (page->knobs[i].title) {
      int tw = u8g2_GetStrWidth(&g_u8g2, page->knobs[i].title);
      u8g2_DrawStr(&g_u8g2, cx - (tw / 2), 7, page->knobs[i].title);
    }

    if (set_button_held) {
      // Draw filled knob for MOD Depth editing
      u8g2_DrawDisc(&g_u8g2, cx, cy, 13, U8G2_DRAW_ALL);
    } else {
      u8g2_DrawEllipse(&g_u8g2, cx, cy, 13, 13, U8G2_DRAW_ALL);
      u8g2_DrawEllipse(&g_u8g2, cx, cy, 12, 12, U8G2_DRAW_ALL);
    }

    int lx = cx + knob_lut[val][0];
    int ly = cy + knob_lut[val][1];

    if (set_button_held) {
      u8g2_SetDrawColor(&g_u8g2, 0); // Black pointer on white disc
    } else {
      u8g2_SetDrawColor(&g_u8g2, 2); // XOR pointer
    }
    u8g2_DrawLine(&g_u8g2, cx, cy, lx, ly);
    u8g2_SetDrawColor(&g_u8g2, 1);

    char buf[16]; // Increased size for formatted strings
    if (set_button_held) {
      // Show modulation depth in percent: 64 is 0%, 127 is +100%, 0 is -100%
      int depth = (int)page->knobs[i].mod_amount - 64;
      int pct = 0;
      if (depth > 0)
        pct = (depth * 100) / 63;
      else if (depth < 0)
        pct = (depth * 100) / 64;
      snprintf(buf, sizeof(buf), "%+d%%", pct);
    } else if (page->knobs[i].value_str[0] != '\0') {
      snprintf(buf, sizeof(buf), "%s", page->knobs[i].value_str);
    } else {
      snprintf(buf, sizeof(buf), "%d", val);
    }
    int vw = u8g2_GetStrWidth(&g_u8g2, buf);
    u8g2_DrawStr(&g_u8g2, cx - (vw / 2), 45, buf);

    if (page->layout_id != 3 && page->layout_id != 4) {
      // Draw the frame XBM
      u8g2_DrawXBM(&g_u8g2, x_off + 3, 47, 27, 7, image_bar1_bits);

      // Draw dynamic interior lines for modulation amount
      int depth = (int)page->knobs[i].mod_amount - 64; // -64 to +63
      int center_x = cx;
      int bar_y = 47 + 2;
      int bar_h = 3;

      if (depth != 0) {
        int w = (depth > 0) ? (depth * 12 / 63) : (depth * 12 / 64);
        if (w > 0) {
          // Positive depth: draw from center to right
          for (int x = 0; x <= w; x++) {
            u8g2_DrawVLine(&g_u8g2, center_x + x, bar_y, bar_h);
          }
        } else if (w < 0) {
          // Negative depth: draw from center to left
          for (int x = 0; x >= w; x--) {
            u8g2_DrawVLine(&g_u8g2, center_x + x, bar_y, bar_h);
          }
        }
      }

      if (page->knobs[i].mod_label) {
        int mw = u8g2_GetStrWidth(&g_u8g2, page->knobs[i].mod_label);
        u8g2_DrawStr(&g_u8g2, cx - (mw / 2), 63, page->knobs[i].mod_label);
      }
    }
  }

  if (page->layout_id == 3) {
      u8g2_SetDrawColor(&g_u8g2, 1);
      int w;
      char b1[16], b2[16];
      
      w = u8g2_GetStrWidth(&g_u8g2, "page");
      u8g2_DrawStr(&g_u8g2, 16 - (w/2), 54, "page");
      snprintf(b1, sizeof(b1), "%d/4", seq_state.current_page + 1);
      w = u8g2_GetStrWidth(&g_u8g2, b1);
      u8g2_DrawStr(&g_u8g2, 16 - (w/2), 64, b1);

      w = u8g2_GetStrWidth(&g_u8g2, "step");
      u8g2_DrawStr(&g_u8g2, 48 - (w/2), 54, "step");
      
      int max_step = 64;
      for (int i = 0; i < SEQ_MAX_STEPS; i++) {
          if (current_seq.steps[i].stop_flag) {
              max_step = i;
              break;
          }
      }
      if (max_step < 1) max_step = 1;
      
      snprintf(b2, sizeof(b2), "%d/%d", seq_state.current_step + 1, max_step);
      w = u8g2_GetStrWidth(&g_u8g2, b2);
      u8g2_DrawStr(&g_u8g2, 48 - (w/2), 64, b2);

      if (seq_state.is_playing) {
          // Play icon
          u8g2_DrawTriangle(&g_u8g2, 72 + 14, 32 + 24, 72 + 2, 32 + 17, 72 + 2, 32 + 30);
      } else {
          // Pause icon
          u8g2_DrawBox(&g_u8g2, 72 + 2, 48 + 1, 4, 14);
          u8g2_DrawBox(&g_u8g2, 72 + 11, 48 + 1, 4, 14);
      }

      w = u8g2_GetStrWidth(&g_u8g2, "slot");
      u8g2_DrawStr(&g_u8g2, 112 - (w/2), 54, "slot");
      w = u8g2_GetStrWidth(&g_u8g2, "---");
      u8g2_DrawStr(&g_u8g2, 112 - (w/2), 64, "---");
  }

  finalize:
    // --- 5. Draw status message overlay ---
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (ui_status_msg[0] != '\0' && now < ui_status_msg_timeout_ms) {
    u8g2_SetDrawColor(&g_u8g2, 0); // Black background
    u8g2_DrawBox(&g_u8g2, 0, 20, 128, 24);
    u8g2_SetDrawColor(&g_u8g2, 1); // White frame
    u8g2_DrawFrame(&g_u8g2, 0, 20, 128, 24);

    u8g2_SetFont(&g_u8g2, u8g2_font_6x12_tr);
    int w = u8g2_GetStrWidth(&g_u8g2, ui_status_msg);
    u8g2_DrawStr(&g_u8g2, (128 - w) / 2, 36, ui_status_msg);
  }

  u8g2_SendBuffer(&g_u8g2);
}

// ##waveforms

// ```c
//     // Синусоида (14x7)
//     static const uint8_t icon_sin_bits[] = {0x1c, 0x00, 0x22, 0x00, 0x41,
//     0x00, 0x41, 0x00, 0x00, 0x0a, 0x00, 0x14, 0x00, 0x08};
// // Пила (Saw)
// static const uint8_t icon_saw_bits[] = {0x01, 0x00, 0x03, 0x00, 0x05, 0x00,
// 0x09, 0x00, 0x11, 0x00, 0x21, 0x00, 0x7f, 0x00};
// // Треугольник (Tri)
// static const uint8_t icon_tri_bits[] = {0x00, 0x00, 0x04, 0x00, 0x0a, 0x00,
// 0x11, 0x00, 0x20, 0x08, 0x40, 0x04, 0x80, 0x03};
// // Пульс (Pulse 25%)
// static const uint8_t icon_pulse_bits[] = {0x1f, 0x00, 0x10, 0x00, 0x10, 0x00,
// 0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0xf0, 0x3f};
// // PAM4 (Ступеньки)
// static const uint8_t icon_pam4_bits[] = {0x03, 0x00, 0x04, 0x00, 0x18, 0x00,
// 0x20, 0x00, 0x60, 0x00, 0x80, 0x00, 0x00, 0x03};

// typedef enum
// {
//   WAVE_SIN,
//   WAVE_TRI,
//   WAVE_SAW,
//   WAVE_REVSAW,
//   WAVE_SQUARE,
//   WAVE_PULSE,
//   WAVE_PAM4
// } WaveType;

// void draw_wave_icon(u8g2_t *u8g2, int x, int y, WaveType type, bool
// left_half)
// {
//   // Устанавливаем окно отсечения, чтобы рисовать только половину иконки (7
//   пикселей в ширину) if (left_half)
//   {
//     u8g2_SetMaxClipWindow(u8g2); // Сброс
//     u8g2_SetClipWindow(u8g2, x, y, x + 6, y + 7);
//   }
//   else
//   {
//     u8g2_SetClipWindow(u8g2, x + 7, y, x + 13, y + 7);
//   }

//   switch (type)
//   {
//   case WAVE_SIN:
//     u8g2_DrawXBM(u8g2, x, y, 14, 7, icon_sin_bits);
//     break;
//   case WAVE_TRI:
//     u8g2_DrawXBM(u8g2, x, y, 14, 7, icon_tri_bits);
//     break;
//   case WAVE_SAW:
//     u8g2_DrawXBM(u8g2, x, y, 14, 7, icon_saw_bits);
//     break;
//   case WAVE_REVSAW:                                 // Рисуем Saw зеркально
//   по горизонтали
//     u8g2_DrawXBM(u8g2, x, y, 14, 7, icon_saw_bits); // Здесь логика
//     зеркалирования если нужно break;
//   case WAVE_SQUARE:
//     u8g2_DrawFrame(u8g2, x, y, 14, 7);
//     break; // Экономим XBM
//   case WAVE_PULSE:
//     u8g2_DrawXBM(u8g2, x, y, 14, 7, icon_pulse_bits);
//     break;
//   case WAVE_PAM4:
//     u8g2_DrawXBM(u8g2, x, y, 14, 7, icon_pam4_bits);
//     break;
//   }
//   u8g2_SetMaxClipWindow(u8g2); // Возвращаем полный экран
// }

// // Пример отрисовки гибрида Sin-Saw
// void draw_hybrid(u8g2_t *u8g2, int x, int y, WaveType left, WaveType right)
// {
//   draw_wave_icon(u8g2, x, y, left, true);
//   draw_wave_icon(u8g2, x, y, right, false);
// }
// ```
