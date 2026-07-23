#include "usb_midi.h"
#include "tusb.h"
#include "pico/bootrom.h"

void usb_midi_init(void) {
    tud_init(0);
}

void usb_midi_task(void) {
    tud_task();
}

#if CFG_TUD_CDC
extern "C" {
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* p_line_coding) {
    if (p_line_coding->bit_rate == 1200) {
        reset_usb_boot(0, 0);
    }
}
}
#endif
