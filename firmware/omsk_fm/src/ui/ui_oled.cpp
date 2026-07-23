#include "ui_oled.h"
#include "../sw_config.h"
#include "../../../shared/image_logo.h"
#include "ui_logic.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "u8g2.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../synth/synth.h"
#include "../synth/fm_synth.h"
#include "ui_state.h"

#define OLED_I2C_INST OLED_I2C
static u8g2_t g_u8g2;
static int g_oled_ready = 0;

static const char* LFO_NAMES[] = {"TRI ", "SAW+", "SAW-", "SQR ", "SIN ", "S&H "};

struct DexedOp {
  uint8_t x;
  uint8_t y;
  uint8_t link;
  uint8_t fb;
};

static void draw_algorithm_diagram(int algo, int x_start) {
  // Print "Alg: X" at the top-left (Y=6)
  u8g2_SetFont(&g_u8g2, u8g2_font_4x6_tf);
  char num_str[16];
  snprintf(num_str, sizeof(num_str), "Alg: %d", algo + 1);
  u8g2_DrawStr(&g_u8g2, x_start + 2, 6, num_str);
  
  // Grid area starts at Y = 6, X = 42
  int grid_x = 42;
  int grid_y = 6;
  
  // Grid step sizes (5-pixel gaps between 8x9 boxes)
  int col_w = 13;
  int row_h = 14;
  
  static const DexedOp k_dexed_algos[32][6] = {
    // Alg 1 (case 0 in dexed)
    { {2, 3, 1, 0}, {2, 2, 0, 0}, {3, 3, 2, 0}, {3, 2, 0, 0}, {3, 1, 0, 0}, {3, 0, 0, 1} },
    // Alg 2
    { {2, 3, 1, 0}, {2, 2, 0, 1}, {3, 3, 2, 0}, {3, 2, 0, 0}, {3, 1, 0, 0}, {3, 0, 0, 0} },
    // Alg 3
    { {2, 3, 1, 0}, {2, 2, 0, 0}, {2, 1, 0, 0}, {3, 3, 2, 0}, {3, 2, 0, 0}, {3, 1, 0, 1} },
    // Alg 4
    { {2, 3, 1, 0}, {2, 2, 0, 0}, {2, 1, 0, 0}, {3, 3, 2, 0}, {3, 2, 0, 0}, {3, 1, 0, 2} },
    // Alg 5
    { {2, 3, 1, 0}, {2, 2, 0, 0}, {3, 3, 1, 0}, {3, 2, 0, 0}, {4, 3, 2, 0}, {4, 2, 0, 1} },
    // Alg 6
    { {2, 3, 1, 0}, {2, 2, 0, 0}, {3, 3, 1, 0}, {3, 2, 0, 0}, {4, 3, 2, 0}, {4, 2, 0, 3} },
    // Alg 7
    { {2, 3, 1, 0}, {2, 2, 0, 0}, {3, 3, 2, 0}, {3, 2, 0, 0}, {4, 2, 7, 0}, {4, 1, 0, 1} },
    // Alg 8
    { {2, 3, 1, 0}, {2, 2, 0, 0}, {3, 3, 2, 0}, {3, 2, 0, 4}, {4, 2, 7, 0}, {4, 1, 0, 0} },
    // Alg 9
    { {2, 3, 1, 0}, {2, 2, 0, 1}, {3, 3, 2, 0}, {3, 2, 0, 0}, {4, 2, 7, 0}, {4, 1, 0, 0} },
    // Alg 10
    { {3, 3, 2, 0}, {3, 2, 0, 0}, {3, 1, 0, 1}, {2, 3, 1, 0}, {1, 2, 1, 0}, {2, 2, 0, 0} },
    // Alg 11
    { {3, 3, 2, 0}, {3, 2, 0, 0}, {3, 1, 0, 0}, {2, 3, 1, 0}, {1, 2, 1, 0}, {2, 2, 0, 1} },
    // Alg 12
    { {4, 3, 2, 0}, {4, 2, 0, 1}, {2, 3, 6, 0}, {1, 2, 1, 0}, {2, 2, 0, 0}, {3, 2, 7, 0} },
    // Alg 13
    { {4, 3, 2, 0}, {4, 2, 0, 0}, {2, 3, 6, 0}, {1, 2, 1, 0}, {2, 2, 0, 0}, {3, 2, 7, 1} },
    // Alg 14
    { {2, 3, 1, 0}, {2, 2, 0, 0}, {3, 3, 2, 0}, {3, 2, 0, 0}, {3, 1, 0, 0}, {4, 1, 7, 1} },
    // Alg 15
    { {2, 3, 1, 0}, {2, 2, 0, 4}, {3, 3, 2, 0}, {3, 2, 0, 0}, {3, 1, 0, 0}, {4, 1, 7, 0} },
    // Alg 16
    { {3, 3, 0, 0}, {2, 2, 1, 0}, {3, 2, 0, 0}, {3, 1, 0, 0}, {4, 2, 7, 0}, {4, 1, 0, 1} },
    // Alg 17
    { {3, 3, 0, 0}, {2, 2, 1, 4}, {3, 2, 0, 0}, {3, 1, 0, 0}, {4, 2, 7, 0}, {4, 1, 0, 0} },
    // Alg 18
    { {3, 3, 0, 0}, {2, 2, 1, 0}, {3, 2, 0, 4}, {4, 2, 7, 0}, {4, 1, 0, 0}, {4, 0, 0, 0} },
    // Alg 19
    { {2, 3, 1, 0}, {2, 2, 0, 0}, {2, 1, 0, 0}, {3, 3, 1, 0}, {4, 3, 2, 0}, {3, 2, 3, 1} },
    // Alg 20
    { {1, 3, 1, 0}, {2, 3, 6, 0}, {1, 2, 3, 1}, {4, 3, 2, 0}, {3, 2, 1, 0}, {4, 2, 0, 0} },
    // Alg 21
    { {1, 3, 1, 0}, {2, 3, 1, 0}, {1, 2, 3, 1}, {3, 3, 1, 0}, {4, 3, 2, 0}, {3, 2, 3, 0} },
    // Alg 22
    { {1, 3, 1, 0}, {1, 2, 0, 0}, {2, 3, 1, 0}, {3, 3, 1, 0}, {4, 3, 2, 0}, {3, 2, 4, 1} },
    // Alg 23
    { {1, 3, 1, 0}, {2, 3, 1, 0}, {2, 2, 0, 0}, {3, 3, 1, 0}, {4, 3, 2, 0}, {3, 2, 3, 1} },
    // Alg 24
    { {0, 3, 1, 0}, {1, 3, 1, 0}, {2, 3, 1, 0}, {3, 3, 1, 0}, {4, 3, 2, 0}, {3, 2, 4, 1} },
    // Alg 25
    { {0, 3, 1, 0}, {1, 3, 1, 0}, {2, 3, 1, 0}, {3, 3, 1, 0}, {4, 3, 2, 0}, {3, 2, 3, 1} },
    // Alg 26
    { {1, 3, 1, 0}, {2, 3, 6, 0}, {2, 2, 0, 0}, {4, 3, 2, 0}, {3, 2, 1, 0}, {4, 2, 0, 1} },
    // Alg 27
    { {1, 3, 1, 0}, {2, 3, 6, 0}, {2, 2, 0, 1}, {4, 3, 2, 0}, {3, 2, 1, 0}, {4, 2, 0, 0} },
    // Alg 28
    { {2, 3, 1, 0}, {2, 2, 0, 0}, {3, 3, 1, 0}, {3, 2, 0, 0}, {3, 1, 0, 1}, {4, 3, 2, 0} },
    // Alg 29
    { {1, 3, 1, 0}, {2, 3, 1, 0}, {3, 3, 1, 0}, {3, 2, 0, 0}, {4, 3, 2, 0}, {4, 2, 0, 1} },
    // Alg 30
    { {1, 3, 1, 0}, {2, 3, 1, 0}, {3, 3, 1, 0}, {3, 2, 0, 0}, {3, 1, 0, 1}, {4, 3, 2, 0} },
    // Alg 31
    { {0, 3, 1, 0}, {1, 3, 1, 0}, {2, 3, 1, 0}, {3, 3, 1, 0}, {4, 3, 2, 0}, {4, 2, 0, 1} },
    // Alg 32
    { {0, 3, 1, 0}, {1, 3, 1, 0}, {2, 3, 1, 0}, {3, 3, 1, 0}, {4, 3, 1, 0}, {5, 3, 2, 1} }
  };
  
  const DexedOp *ops = k_dexed_algos[algo < 32 ? algo : 0];
  
  // 1. Draw connection lines
  for (int op = 0; op < 6; op++) {
    const DexedOp &dop = ops[op];
    int cx = grid_x + dop.x * col_w;
    int cy = grid_y + dop.y * row_h;
    
    switch (dop.link) {
      case 0: // LINE DOWN
        u8g2_DrawVLine(&g_u8g2, cx + 4, cy + 10, (dop.y == 3) ? (63 - (cy + 10) + 1) : 5);
        break;
      case 1: // LINE RIGHT
        u8g2_DrawVLine(&g_u8g2, cx + 4, cy + 10, 3);
        u8g2_DrawHLine(&g_u8g2, cx + 4, cy + 12, col_w);
        break;
      case 2: // LINE DOWN JOIN
        u8g2_DrawVLine(&g_u8g2, cx + 4, cy + 10, (dop.y == 3) ? (63 - (cy + 10) + 1) : 3);
        break;
      case 3: // LINE RIGHT AND DOWN
        u8g2_DrawVLine(&g_u8g2, cx + 4, cy + 10, 5);
        u8g2_DrawHLine(&g_u8g2, cx + 4, cy + 12, col_w);
        u8g2_DrawVLine(&g_u8g2, cx + 4 + col_w, cy + 12, (dop.y == 3) ? (63 - (cy + 12) + 1) : 3);
        break;
      case 4: // LINE RIGHT+LEFT AND DOWN
        u8g2_DrawVLine(&g_u8g2, cx + 4, cy + 10, 5);
        u8g2_DrawHLine(&g_u8g2, cx + 4 - col_w, cy + 12, col_w * 2);
        u8g2_DrawVLine(&g_u8g2, cx + 4 + col_w, cy + 12, (dop.y == 3) ? (63 - (cy + 12) + 1) : 3);
        u8g2_DrawVLine(&g_u8g2, cx + 4 - col_w, cy + 12, (dop.y == 3) ? (63 - (cy + 12) + 1) : 3);
        break;
      case 6: // LINE RIGHT double distance
        u8g2_DrawVLine(&g_u8g2, cx + 4, cy + 10, 3);
        u8g2_DrawHLine(&g_u8g2, cx + 4, cy + 12, col_w * 2);
        break;
      case 7: // LINE LEFT
        u8g2_DrawVLine(&g_u8g2, cx + 4, cy + 10, 3);
        u8g2_DrawHLine(&g_u8g2, cx + 4 - col_w, cy + 12, col_w);
        break;
    }
  }
  
  // 2. Draw operator boxes with numbers (size 8x9)
  u8g2_SetFont(&g_u8g2, u8g2_font_4x6_tf);
  for (int op = 0; op < 6; op++) {
    const DexedOp &dop = ops[op];
    int cx = grid_x + dop.x * col_w;
    int cy = grid_y + dop.y * row_h;
    
    u8g2_SetDrawColor(&g_u8g2, 0);
    u8g2_DrawBox(&g_u8g2, cx + 1, cy + 1, 8, 9);
    u8g2_SetDrawColor(&g_u8g2, 1);
    
    u8g2_DrawFrame(&g_u8g2, cx + 1, cy + 1, 8, 9);
    
    char op_str[2] = {(char)('1' + op), '\0'};
    u8g2_DrawStr(&g_u8g2, cx + 3, cy + 8, op_str);
  }
  
  // 3. Draw feedback loops (2-pixel gap from edges)
  for (int op = 0; op < 6; op++) {
    const DexedOp &dop = ops[op];
    if (dop.fb > 0) {
      int cx = grid_x + dop.x * col_w;
      int cy = grid_y + dop.y * row_h;
      
      int y_loop_top = cy - 2;
      
      // Draw top vertical segment from top of box (cy + 1) to y_loop_top (cy - 2)
      u8g2_DrawVLine(&g_u8g2, cx + 4, y_loop_top, 3);
      
      if (dop.fb == 1) { // 1-row feedback loop on the right
        int x_loop = cx + 11;
        int y_loop_bottom = cy + 12;
        // Top horizontal segment
        u8g2_DrawHLine(&g_u8g2, cx + 4, y_loop_top, 8);
        // Right vertical segment
        u8g2_DrawVLine(&g_u8g2, x_loop, y_loop_top, y_loop_bottom - y_loop_top + 1);
        // Bottom horizontal segment
        u8g2_DrawHLine(&g_u8g2, cx + 4, y_loop_bottom, 8);
        // Bottom vertical segment touching bottom of box (cy + 9)
        u8g2_DrawVLine(&g_u8g2, cx + 4, cy + 9, 4);
      } 
      else if (dop.fb == 2 || dop.fb == 3) { // Spans to bottom of Row 3
        int x_loop = cx + 11;
        int y_loop_bottom = grid_y + 3 * row_h + 12; // Always bottom of Row 3
        // Top horizontal segment
        u8g2_DrawHLine(&g_u8g2, cx + 4, y_loop_top, 8);
        // Right vertical segment
        u8g2_DrawVLine(&g_u8g2, x_loop, y_loop_top, y_loop_bottom - y_loop_top + 1);
        // Bottom horizontal segment
        u8g2_DrawHLine(&g_u8g2, cx + 4, y_loop_bottom, 8);
        // Bottom vertical segment touching bottom of Row 3 box
        u8g2_DrawVLine(&g_u8g2, cx + 4, grid_y + 3 * row_h + 9, 4);
      } 
      else if (dop.fb == 4) { // 1-row feedback loop on the left
        int x_loop = cx - 2;
        int y_loop_bottom = cy + 12;
        // Top horizontal segment
        u8g2_DrawHLine(&g_u8g2, x_loop, y_loop_top, 7);
        // Left vertical segment
        u8g2_DrawVLine(&g_u8g2, x_loop, y_loop_top, y_loop_bottom - y_loop_top + 1);
        // Bottom horizontal segment
        u8g2_DrawHLine(&g_u8g2, x_loop, y_loop_bottom, 7);
        // Bottom vertical segment touching bottom of box (cy + 9)
        u8g2_DrawVLine(&g_u8g2, cx + 4, cy + 9, 4);
      }
    }
  }
}

static uint8_t u8x8_byte_pico_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
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

static uint8_t u8x8_gpio_and_delay_pico(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
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

void ui_oled_init(void) {
  i2c_init(OLED_I2C_INST, 400 * 1000);
  gpio_set_function(PIN_OLED_SCL, GPIO_FUNC_I2C);
  gpio_set_function(PIN_OLED_SDA, GPIO_FUNC_I2C);
  gpio_pull_up(PIN_OLED_SCL);
  gpio_pull_up(PIN_OLED_SDA);

#if CFG_OLED_TYPE == 1
  u8g2_Setup_sh1107_i2c_64x128_f(&g_u8g2, U8G2_R1, u8x8_byte_pico_hw_i2c, u8x8_gpio_and_delay_pico);
#else
#ifdef OLED_FLIP
  u8g2_Setup_ssd1312_i2c_128x64_noname_f(&g_u8g2, U8G2_R2, u8x8_byte_pico_hw_i2c, u8x8_gpio_and_delay_pico);
#else
  u8g2_Setup_ssd1312_i2c_128x64_noname_f(&g_u8g2, U8G2_R0, u8x8_byte_pico_hw_i2c, u8x8_gpio_and_delay_pico);
#endif
#endif

  u8g2_InitDisplay(&g_u8g2);
  u8g2_SetPowerSave(&g_u8g2, 0);
  u8x8_SetI2CAddress(&g_u8g2.u8x8, 0x3C << 1);

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
  sleep_ms(1500);

  g_oled_ready = 1;
  ui_oled_set_brightness(OLED_BRIGHTNESS_PERCENT);
}

void ui_oled_set_power(bool on) {
  if (g_oled_ready) {
    u8g2_SetPowerSave(&g_u8g2, on ? 0 : 1);
  }
}

void ui_oled_set_brightness(uint8_t percentage) {
  if (g_oled_ready) {
    u8g2_SetContrast(&g_u8g2, (percentage * 255) / 100);
  }
}

static void draw_dashed_vline(int x, int y1, int y2) {
  for (int y = y1; y <= y2; y += 2) {
    u8g2_DrawPixel(&g_u8g2, x, y);
  }
}

static void draw_eg_graph(void) {
  FmOperatorPatch &op = g_active_patch.op[active_op];
  int x0 = 10, y0 = 42;
  int w = 24;
  u8g2_DrawHLine(&g_u8g2, 5, y0, 118);
  int x = x0;
  int last_y = y0;
  for (int i = 0; i < 4; i++) {
    int lvl = y0 - (op.levels[i] * 28 / 99);
    int next_x = x + (100 - op.rates[i]) * w / 99 + 4;
    if (next_x > 120) next_x = 120;
    u8g2_DrawLine(&g_u8g2, x, last_y, next_x, lvl);
    u8g2_DrawBox(&g_u8g2, next_x - 1, lvl - 1, 3, 3);
    x = next_x;
    last_y = lvl;
  }
}

static void draw_pitch_eg_graph(void) {
  int x0 = 10, y0 = 28;
  int w = 24;
  u8g2_DrawHLine(&g_u8g2, 5, y0, 118);
  int x = x0;
  int last_y = y0;
  for (int i = 0; i < 4; i++) {
    // Pitch EG levels are centered around 50 (0 detune)
    int lvl = y0 - ((int)g_active_patch.pitch_eg_levels[i] - 50) * 14 / 50;
    int next_x = x + (100 - g_active_patch.pitch_eg_rates[i]) * w / 99 + 4;
    if (next_x > 120) next_x = 120;
    u8g2_DrawLine(&g_u8g2, x, last_y, next_x, lvl);
    u8g2_DrawBox(&g_u8g2, next_x - 1, lvl - 1, 3, 3);
    x = next_x;
    last_y = lvl;
  }
}

static void draw_vcf_graph(void) {
  uint8_t cutoff = params.vcf1_cutoff;
  uint8_t res = params.vcf1_res;
  draw_dashed_vline(cutoff, 13, 44);
  int prev_y = -1;
  for (int x = 0; x < 128; x++) {
    int y = 28;
    if (x < cutoff) {
      if (cutoff - x < 15) {
        float dist = (15 - (cutoff - x)) / 15.0f;
        y = 28 - (int)(dist * (res * 12.0f / 127.0f));
      }
    } else {
      float dist = (x - cutoff);
      float peak = (res * 12.0f / 127.0f);
      y = 28 - (int)(peak * expf(-dist / 5.0f)) + (int)(dist * 0.5f);
    }
    if (y < 13) y = 13;
    if (y > 44) y = 44;
    if (prev_y == -1) prev_y = y;
    int ymin = (y < prev_y) ? y : prev_y;
    int h = abs(y - prev_y) + 1;
    u8g2_DrawVLine(&g_u8g2, x, ymin, h);
    prev_y = y;
  }
}

static bool ui_oled_has_graph(ModuleID m) {
  return (m == MOD_EG || m == MOD_FILT || m == MOD_PITCH_EG);
}

static void ui_oled_draw_graph(const OledPage *page) {
  // Divider
  u8g2_DrawHLine(&g_u8g2, 0, 45, 128);

  // Draw appropriate graph in Y=12..44
  if (selected_module == MOD_EG) {
    draw_eg_graph();
  } else if (selected_module == MOD_FILT) {
    draw_vcf_graph();
  } else if (selected_module == MOD_PITCH_EG) {
    draw_pitch_eg_graph();
  }

  // Draw info panel at bottom Y=46..63
  u8g2_SetFont(&g_u8g2, u8g2_font_4x6_tf);
  for (int i = 0; i < 4; i++) {
    int x_off = i * 32;
    int cx = 16 + x_off;

    if (page->knobs[i].title) {
      int tw = u8g2_GetStrWidth(&g_u8g2, page->knobs[i].title);
      u8g2_DrawStr(&g_u8g2, cx - (tw / 2), 52, page->knobs[i].title);
    }

    char buf[16];
    if (page->knobs[i].value_str[0] != '\0') {
      snprintf(buf, sizeof(buf), "%s", page->knobs[i].value_str);
    } else {
      snprintf(buf, sizeof(buf), "%d", page->knobs[i].value);
    }
    int vw = u8g2_GetStrWidth(&g_u8g2, buf);
    u8g2_DrawStr(&g_u8g2, cx - (vw / 2), 62, buf);
    if (i > 0) u8g2_DrawVLine(&g_u8g2, x_off, 46, 18);
  }
}

void ui_oled_draw(const OledPage *page) {
  if (!g_oled_ready || !page) return;

  u8g2_ClearBuffer(&g_u8g2);
  u8g2_SetDrawColor(&g_u8g2, 1);

  // Top Status Bar (Y=0..10)
  u8g2_SetFont(&g_u8g2, u8g2_font_5x7_tf);
  char title_buf[32] = "";
  char page_buf[16] = "";
  char op_buf[16] = "";

  switch (selected_module) {
    case MOD_FREQ: strcpy(title_buf, "OSCILLATOR FREQ"); break;
    case MOD_LVL_MOD: strcpy(title_buf, "LEVELS & VEL"); break;
    case MOD_EG:
      strcpy(title_buf, "EG");
      snprintf(page_buf, sizeof(page_buf), "%d/2", sub_page + 1);
      break;
    case MOD_LFO:
      strcpy(title_buf, "LFO");
      snprintf(page_buf, sizeof(page_buf), "%d/2", sub_page + 1);
      break;
    case MOD_KBDSCALE:
      strcpy(title_buf, "KBD SCALE");
      snprintf(page_buf, sizeof(page_buf), "%d/2", sub_page + 1);
      break;
    case MOD_FILT: strcpy(title_buf, "GLOBAL FILTER"); break;
    case MOD_ALGO_FB:
#ifdef SHOW_ICONS
      strcpy(title_buf, "ALGO");
#else
      strcpy(title_buf, "ALGO & FEEDBACK");
#endif
      break;
    case MOD_PITCH_EG:
      strcpy(title_buf, "PITCH EG");
      snprintf(page_buf, sizeof(page_buf), "%d/2", sub_page + 1);
      break;
    case MOD_MEM: strcpy(title_buf, "PATCH PRESETS"); break;
    case MOD_SYS: strcpy(title_buf, "SYSTEM OPTIONS"); break;
    default: strcpy(title_buf, "PARAMETERS"); break;
  }

  // Show active operator only for operator-specific modules
  bool is_op_specific = (selected_module == MOD_FREQ ||
                         selected_module == MOD_LVL_MOD ||
                         selected_module == MOD_EG ||
                         selected_module == MOD_KBDSCALE);
  if (is_op_specific) {
    snprintf(op_buf, sizeof(op_buf), "OP %d", active_op + 1);
  }

  // Print title
  u8g2_DrawStr(&g_u8g2, 2, 8, title_buf);

  // Print page number next to title if present
  if (page_buf[0] != '\0') {
    int title_w = u8g2_GetStrWidth(&g_u8g2, title_buf);
    u8g2_DrawStr(&g_u8g2, title_w + 8, 8, page_buf);
  }

  // Print active operator on the right
  if (op_buf[0] != '\0') {
    int op_w = u8g2_GetStrWidth(&g_u8g2, op_buf);
    u8g2_DrawStr(&g_u8g2, 126 - op_w, 8, op_buf);
  }
  
  // Thin separator line
#ifdef SHOW_ICONS
  if (selected_module == MOD_ALGO_FB) {
    u8g2_DrawHLine(&g_u8g2, 0, 11, 34);
    u8g2_DrawVLine(&g_u8g2, 34, 0, 12);
  } else {
    u8g2_DrawHLine(&g_u8g2, 0, 11, 128);
  }
#else
  u8g2_DrawHLine(&g_u8g2, 0, 11, 128);
#endif

  // Graph overlay or knobs
  if (ui_oled_view_graph && ui_oled_has_graph(selected_module)) {
    ui_oled_draw_graph(page);
  } else {
    // Normal Knobs View (Y=12..63)
    for (int i = 0; i < 4; i++) {
      if (!page->knobs[i].title) continue;

      int x_off = i * 32;
      int cx = 16 + x_off;
      int cy = 30; // Shifted down slightly to leave space for title

      bool hide_label = false;
#ifdef SHOW_ICONS
      if (selected_module == MOD_FILT && (i == 1 || i == 2)) hide_label = true;
      if ((selected_module == MOD_EG || selected_module == MOD_PITCH_EG) && (i == 1 || i == 2 || i == 3)) hide_label = true;
      if (selected_module == MOD_ALGO_FB && (i == 1 || i == 2 || i == 3)) hide_label = true;
#endif

      if (!hide_label) {
        u8g2_SetFont(&g_u8g2, u8g2_font_4x6_tf);
        int tw = u8g2_GetStrWidth(&g_u8g2, page->knobs[i].title);
        u8g2_DrawStr(&g_u8g2, cx - (tw / 2), 18, page->knobs[i].title);
      }

#ifdef SHOW_ICONS
      bool drawn_icon = false;
      int icon_x = cx - 14;
      int icon_y = 22;
      int icon_w = 28;
      int icon_h = 18;

      if (selected_module == MOD_LFO && sub_page == 0 && i == 0) {
        u8g2_DrawFrame(&g_u8g2, icon_x, icon_y, icon_w, icon_h);
        int prev_y = -1;
        for (int dx = 2; dx < 26; dx++) {
          float t = (float)(dx - 2) / 23.0f * 2.0f * (float)M_PI;
          int ly = icon_y + 9;
          uint8_t wave_val = g_active_patch.lfo_waveform % 6;
          if (wave_val == 0) { // TRI
            if (dx < 8) ly = icon_y + 9 - (dx - 2) * 6 / 6;
            else if (dx < 20) ly = icon_y + 3 + (dx - 8) * 12 / 12;
            else ly = icon_y + 15 - (dx - 20) * 6 / 6;
          } else if (wave_val == 1) { // SAW+
            ly = icon_y + 15 - (dx - 2) * 12 / 23;
            if (dx == 25) u8g2_DrawVLine(&g_u8g2, icon_x + dx, icon_y + 3, 12);
          } else if (wave_val == 2) { // SAW-
            ly = icon_y + 3 + (dx - 2) * 12 / 23;
            if (dx == 2) u8g2_DrawVLine(&g_u8g2, icon_x + dx, icon_y + 3, 12);
          } else if (wave_val == 3) { // SQR
            if (dx < 14) ly = icon_y + 3;
            else ly = icon_y + 15;
            if (dx == 14) u8g2_DrawVLine(&g_u8g2, icon_x + dx, icon_y + 3, 12);
          } else if (wave_val == 4) { // SINE
            ly = icon_y + 9 + (int)(6.0f * sinf(t));
          } else { // S&H
            if (dx < 8) ly = icon_y + 12;
            else if (dx < 14) ly = icon_y + 4;
            else if (dx < 20) ly = icon_y + 15;
            else ly = icon_y + 8;
            if (dx == 8) u8g2_DrawVLine(&g_u8g2, icon_x + 8, icon_y + 4, 8);
            if (dx == 14) u8g2_DrawVLine(&g_u8g2, icon_x + 14, icon_y + 4, 11);
            if (dx == 20) u8g2_DrawVLine(&g_u8g2, icon_x + 20, icon_y + 8, 7);
          }
          int lx = icon_x + dx;
          if (prev_y != -1 && wave_val != 0 && wave_val != 3 && wave_val != 5) {
            u8g2_DrawLine(&g_u8g2, lx - 1, prev_y, lx, ly);
          } else if (wave_val == 0 || wave_val == 3 || wave_val == 5) {
            u8g2_DrawPixel(&g_u8g2, lx, ly);
            if (prev_y != -1 && wave_val == 0) u8g2_DrawLine(&g_u8g2, lx - 1, prev_y, lx, ly);
          }
          prev_y = ly;
        }
        drawn_icon = true;
      } else if (selected_module == MOD_FILT) {
        if (i == 0) {
          int graph_w = 92;
          u8g2_DrawFrame(&g_u8g2, icon_x, icon_y, graph_w, icon_h);
          int cut_val = params.vcf1_cutoff;
          int res_val = params.vcf1_res;
          
          float xc_f = (float)icon_x + 2.0f + ((float)cut_val / 127.0f) * (float)(graph_w - 4);
          float Q = (float)res_val / 127.0f;
          int prev_y = -1;
          for (int dx = 2; dx < graph_w - 2; dx++) {
            float lx = (float)(icon_x + dx);
            float dist = lx - xc_f;
            float val = 0.0f;
            // Draw low-pass curve similar to omsk_grain
            if (dist < 0.0f) {
              val = 4.0f + Q * 8.0f * expf(dist / 8.0f);
            } else {
              val = (4.0f + Q * 8.0f) * expf(-dist / (5.0f + (1.0f - Q) * 8.0f));
            }
            if (val > (float)(icon_h - 4)) val = (float)(icon_h - 4);
            int ly = icon_y + icon_h - 2 - (int)val;
            if (prev_y != -1) {
              u8g2_DrawLine(&g_u8g2, (int)lx - 1, prev_y, (int)lx, ly);
            }
            prev_y = ly;
          }
          drawn_icon = true;
        } else if (i == 1 || i == 2) {
          drawn_icon = true; // Handled by column 0
        }
      } else if (selected_module == MOD_EG || selected_module == MOD_PITCH_EG) {
        if (i == 0) {
          uint8_t R1, R2, R3, R4, L1, L2, L3, L4;
          if (selected_module == MOD_EG) {
            FmOperatorPatch &op = g_active_patch.op[active_op];
            R1 = op.rates[0]; R2 = op.rates[1]; R3 = op.rates[2]; R4 = op.rates[3];
            L1 = op.levels[0]; L2 = op.levels[1]; L3 = op.levels[2]; L4 = op.levels[3];
          } else {
            R1 = g_active_patch.pitch_eg_rates[0]; R2 = g_active_patch.pitch_eg_rates[1];
            R3 = g_active_patch.pitch_eg_rates[2]; R4 = g_active_patch.pitch_eg_rates[3];
            L1 = g_active_patch.pitch_eg_levels[0]; L2 = g_active_patch.pitch_eg_levels[1];
            L3 = g_active_patch.pitch_eg_levels[2]; L4 = g_active_patch.pitch_eg_levels[3];
          }

          int w1 = 4 + (99 - R1) * 20 / 99;
          int w2 = 4 + (99 - R2) * 20 / 99;
          int w3 = 4 + (99 - R3) * 20 / 99;
          int w4 = 4 + (99 - R4) * 20 / 99;
          int total_w = w1 + w2 + w3 + w4;
          
          int graph_x = 4;
          int graph_w = 120;
          
          int x0 = graph_x;
          int x1 = x0 + w1 * graph_w / total_w;
          int x2 = x1 + w2 * graph_w / total_w;
          int x3 = x2 + w3 * graph_w / total_w;
          int x4 = graph_x + graph_w;
          
          int y_L4 = icon_y + icon_h - 2 - (L4 * (icon_h - 4) / 99);
          int y_L1 = icon_y + icon_h - 2 - (L1 * (icon_h - 4) / 99);
          int y_L2 = icon_y + icon_h - 2 - (L2 * (icon_h - 4) / 99);
          int y_L3 = icon_y + icon_h - 2 - (L3 * (icon_h - 4) / 99);
          
          u8g2_DrawFrame(&g_u8g2, graph_x, icon_y, graph_w, icon_h);
          u8g2_DrawLine(&g_u8g2, x0, y_L4, x1, y_L1);
          u8g2_DrawLine(&g_u8g2, x1, y_L1, x2, y_L2);
          u8g2_DrawLine(&g_u8g2, x2, y_L2, x3, y_L3);
          u8g2_DrawLine(&g_u8g2, x3, y_L3, x4, y_L4);
          
          drawn_icon = true;
        } else {
          drawn_icon = true; // Handled by column 0
        }
      } else if (selected_module == MOD_ALGO_FB) {
        if (i == 1) {
          draw_algorithm_diagram(g_active_patch.algorithm, 34);
          drawn_icon = true;
        } else if (i == 2 || i == 3) {
          drawn_icon = true; // Covered by algorithm diagram
        }
      }

      if (!drawn_icon) {
        u8g2_DrawEllipse(&g_u8g2, cx, cy, 9, 9, U8G2_DRAW_ALL);
        uint8_t val = page->knobs[i].value;
        if (val > 127) val = 127;
        float angle = 225.0f - (val * 270.0f / 127.0f);
        float rad = angle * (float)M_PI / 180.0f;
        int lx = cx + (int)(8.0f * cosf(rad));
        int ly = cy - (int)(8.0f * sinf(rad));
        u8g2_DrawLine(&g_u8g2, cx, cy, lx, ly);
      }
#else
      u8g2_DrawEllipse(&g_u8g2, cx, cy, 9, 9, U8G2_DRAW_ALL);

      // Pointer line via trig
      uint8_t val = page->knobs[i].value;
      if (val > 127) val = 127;
      float angle = 225.0f - (val * 270.0f / 127.0f);
      float rad = angle * (float)M_PI / 180.0f;
      int lx = cx + (int)(8.0f * cosf(rad));
      int ly = cy - (int)(8.0f * sinf(rad));
      u8g2_DrawLine(&g_u8g2, cx, cy, lx, ly);
#endif

      if (!hide_label) {
        char buf[16];
        if (page->knobs[i].value_str[0] != '\0') {
          snprintf(buf, sizeof(buf), "%s", page->knobs[i].value_str);
        } else {
          snprintf(buf, sizeof(buf), "%d", page->knobs[i].value);
        }
        int vw = u8g2_GetStrWidth(&g_u8g2, buf);
        u8g2_DrawStr(&g_u8g2, cx - (vw / 2), 52, buf);
      }
    }
  }

  // Status message overlay if active
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if (ui_status_msg[0] != '\0' && now < ui_status_msg_timeout_ms) {
    u8g2_SetDrawColor(&g_u8g2, 0);
    u8g2_DrawBox(&g_u8g2, 0, 20, 128, 24);
    u8g2_SetDrawColor(&g_u8g2, 1);
    u8g2_DrawFrame(&g_u8g2, 0, 20, 128, 24);
    u8g2_SetFont(&g_u8g2, u8g2_font_6x12_tr);
    int w = u8g2_GetStrWidth(&g_u8g2, ui_status_msg);
    u8g2_DrawStr(&g_u8g2, (128 - w) / 2, 36, ui_status_msg);
  }

  u8g2_SendBuffer(&g_u8g2);
  g_oled_dirty = false;
  g_oled_draw_count++;
}
