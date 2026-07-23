#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#include "pico/stdlib.h"
#include "sw_config.h"

#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU OPT_MCU_RP2350
#endif
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_NONE
#endif
#define CFG_TUSB_DEBUG 0

#define CFG_TUD_ENDPOINT0_SIZE 64

#ifndef CFG_TUD_ENABLED
#define CFG_TUD_ENABLED 1
#endif

#define CFG_TUD_AUDIO  0
#define CFG_TUD_CDC    (CFG_ENABLE_DEBUG)
#define CFG_TUD_MIDI   (CFG_ENABLE_USB_MIDI)
#define CFG_TUD_MSC    0
#define CFG_TUD_HID    0
#define CFG_TUD_VENDOR 0

#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 256

#define CFG_TUD_MIDI_RX_BUFSIZE 512
#define CFG_TUD_MIDI_TX_BUFSIZE 128

#endif // _TUSB_CONFIG_H_
