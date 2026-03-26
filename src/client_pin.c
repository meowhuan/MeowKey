#include "client_pin.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bsp/board_api.h"
#include "cbor.h"
#include "credential_store.h"
#include "diagnostics.h"
#include "mbedtls/aes.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md.h"
#include "mbedtls/private_access.h"
#include "mbedtls/sha256.h"
#include "pico/rand.h"

enum {
    CTAP2_STATUS_OK = 0x00,
    CTAP2_ERR_INVALID_CBOR = 0x12,
    CTAP2_ERR_MISSING_PARAMETER = 0x14,
    CTAP2_ERR_PIN_INVALID = 0x31,
    CTAP2_ERR_PIN_BLOCKED = 0x32,
    CTAP2_ERR_PIN_AUTH_INVALID = 0x33,
    CTAP2_ERR_PIN_NOT_SET = 0x35,
    CTAP2_ERR_PIN_REQUIRED = 0x36,
    CTAP2_ERR_PIN_POLICY_VIOLATION = 0x37,
    CTAP2_ERR_PIN_TOKEN_EXPIRED = 0x38,
    CTAP2_ERR_UNSUPPORTED_OPTION = 0x2b,
};

enum {
    CLIENT_PIN_SUBCMD_GET_RETRIES = 1,
    CLIENT_PIN_SUBCMD_GET_KEY_AGREEMENT = 2,
    CLIENT_PIN_SUBCMD_SET_PIN = 3,
    CLIENT_PIN_SUBCMD_CHANGE_PIN = 4,
    CLIENT_PIN_SUBCMD_GET_PIN_TOKEN = 5,
    CLIENT_PIN_SUBCMD_GET_PIN_TOKEN_WITH_PERMISSIONS = 9,
    CLIENT_PIN_RETRIES_DEFAULT = 8,
    CLIENT_PIN_TOKEN_SIZE = 32,
    CLIENT_PIN_HASH_SIZE = 16,
    CLIENT_PIN_TOKEN_LIFETIME_MS = 30000u,
};

typedef struct {
    bool have_protocol;
    uint8_t protocol;
    bool have_subcommand;
    uint8_t subcommand;
    bool have_key_agreement;
    uint8_t peer_public_key[65];
    bool have_pin_uv_auth_param;
    uint8_t pin_uv_auth_param[32];
    size_t pin_uv_auth_param_length;
    bool have_new_pin_enc;
    uint8_t new_pin_enc[80];
    size_t new_pin_enc_length;
    bool have_pin_hash_enc;
    uint8_t pin_hash_enc[32];
    size_t pin_hash_enc_length;
    bool have_permissions;
    int64_t permissions;
} client_pin_request_t;

static bool s_key_agreement_ready = false;
static uint8_t s_key_agreement_private[32];
static uint8_t s_key_agreement_public[65];
static bool s_runtime_pin_token_ready = false;
static uint8_t s_runtime_pin_token[CLIENT_PIN_TOKEN_SIZE];
static uint32_t s_runtime_pin_token_issued_at_ms = 0u;

static int client_pin_random(void *context, unsigned char *output, size_t output_length) {
    (void)context;
    while (output_length > 0u) {
        uint32_t value = get_rand_32();
        size_t chunk_length = output_length < sizeof(value) ? output_length : sizeof(value);
        memcpy(output, &value, chunk_length);
        output += chunk_length;
        output_length -= chunk_length;
    }
    return 0;
}

static void secure_zero(void *buffer, size_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;
    while (length-- > 0u) {
        *bytes++ = 0u;
    }
}

static bool constant_time_equal(const uint8_t *left, const uint8_t *right, size_t length) {
    uint8_t diff = 0u;
    size_t index;

    for (index = 0; index < length; ++index) {
        diff |= (uint8_t)(left[index] ^ right[index]);
    }

    return diff == 0u;
}

static void clear_runtime_pin_token(void) {
    secure_zero(s_runtime_pin_token, sizeof(s_runtime_pin_token));
    s_runtime_pin_token_ready = false;
    s_runtime_pin_token_issued_at_ms = 0u;
}

static bool rotate_runtime_pin_token(void) {
    client_pin_random(NULL, s_runtime_pin_token, sizeof(s_runtime_pin_token));
    s_runtime_pin_token_ready = true;
    s_runtime_pin_token_issued_at_ms = board_millis();
    return true;
}

static bool runtime_pin_token_is_usable(void) {
    if (!s_runtime_pin_token_ready) {
        return false;
    }
    if ((board_millis() - s_runtime_pin_token_issued_at_ms) > CLIENT_PIN_TOKEN_LIFETIME_MS) {
        clear_runtime_pin_token();
        return false;
    }
    return true;
}

static bool sha256_bytes(const uint8_t *data, size_t length, uint8_t output[32]) {
    return mbedtls_sha256(data, length, output, 0) == 0;
}

static bool hmac_sha256(const uint8_t *key,
                        size_t key_length,
                        const uint8_t *data,
                        size_t data_length,
                        uint8_t output[32]) {
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) {
        return false;
    }
    return mbedtls_md_hmac(info, key, key_length, data, data_length, output) == 0;
}

static bool aes256_cbc_crypt(bool encrypt,
                             const uint8_t key[32],
                             const uint8_t *input,
                             size_t input_length,
                             uint8_t *output) {
    mbedtls_aes_context aes;
    uint8_t iv[16] = {0};
    int result;

    if ((input_length % 16u) != 0u) {
        return false;
    }

    mbedtls_aes_init(&aes);
    result = encrypt
        ? mbedtls_aes_setkey_enc(&aes, key, 256)
        : mbedtls_aes_setkey_dec(&aes, key, 256);
    if (result == 0) {
        memcpy(output, input, input_length);
        result = mbedtls_aes_crypt_cbc(
            &aes,
            encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
            input_length,
            iv,
            output,
            output);
    }
    mbedtls_aes_free(&aes);
    return result == 0;
}

static bool load_key_agreement(void) {
    mbedtls_ecp_group group;
    mbedtls_ecp_point public_point;
    mbedtls_mpi private_key;
    size_t public_key_length = 0u;
    int result;

    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&public_point);
    mbedtls_mpi_init(&private_key);

    result = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
    if (result == 0) {
        result = mbedtls_ecp_gen_keypair(&group, &private_key, &public_point, client_pin_random, NULL);
    }
    if (result == 0) {
        result = mbedtls_mpi_write_binary(&private_key, s_key_agreement_private, sizeof(s_key_agreement_private));
    }
    if (result == 0) {
        result = mbedtls_ecp_point_write_binary(
            &group,
            &public_point,
            MBEDTLS_ECP_PF_UNCOMPRESSED,
            &public_key_length,
            s_key_agreement_public,
            sizeof(s_key_agreement_public));
    }

    mbedtls_mpi_free(&private_key);
    mbedtls_ecp_point_free(&public_point);
    mbedtls_ecp_group_free(&group);

    s_key_agreement_ready = result == 0 && public_key_length == sizeof(s_key_agreement_public);
    return s_key_agreement_ready;
}

static bool parse_key_agreement_map(meowkey_cbor_reader_t *reader, uint8_t public_key[65]) {
    size_t count;
    size_t index;
    meowkey_cbor_view_t x = {0};
    meowkey_cbor_view_t y = {0};

    if (!meowkey_cbor_read_map_start(reader, &count)) {
        return false;
    }

    for (index = 0; index < count; ++index) {
        int64_t key = 0;
        if (!meowkey_cbor_read_int(reader, &key)) {
            return false;
        }

        if (key == -2) {
            if (!meowkey_cbor_read_bytes(reader, &x)) {
                return false;
            }
        } else if (key == -3) {
            if (!meowkey_cbor_read_bytes(reader, &y)) {
                return false;
            }
        } else {
            if (!meowkey_cbor_skip(reader)) {
                return false;
            }
        }
    }

    if (x.length != 32u || y.length != 32u) {
        return false;
    }

    public_key[0] = 0x04u;
    memcpy(&public_key[1], x.data, 32u);
    memcpy(&public_key[33], y.data, 32u);
    return true;
}

static bool parse_client_pin_request(const uint8_t *request_data,
                                     size_t request_length,
                                     client_pin_request_t *request) {
    meowkey_cbor_reader_t reader;
    size_t count;
    size_t index;

    memset(request, 0, sizeof(*request));
    meowkey_cbor_reader_init(&reader, request_data, request_length);
    if (!meowkey_cbor_read_map_start(&reader, &count)) {
        return false;
    }

    for (index = 0; index < count; ++index) {
        int64_t key = 0;
        if (!meowkey_cbor_read_int(&reader, &key)) {
            return false;
        }

        switch (key) {
        case 1: {
            int64_t value = 0;
            if (!meowkey_cbor_read_int(&reader, &value)) {
                return false;
            }
            request->have_protocol = true;
            request->protocol = (uint8_t)value;
            break;
        }

        case 2: {
            int64_t value = 0;
            if (!meowkey_cbor_read_int(&reader, &value)) {
                return false;
            }
            request->have_subcommand = true;
            request->subcommand = (uint8_t)value;
            break;
        }

        case 3:
            request->have_key_agreement = parse_key_agreement_map(&reader, request->peer_public_key);
            if (!request->have_key_agreement) {
                return false;
            }
            break;

        case 4: {
            meowkey_cbor_view_t value;
            if (!meowkey_cbor_read_bytes(&reader, &value) || value.length > sizeof(request->pin_uv_auth_param)) {
                return false;
            }
            memcpy(request->pin_uv_auth_param, value.data, value.length);
            request->pin_uv_auth_param_length = value.length;
            request->have_pin_uv_auth_param = true;
            break;
        }

        case 5: {
            meowkey_cbor_view_t value;
            if (!meowkey_cbor_read_bytes(&reader, &value) || value.length > sizeof(request->new_pin_enc)) {
                return false;
            }
            memcpy(request->new_pin_enc, value.data, value.length);
            request->new_pin_enc_length = value.length;
            request->have_new_pin_enc = true;
            break;
        }

        case 6: {
            meowkey_cbor_view_t value;
            if (!meowkey_cbor_read_bytes(&reader, &value) || value.length > sizeof(request->pin_hash_enc)) {
                return false;
            }
            memcpy(request->pin_hash_enc, value.data, value.length);
            request->pin_hash_enc_length = value.length;
            request->have_pin_hash_enc = true;
            break;
        }

        case 9: {
            int64_t value = 0;
            if (!meowkey_cbor_read_int(&reader, &value)) {
                return false;
            }
            request->have_permissions = true;
            request->permissions = value;
            break;
        }

        default:
            if (!meowkey_cbor_skip(&reader)) {
                return false;
            }
            break;
        }
    }

    return request->have_protocol && request->have_subcommand;
}

static bool derive_shared_secret(const uint8_t peer_public_key[65], uint8_t output[32]) {
    mbedtls_ecp_group group;
    mbedtls_ecp_point peer_point;
    mbedtls_ecp_point shared_point;
    mbedtls_mpi private_key;
    uint8_t x_coordinate[32];
    int result;

    if (!s_key_agreement_ready) {
        return false;
    }

    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&peer_point);
    mbedtls_ecp_point_init(&shared_point);
    mbedtls_mpi_init(&private_key);

    result = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
    if (result == 0) {
        result = mbedtls_ecp_point_read_binary(&group, &peer_point, peer_public_key, 65u);
    }
    if (result == 0) {
        result = mbedtls_ecp_check_pubkey(&group, &peer_point);
    }
    if (result == 0) {
        result = mbedtls_mpi_read_binary(&private_key, s_key_agreement_private, sizeof(s_key_agreement_private));
    }
    if (result == 0) {
        result = mbedtls_ecp_mul(&group, &shared_point, &private_key, &peer_point, client_pin_random, NULL);
    }
    if (result == 0) {
        result = mbedtls_mpi_write_binary(&shared_point.MBEDTLS_PRIVATE(X), x_coordinate, sizeof(x_coordinate));
        if (result == 0) {
            result = sha256_bytes(x_coordinate, sizeof(x_coordinate), output) ? 0 : -1;
        }
    }

    secure_zero(x_coordinate, sizeof(x_coordinate));
    mbedtls_mpi_free(&private_key);
    mbedtls_ecp_point_free(&shared_point);
    mbedtls_ecp_point_free(&peer_point);
    mbedtls_ecp_group_free(&group);

    return result == 0;
}

static bool write_empty_map(uint8_t *response, size_t *response_length) {
    meowkey_cbor_writer_t writer;
    meowkey_cbor_writer_init(&writer, response, *response_length);
    meowkey_cbor_write_map_start(&writer, 0u);
    if (writer.failed) {
        return false;
    }
    *response_length = writer.length;
    return true;
}

static uint8_t build_key_agreement_response(uint8_t *response, size_t *response_length) {
    meowkey_cbor_writer_t writer;

    if (!s_key_agreement_ready && !load_key_agreement()) {
        return CTAP2_ERR_INVALID_CBOR;
    }

    meowkey_cbor_writer_init(&writer, response, *response_length);
    meowkey_cbor_write_map_start(&writer, 1u);
    meowkey_cbor_write_int(&writer, 1);
    meowkey_cbor_write_map_start(&writer, 5u);
    meowkey_cbor_write_int(&writer, 1);
    meowkey_cbor_write_int(&writer, 2);
    meowkey_cbor_write_int(&writer, 3);
    meowkey_cbor_write_int(&writer, -25);
    meowkey_cbor_write_int(&writer, -1);
    meowkey_cbor_write_int(&writer, 1);
    meowkey_cbor_write_int(&writer, -2);
    meowkey_cbor_write_bytes(&writer, &s_key_agreement_public[1], 32u);
    meowkey_cbor_write_int(&writer, -3);
    meowkey_cbor_write_bytes(&writer, &s_key_agreement_public[33], 32u);

    if (writer.failed) {
        return CTAP2_ERR_INVALID_CBOR;
    }
    *response_length = writer.length;
    return CTAP2_STATUS_OK;
}

static uint8_t build_retries_response(uint8_t retries, uint8_t *response, size_t *response_length) {
    meowkey_cbor_writer_t writer;
    meowkey_cbor_writer_init(&writer, response, *response_length);
    meowkey_cbor_write_map_start(&writer, 1u);
    meowkey_cbor_write_int(&writer, 3);
    meowkey_cbor_write_int(&writer, retries);
    if (writer.failed) {
        return CTAP2_ERR_INVALID_CBOR;
    }
    *response_length = writer.length;
    return CTAP2_STATUS_OK;
}

static uint8_t build_token_response(const uint8_t encrypted_token[CLIENT_PIN_TOKEN_SIZE],
                                    uint8_t *response,
                                    size_t *response_length) {
    meowkey_cbor_writer_t writer;
    meowkey_cbor_writer_init(&writer, response, *response_length);
    meowkey_cbor_write_map_start(&writer, 1u);
    meowkey_cbor_write_int(&writer, 2);
    meowkey_cbor_write_bytes(&writer, encrypted_token, CLIENT_PIN_TOKEN_SIZE);
    if (writer.failed) {
        return CTAP2_ERR_INVALID_CBOR;
    }
    *response_length = writer.length;
    return CTAP2_STATUS_OK;
}

static uint8_t validate_shared_secret_hmac(const uint8_t shared_secret[32],
                                           const uint8_t *message,
                                           size_t message_length,
                                           const uint8_t *provided_param,
                                           size_t provided_param_length) {
    uint8_t expected[32];
    uint8_t status;

    if (provided_param_length != 16u || !hmac_sha256(shared_secret, 32u, message, message_length, expected)) {
        return CTAP2_ERR_PIN_AUTH_INVALID;
    }
    status = constant_time_equal(expected, provided_param, 16u) ? CTAP2_STATUS_OK : CTAP2_ERR_PIN_AUTH_INVALID;
    secure_zero(expected, sizeof(expected));
    return status;
}

static uint8_t parse_new_pin_hash(const uint8_t shared_secret[32],
                                  const uint8_t *new_pin_enc,
                                  size_t new_pin_enc_length,
                                  uint8_t pin_hash[32],
                                  size_t *pin_length_out) {
    uint8_t decrypted_pin[80];
    size_t pin_length = 0u;

    if (!aes256_cbc_crypt(false, shared_secret, new_pin_enc, new_pin_enc_length, decrypted_pin)) {
        return CTAP2_ERR_INVALID_CBOR;
    }

    while (pin_length < new_pin_enc_length && decrypted_pin[pin_length] != 0u) {
        pin_length += 1u;
    }
    if (pin_length < 4u || pin_length > 63u) {
        secure_zero(decrypted_pin, sizeof(decrypted_pin));
        return CTAP2_ERR_PIN_POLICY_VIOLATION;
    }
    if (!sha256_bytes(decrypted_pin, pin_length, pin_hash)) {
        secure_zero(decrypted_pin, sizeof(decrypted_pin));
        return CTAP2_ERR_INVALID_CBOR;
    }

    secure_zero(decrypted_pin, sizeof(decrypted_pin));
    if (pin_length_out != NULL) {
        *pin_length_out = pin_length;
    }
    return CTAP2_STATUS_OK;
}

static uint8_t verify_pin_hash_or_consume_retry(meowkey_pin_state_t *pin_state, const uint8_t provided_pin_hash[32]) {
    if (constant_time_equal(provided_pin_hash, pin_state->pin_hash, CLIENT_PIN_HASH_SIZE)) {
        return CTAP2_STATUS_OK;
    }

    if (pin_state->retries > 0u) {
        pin_state->retries -= 1u;
        (void)meowkey_store_set_pin_state(pin_state);
    }
    meowkey_diag_logf("clientPIN pinHash mismatch retries=%u", pin_state->retries);
    return pin_state->retries == 0u ? CTAP2_ERR_PIN_BLOCKED : CTAP2_ERR_PIN_INVALID;
}

static uint8_t handle_set_pin(const client_pin_request_t *request,
                              uint8_t *response,
                              size_t *response_length) {
    meowkey_pin_state_t pin_state;
    uint8_t shared_secret[32];
    uint8_t pin_hash[32];
    size_t pin_length = 0u;
    uint8_t status;

    meowkey_store_get_pin_state(&pin_state);
    if (pin_state.configured) {
        return CTAP2_ERR_UNSUPPORTED_OPTION;
    }
    if (!request->have_key_agreement || !request->have_pin_uv_auth_param || !request->have_new_pin_enc) {
        return CTAP2_ERR_MISSING_PARAMETER;
    }
    if (!derive_shared_secret(request->peer_public_key, shared_secret)) {
        return CTAP2_ERR_INVALID_CBOR;
    }
    if (validate_shared_secret_hmac(shared_secret,
                                    request->new_pin_enc,
                                    request->new_pin_enc_length,
                                    request->pin_uv_auth_param,
                                    request->pin_uv_auth_param_length) != CTAP2_STATUS_OK) {
        return CTAP2_ERR_PIN_AUTH_INVALID;
    }
    status = parse_new_pin_hash(shared_secret, request->new_pin_enc, request->new_pin_enc_length, pin_hash, &pin_length);
    if (status != CTAP2_STATUS_OK) {
        secure_zero(shared_secret, sizeof(shared_secret));
        secure_zero(pin_hash, sizeof(pin_hash));
        return status;
    }

    memset(&pin_state, 0, sizeof(pin_state));
    pin_state.configured = true;
    pin_state.retries = CLIENT_PIN_RETRIES_DEFAULT;
    memcpy(pin_state.pin_hash, pin_hash, CLIENT_PIN_HASH_SIZE);
    if (!meowkey_store_set_pin_state(&pin_state)) {
        secure_zero(shared_secret, sizeof(shared_secret));
        secure_zero(pin_hash, sizeof(pin_hash));
        return CTAP2_ERR_INVALID_CBOR;
    }
    clear_runtime_pin_token();
    secure_zero(shared_secret, sizeof(shared_secret));
    secure_zero(pin_hash, sizeof(pin_hash));

    meowkey_diag_logf("clientPIN setPIN success pinLength=%lu", (unsigned long)pin_length);
    return write_empty_map(response, response_length) ? CTAP2_STATUS_OK : CTAP2_ERR_INVALID_CBOR;
}

static uint8_t handle_change_pin(const client_pin_request_t *request,
                                 uint8_t *response,
                                 size_t *response_length) {
    meowkey_pin_state_t pin_state;
    uint8_t shared_secret[32];
    uint8_t decrypted_pin_hash[32];
    uint8_t pin_hash[32];
    uint8_t auth_message[sizeof(request->new_pin_enc) + sizeof(request->pin_hash_enc)];
    size_t auth_message_length = request->new_pin_enc_length + request->pin_hash_enc_length;
    size_t pin_length = 0u;
    uint8_t status;

    meowkey_store_get_pin_state(&pin_state);
    if (!pin_state.configured) {
        return CTAP2_ERR_PIN_NOT_SET;
    }
    if (pin_state.retries == 0u) {
        return CTAP2_ERR_PIN_BLOCKED;
    }
    if (!request->have_key_agreement || !request->have_pin_uv_auth_param ||
        !request->have_new_pin_enc || !request->have_pin_hash_enc) {
        return CTAP2_ERR_MISSING_PARAMETER;
    }
    if (request->pin_hash_enc_length != 16u) {
        return CTAP2_ERR_INVALID_CBOR;
    }
    if (!derive_shared_secret(request->peer_public_key, shared_secret)) {
        return CTAP2_ERR_INVALID_CBOR;
    }

    memcpy(auth_message, request->new_pin_enc, request->new_pin_enc_length);
    memcpy(&auth_message[request->new_pin_enc_length], request->pin_hash_enc, request->pin_hash_enc_length);
    status = validate_shared_secret_hmac(shared_secret,
                                         auth_message,
                                         auth_message_length,
                                         request->pin_uv_auth_param,
                                         request->pin_uv_auth_param_length);
    if (status != CTAP2_STATUS_OK) {
        secure_zero(shared_secret, sizeof(shared_secret));
        return status;
    }
    if (!aes256_cbc_crypt(false,
                          shared_secret,
                          request->pin_hash_enc,
                          request->pin_hash_enc_length,
                          decrypted_pin_hash)) {
        secure_zero(shared_secret, sizeof(shared_secret));
        return CTAP2_ERR_INVALID_CBOR;
    }

    status = verify_pin_hash_or_consume_retry(&pin_state, decrypted_pin_hash);
    secure_zero(decrypted_pin_hash, sizeof(decrypted_pin_hash));
    if (status != CTAP2_STATUS_OK) {
        secure_zero(shared_secret, sizeof(shared_secret));
        return status;
    }

    status = parse_new_pin_hash(shared_secret, request->new_pin_enc, request->new_pin_enc_length, pin_hash, &pin_length);
    if (status != CTAP2_STATUS_OK) {
        secure_zero(shared_secret, sizeof(shared_secret));
        secure_zero(pin_hash, sizeof(pin_hash));
        return status;
    }

    pin_state.retries = CLIENT_PIN_RETRIES_DEFAULT;
    memcpy(pin_state.pin_hash, pin_hash, CLIENT_PIN_HASH_SIZE);
    if (!meowkey_store_set_pin_state(&pin_state)) {
        secure_zero(shared_secret, sizeof(shared_secret));
        secure_zero(pin_hash, sizeof(pin_hash));
        return CTAP2_ERR_INVALID_CBOR;
    }

    clear_runtime_pin_token();
    secure_zero(shared_secret, sizeof(shared_secret));
    secure_zero(pin_hash, sizeof(pin_hash));
    meowkey_diag_logf("clientPIN changePIN success pinLength=%lu", (unsigned long)pin_length);
    return write_empty_map(response, response_length) ? CTAP2_STATUS_OK : CTAP2_ERR_INVALID_CBOR;
}

static uint8_t handle_get_pin_token(const client_pin_request_t *request,
                                    uint8_t *response,
                                    size_t *response_length) {
    meowkey_pin_state_t pin_state;
    uint8_t shared_secret[32];
    uint8_t decrypted_pin_hash[32];
    uint8_t encrypted_token[CLIENT_PIN_TOKEN_SIZE];

    meowkey_store_get_pin_state(&pin_state);
    if (!pin_state.configured) {
        return CTAP2_ERR_PIN_NOT_SET;
    }
    if (pin_state.retries == 0u) {
        return CTAP2_ERR_PIN_BLOCKED;
    }
    if (!request->have_key_agreement || !request->have_pin_hash_enc) {
        return CTAP2_ERR_MISSING_PARAMETER;
    }
    if (request->pin_hash_enc_length != 16u) {
        return CTAP2_ERR_INVALID_CBOR;
    }
    if (!derive_shared_secret(request->peer_public_key, shared_secret)) {
        return CTAP2_ERR_INVALID_CBOR;
    }
    if (!aes256_cbc_crypt(false,
                          shared_secret,
                          request->pin_hash_enc,
                          request->pin_hash_enc_length,
                          decrypted_pin_hash)) {
        secure_zero(shared_secret, sizeof(shared_secret));
        return CTAP2_ERR_INVALID_CBOR;
    }

    {
        uint8_t status = verify_pin_hash_or_consume_retry(&pin_state, decrypted_pin_hash);
        if (status != CTAP2_STATUS_OK) {
            secure_zero(shared_secret, sizeof(shared_secret));
            secure_zero(decrypted_pin_hash, sizeof(decrypted_pin_hash));
            return status;
        }
    }

    pin_state.retries = CLIENT_PIN_RETRIES_DEFAULT;
    (void)meowkey_store_set_pin_state(&pin_state);
    if (!rotate_runtime_pin_token()) {
        secure_zero(shared_secret, sizeof(shared_secret));
        secure_zero(decrypted_pin_hash, sizeof(decrypted_pin_hash));
        return CTAP2_ERR_INVALID_CBOR;
    }

    if (!aes256_cbc_crypt(true,
                          shared_secret,
                          s_runtime_pin_token,
                          sizeof(s_runtime_pin_token),
                          encrypted_token)) {
        secure_zero(shared_secret, sizeof(shared_secret));
        secure_zero(decrypted_pin_hash, sizeof(decrypted_pin_hash));
        return CTAP2_ERR_INVALID_CBOR;
    }
    secure_zero(shared_secret, sizeof(shared_secret));
    secure_zero(decrypted_pin_hash, sizeof(decrypted_pin_hash));

    meowkey_diag_logf("clientPIN getPinToken success");
    return build_token_response(encrypted_token, response, response_length);
}

bool meowkey_client_pin_is_configured(void) {
    meowkey_pin_state_t state;
    meowkey_store_get_pin_state(&state);
    return state.configured;
}

uint8_t meowkey_client_pin_handle(const uint8_t *request,
                                  size_t request_length,
                                  uint8_t *response,
                                  size_t *response_length) {
    client_pin_request_t parsed;
    meowkey_pin_state_t pin_state;

    meowkey_store_init();
    meowkey_store_get_pin_state(&pin_state);

    if (!parse_client_pin_request(request, request_length, &parsed)) {
        return CTAP2_ERR_INVALID_CBOR;
    }
    if (parsed.protocol != MEOWKEY_PIN_UV_AUTH_PROTOCOL_1) {
        return CTAP2_ERR_UNSUPPORTED_OPTION;
    }

    meowkey_diag_logf("clientPIN subcmd=%u configured=%u", parsed.subcommand, pin_state.configured ? 1u : 0u);

    switch (parsed.subcommand) {
    case CLIENT_PIN_SUBCMD_GET_RETRIES:
        return build_retries_response(pin_state.retries, response, response_length);

    case CLIENT_PIN_SUBCMD_GET_KEY_AGREEMENT:
        return build_key_agreement_response(response, response_length);

    case CLIENT_PIN_SUBCMD_SET_PIN:
        return handle_set_pin(&parsed, response, response_length);

    case CLIENT_PIN_SUBCMD_CHANGE_PIN:
        return handle_change_pin(&parsed, response, response_length);

    case CLIENT_PIN_SUBCMD_GET_PIN_TOKEN:
        return handle_get_pin_token(&parsed, response, response_length);

    case CLIENT_PIN_SUBCMD_GET_PIN_TOKEN_WITH_PERMISSIONS:
        if (!parsed.have_permissions) {
            return CTAP2_ERR_MISSING_PARAMETER;
        }
        meowkey_diag_logf("clientPIN permissions token request unsupported");
        return CTAP2_ERR_UNSUPPORTED_OPTION;

    default:
        return CTAP2_ERR_UNSUPPORTED_OPTION;
    }
}

uint8_t meowkey_client_pin_verify_auth(const uint8_t client_data_hash[32],
                                       uint8_t protocol,
                                       const uint8_t *pin_uv_auth_param,
                                       size_t pin_uv_auth_param_length) {
    meowkey_pin_state_t state;
    uint8_t expected[32];

    meowkey_store_get_pin_state(&state);
    if (!state.configured) {
        if (pin_uv_auth_param != NULL) {
            meowkey_diag_logf("pinUvAuth present but PIN not set");
            return CTAP2_ERR_PIN_NOT_SET;
        }
        return CTAP2_STATUS_OK;
    }
    if (protocol != MEOWKEY_PIN_UV_AUTH_PROTOCOL_1 || pin_uv_auth_param == NULL) {
        meowkey_diag_logf("pinUvAuth missing or protocol unsupported");
        return CTAP2_ERR_PIN_REQUIRED;
    }
    if (!runtime_pin_token_is_usable()) {
        meowkey_diag_logf("pinUvAuth token expired");
        return CTAP2_ERR_PIN_TOKEN_EXPIRED;
    }
    if (!hmac_sha256(s_runtime_pin_token, sizeof(s_runtime_pin_token), client_data_hash, 32u, expected)) {
        return CTAP2_ERR_INVALID_CBOR;
    }
    if (pin_uv_auth_param_length != 16u || !constant_time_equal(expected, pin_uv_auth_param, 16u)) {
        meowkey_diag_logf("pinUvAuth invalid");
        return CTAP2_ERR_PIN_AUTH_INVALID;
    }
    return CTAP2_STATUS_OK;
}

bool meowkey_client_pin_get_shared_secret(const uint8_t peer_public_key[65],
                                          uint8_t output[MEOWKEY_SHARED_SECRET_SIZE]) {
    return derive_shared_secret(peer_public_key, output);
}

bool meowkey_client_pin_encrypt_with_shared_secret(const uint8_t shared_secret[MEOWKEY_SHARED_SECRET_SIZE],
                                                   const uint8_t *input,
                                                   size_t input_length,
                                                   uint8_t *output) {
    return aes256_cbc_crypt(true, shared_secret, input, input_length, output);
}

bool meowkey_client_pin_decrypt_with_shared_secret(const uint8_t shared_secret[MEOWKEY_SHARED_SECRET_SIZE],
                                                   const uint8_t *input,
                                                   size_t input_length,
                                                   uint8_t *output) {
    return aes256_cbc_crypt(false, shared_secret, input, input_length, output);
}

uint8_t meowkey_client_pin_verify_shared_secret_auth(const uint8_t shared_secret[MEOWKEY_SHARED_SECRET_SIZE],
                                                     const uint8_t *message,
                                                     size_t message_length,
                                                     const uint8_t *provided_param,
                                                     size_t provided_param_length) {
    return validate_shared_secret_hmac(shared_secret,
                                       message,
                                       message_length,
                                       provided_param,
                                       provided_param_length);
}
