#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

// TinyUSB configuration for RP2350 with USB MIDI

#define CFG_TUSB_RHPORT0_MODE  OPT_MODE_DEVICE
#define CFG_TUSB_MCU           OPT_MCU_RP2040   // RP2350 uses same driver

// Device class: USB MIDI
#define CFG_TUD_MIDI           1
#define CFG_TUD_MIDI_RX_BUFSIZE 64
#define CFG_TUD_MIDI_TX_BUFSIZE 64

#define CFG_TUD_CDC            1
#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 256
#define CFG_TUD_MSC            0
#define CFG_TUD_HID            0
#define CFG_TUD_VENDOR         0


#endif // TUSB_CONFIG_H
