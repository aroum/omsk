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

// Interfaces and endpoints
#if CFG_ENABLE_USB_AUDIO

enum {
#if CFG_TUD_CDC
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
#endif
#if CFG_TUD_MIDI
    ITF_NUM_MIDI,
    ITF_NUM_MIDI_STREAMING,
#endif
#if CFG_TUD_AUDIO
    ITF_NUM_AUDIO_CONTROL,
    ITF_NUM_AUDIO_STREAMING_MIC,
#endif
    ITF_NUM_TOTAL
};

#define AUDIO_ITF_COUNT 2

#ifndef TUD_AUDIO_INTERFACE_STEREO_DESC_LEN
#define TUD_AUDIO_INTERFACE_STEREO_DESC_LEN (TUD_AUDIO_DESC_IAD_LEN\
    + TUD_AUDIO_DESC_STD_AC_LEN\
    + TUD_AUDIO_DESC_CS_AC_LEN\
    + TUD_AUDIO_DESC_CLK_SRC_LEN\
    + TUD_AUDIO_DESC_INPUT_TERM_LEN\
    + TUD_AUDIO_DESC_OUTPUT_TERM_LEN\
    + TUD_AUDIO_DESC_STD_AS_INT_LEN\
    + TUD_AUDIO_DESC_STD_AS_INT_LEN\
    + TUD_AUDIO_DESC_CS_AS_INT_LEN\
    + TUD_AUDIO_DESC_TYPE_I_FORMAT_LEN\
    + TUD_AUDIO_DESC_STD_AS_ISO_EP_LEN\
    + TUD_AUDIO_DESC_CS_AS_ISO_EP_LEN)
#endif

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + \
                           (CFG_TUD_CDC ? TUD_CDC_DESC_LEN : 0) + \
                           (CFG_TUD_MIDI ? TUD_MIDI_DESC_LEN : 0) + \
                           (CFG_TUD_AUDIO ? TUD_AUDIO_INTERFACE_STEREO_DESC_LEN : 0))

#else

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

#endif

#if CFG_TUD_CDC
#define EPNUM_CDC_NOTIF  0x81
#define EPNUM_CDC_OUT    0x02
#define EPNUM_CDC_IN     0x82
#endif

#if CFG_TUD_MIDI
#define EPNUM_MIDI_OUT  0x04
#define EPNUM_MIDI_IN   0x84
#endif

#if CFG_TUD_AUDIO
#define EPNUM_AUDIO_IN   0x05
#define EPNUM_AUDIO_INT  0x06
#endif

#if CFG_ENABLE_USB_AUDIO

#ifndef TUD_AUDIO_INTERFACE_STEREO_DESCRIPTOR
#define UAC2_ENTITY_CLOCK               0x04
#define UAC2_ENTITY_MIC_INPUT_TERMINAL  0x11
#define UAC2_ENTITY_MIC_OUTPUT_TERMINAL 0x13

#define TUD_AUDIO_INTERFACE_STEREO_DESCRIPTOR(_stridx, _epin, _epint) \
    TUD_AUDIO_DESC_IAD(ITF_NUM_AUDIO_CONTROL, AUDIO_ITF_COUNT, 0x00),\
    TUD_AUDIO_DESC_STD_AC(ITF_NUM_AUDIO_CONTROL, 0x00, _stridx),\
    TUD_AUDIO_DESC_CS_AC(0x0200, AUDIO_FUNC_MICROPHONE, TUD_AUDIO_DESC_CLK_SRC_LEN+TUD_AUDIO_DESC_INPUT_TERM_LEN+TUD_AUDIO_DESC_OUTPUT_TERM_LEN, AUDIO_CS_AS_INTERFACE_CTRL_LATENCY_POS),\
    TUD_AUDIO_DESC_CLK_SRC(UAC2_ENTITY_CLOCK, AUDIO_CLOCK_SOURCE_ATT_INT_FIX_CLK, (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS), 0x00, 0x00),\
    TUD_AUDIO_DESC_INPUT_TERM(UAC2_ENTITY_MIC_INPUT_TERMINAL, AUDIO_TERM_TYPE_IN_GENERIC_MIC, 0x00, UAC2_ENTITY_CLOCK, 0x02, (AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT), 0x00, 0 * (AUDIO_CTRL_R << AUDIO_IN_TERM_CTRL_CONNECTOR_POS), 0x00),\
    TUD_AUDIO_DESC_OUTPUT_TERM(UAC2_ENTITY_MIC_OUTPUT_TERMINAL, AUDIO_TERM_TYPE_USB_STREAMING, 0x00, UAC2_ENTITY_MIC_INPUT_TERMINAL, UAC2_ENTITY_CLOCK, 0x0000, 0x00),\
    TUD_AUDIO_DESC_STD_AS_INT((uint8_t)(ITF_NUM_AUDIO_STREAMING_MIC), 0x00, 0x00, 0x04),\
    TUD_AUDIO_DESC_STD_AS_INT((uint8_t)(ITF_NUM_AUDIO_STREAMING_MIC), 0x01, 0x01, 0x04),\
    TUD_AUDIO_DESC_CS_AS_INT(UAC2_ENTITY_MIC_OUTPUT_TERMINAL, AUDIO_CTRL_NONE, AUDIO_FORMAT_TYPE_I, AUDIO_DATA_FORMAT_TYPE_I_PCM, CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX, (AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT), 0x00),\
    TUD_AUDIO_DESC_TYPE_I_FORMAT(CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_TX, CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_TX),\
    TUD_AUDIO_DESC_STD_AS_ISO_EP(_epin, (uint8_t) ((uint8_t)TUSB_XFER_ISOCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_ASYNCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_DATA), TUD_AUDIO_EP_SIZE(CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE, CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_TX, CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX), 0x01),\
    TUD_AUDIO_DESC_CS_AS_ISO_EP(AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, AUDIO_CTRL_NONE, AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0x0000)
#endif

static uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    // TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

#if CFG_TUD_CDC
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 64, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
#endif
#if CFG_TUD_MIDI
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
#endif
#if CFG_TUD_AUDIO
    TUD_AUDIO_INTERFACE_STEREO_DESCRIPTOR(
        2,
        EPNUM_AUDIO_IN | 0x80,
        EPNUM_AUDIO_INT | 0x80),
#endif
};

#else

static uint8_t const desc_fs_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
#if CFG_TUD_CDC
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 64, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
#endif
#if CFG_TUD_MIDI
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
#endif
#if CFG_TUD_AUDIO
    TUD_AUDIO_MIC_ONE_CH_DESCRIPTOR(
        ITF_NUM_AUDIO_CONTROL,
        4,
        CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX,
        CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX * 8,
        0x80 | EPNUM_AUDIO,
        CFG_TUD_AUDIO_EP_SZ_IN),
#endif
};

#endif

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
