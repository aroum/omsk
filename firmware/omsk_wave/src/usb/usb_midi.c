#include "tusb.h"
#include "usb_config.h"
#include "audio.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
// UAC2 is handled in usb_audio.c

void usb_audio_init(void);

void usb_midi_init(void) {
    tud_init(0);
#if CFG_ENABLE_USB_AUDIO
    usb_audio_init();
#endif
}

void usb_midi_task(void) {
    tud_task();
#if CFG_ENABLE_USB_AUDIO
    // UAC2 uses ISR push in usb_audio.c
#endif
}

void usb_debug_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
#if CFG_TUD_CDC
    char buf[256];
    char out[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    if (n > 0 && tud_cdc_connected()) {
        uint32_t w = 0;
        for (int i = 0; i < n && w + 2 < sizeof(out); i++) {
            char c = buf[i];
            if (c == '\n') {
                out[w++] = '\r';
                out[w++] = '\n';
            } else {
                out[w++] = c;
            }
        }
        if (w) {
            tud_cdc_write(out, w);
            tud_cdc_write_flush();
        }
    }
#else
    vprintf(fmt, args);
#endif
    va_end(args);
}

#include "pico/bootrom.h"

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* p_line_coding) {
    if (p_line_coding->bit_rate == 1200) {
        reset_usb_boot(0, 0);
    }
}
