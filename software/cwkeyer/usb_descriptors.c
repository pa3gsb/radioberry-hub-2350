#include "tusb.h"
#include <string.h>

#ifdef RB_USB_MIDI_ONLY
enum {
    ITF_NUM_MIDI = 0,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL
};

enum {
    EPNUM_MIDI_OUT = 0x01,
    EPNUM_MIDI_IN  = 0x81,
};
#else
enum {
    ITF_NUM_AUDIO_CONTROL = 0,
    ITF_NUM_AUDIO_STREAMING,
    ITF_NUM_AUDIO_SPK_CONTROL,
    ITF_NUM_AUDIO_SPK_STREAMING,
    ITF_NUM_CDC,
    ITF_NUM_CDC_DATA,
    ITF_NUM_MIDI,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL
};

enum {
    EPNUM_AUDIO_IN  = 0x81,
    EPNUM_AUDIO_OUT = 0x01,
    EPNUM_CDC_NOTIF = 0x82,
    EPNUM_CDC_OUT   = 0x02,
    EPNUM_CDC_IN    = 0x83,
    EPNUM_MIDI_OUT  = 0x03,
    EPNUM_MIDI_IN   = 0x84,
};
#endif

#if CFG_TUD_AUDIO > 0
// Mono speaker descriptor without feedback endpoint (adaptive endpoint mode).
#define TUD_AUDIO_SPEAKER_MONO_SIMPLE_DESCRIPTOR(_itfnum, _stridx, _nBytesPerSample, _nBitsUsedPerSample, _epout, _epsize) \
    TUD_AUDIO_DESC_IAD(_itfnum, 0x02, 0x00), \
    TUD_AUDIO_DESC_STD_AC(_itfnum, 0x00, _stridx), \
    TUD_AUDIO_DESC_CS_AC(0x0200, AUDIO_FUNC_DESKTOP_SPEAKER, \
        TUD_AUDIO_DESC_CLK_SRC_LEN + TUD_AUDIO_DESC_INPUT_TERM_LEN + TUD_AUDIO_DESC_OUTPUT_TERM_LEN + TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL_LEN, \
        AUDIO_CS_AS_INTERFACE_CTRL_LATENCY_POS), \
    TUD_AUDIO_DESC_CLK_SRC(0x04, AUDIO_CLOCK_SOURCE_ATT_INT_FIX_CLK, \
        AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS, 0x01, 0x00), \
    TUD_AUDIO_DESC_INPUT_TERM(0x01, AUDIO_TERM_TYPE_USB_STREAMING, 0x00, 0x04, 0x01, \
        AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, 0x00, 0, 0x00), \
    TUD_AUDIO_DESC_OUTPUT_TERM(0x03, AUDIO_TERM_TYPE_OUT_DESKTOP_SPEAKER, 0x01, 0x02, 0x04, 0x0000, 0x00), \
    TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL(0x02, 0x01, \
        AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS | AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS, \
        AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS | AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS, 0x00), \
    TUD_AUDIO_DESC_STD_AS_INT(_itfnum + 1, 0x00, 0x00, 0x00), \
    TUD_AUDIO_DESC_STD_AS_INT(_itfnum + 1, 0x01, 0x01, 0x00), \
    TUD_AUDIO_DESC_CS_AS_INT(0x01, AUDIO_CTRL_NONE, AUDIO_FORMAT_TYPE_I, \
        AUDIO_DATA_FORMAT_TYPE_I_PCM, 0x01, AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, 0x00), \
    TUD_AUDIO_DESC_TYPE_I_FORMAT(_nBytesPerSample, _nBitsUsedPerSample), \
    TUD_AUDIO_DESC_STD_AS_ISO_EP(_epout, \
        (uint8_t)(TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ADAPTIVE | TUSB_ISO_EP_ATT_DATA), _epsize, 0x01), \
    TUD_AUDIO_DESC_CS_AS_ISO_EP(AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, AUDIO_CTRL_NONE, \
        AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0x0000)

#define TUD_AUDIO_SPEAKER_MONO_SIMPLE_DESC_LEN (TUD_AUDIO_DESC_IAD_LEN + TUD_AUDIO_DESC_STD_AC_LEN + TUD_AUDIO_DESC_CS_AC_LEN + \
    TUD_AUDIO_DESC_CLK_SRC_LEN + TUD_AUDIO_DESC_INPUT_TERM_LEN + TUD_AUDIO_DESC_OUTPUT_TERM_LEN + \
    TUD_AUDIO_DESC_FEATURE_UNIT_ONE_CHANNEL_LEN + TUD_AUDIO_DESC_STD_AS_INT_LEN * 2 + \
    TUD_AUDIO_DESC_CS_AS_INT_LEN + TUD_AUDIO_DESC_TYPE_I_FORMAT_LEN + TUD_AUDIO_DESC_STD_AS_ISO_EP_LEN + TUD_AUDIO_DESC_CS_AS_ISO_EP_LEN)

// Full-duplex via two audio functions: microphone + speaker
#define TUD_AUDIO_HEADSET_FULL_DUPLEX_DESCRIPTOR(_itfnum, _stridx) \
    TUD_AUDIO_MIC_ONE_CH_DESCRIPTOR(_itfnum, _stridx, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX*8, EPNUM_AUDIO_IN, CFG_TUD_AUDIO_EP_SZ_IN), \
    TUD_AUDIO_SPEAKER_MONO_SIMPLE_DESCRIPTOR(_itfnum + 2, _stridx + 1, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX*8, EPNUM_AUDIO_OUT, CFG_TUD_AUDIO_EP_SZ_OUT)

#define TUD_AUDIO_HEADSET_FULL_DUPLEX_DESC_LEN (TUD_AUDIO_MIC_ONE_CH_DESC_LEN + TUD_AUDIO_SPEAKER_MONO_SIMPLE_DESC_LEN)
#endif

#ifdef RB_USB_MIDI_ONLY
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)
#else
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_AUDIO_HEADSET_FULL_DUPLEX_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_LEN)
#endif

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
#ifdef RB_USB_MIDI_ONLY
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
#else
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
#endif
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = 0x2E8A,
#ifdef RB_USB_MIDI_ONLY
    .idProduct = 0x401A,
    .bcdDevice = 0x0105,
#else
    .idProduct = 0x4019,
    .bcdDevice = 0x0104,
#endif

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,

    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),

#ifdef RB_USB_MIDI_ONLY
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 4, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
#else
    // Microphone + speaker
    TUD_AUDIO_HEADSET_FULL_DUPLEX_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4),

    // CDC serial
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 6, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

    // MIDI
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 7, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
#endif
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

static const char *string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "CW",
#ifdef RB_USB_MIDI_ONLY
    "Radioberry CWKeyer MIDI",
    "RB2350-MIDI-0005",
    "Radioberry MIDI",
#else
    "Radioberry CWKeyer",
    "RB2350-0004",
    "Radioberry TX Audio",
    "Radioberry RX Audio",
    "CDC",
    "Radioberry MIDI",
#endif
};

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t chr_count;

    if (index == 0) {
        _desc_str[1] = 0x0409;
        chr_count = 1;
    } else {
        if (index >= (sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
            return NULL;
        }
        const char *str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) {
            chr_count = 31;
        }
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}