#include "leds.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "ws2812.pio.h"
#include "sw_config.h"

#define NUM_LEDS 22
#define LED_PIN  PIN_RGB_LED

// WS2812 reset pulse must be >50 µs after last bit
#define WS2812_RESET_US 60

static uint32_t led_colors[NUM_LEDS];

// DMA transfer buffer: pixels pre-shifted by 8 so MSB lands at bit 31
static uint32_t dma_buf[NUM_LEDS];

static PIO   led_pio = pio1;
static uint  led_sm;
static int   led_dma_chan = -1;


static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)(r) << 8) | ((uint32_t)(g) << 16) | (uint32_t)(b);
}

void leds_init(void) {
#if CFG_ENABLE_RGB_LED
    uint offset = pio_add_program(led_pio, &ws2812_program);
    led_sm = pio_claim_unused_sm(led_pio, true);
    ws2812_program_init(led_pio, led_sm, offset, LED_PIN, 800000, 24);

    led_dma_chan = dma_claim_unused_channel(true);

    dma_channel_config cfg = dma_channel_get_default_config(led_dma_chan);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    // Pace DMA to PIO TX FIFO not-full
    channel_config_set_dreq(&cfg, pio_get_dreq(led_pio, led_sm, true));

    dma_channel_configure(
        led_dma_chan,
        &cfg,
        &led_pio->txf[led_sm], // Write address: PIO TX FIFO
        dma_buf,               // Read address: pixel buffer
        NUM_LEDS,              // Transfer count
        false                  // Don't start yet
    );
#endif
}

void leds_set_pixel(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < 0 || index >= NUM_LEDS) return;
    led_colors[index] = urgb_u32(r, g, b);
}

void leds_show(void) {
#if CFG_ENABLE_RGB_LED
    if (led_dma_chan >= 0) {
        // Wait for the DMA transfer to finish
        while (dma_channel_is_busy(led_dma_chan)) {
            tight_loop_contents();
        }
        // Wait for the PIO TX FIFO to become empty
        while (!pio_sm_is_tx_fifo_empty(led_pio, led_sm)) {
            tight_loop_contents();
        }
        // Wait for the WS2812 reset time (without using sleep_us)
        uint32_t reset_start = time_us_32();
        while (time_us_32() - reset_start < WS2812_RESET_US) {
            tight_loop_contents();
        }
    }

    // Build DMA buffer: each pixel must arrive at PIO as its MSB in bit 31
    for (int i = 0; i < NUM_LEDS; i++) {
        dma_buf[i] = led_colors[i] << 8u;
    }

    // Reconfigure read address and restart DMA (non-blocking)
    dma_channel_set_read_addr(led_dma_chan, dma_buf, false);
    dma_channel_set_trans_count(led_dma_chan, NUM_LEDS, true); // true = start
#endif
}

void leds_set_all(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < NUM_LEDS; i++) {
        leds_set_pixel(i, r, g, b);
    }
}
