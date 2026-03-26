#include "ctap2.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cbor.h"
#include "client_pin.h"
#include "ctap_hid.h"
#include "credential_store.h"
#include "user_presence.h"
#include "webauthn.h"

enum {
    CTAP2_STATUS_OK = 0x00,
    CTAP2_ERR_INVALID_COMMAND = 0x01,
    CTAP2_ERR_INVALID_LENGTH = 0x03,
};

enum {
    CTAP2_CMD_MAKE_CREDENTIAL = 0x01,
    CTAP2_CMD_GET_ASSERTION = 0x02,
    CTAP2_CMD_GET_INFO = 0x04,
    CTAP2_CMD_CLIENT_PIN = 0x06,
    CTAP2_CMD_GET_NEXT_ASSERTION = 0x08,
};

static void ctap2_write_status_only(uint8_t status,
                                    uint8_t *response,
                                    size_t *response_length) {
    response[0] = status;
    *response_length = 1;
}

static uint8_t ctap2_build_get_info(uint8_t *response, size_t *response_length) {
    meowkey_cbor_writer_t writer;
    meowkey_pin_state_t pin_state;

    meowkey_store_init();
    meowkey_user_presence_init();
    meowkey_store_get_pin_state(&pin_state);
    meowkey_cbor_writer_init(&writer, response, *response_length);

    meowkey_cbor_write_map_start(&writer, 8u);
    meowkey_cbor_write_int(&writer, 1);
    meowkey_cbor_write_array_start(&writer, 1u);
    meowkey_cbor_write_text(&writer, "FIDO_2_0", 8u);

    meowkey_cbor_write_int(&writer, 2);
    meowkey_cbor_write_array_start(&writer, 1u);
    meowkey_cbor_write_text(&writer, "hmac-secret", 11u);

    meowkey_cbor_write_int(&writer, 3);
    meowkey_cbor_write_bytes(&writer, (const uint8_t *)"\x4d\x65\x6f\x77\x4b\x65\x79\x00\x52\x50\x32\x33\x35\x30\x00\x01", 16u);

    meowkey_cbor_write_int(&writer, 4);
    meowkey_cbor_write_map_start(&writer, 6u);
    meowkey_cbor_write_text(&writer, "rk", 2u);
    meowkey_cbor_write_bool(&writer, true);
    meowkey_cbor_write_text(&writer, "up", 2u);
    meowkey_cbor_write_bool(&writer, true);
    meowkey_cbor_write_text(&writer, "uv", 2u);
    meowkey_cbor_write_bool(&writer, false);
    meowkey_cbor_write_text(&writer, "plat", 4u);
    meowkey_cbor_write_bool(&writer, false);
    meowkey_cbor_write_text(&writer, "clientPin", 9u);
    meowkey_cbor_write_bool(&writer, pin_state.configured);
    meowkey_cbor_write_text(&writer, "makeCredUvNotRqd", 16u);
    meowkey_cbor_write_bool(&writer, true);

    meowkey_cbor_write_int(&writer, 5);
    meowkey_cbor_write_int(&writer, CTAP_HID_MAX_MESSAGE_SIZE);

    meowkey_cbor_write_int(&writer, 6);
    meowkey_cbor_write_array_start(&writer, 1u);
    meowkey_cbor_write_int(&writer, 1);

    meowkey_cbor_write_int(&writer, 9);
    meowkey_cbor_write_array_start(&writer, 1u);
    meowkey_cbor_write_text(&writer, "usb", 3u);

    meowkey_cbor_write_int(&writer, 10);
    meowkey_cbor_write_array_start(&writer, 1u);
    meowkey_cbor_write_map_start(&writer, 2u);
    meowkey_cbor_write_text(&writer, "type", 4u);
    meowkey_cbor_write_text(&writer, "public-key", 10u);
    meowkey_cbor_write_text(&writer, "alg", 3u);
    meowkey_cbor_write_int(&writer, -7);

    if (writer.failed) {
        return CTAP2_ERR_INVALID_LENGTH;
    }

    *response_length = writer.length;
    return CTAP2_STATUS_OK;
}

bool ctap2_handle_cbor(uint8_t const *request,
                       size_t request_length,
                       uint8_t *response,
                       size_t *response_length) {
    uint8_t status = CTAP2_STATUS_OK;
    size_t body_length;

    if (request_length == 0) {
        ctap2_write_status_only(CTAP2_ERR_INVALID_LENGTH, response, response_length);
        return true;
    }

    body_length = *response_length > 0u ? (*response_length - 1u) : 0u;
    switch (request[0]) {
    case CTAP2_CMD_MAKE_CREDENTIAL:
        status = meowkey_webauthn_make_credential(&request[1], request_length - 1u, &response[1], &body_length);
        break;

    case CTAP2_CMD_GET_ASSERTION:
        status = meowkey_webauthn_get_assertion(&request[1], request_length - 1u, &response[1], &body_length);
        break;

    case CTAP2_CMD_GET_NEXT_ASSERTION:
        status = meowkey_webauthn_get_next_assertion(&request[1], request_length - 1u, &response[1], &body_length);
        break;

    case CTAP2_CMD_GET_INFO:
        status = ctap2_build_get_info(&response[1], &body_length);
        break;

    case CTAP2_CMD_CLIENT_PIN:
        status = meowkey_client_pin_handle(&request[1], request_length - 1u, &response[1], &body_length);
        break;

    default:
        ctap2_write_status_only(CTAP2_ERR_INVALID_COMMAND, response, response_length);
        return true;
    }

    response[0] = status;
    *response_length = status == CTAP2_STATUS_OK ? (body_length + 1u) : 1u;
    return true;
}
