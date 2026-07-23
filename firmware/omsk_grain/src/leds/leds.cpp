#include "leds.h"
#include "../sw_config.h"
#include "../ui/ui_state.h"
#include "../synth/synth.h"
#include "../midi/midi_map.h"
#include "../../shared/hardware/matrix.h"
#include "../../shared/hardware/colors.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"
#include <math.h>

extern Synth synth;
extern UIState ui;
extern bool matrix_curr[16];
extern bool is_sleeping;

static PIO ws2812_pio_inst;
static uint ws2812_sm;

struct RGBColor {
    uint8_t r, g, b;
};

// Colors matching specified page theme
static const RGBColor PAGE_COLORS[PAGE_COUNT] = {
    {COLOR_VCO_R, COLOR_VCO_G, COLOR_VCO_B},                      // PAGE_GRAIN1: Green
    {COLOR_VCO_R, 100, COLOR_VCO_B},                              // PAGE_GRAIN2: Dark Green
    {128, COLOR_VCO_G, COLOR_VCO_B},                              // PAGE_GRAIN3: Light Green
    {COLOR_FILTER_R, COLOR_FILTER_G, COLOR_FILTER_B},            // PAGE_FILT: Filter Color (Magenta)
    {COLOR_ARP_JIT_R, COLOR_ARP_JIT_G, COLOR_ARP_JIT_B},          // PAGE_JIT: Jitter Color (Yellow-Green)
    {COLOR_LFO_R, COLOR_LFO_G, COLOR_LFO_B},                      // PAGE_LFO1: LFO Color (Light Blue)
    {COLOR_LFO_R, COLOR_LFO_G, COLOR_LFO_B},                      // PAGE_LFO2: LFO Color (Light Blue)
    {COLOR_EG_R, COLOR_EG_G, COLOR_EG_B},                          // PAGE_EG: EG Color (Orange)
    {COLOR_MOD_R, COLOR_MOD_G, COLOR_MOD_B},                      // PAGE_MOD: Mod Color (Cyan)
    {COLOR_FX_R, COLOR_FX_G, COLOR_FX_B},                          // PAGE_FX: FX Color (Yellow)
    {COLOR_MIXER_R, COLOR_MIXER_G, COLOR_MIXER_B},                // PAGE_MIX: Mixer Color (Purple)
    {COLOR_SYS_R, COLOR_SYS_G, COLOR_SYS_B}                        // PAGE_SYS: System Color (White)
};

// Voice LED colors mapping
static const RGBColor VOICE_COLORS[NUM_VOICES] = {
    {255, 0, 0},      // V1: Red
    {0, 255, 0},      // V2: Green
    {0, 0, 255},      // V3: Blue
    {255, 0, 255}     // V4: Magenta
};



#include "hardware/dma.h"

#define WS2812_RESET_US 60

static int rgb_dma_chan = -1;
static uint32_t rgb_dma_buf[NUM_RGB_LEDS];

void leds_init(void) {
    ws2812_pio_inst = pio0;
    int sm = pio_claim_unused_sm(ws2812_pio_inst, false);
    if (sm < 0) {
        ws2812_pio_inst = pio1;
        sm = pio_claim_unused_sm(ws2812_pio_inst, true);
    }
    ws2812_sm = (uint)sm;

    uint offset = pio_add_program(ws2812_pio_inst, &ws2812_program);
    // ws2812 protocol requires 800kHz freq
    ws2812_program_init(ws2812_pio_inst, ws2812_sm, offset, PIN_RGB_LED, 800000, 24);

    rgb_dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(rgb_dma_chan);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(ws2812_pio_inst, ws2812_sm, true));

    dma_channel_configure(
        rgb_dma_chan,
        &cfg,
        &ws2812_pio_inst->txf[ws2812_sm], // Write address: PIO TX FIFO
        rgb_dma_buf,                      // Read address: DMA buffer
        NUM_RGB_LEDS,                     // Transfer count
        false                             // Don't start yet
    );
}

void leds_show(void) {
    if (rgb_dma_chan >= 0) {
        // Wait for the DMA transfer to finish
        while (dma_channel_is_busy(rgb_dma_chan)) {
            tight_loop_contents();
        }
        // Wait for the PIO TX FIFO to become empty
        while (!pio_sm_is_tx_fifo_empty(ws2812_pio_inst, ws2812_sm)) {
            tight_loop_contents();
        }
        // Wait for the WS2812 reset time (without using sleep_us)
        uint32_t reset_start = time_us_32();
        while (time_us_32() - reset_start < WS2812_RESET_US) {
            tight_loop_contents();
        }
    }

    if (is_sleeping) {
        for (int i = 0; i < NUM_RGB_LEDS; i++) {
            rgb_dma_buf[i] = 0;
        }
    } else {
        RGBColor leds[NUM_RGB_LEDS];
        for (int i = 0; i < NUM_RGB_LEDS; i++) {
            leds[i] = {0, 0, 0};
        }

        Page current_page = ui.active_page;
        const PageLayout &layout = PAGE_LAYOUTS[current_page];

        // 1. Encoder LEDs
        int encoder_led_indices[4] = {LED_ENCODER_1, LED_ENCODER_2, LED_ENCODER_3, LED_ENCODER_4};
        for (int e = 0; e < 4; e++) {
            ParamId pid = layout.enc[e];
            int led_idx = encoder_led_indices[e];
            if (pid != PARAM_NONE) {
                float val = get_normalized_param_value(synth.get_voice(0), ui.active_voice, (int)pid, (int)current_page);
                get_cold_hot_color(val, &leds[led_idx].r, &leds[led_idx].g, &leds[led_idx].b);
            } else {
                leds[led_idx] = {0, 0, 0};
            }
        }

        // 2. Mode LEDs
        RGBColor mode_color = PAGE_COLORS[current_page];
        leds[LED_MODE_1] = mode_color;
        leds[LED_MODE_2] = mode_color;

        // 3. Button LEDs (7-22 -> indices 6-21)
        // Dim factor is ~15% (scaled by MAX_BRIGHTNESS)
        float dim_factor = 0.15f;

        // Mode selectors: buttons 0-11 -> mapped LEDs
        for (int b = 0; b < 12; b++) {
            int led_idx = MATRIX_LED_MAP[b];
            RGBColor page_color = PAGE_COLORS[b]; // Maps directly 1-to-1 to Page enum
            if (matrix_curr[b]) {
                leds[led_idx] = page_color; // Bright
            } else {
                leds[led_idx].r = (uint8_t)(page_color.r * dim_factor);
                leds[led_idx].g = (uint8_t)(page_color.g * dim_factor);
                leds[led_idx].b = (uint8_t)(page_color.b * dim_factor);
            }
        }

        // Voice triggers: buttons 12-15 -> LEDs 18-21
        for (int v = 0; v < NUM_VOICES; v++) {
            int button_idx = v + 12;
            int led_idx = MATRIX_LED_MAP[button_idx];
            RGBColor voice_color = VOICE_COLORS[v];
            if (matrix_curr[button_idx] || ui.voices_triggered[v]) {
                leds[led_idx] = voice_color; // Bright
            } else {
                leds[led_idx].r = (uint8_t)(voice_color.r * dim_factor);
                leds[led_idx].g = (uint8_t)(voice_color.g * dim_factor);
                leds[led_idx].b = (uint8_t)(voice_color.b * dim_factor);
            }
        }

        // Apply global brightness scale and output pixels in GRB format
        for (int i = 0; i < NUM_RGB_LEDS; i++) {
            uint32_t r = (leds[i].r * RGB_MAX_BRIGHTNESS) / 255;
            uint32_t g = (leds[i].g * RGB_MAX_BRIGHTNESS) / 255;
            uint32_t b = (leds[i].b * RGB_MAX_BRIGHTNESS) / 255;
            uint32_t grb = (g << 16) | (r << 8) | b;
            rgb_dma_buf[i] = grb << 8u;
        }
    }

    if (rgb_dma_chan >= 0) {
        // Reconfigure read address and restart DMA (non-blocking)
        dma_channel_set_read_addr(rgb_dma_chan, rgb_dma_buf, false);
        dma_channel_set_trans_count(rgb_dma_chan, NUM_RGB_LEDS, true); // true = start
    }
}
