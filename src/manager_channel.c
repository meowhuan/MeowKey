#include "manager_channel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "board_id.h"
#include "credential_store.h"
#include "ctap_hid.h"
#include "meowkey_build_config.h"
#include "pico/unique_id.h"
#include "tusb.h"
#include "user_presence.h"

enum {
    MEOWKEY_MANAGER_REQUEST_MAGIC_0 = 'M',
    MEOWKEY_MANAGER_REQUEST_MAGIC_1 = 'K',
    MEOWKEY_MANAGER_REQUEST_MAGIC_2 = 'M',
    MEOWKEY_MANAGER_REQUEST_MAGIC_3 = '1',
    MEOWKEY_MANAGER_HEADER_SIZE = 10u,
    MEOWKEY_MANAGER_MAX_RESPONSE_SIZE = 1024u,
};

enum {
    MEOWKEY_MANAGER_CMD_GET_SNAPSHOT = 0x01u,
    MEOWKEY_MANAGER_CMD_PING = 0x02u,
};

enum {
    MEOWKEY_MANAGER_STATUS_OK = 0x00u,
    MEOWKEY_MANAGER_STATUS_INVALID_REQUEST = 0x01u,
    MEOWKEY_MANAGER_STATUS_UNSUPPORTED_COMMAND = 0x02u,
    MEOWKEY_MANAGER_STATUS_INTERNAL_ERROR = 0x03u,
};

static uint8_t s_response_buffer[MEOWKEY_MANAGER_MAX_RESPONSE_SIZE];

static uint16_t read_le16(const uint8_t *data) {
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8u);
}

static void write_le16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)(value >> 8u);
}

static const char *bool_json(bool value) {
    return value ? "true" : "false";
}

static const char *user_presence_source_name(uint8_t source) {
    switch (source) {
    case MEOWKEY_USER_PRESENCE_SOURCE_NONE:
        return "none";
    case MEOWKEY_USER_PRESENCE_SOURCE_BOOTSEL:
        return "bootsel";
    case MEOWKEY_USER_PRESENCE_SOURCE_GPIO:
        return "gpio";
    default:
        return "unknown";
    }
}

static const char *build_flavor_name(void) {
#if MEOWKEY_ENABLE_DEBUG_HID
    return "debug";
#else
    return "hardened";
#endif
}

static size_t append_format(char *output, size_t capacity, size_t used, const char *format, ...) {
    va_list args;
    int written;

    if (used >= capacity) {
        return used;
    }

    va_start(args, format);
    written = vsnprintf(&output[used], capacity - used, format, args);
    va_end(args);
    if (written < 0) {
        return capacity;
    }
    if ((size_t)written >= (capacity - used)) {
        return capacity;
    }

    return used + (size_t)written;
}

static size_t build_snapshot_json(char *output, size_t output_capacity) {
    meowkey_pin_state_t pin_state;
    meowkey_user_presence_config_t user_presence;
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    size_t used = 0u;
    uint32_t credential_count;
    uint32_t credential_capacity;
    uint32_t board_code;
    bool board_detected;

    meowkey_store_init();
    meowkey_board_id_init();
    meowkey_user_presence_init();
    meowkey_store_get_pin_state(&pin_state);
    meowkey_user_presence_get_config(&user_presence);
    pico_get_unique_board_id_string(serial, sizeof(serial));
    credential_count = meowkey_store_get_credential_count();
    credential_capacity = meowkey_store_get_credential_capacity();
    board_code = meowkey_board_id_get_code();
    board_detected = meowkey_board_id_is_detected();

    used = append_format(output, output_capacity, used,
        "{"
        "\"protocolVersion\":%u,"
        "\"transport\":\"winusb-bulk-v1\","
        "\"deviceName\":\"%s\","
        "\"productName\":\"%s\","
        "\"serial\":\"%s\","
        "\"usb\":{\"vid\":\"0x%04x\",\"pid\":\"0x%04x\"},",
        (unsigned int)MEOWKEY_MANAGER_PROTOCOL_VERSION,
        MEOWKEY_USB_PRODUCT_NAME,
        MEOWKEY_PRODUCT_NAME,
        serial,
        (unsigned int)MEOWKEY_USB_VID,
        (unsigned int)MEOWKEY_USB_PID);

    used = append_format(output, output_capacity, used,
        "\"build\":{"
        "\"flavor\":\"%s\","
        "\"version\":\"%d.%d.%d%s%s\","
        "\"debugHidEnabled\":%s,"
        "\"signedBootEnabled\":%s,"
        "\"antiRollbackEnabled\":%s"
        "},",
        build_flavor_name(),
        MEOWKEY_VERSION_MAJOR,
        MEOWKEY_VERSION_MINOR,
        MEOWKEY_VERSION_PATCH,
        strlen(MEOWKEY_VERSION_LABEL) > 0u ? "-" : "",
        MEOWKEY_VERSION_LABEL,
        bool_json(MEOWKEY_ENABLE_DEBUG_HID != 0),
        bool_json(MEOWKEY_ENABLE_SIGNED_BOOT != 0),
        bool_json(MEOWKEY_ENABLE_ANTI_ROLLBACK != 0));

    used = append_format(output, output_capacity, used,
        "\"board\":{"
        "\"detected\":%s,"
        "\"code\":\"0x%08lx\","
        "\"summary\":\"%s\""
        "},",
        bool_json(board_detected),
        (unsigned long)board_code,
        meowkey_board_id_summary());

    used = append_format(output, output_capacity, used,
        "\"credentials\":{"
        "\"count\":%lu,"
        "\"capacity\":%lu,"
        "\"storeFormatVersion\":%lu"
        "},"
        "\"pin\":{"
        "\"configured\":%s,"
        "\"retries\":%u"
        "},",
        (unsigned long)credential_count,
        (unsigned long)credential_capacity,
        (unsigned long)meowkey_store_get_format_version(),
        bool_json(pin_state.configured),
        (unsigned int)pin_state.retries);

    used = append_format(output, output_capacity, used,
        "\"userPresence\":{"
        "\"enabled\":%s,"
        "\"source\":\"%s\","
        "\"gpioPin\":%d,"
        "\"gpioActiveLow\":%s,"
        "\"tapCount\":%u,"
        "\"gestureWindowMs\":%u,"
        "\"requestTimeoutMs\":%u,"
        "\"sessionOverride\":%s"
        "},",
        bool_json(meowkey_user_presence_is_enabled()),
        user_presence_source_name(user_presence.source),
        (int)user_presence.gpio_pin,
        bool_json(user_presence.gpio_active_low != 0u),
        (unsigned int)user_presence.tap_count,
        (unsigned int)user_presence.gesture_window_ms,
        (unsigned int)user_presence.request_timeout_ms,
        bool_json(meowkey_user_presence_has_session_override()));

    used = append_format(output, output_capacity, used,
        "\"interfaces\":{"
        "\"fidoHid\":true,"
        "\"management\":true,"
        "\"debugHid\":%s"
        "},"
        "\"ctap\":{\"configured\":%s}"
        "}",
        bool_json(MEOWKEY_ENABLE_DEBUG_HID != 0),
        bool_json(ctap_hid_is_configured()));

    if (used >= output_capacity) {
        return 0u;
    }

    return used;
}

static size_t build_ping_json(char *output, size_t output_capacity) {
    return (size_t)snprintf(
        output,
        output_capacity,
        "{\"protocolVersion\":%u,\"ok\":true}",
        (unsigned int)MEOWKEY_MANAGER_PROTOCOL_VERSION);
}

static void send_response(uint8_t status, uint8_t command, const char *payload, size_t payload_length) {
    size_t total_length;
    uint32_t written;

    if (payload_length > (sizeof(s_response_buffer) - MEOWKEY_MANAGER_HEADER_SIZE)) {
        payload_length = sizeof(s_response_buffer) - MEOWKEY_MANAGER_HEADER_SIZE;
    }

    s_response_buffer[0] = MEOWKEY_MANAGER_REQUEST_MAGIC_0;
    s_response_buffer[1] = MEOWKEY_MANAGER_REQUEST_MAGIC_1;
    s_response_buffer[2] = MEOWKEY_MANAGER_REQUEST_MAGIC_2;
    s_response_buffer[3] = MEOWKEY_MANAGER_REQUEST_MAGIC_3;
    s_response_buffer[4] = MEOWKEY_MANAGER_PROTOCOL_VERSION;
    s_response_buffer[5] = status;
    s_response_buffer[6] = command;
    s_response_buffer[7] = 0u;
    write_le16(&s_response_buffer[8], (uint16_t)payload_length);
    if (payload_length > 0u && payload != NULL) {
        memcpy(&s_response_buffer[MEOWKEY_MANAGER_HEADER_SIZE], payload, payload_length);
    }

    total_length = MEOWKEY_MANAGER_HEADER_SIZE + payload_length;
    written = tud_vendor_n_write(0u, s_response_buffer, (uint32_t)total_length);
    if (written == total_length) {
        (void)tud_vendor_n_write_flush(0u);
    }
}

void meowkey_manager_init(void) {
    memset(s_response_buffer, 0, sizeof(s_response_buffer));
}

void tud_vendor_rx_cb(uint8_t itf, uint8_t const *buffer, uint16_t bufsize) {
    uint16_t payload_length;
    char payload[MEOWKEY_MANAGER_MAX_RESPONSE_SIZE - MEOWKEY_MANAGER_HEADER_SIZE];
    size_t response_length = 0u;
    uint8_t command;

    if (itf != 0u) {
        return;
    }

    if (bufsize < MEOWKEY_MANAGER_HEADER_SIZE) {
        send_response(MEOWKEY_MANAGER_STATUS_INVALID_REQUEST, 0u, NULL, 0u);
        tud_vendor_read_flush();
        return;
    }

    if (buffer[0] != MEOWKEY_MANAGER_REQUEST_MAGIC_0 ||
        buffer[1] != MEOWKEY_MANAGER_REQUEST_MAGIC_1 ||
        buffer[2] != MEOWKEY_MANAGER_REQUEST_MAGIC_2 ||
        buffer[3] != MEOWKEY_MANAGER_REQUEST_MAGIC_3 ||
        buffer[4] != MEOWKEY_MANAGER_PROTOCOL_VERSION) {
        send_response(MEOWKEY_MANAGER_STATUS_INVALID_REQUEST, 0u, NULL, 0u);
        tud_vendor_read_flush();
        return;
    }

    command = buffer[5];
    payload_length = read_le16(&buffer[8]);
    if ((size_t)payload_length != (bufsize - MEOWKEY_MANAGER_HEADER_SIZE)) {
        send_response(MEOWKEY_MANAGER_STATUS_INVALID_REQUEST, command, NULL, 0u);
        tud_vendor_read_flush();
        return;
    }

    switch (command) {
    case MEOWKEY_MANAGER_CMD_GET_SNAPSHOT:
        response_length = build_snapshot_json(payload, sizeof(payload));
        if (response_length == 0u) {
            send_response(MEOWKEY_MANAGER_STATUS_INTERNAL_ERROR, command, NULL, 0u);
        } else {
            send_response(MEOWKEY_MANAGER_STATUS_OK, command, payload, response_length);
        }
        break;

    case MEOWKEY_MANAGER_CMD_PING:
        response_length = build_ping_json(payload, sizeof(payload));
        send_response(MEOWKEY_MANAGER_STATUS_OK, command, payload, response_length);
        break;

    default:
        send_response(MEOWKEY_MANAGER_STATUS_UNSUPPORTED_COMMAND, command, NULL, 0u);
        break;
    }

    tud_vendor_read_flush();
}
