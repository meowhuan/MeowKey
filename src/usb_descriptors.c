#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ctap_hid.h"
#include "meowkey_build_config.h"
#include "pico/unique_id.h"
#include "tusb.h"

enum {
    USB_VID = 0xCafe,
    USB_PID = 0x4004,
    USB_BCD = 0x0100,
};

#if MEOWKEY_ENABLE_DEBUG_HID
enum {
    ITF_NUM_FIDO_HID = 0,
    ITF_NUM_DEBUG_HID,
    ITF_NUM_TOTAL,
};

enum {
    EPNUM_FIDO_HID = 0x01,
    EPNUM_DEBUG_HID = 0x02,
};
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + (2 * TUD_HID_INOUT_DESC_LEN))
#else
enum {
    ITF_NUM_FIDO_HID = 0,
    ITF_NUM_TOTAL,
};

enum {
    EPNUM_FIDO_HID = 0x01,
};
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)
#endif

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
};

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = USB_BCD,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 0x01,
};

static const uint8_t desc_hid_report[] = {
    0x06, 0xD0, 0xF1,
    0x09, 0x01,
    0xA1, 0x01,
    0x09, 0x20,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x40,
    0x81, 0x02,
    0x09, 0x21,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x40,
    0x91, 0x02,
    0xC0,
};

#if MEOWKEY_ENABLE_DEBUG_HID
static const uint8_t desc_debug_hid_report[] = {
    0x06, 0x00, 0xFF,
    0x09, 0x01,
    0xA1, 0x01,
    0x09, 0x20,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x40,
    0x81, 0x02,
    0x09, 0x21,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, 0x40,
    0x91, 0x02,
    0xC0,
};
#endif

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_FIDO_HID,
                             0,
                             HID_ITF_PROTOCOL_NONE,
                             sizeof(desc_hid_report),
                             EPNUM_FIDO_HID,
                             (uint8_t)(0x80u | EPNUM_FIDO_HID),
                             CFG_TUD_HID_EP_BUFSIZE,
                             5)
#if MEOWKEY_ENABLE_DEBUG_HID
    ,
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_DEBUG_HID,
                             0,
                             HID_ITF_PROTOCOL_NONE,
                             sizeof(desc_debug_hid_report),
                             EPNUM_DEBUG_HID,
                             (uint8_t)(0x80u | EPNUM_DEBUG_HID),
                             CFG_TUD_HID_EP_BUFSIZE,
                             5)
#endif
};

static const char *const string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "MeowKey",
    MEOWKEY_USB_PRODUCT_NAME,
    NULL,
};

static uint16_t s_desc_str[32 + 1];

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
#if MEOWKEY_ENABLE_DEBUG_HID
    if (instance == CTAP_HID_DEBUG_INSTANCE) {
        return desc_debug_hid_report;
    }
#else
    (void)instance;
#endif
    return desc_hid_report;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    size_t count = 0;
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

    (void)langid;

    if (index == STRID_LANGID) {
        memcpy(&s_desc_str[1], string_desc_arr[0], 2);
        count = 1;
    } else if (index == STRID_SERIAL) {
        pico_get_unique_board_id_string(serial, sizeof(serial));
        count = strlen(serial);
        if (count > 32) {
            count = 32;
        }
        for (size_t i = 0; i < count; ++i) {
            s_desc_str[1 + i] = (uint16_t)serial[i];
        }
    } else {
        char const *str;

        if (index >= (sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
            return NULL;
        }

        str = string_desc_arr[index];
        count = strlen(str);
        if (count > 32) {
            count = 32;
        }
        for (size_t i = 0; i < count; ++i) {
            s_desc_str[1 + i] = (uint16_t)str[i];
        }
    }

    s_desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | ((count * 2u) + 2u));
    return s_desc_str;
}
