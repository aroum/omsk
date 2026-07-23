#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU OPT_MCU_RP2040
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_NONE
#endif

#define CFG_TUSB_DEBUG 0

// RHPort number used for device can be 0 or 1, for RP2040 it is 0
#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE

#define CFG_TUD_ENABLED 1
#define CFG_TUD_CDC 0
#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 1
#define CFG_TUD_VENDOR 0

#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 64

#endif
