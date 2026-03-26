#include "ctap_hid.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ctap2.h"
#include "credential_store.h"
#include "diagnostics.h"
#include "meowkey_build_config.h"
#include "tusb.h"

enum {
    CTAP_HID_BROADCAST_CID = 0xFFFFFFFFu,
    CTAP_HID_INIT_HEADER_SIZE = 7u,
    CTAP_HID_INIT_PAYLOAD_SIZE = CTAP_HID_PACKET_SIZE - CTAP_HID_INIT_HEADER_SIZE,
    CTAP_HID_CONT_HEADER_SIZE = 5u,
    CTAP_HID_CONT_PAYLOAD_SIZE = CTAP_HID_PACKET_SIZE - CTAP_HID_CONT_HEADER_SIZE,
};

enum {
    CTAP_HID_CMD_PING = 0x01,
    CTAP_HID_CMD_MSG = 0x03,
    CTAP_HID_CMD_LOCK = 0x04,
    CTAP_HID_CMD_INIT = 0x06,
    CTAP_HID_CMD_WINK = 0x08,
    CTAP_HID_CMD_CBOR = 0x10,
    CTAP_HID_CMD_CANCEL = 0x11,
    CTAP_HID_CMD_DIAG = 0x40,
    CTAP_HID_CMD_ERROR = 0x3F,
};

enum {
    CTAP_HID_ERR_INVALID_CMD = 0x01,
    CTAP_HID_ERR_INVALID_LEN = 0x03,
    CTAP_HID_ERR_INVALID_SEQ = 0x04,
    CTAP_HID_ERR_INVALID_CID = 0x0B,
};

enum {
    CTAP_HID_CAPABILITY_CBOR = 0x04,
    CTAP_HID_CAPABILITY_NO_MSG = 0x08,
    CTAP_HID_INTERFACE_COUNT = MEOWKEY_ENABLE_DEBUG_HID ? 2 : 1,
};

typedef struct {
    bool seen_init;
    bool seen_cbor;

    bool request_in_progress;
    uint32_t request_cid;
    uint8_t request_cmd;
    uint16_t request_total_length;
    size_t request_received;
    uint8_t request_next_seq;
    uint8_t request_buffer[CTAP_HID_MAX_MESSAGE_SIZE];

    bool response_in_progress;
    uint32_t response_cid;
    uint8_t response_cmd;
    size_t response_length;
    size_t response_sent;
    uint8_t response_next_seq;
    uint8_t response_buffer[CTAP_HID_MAX_MESSAGE_SIZE];
} ctap_hid_interface_state_t;

static uint32_t s_next_channel_id = 1u;
static ctap_hid_interface_state_t s_interfaces[CTAP_HID_INTERFACE_COUNT];

static const char *interface_name(uint8_t instance) {
#if MEOWKEY_ENABLE_DEBUG_HID
    return instance == CTAP_HID_FIDO_INSTANCE ? "FIDO" : "DEBUG";
#else
    (void)instance;
    return "FIDO";
#endif
}

static const char *hid_command_name(uint8_t command) {
    switch (command) {
    case CTAP_HID_CMD_PING:
        return "PING";
    case CTAP_HID_CMD_INIT:
        return "INIT";
    case CTAP_HID_CMD_CBOR:
        return "CBOR";
    case CTAP_HID_CMD_CANCEL:
        return "CANCEL";
    case CTAP_HID_CMD_DIAG:
        return "DIAG";
    case CTAP_HID_CMD_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

static const char *ctap_command_name(uint8_t command) {
    switch (command) {
    case 0x01:
        return "makeCredential";
    case 0x02:
        return "getAssertion";
    case 0x04:
        return "getInfo";
    case 0x06:
        return "clientPIN";
    default:
        return "unknown";
    }
}

static const char *ctap_status_name(uint8_t status) {
    switch (status) {
    case 0x00:
        return "OK";
    case 0x12:
        return "INVALID_CBOR";
    case 0x14:
        return "MISSING_PARAMETER";
    case 0x19:
        return "CREDENTIAL_EXCLUDED";
    case 0x22:
        return "INVALID_CREDENTIAL";
    case 0x26:
        return "UNSUPPORTED_ALGORITHM";
    case 0x28:
        return "KEY_STORE_FULL";
    case 0x2b:
        return "UNSUPPORTED_OPTION";
    case 0x2e:
        return "NO_CREDENTIALS";
    case 0x31:
        return "PIN_INVALID";
    case 0x32:
        return "PIN_BLOCKED";
    case 0x33:
        return "PIN_AUTH_INVALID";
    case 0x35:
        return "PIN_NOT_SET";
    case 0x36:
        return "PIN_REQUIRED";
    default:
        return "OTHER";
    }
}

static void log_hid_message(const char *direction,
                            uint8_t instance,
                            uint32_t cid,
                            uint8_t command,
                            size_t payload_length) {
    meowkey_diag_logf(
        "%s %s %s cid=0x%08lx len=%lu",
        interface_name(instance),
        direction,
        hid_command_name(command),
        (unsigned long)cid,
        (unsigned long)payload_length);
}

static uint16_t read_be16(uint8_t const *data) {
    return (uint16_t)((uint16_t)data[0] << 8u) | (uint16_t)data[1];
}

static uint32_t read_be32(uint8_t const *data) {
    return ((uint32_t)data[0] << 24u) |
           ((uint32_t)data[1] << 16u) |
           ((uint32_t)data[2] << 8u) |
           (uint32_t)data[3];
}

static void write_be32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24u);
    data[1] = (uint8_t)(value >> 16u);
    data[2] = (uint8_t)(value >> 8u);
    data[3] = (uint8_t)value;
}

static size_t min_size(size_t a, size_t b) {
    return a < b ? a : b;
}

static size_t hex_prefix(const uint8_t *data, size_t length, char *output, size_t output_capacity) {
    static const char k_hex[] = "0123456789abcdef";
    size_t index;
    size_t prefix_length = length < 8u ? length : 8u;

    if (output_capacity == 0u) {
        return 0u;
    }

    if ((prefix_length * 2u + 1u) > output_capacity) {
        prefix_length = (output_capacity - 1u) / 2u;
    }

    for (index = 0; index < prefix_length; ++index) {
        output[index * 2u] = k_hex[data[index] >> 4u];
        output[index * 2u + 1u] = k_hex[data[index] & 0x0fu];
    }
    output[prefix_length * 2u] = '\0';
    return prefix_length * 2u;
}

static size_t list_credentials(char *output, size_t output_capacity) {
    size_t used = 0u;
    uint32_t total = meowkey_store_get_credential_count();
    uint32_t capacity = meowkey_store_get_credential_capacity();
    uint32_t slot_index;
    uint32_t shown = 0u;

    if (output_capacity == 0u) {
        return 0u;
    }

    used += meowkey_store_write_summary(&output[used], output_capacity - used);
    if (used >= output_capacity) {
        output[output_capacity - 1u] = '\0';
        return output_capacity - 1u;
    }

    used += (size_t)snprintf(&output[used],
                             output_capacity - used,
                             "credential-count=%lu capacity=%lu\n",
                             (unsigned long)total,
                             (unsigned long)capacity);
    if (used >= output_capacity) {
        output[output_capacity - 1u] = '\0';
        return output_capacity - 1u;
    }

    for (slot_index = 0; slot_index < capacity; ++slot_index) {
        meowkey_credential_record_t record;
        char credential_id_prefix[17];
        int line_length;

        if (!meowkey_store_get_credential_by_slot(slot_index, &record)) {
            continue;
        }

        (void)hex_prefix(record.credential_id, record.credential_id_length, credential_id_prefix, sizeof(credential_id_prefix));
        line_length = snprintf(&output[used],
                               output_capacity - used,
                               "[slot %lu] rp=%s user=%s sign=%lu id=%s%s\n",
                               (unsigned long)slot_index,
                               record.rp_id_length > 0u ? record.rp_id : "(none)",
                               record.user_name_length > 0u ? record.user_name : "(anonymous)",
                               (unsigned long)record.sign_count,
                               credential_id_prefix,
                               record.credential_id_length > 8u ? "..." : "");
        if (line_length < 0 || (size_t)line_length >= (output_capacity - used)) {
            used += (size_t)snprintf(&output[used],
                                     output_capacity - used,
                                     "... truncated, %lu more credential(s)\n",
                                     (unsigned long)(total > shown ? (total - shown) : 0u));
            if (used >= output_capacity) {
                output[output_capacity - 1u] = '\0';
                return output_capacity - 1u;
            }
            return used;
        }

        used += (size_t)line_length;
        shown += 1u;
    }

    if (shown == 0u) {
        used += (size_t)snprintf(&output[used], output_capacity - used, "(empty)\n");
    }
    if (used >= output_capacity) {
        output[output_capacity - 1u] = '\0';
        return output_capacity - 1u;
    }
    return used;
}

static void clear_request_state(ctap_hid_interface_state_t *state) {
    state->request_in_progress = false;
    state->request_cid = 0u;
    state->request_cmd = 0u;
    state->request_total_length = 0u;
    state->request_received = 0u;
    state->request_next_seq = 0u;
}

static void clear_response_state(ctap_hid_interface_state_t *state) {
    state->response_in_progress = false;
    state->response_cid = 0u;
    state->response_cmd = 0u;
    state->response_length = 0u;
    state->response_sent = 0u;
    state->response_next_seq = 0u;
}

static void queue_response(ctap_hid_interface_state_t *state,
                           uint32_t cid,
                           uint8_t command,
                           uint8_t const *payload,
                           size_t payload_length) {
    if (payload_length > CTAP_HID_MAX_MESSAGE_SIZE) {
        payload_length = CTAP_HID_MAX_MESSAGE_SIZE;
    }

    clear_response_state(state);
    state->response_cid = cid;
    state->response_cmd = command;
    state->response_length = payload_length;
    if (payload_length > 0u) {
        memcpy(state->response_buffer, payload, payload_length);
    }
    state->response_in_progress = true;
}

static void queue_error(ctap_hid_interface_state_t *state, uint32_t cid, uint8_t error) {
    queue_response(state, cid, CTAP_HID_CMD_ERROR, &error, 1u);
}

static uint32_t allocate_channel(void) {
    uint32_t channel_id = s_next_channel_id++;
    if (channel_id == 0u || channel_id == CTAP_HID_BROADCAST_CID) {
        s_next_channel_id = 1u;
        channel_id = s_next_channel_id++;
    }
    return channel_id;
}

static void handle_init(ctap_hid_interface_state_t *state,
                        uint32_t cid,
                        uint8_t const *payload,
                        size_t payload_length) {
    uint8_t response[17];
    uint32_t allocated_cid = (cid == CTAP_HID_BROADCAST_CID) ? allocate_channel() : cid;

    if (payload_length != 8u) {
        queue_error(state, cid, CTAP_HID_ERR_INVALID_LEN);
        return;
    }

    memcpy(response, payload, 8u);
    write_be32(&response[8], allocated_cid);
    response[12] = 2;
    response[13] = 1;
    response[14] = 0;
    response[15] = 1;
    response[16] = CTAP_HID_CAPABILITY_CBOR | CTAP_HID_CAPABILITY_NO_MSG;

    state->seen_init = true;
    queue_response(state, cid, CTAP_HID_CMD_INIT, response, sizeof(response));
}

static void handle_ping(ctap_hid_interface_state_t *state,
                        uint32_t cid,
                        uint8_t const *payload,
                        size_t payload_length) {
    queue_response(state, cid, CTAP_HID_CMD_PING, payload, payload_length);
}

static void handle_diagnostics(ctap_hid_interface_state_t *state,
                               uint8_t instance,
                               uint32_t cid,
                               uint8_t const *payload,
                               size_t payload_length) {
    static uint8_t response[CTAP_HID_MAX_MESSAGE_SIZE];
    size_t response_length = 0u;
    uint8_t action = payload_length > 0u ? payload[0] : 1u;

    if (!MEOWKEY_ENABLE_DEBUG_HID || instance != CTAP_HID_DEBUG_INSTANCE) {
        queue_error(state, cid, CTAP_HID_ERR_INVALID_CMD);
        return;
    }

    if (action == 2u) {
        meowkey_diag_clear();
        response_length = (size_t)snprintf((char *)response, sizeof(response), "诊断日志已清空。\n");
    } else if (action == 3u) {
        if (meowkey_store_clear_credentials()) {
            meowkey_diag_logf("credential store cleared via debug diag");
            response_length = (size_t)snprintf((char *)response, sizeof(response), "凭据存储已清空。\n");
        } else {
            response_length = (size_t)snprintf((char *)response, sizeof(response), "凭据存储清空失败。\n");
        }
    } else if (action == 4u) {
        response_length = list_credentials((char *)response, sizeof(response));
    } else {
        response_length = meowkey_diag_snapshot((char *)response, sizeof(response));
    }

    queue_response(state, cid, CTAP_HID_CMD_DIAG, response, response_length);
}

static void handle_cbor(ctap_hid_interface_state_t *state,
                        uint8_t instance,
                        uint32_t cid,
                        uint8_t const *payload,
                        size_t payload_length) {
    uint8_t response[CTAP_HID_MAX_MESSAGE_SIZE];
    size_t response_length = sizeof(response);

    if (!ctap2_handle_cbor(payload, payload_length, response, &response_length)) {
        queue_error(state, cid, CTAP_HID_ERR_INVALID_CMD);
        return;
    }

    if (instance == CTAP_HID_FIDO_INSTANCE && payload_length > 0u) {
        state->seen_cbor = true;
    }

    if (payload_length > 0u) {
        meowkey_diag_logf(
            "%s CTAP %s status=0x%02x(%s) req=%lu resp=%lu",
            interface_name(instance),
            ctap_command_name(payload[0]),
            response[0],
            ctap_status_name(response[0]),
            (unsigned long)payload_length,
            (unsigned long)response_length);
    }

    if (response_length > CTAP_HID_MAX_MESSAGE_SIZE) {
        queue_error(state, cid, CTAP_HID_ERR_INVALID_LEN);
        return;
    }

    queue_response(state, cid, CTAP_HID_CMD_CBOR, response, response_length);
}

static void dispatch_request(ctap_hid_interface_state_t *state,
                             uint8_t instance,
                             uint32_t cid,
                             uint8_t command,
                             uint8_t const *payload,
                             size_t payload_length) {
    if (instance == CTAP_HID_FIDO_INSTANCE || command == CTAP_HID_CMD_DIAG) {
        log_hid_message("RX", instance, cid, command, payload_length);
    }

    switch (command) {
    case CTAP_HID_CMD_INIT:
        handle_init(state, cid, payload, payload_length);
        break;

    case CTAP_HID_CMD_PING:
        handle_ping(state, cid, payload, payload_length);
        break;

    case CTAP_HID_CMD_CBOR:
        handle_cbor(state, instance, cid, payload, payload_length);
        break;

    case CTAP_HID_CMD_CANCEL:
        break;

    case CTAP_HID_CMD_DIAG:
        handle_diagnostics(state, instance, cid, payload, payload_length);
        break;

    case CTAP_HID_CMD_MSG:
    case CTAP_HID_CMD_LOCK:
    case CTAP_HID_CMD_WINK:
    default:
        queue_error(state, cid, CTAP_HID_ERR_INVALID_CMD);
        break;
    }

    if (state->response_in_progress && (instance == CTAP_HID_FIDO_INSTANCE || command == CTAP_HID_CMD_DIAG)) {
        log_hid_message("TX", instance, state->response_cid, state->response_cmd, state->response_length);
    }
}

static void send_next_response_packet(uint8_t instance, ctap_hid_interface_state_t *state) {
    uint8_t packet[CTAP_HID_PACKET_SIZE];
    size_t remaining;
    size_t chunk_length;

    if (!state->response_in_progress || !tud_hid_n_ready(instance)) {
        return;
    }

    memset(packet, 0, sizeof(packet));
    write_be32(packet, state->response_cid);
    remaining = state->response_length - state->response_sent;

    if (state->response_sent == 0u) {
        chunk_length = min_size(remaining, CTAP_HID_INIT_PAYLOAD_SIZE);
        packet[4] = (uint8_t)(0x80u | state->response_cmd);
        packet[5] = (uint8_t)(state->response_length >> 8u);
        packet[6] = (uint8_t)state->response_length;
        if (chunk_length > 0u) {
            memcpy(&packet[7], state->response_buffer, chunk_length);
        }
    } else {
        chunk_length = min_size(remaining, CTAP_HID_CONT_PAYLOAD_SIZE);
        packet[4] = state->response_next_seq++;
        if (chunk_length > 0u) {
            memcpy(&packet[5], &state->response_buffer[state->response_sent], chunk_length);
        }
    }

    tud_hid_n_report(instance, 0, packet, sizeof(packet));
    state->response_sent += chunk_length;

    if (state->response_sent >= state->response_length) {
        clear_response_state(state);
    }
}

void ctap_hid_init(void) {
    s_next_channel_id = 1u;
    memset(s_interfaces, 0, sizeof(s_interfaces));
}

bool ctap_hid_is_configured(void) {
    return s_interfaces[CTAP_HID_FIDO_INSTANCE].seen_cbor;
}

void ctap_hid_task(void) {
    uint8_t instance;
    for (instance = 0; instance < CTAP_HID_INTERFACE_COUNT; ++instance) {
        send_next_response_packet(instance, &s_interfaces[instance]);
    }
}

void ctap_hid_handle_report(uint8_t instance, uint8_t const *report, size_t report_length) {
    ctap_hid_interface_state_t *state;
    uint32_t cid;
    uint8_t command;
    uint16_t payload_length;
    uint8_t const *payload;
    size_t chunk_length;

    if (instance >= CTAP_HID_INTERFACE_COUNT) {
        return;
    }

    state = &s_interfaces[instance];

    if (report_length != CTAP_HID_PACKET_SIZE) {
        return;
    }

    cid = read_be32(report);

    if ((report[4] & 0x80u) == 0u) {
        if (!state->request_in_progress || cid != state->request_cid) {
            if (cid != CTAP_HID_BROADCAST_CID) {
                queue_error(state, cid, CTAP_HID_ERR_INVALID_SEQ);
            }
            return;
        }
        if (report[4] != state->request_next_seq) {
            queue_error(state, cid, CTAP_HID_ERR_INVALID_SEQ);
            clear_request_state(state);
            return;
        }

        state->request_next_seq++;
        chunk_length = min_size(
            (size_t)(state->request_total_length - state->request_received),
            CTAP_HID_CONT_PAYLOAD_SIZE);
        memcpy(&state->request_buffer[state->request_received], &report[5], chunk_length);
        state->request_received += chunk_length;
        if (state->request_received >= state->request_total_length) {
            dispatch_request(
                state,
                instance,
                state->request_cid,
                state->request_cmd,
                state->request_buffer,
                state->request_total_length);
            clear_request_state(state);
        }
        return;
    }

    command = (uint8_t)(report[4] & 0x7Fu);
    payload_length = read_be16(&report[5]);
    payload = &report[7];

    if (payload_length > CTAP_HID_MAX_MESSAGE_SIZE) {
        queue_error(state, cid, CTAP_HID_ERR_INVALID_LEN);
        clear_request_state(state);
        return;
    }

    if (cid == CTAP_HID_BROADCAST_CID && command != CTAP_HID_CMD_INIT) {
        queue_error(state, cid, CTAP_HID_ERR_INVALID_CID);
        clear_request_state(state);
        return;
    }

    clear_request_state(state);
    state->request_in_progress = true;
    state->request_cid = cid;
    state->request_cmd = command;
    state->request_total_length = payload_length;
    state->request_received = min_size(payload_length, CTAP_HID_INIT_PAYLOAD_SIZE);
    if (state->request_received > 0u) {
        memcpy(state->request_buffer, payload, state->request_received);
    }

    if (state->request_received >= state->request_total_length) {
        dispatch_request(state, instance, cid, command, state->request_buffer, payload_length);
        clear_request_state(state);
    }
}
