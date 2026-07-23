#include "midi_uart.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "../hw_config.h"

#define MIDI_UART uart0

#define RX_BUF_SIZE 256
static volatile uint8_t rx_buffer[RX_BUF_SIZE];
static volatile uint8_t rx_head = 0;
static volatile uint8_t rx_tail = 0;

static void on_uart_rx() {
    while (uart_is_readable(MIDI_UART)) {
        uint8_t ch = (uint8_t)uart_getc(MIDI_UART);
        uint8_t next = (uint8_t)(rx_head + 1);
        if (next != rx_tail) {
            rx_buffer[rx_head] = ch;
            rx_head = next;
        }
    }
}

void midi_uart_init(void) {
    uart_init(MIDI_UART, 31250);
    gpio_set_function(PIN_MIDI_JACK_OUT, GPIO_FUNC_UART);
    gpio_set_function(PIN_MIDI_JACK_IN, GPIO_FUNC_UART);
    uart_set_hw_flow(MIDI_UART, false, false);
    uart_set_format(MIDI_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(MIDI_UART, true);

    // Setup interrupt handler for RX
    irq_set_exclusive_handler(UART0_IRQ, on_uart_rx);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(MIDI_UART, true, false);
}

void midi_uart_write_byte(uint8_t byte) {
    if (uart_is_writable(MIDI_UART)) {
        uart_putc(MIDI_UART, byte);
    }
}

bool midi_uart_read_byte(uint8_t *byte) {
    if (rx_head != rx_tail) {
        *byte = rx_buffer[rx_tail];
        rx_tail = (uint8_t)(rx_tail + 1);
        return true;
    }
    return false;
}
