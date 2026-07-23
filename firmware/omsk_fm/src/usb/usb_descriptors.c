#include "tusb.h"
#include "usb_config.h"
#include <string.h>

#if CFG_TUD_CDC || CFG_TUD_MIDI || CFG_TUD_AUDIO
#define USB_DEV_CLASS      0xEF
#define USB_DEV_SUBCLASS   0x02
#define USB_DEV_PROTOCOL   0x01
#else
#define USB_DEV_CLASS      0x00
#define USB_DEV_SUBCLASS   0x00
#define USB_DEV_PROTOCOL   0x00
#endif

static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = USB_DEV_CLASS,
    .bDeviceSubClass = USB_DEV_SUBCLASS,
    .bDeviceProtocol = USB_DEV_PROTOCOL,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const * tud_descriptor_device_cb(void) {
    return (uint8_t const *) &desc_device;
}

enum {
#if CFG_TUD_CDC
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
#endif
#if CFG_TUD_MIDI
    ITF_NUM_MIDI,
    ITF_NUM_MIDI_STREAMING,
#endif
    ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + \
                           (CFG_TUD_CDC ? TUD_CDC_DESC_LEN : 0) + \
                           (CFG_TUD_MIDI ? TUD_MIDI_DESC_LEN : 0))

#if CFG_TUD_CDC
#define EPNUM_CDC_NOTIF  0x81
#define EPNUM_CDC_OUT    0x02
#define EPNUM_CDC_IN     0x82
#endif

#if CFG_TUD_MIDI
#define EPNUM_MIDI_OUT  0x04
#define EPNUM_MIDI_IN   0x84
#endif

static uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
#if CFG_TUD_CDC
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 64, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
#endif
#if CFG_TUD_MIDI
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
#endif
};

uint8_t const * tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_fs_configuration;
}

static uint16_t _desc_str[32 + 1];

static const char *string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    USB_MANUFACTURER,
    USB_PRODUCT,
    "123456",
    "Output",
    "Input"
};

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (!(index < (sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))) {
            return NULL;
        }
        const char* str = string_desc_arr[index];
        chr_count = strlen(str);
        size_t max_count = (sizeof(_desc_str) / sizeof(_desc_str[0])) - 1;
        if (chr_count > max_count) {
            chr_count = max_count;
        }
        for (size_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
