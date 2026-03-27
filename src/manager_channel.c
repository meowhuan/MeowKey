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
#include "security_status.h"
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
    MEOWKEY_MANAGER_CMD_GET_CREDENTIAL_SUMMARIES = 0x03u,
    MEOWKEY_MANAGER_CMD_GET_SECURITY_STATE = 0x04u,
};

enum {
    MEOWKEY_MANAGER_STATUS_OK = 0x00u,
    MEOWKEY_MANAGER_STATUS_INVALID_REQUEST = 0x01u,
    MEOWKEY_MANAGER_STATUS_UNSUPPORTED_COMMAND = 0x02u,
    MEOWKEY_MANAGER_STATUS_INTERNAL_ERROR = 0x03u,
};

enum {
    MEOWKEY_MANAGER_CREDENTIAL_PAGE_DEFAULT_LIMIT = 8u,
    MEOWKEY_MANAGER_CREDENTIAL_PAGE_MAX_LIMIT = 16u,
    MEOWKEY_MANAGER_CREDENTIAL_TAIL_RESERVE = 160u,
    MEOWKEY_MANAGER_RP_ID_PREVIEW_SIZE = 24u,
    MEOWKEY_MANAGER_USER_NAME_PREVIEW_SIZE = 24u,
    MEOWKEY_MANAGER_DISPLAY_NAME_PREVIEW_SIZE = 24u,
    MEOWKEY_MANAGER_CREDENTIAL_ID_PREFIX_BYTES = 8u,
};

static uint8_t s_response_buffer[MEOWKEY_MANAGER_MAX_RESPONSE_SIZE];

typedef struct {
    uint16_t cursor;
    uint16_t limit;
} meowkey_manager_credential_page_t;

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

static size_t append_literal(char *output, size_t capacity, size_t used, const char *text) {
    size_t length;

    if (text == NULL) {
        return used;
    }
    if (used >= capacity) {
        return capacity;
    }

    length = strlen(text);
    if (length >= (capacity - used)) {
        return capacity;
    }

    memcpy(&output[used], text, length);
    used += length;
    output[used] = '\0';
    return used;
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

static bool json_escape_copy(const char *value, size_t value_length, char *output, size_t output_capacity) {
    static const char k_hex[] = "0123456789abcdef";
    size_t index;
    size_t used = 0u;

    if (output == NULL || output_capacity == 0u) {
        return false;
    }

    if (value == NULL) {
        output[0] = '\0';
        return true;
    }

    for (index = 0u; index < value_length; ++index) {
        uint8_t ch = (uint8_t)value[index];

        switch (ch) {
        case '\"':
        case '\\':
            if ((used + 2u) >= output_capacity) {
                return false;
            }
            output[used++] = '\\';
            output[used++] = (char)ch;
            break;

        case '\b':
        case '\f':
        case '\n':
        case '\r':
        case '\t':
            if ((used + 2u) >= output_capacity) {
                return false;
            }
            output[used++] = '\\';
            output[used++] = ch == '\b' ? 'b'
                : ch == '\f' ? 'f'
                : ch == '\n' ? 'n'
                : ch == '\r' ? 'r'
                : 't';
            break;

        default:
            if (ch < 0x20u) {
                if ((used + 6u) >= output_capacity) {
                    return false;
                }
                output[used++] = '\\';
                output[used++] = 'u';
                output[used++] = '0';
                output[used++] = '0';
                output[used++] = k_hex[ch >> 4u];
                output[used++] = k_hex[ch & 0x0fu];
            } else {
                if ((used + 1u) >= output_capacity) {
                    return false;
                }
                output[used++] = (char)ch;
            }
            break;
        }
    }

    output[used] = '\0';
    return true;
}

static bool json_escape_cstr(const char *value, char *output, size_t output_capacity) {
    return json_escape_copy(value, value != NULL ? strlen(value) : 0u, output, output_capacity);
}

static size_t string_preview_copy(const char *source,
                                  size_t source_length,
                                  char *output,
                                  size_t output_capacity,
                                  size_t max_preview_length) {
    size_t preview_length = source_length;

    if (output == NULL || output_capacity == 0u) {
        return 0u;
    }

    if (preview_length > max_preview_length) {
        preview_length = max_preview_length;
    }
    if (preview_length >= output_capacity) {
        preview_length = output_capacity - 1u;
    }

    if (preview_length > 0u && source != NULL) {
        memcpy(output, source, preview_length);
    }
    output[preview_length] = '\0';
    return preview_length;
}

static size_t hex_prefix(const uint8_t *data, size_t length, char *output, size_t output_capacity) {
    static const char k_hex[] = "0123456789abcdef";
    size_t index;
    size_t prefix_length = length < MEOWKEY_MANAGER_CREDENTIAL_ID_PREFIX_BYTES
        ? length
        : MEOWKEY_MANAGER_CREDENTIAL_ID_PREFIX_BYTES;

    if (output == NULL || output_capacity == 0u) {
        return 0u;
    }

    if ((prefix_length * 2u + 1u) > output_capacity) {
        prefix_length = (output_capacity - 1u) / 2u;
    }

    for (index = 0u; index < prefix_length; ++index) {
        output[index * 2u] = k_hex[data[index] >> 4u];
        output[index * 2u + 1u] = k_hex[data[index] & 0x0fu];
    }
    output[prefix_length * 2u] = '\0';
    return prefix_length * 2u;
}

static bool parse_credential_page_request(const uint8_t *payload,
                                          size_t payload_length,
                                          meowkey_manager_credential_page_t *request) {
    if (request == NULL) {
        return false;
    }

    request->cursor = 0u;
    request->limit = MEOWKEY_MANAGER_CREDENTIAL_PAGE_DEFAULT_LIMIT;

    if (payload_length == 0u) {
        return true;
    }
    if (payload == NULL || payload_length != 4u) {
        return false;
    }

    request->cursor = read_le16(payload);
    request->limit = read_le16(&payload[2]);
    if (request->limit == 0u) {
        request->limit = MEOWKEY_MANAGER_CREDENTIAL_PAGE_DEFAULT_LIMIT;
    }
    if (request->limit > MEOWKEY_MANAGER_CREDENTIAL_PAGE_MAX_LIMIT) {
        request->limit = MEOWKEY_MANAGER_CREDENTIAL_PAGE_MAX_LIMIT;
    }
    return true;
}

static size_t build_user_presence_config_json(char *output,
                                              size_t output_capacity,
                                              const meowkey_user_presence_config_t *config) {
    bool enabled;
    int written;

    if (output == NULL || output_capacity == 0u || config == NULL) {
        return 0u;
    }

    enabled = config->source != MEOWKEY_USER_PRESENCE_SOURCE_NONE;
    written = snprintf(output,
                       output_capacity,
                       "{"
                       "\"enabled\":%s,"
                       "\"source\":\"%s\","
                       "\"gpioPin\":%d,"
                       "\"gpioActiveLow\":%s,"
                       "\"tapCount\":%u,"
                       "\"gestureWindowMs\":%u,"
                       "\"requestTimeoutMs\":%u"
                       "}",
                       bool_json(enabled),
                       user_presence_source_name(config->source),
                       (int)config->gpio_pin,
                       bool_json(config->gpio_active_low != 0u),
                       (unsigned int)config->tap_count,
                       (unsigned int)config->gesture_window_ms,
                       (unsigned int)config->request_timeout_ms);
    if (written < 0 || (size_t)written >= output_capacity) {
        return 0u;
    }
    return (size_t)written;
}

static bool build_version_string_json(char *output, size_t output_capacity) {
    char version[64];
    int written;

    if (output == NULL || output_capacity == 0u) {
        return false;
    }

    written = snprintf(version,
                       sizeof(version),
                       "%d.%d.%d%s%s",
                       MEOWKEY_VERSION_MAJOR,
                       MEOWKEY_VERSION_MINOR,
                       MEOWKEY_VERSION_PATCH,
                       strlen(MEOWKEY_VERSION_LABEL) > 0u ? "-" : "",
                       MEOWKEY_VERSION_LABEL);
    if (written < 0 || (size_t)written >= sizeof(version)) {
        return false;
    }

    return json_escape_cstr(version, output, output_capacity);
}

static size_t build_snapshot_json(char *output, size_t output_capacity) {
    meowkey_pin_state_t pin_state;
    meowkey_user_presence_config_t user_presence;
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    char escaped_device_name[96];
    char escaped_product_name[96];
    char escaped_serial[(2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 6) + 1];
    char escaped_board_summary[(128u * 6u) + 1u];
    char escaped_version[96];
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
    if (!json_escape_cstr(MEOWKEY_USB_PRODUCT_NAME, escaped_device_name, sizeof(escaped_device_name)) ||
        !json_escape_cstr(MEOWKEY_PRODUCT_NAME, escaped_product_name, sizeof(escaped_product_name)) ||
        !json_escape_cstr(serial, escaped_serial, sizeof(escaped_serial)) ||
        !json_escape_cstr(meowkey_board_id_summary(), escaped_board_summary, sizeof(escaped_board_summary)) ||
        !build_version_string_json(escaped_version, sizeof(escaped_version))) {
        return 0u;
    }

    used = append_format(output, output_capacity, used,
        "{"
        "\"protocolVersion\":%u,"
        "\"transport\":\"winusb-bulk-v1\","
        "\"deviceName\":\"%s\","
        "\"productName\":\"%s\","
        "\"serial\":\"%s\","
        "\"usb\":{\"vid\":\"0x%04x\",\"pid\":\"0x%04x\"},",
        (unsigned int)MEOWKEY_MANAGER_PROTOCOL_VERSION,
        escaped_device_name,
        escaped_product_name,
        escaped_serial,
        (unsigned int)MEOWKEY_USB_VID,
        (unsigned int)MEOWKEY_USB_PID);

    used = append_format(output, output_capacity, used,
        "\"build\":{"
        "\"flavor\":\"%s\","
        "\"version\":\"%s\","
        "\"debugHidEnabled\":%s,"
        "\"signedBootEnabled\":%s,"
        "\"antiRollbackEnabled\":%s,"
        "\"antiRollbackVersion\":%lu"
        "},",
        build_flavor_name(),
        escaped_version,
        bool_json(MEOWKEY_ENABLE_DEBUG_HID != 0),
        bool_json(MEOWKEY_ENABLE_SIGNED_BOOT != 0),
        bool_json(MEOWKEY_ENABLE_ANTI_ROLLBACK != 0),
        (unsigned long)MEOWKEY_ANTI_ROLLBACK_VERSION);

    used = append_format(output, output_capacity, used,
        "\"board\":{"
        "\"detected\":%s,"
        "\"code\":\"0x%08lx\","
        "\"summary\":\"%s\""
        "},",
        bool_json(board_detected),
        (unsigned long)board_code,
        escaped_board_summary);

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

static size_t build_security_state_json(char *output, size_t output_capacity) {
    meowkey_pin_state_t pin_state;
    meowkey_user_presence_config_t effective_user_presence;
    meowkey_user_presence_config_t persisted_user_presence;
    meowkey_security_status_t security_status;
    char effective_up_json[192];
    char persisted_up_json[192];
    char escaped_board_summary[(128u * 6u) + 1u];
    char escaped_version[96];
    size_t used = 0u;

    meowkey_store_init();
    meowkey_board_id_init();
    meowkey_user_presence_init();
    meowkey_store_get_pin_state(&pin_state);
    meowkey_user_presence_get_config(&effective_user_presence);
    meowkey_user_presence_get_persisted_config(&persisted_user_presence);
    meowkey_security_status_get(&security_status);

    if (build_user_presence_config_json(effective_up_json, sizeof(effective_up_json), &effective_user_presence) == 0u ||
        build_user_presence_config_json(persisted_up_json, sizeof(persisted_up_json), &persisted_user_presence) == 0u ||
        !json_escape_cstr(meowkey_board_id_summary(), escaped_board_summary, sizeof(escaped_board_summary)) ||
        !build_version_string_json(escaped_version, sizeof(escaped_version))) {
        return 0u;
    }

    used = append_format(output, output_capacity, used,
        "{"
        "\"protocolVersion\":%u,"
        "\"build\":{"
        "\"flavor\":\"%s\","
        "\"version\":\"%s\","
        "\"signedBootEnabled\":%s,"
        "\"antiRollbackEnabled\":%s,"
        "\"antiRollbackVersion\":%lu"
        "},"
        "\"board\":{"
        "\"detected\":%s,"
        "\"code\":\"0x%08lx\","
        "\"summary\":\"%s\""
        "},"
        "\"interfaces\":{"
        "\"fidoHid\":true,"
        "\"management\":true,"
        "\"debugHid\":%s"
        "},"
        "\"ctap\":{"
        "\"configured\":%s"
        "},"
        "\"pin\":{"
        "\"configured\":%s,"
        "\"retries\":%u"
        "},"
        "\"userPresence\":{"
        "\"sessionOverride\":%s,"
        "\"effective\":%s,"
        "\"persisted\":%s"
        "},"
        "\"otp\":{"
        "\"bootFlags0\":{"
        "\"available\":%s,"
        "\"raw\":\"0x%06lx\","
        "\"rollbackRequired\":%s,"
        "\"flashBootDisabled\":%s,"
        "\"picobootDisabled\":%s"
        "},"
        "\"bootFlags1\":{"
        "\"available\":%s,"
        "\"raw\":\"0x%06lx\","
        "\"keyValidMask\":\"0x%lx\","
        "\"keyInvalidMask\":\"0x%lx\""
        "}"
        "}"
        "}",
        (unsigned int)MEOWKEY_MANAGER_PROTOCOL_VERSION,
        build_flavor_name(),
        escaped_version,
        bool_json(security_status.signed_boot_enabled),
        bool_json(security_status.anti_rollback_enabled),
        (unsigned long)security_status.anti_rollback_version,
        bool_json(meowkey_board_id_is_detected()),
        (unsigned long)meowkey_board_id_get_code(),
        escaped_board_summary,
        bool_json(MEOWKEY_ENABLE_DEBUG_HID != 0),
        bool_json(ctap_hid_is_configured()),
        bool_json(pin_state.configured),
        (unsigned int)pin_state.retries,
        bool_json(meowkey_user_presence_has_session_override()),
        effective_up_json,
        persisted_up_json,
        bool_json(security_status.boot_flags0_available),
        (unsigned long)security_status.boot_flags0_raw,
        bool_json(security_status.rollback_required),
        bool_json(security_status.flash_boot_disabled),
        bool_json(security_status.picoboot_disabled),
        bool_json(security_status.boot_flags1_available),
        (unsigned long)security_status.boot_flags1_raw,
        (unsigned long)security_status.key_valid_mask,
        (unsigned long)security_status.key_invalid_mask);

    return used >= output_capacity ? 0u : used;
}

static size_t build_credential_summary_item_json(const meowkey_credential_record_t *record,
                                                 uint32_t slot_index,
                                                 char *output,
                                                 size_t output_capacity) {
    char rp_preview[MEOWKEY_MANAGER_RP_ID_PREVIEW_SIZE + 1u];
    char user_name_preview[MEOWKEY_MANAGER_USER_NAME_PREVIEW_SIZE + 1u];
    char display_name_preview[MEOWKEY_MANAGER_DISPLAY_NAME_PREVIEW_SIZE + 1u];
    char escaped_rp_preview[(MEOWKEY_MANAGER_RP_ID_PREVIEW_SIZE * 6u) + 1u];
    char escaped_user_name_preview[(MEOWKEY_MANAGER_USER_NAME_PREVIEW_SIZE * 6u) + 1u];
    char escaped_display_name_preview[(MEOWKEY_MANAGER_DISPLAY_NAME_PREVIEW_SIZE * 6u) + 1u];
    char credential_id_prefix[(MEOWKEY_MANAGER_CREDENTIAL_ID_PREFIX_BYTES * 2u) + 1u];
    size_t rp_preview_length;
    size_t user_name_preview_length;
    size_t display_name_preview_length;

    if (record == NULL || output == NULL || output_capacity == 0u) {
        return 0u;
    }

    rp_preview_length = string_preview_copy(record->rp_id,
                                            record->rp_id_length,
                                            rp_preview,
                                            sizeof(rp_preview),
                                            MEOWKEY_MANAGER_RP_ID_PREVIEW_SIZE);
    user_name_preview_length = string_preview_copy(record->user_name,
                                                   record->user_name_length,
                                                   user_name_preview,
                                                   sizeof(user_name_preview),
                                                   MEOWKEY_MANAGER_USER_NAME_PREVIEW_SIZE);
    display_name_preview_length = string_preview_copy(record->display_name,
                                                      record->display_name_length,
                                                      display_name_preview,
                                                      sizeof(display_name_preview),
                                                      MEOWKEY_MANAGER_DISPLAY_NAME_PREVIEW_SIZE);

    if (!json_escape_copy(rp_preview, rp_preview_length, escaped_rp_preview, sizeof(escaped_rp_preview)) ||
        !json_escape_copy(user_name_preview,
                          user_name_preview_length,
                          escaped_user_name_preview,
                          sizeof(escaped_user_name_preview)) ||
        !json_escape_copy(display_name_preview,
                          display_name_preview_length,
                          escaped_display_name_preview,
                          sizeof(escaped_display_name_preview))) {
        return 0u;
    }

    (void)hex_prefix(record->credential_id,
                     record->credential_id_length,
                     credential_id_prefix,
                     sizeof(credential_id_prefix));

    return (size_t)snprintf(output,
                            output_capacity,
                            "{"
                            "\"slot\":%lu,"
                            "\"credentialIdLength\":%lu,"
                            "\"credentialIdPrefix\":\"%s%s\","
                            "\"signCount\":%lu,"
                            "\"discoverable\":%s,"
                            "\"credRandomReady\":%s,"
                            "\"rpIdPreview\":\"%s\","
                            "\"rpIdLength\":%lu,"
                            "\"userNamePreview\":\"%s\","
                            "\"userNameLength\":%lu,"
                            "\"displayNamePreview\":\"%s\","
                            "\"displayNameLength\":%lu"
                            "}",
                            (unsigned long)slot_index,
                            (unsigned long)record->credential_id_length,
                            credential_id_prefix,
                            record->credential_id_length > MEOWKEY_MANAGER_CREDENTIAL_ID_PREFIX_BYTES ? "..." : "",
                            (unsigned long)record->sign_count,
                            bool_json(record->discoverable),
                            bool_json(record->cred_random_ready),
                            escaped_rp_preview,
                            (unsigned long)record->rp_id_length,
                            escaped_user_name_preview,
                            (unsigned long)record->user_name_length,
                            escaped_display_name_preview,
                            (unsigned long)record->display_name_length);
}

static size_t build_credential_summaries_json(char *output,
                                              size_t output_capacity,
                                              const meowkey_manager_credential_page_t *request) {
    uint32_t total;
    uint32_t capacity;
    uint32_t slot_index;
    uint32_t ordinal = 0u;
    uint32_t returned = 0u;
    bool first_item = true;
    bool has_more;
    size_t used = 0u;

    if (request == NULL) {
        return 0u;
    }

    meowkey_store_init();
    total = meowkey_store_get_credential_count();
    capacity = meowkey_store_get_credential_capacity();

    used = append_format(output,
                         output_capacity,
                         used,
                         "{"
                         "\"protocolVersion\":%u,"
                         "\"cursor\":%u,"
                         "\"limit\":%u,"
                         "\"total\":%lu,"
                         "\"capacity\":%lu,"
                         "\"storeFormatVersion\":%lu,"
                         "\"items\":[",
                         (unsigned int)MEOWKEY_MANAGER_PROTOCOL_VERSION,
                         (unsigned int)request->cursor,
                         (unsigned int)request->limit,
                         (unsigned long)total,
                         (unsigned long)capacity,
                         (unsigned long)meowkey_store_get_format_version());
    if (used >= output_capacity) {
        return 0u;
    }

    for (slot_index = 0u; slot_index < capacity; ++slot_index) {
        meowkey_credential_record_t record;
        char item_json[768];
        size_t item_length;

        if (!meowkey_store_get_credential_by_slot(slot_index, &record)) {
            continue;
        }
        if (ordinal < request->cursor) {
            ordinal += 1u;
            continue;
        }
        if (returned >= request->limit) {
            break;
        }

        item_length = build_credential_summary_item_json(&record, slot_index, item_json, sizeof(item_json));
        if (item_length == 0u || item_length >= sizeof(item_json)) {
            return 0u;
        }
        if ((used + item_length + (first_item ? 0u : 1u) + MEOWKEY_MANAGER_CREDENTIAL_TAIL_RESERVE) >= output_capacity) {
            break;
        }

        if (!first_item) {
            used = append_literal(output, output_capacity, used, ",");
        }
        used = append_literal(output, output_capacity, used, item_json);
        if (used >= output_capacity) {
            return 0u;
        }

        first_item = false;
        ordinal += 1u;
        returned += 1u;
    }

    has_more = ((uint32_t)request->cursor + returned) < total;
    used = append_format(output,
                         output_capacity,
                         used,
                         "],"
                         "\"returned\":%lu,"
                         "\"nextCursor\":%lu,"
                         "\"hasMore\":%s"
                         "}",
                         (unsigned long)returned,
                         (unsigned long)((uint32_t)request->cursor + returned),
                         bool_json(has_more));

    return used >= output_capacity ? 0u : used;
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
    uint8_t const *request_payload = NULL;
    char response_payload[MEOWKEY_MANAGER_MAX_RESPONSE_SIZE - MEOWKEY_MANAGER_HEADER_SIZE];
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
    request_payload = &buffer[MEOWKEY_MANAGER_HEADER_SIZE];

    switch (command) {
    case MEOWKEY_MANAGER_CMD_GET_SNAPSHOT:
        if (payload_length != 0u) {
            send_response(MEOWKEY_MANAGER_STATUS_INVALID_REQUEST, command, NULL, 0u);
            break;
        }
        response_length = build_snapshot_json(response_payload, sizeof(response_payload));
        if (response_length == 0u) {
            send_response(MEOWKEY_MANAGER_STATUS_INTERNAL_ERROR, command, NULL, 0u);
        } else {
            send_response(MEOWKEY_MANAGER_STATUS_OK, command, response_payload, response_length);
        }
        break;

    case MEOWKEY_MANAGER_CMD_PING:
        if (payload_length != 0u) {
            send_response(MEOWKEY_MANAGER_STATUS_INVALID_REQUEST, command, NULL, 0u);
            break;
        }
        response_length = build_ping_json(response_payload, sizeof(response_payload));
        send_response(MEOWKEY_MANAGER_STATUS_OK, command, response_payload, response_length);
        break;

    case MEOWKEY_MANAGER_CMD_GET_CREDENTIAL_SUMMARIES: {
        meowkey_manager_credential_page_t request;

        if (!parse_credential_page_request(request_payload, payload_length, &request)) {
            send_response(MEOWKEY_MANAGER_STATUS_INVALID_REQUEST, command, NULL, 0u);
            break;
        }

        response_length = build_credential_summaries_json(response_payload, sizeof(response_payload), &request);
        if (response_length == 0u) {
            send_response(MEOWKEY_MANAGER_STATUS_INTERNAL_ERROR, command, NULL, 0u);
        } else {
            send_response(MEOWKEY_MANAGER_STATUS_OK, command, response_payload, response_length);
        }
        break;
    }

    case MEOWKEY_MANAGER_CMD_GET_SECURITY_STATE:
        if (payload_length != 0u) {
            send_response(MEOWKEY_MANAGER_STATUS_INVALID_REQUEST, command, NULL, 0u);
            break;
        }
        response_length = build_security_state_json(response_payload, sizeof(response_payload));
        if (response_length == 0u) {
            send_response(MEOWKEY_MANAGER_STATUS_INTERNAL_ERROR, command, NULL, 0u);
        } else {
            send_response(MEOWKEY_MANAGER_STATUS_OK, command, response_payload, response_length);
        }
        break;

    default:
        send_response(MEOWKEY_MANAGER_STATUS_UNSUPPORTED_COMMAND, command, NULL, 0u);
        break;
    }

    tud_vendor_read_flush();
}
