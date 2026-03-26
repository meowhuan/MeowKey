#include "webauthn.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bsp/board_api.h"
#include "cbor.h"
#include "client_pin.h"
#include "credential_store.h"
#include "diagnostics.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/md.h"
#include "mbedtls/private_access.h"
#include "mbedtls/sha256.h"
#include "pico/rand.h"
#include "user_presence.h"

enum {
    CTAP2_STATUS_OK = 0x00,
    CTAP1_ERR_INVALID_PARAMETER = 0x02,
    CTAP2_ERR_INVALID_CBOR = 0x12,
    CTAP2_ERR_MISSING_PARAMETER = 0x14,
    CTAP2_ERR_CREDENTIAL_EXCLUDED = 0x19,
    CTAP2_ERR_INVALID_CREDENTIAL = 0x22,
    CTAP2_ERR_UNSUPPORTED_ALGORITHM = 0x26,
    CTAP2_ERR_KEY_STORE_FULL = 0x28,
    CTAP2_ERR_UNSUPPORTED_OPTION = 0x2b,
    CTAP2_ERR_NO_CREDENTIALS = 0x2e,
    CTAP2_ERR_USER_ACTION_TIMEOUT = 0x2f,
    CTAP2_ERR_NOT_ALLOWED = 0x30,
    CTAP2_ERR_PIN_NOT_SET = 0x35,
};

enum {
    CTAP2_COSE_KTY_EC2 = 2,
    CTAP2_COSE_ALG_ES256 = -7,
    CTAP2_COSE_CRV_P256 = 1,
    MEOWKEY_PENDING_ASSERTION_MAX_CREDENTIALS = 128,
    MEOWKEY_PENDING_ASSERTION_TIMEOUT_MS = 30000u,
    MEOWKEY_ASSERTION_UP_REUSE_WINDOW_MS = 1200u,
};

typedef struct {
    uint8_t client_data_hash[32];
    char rp_id[MEOWKEY_RP_ID_SIZE];
    uint8_t user_id[MEOWKEY_USER_ID_SIZE];
    char user_name[MEOWKEY_USER_NAME_SIZE];
    char display_name[MEOWKEY_DISPLAY_NAME_SIZE];
    size_t rp_id_length;
    size_t user_id_length;
    size_t user_name_length;
    size_t display_name_length;
    bool require_resident_key;
    bool require_user_verification;
    bool supports_es256;
    bool credential_excluded;
    bool hmac_secret_requested;
    bool user_verified;
    bool have_pin_uv_auth_param;
    uint8_t pin_uv_auth_param[32];
    size_t pin_uv_auth_param_length;
    uint8_t pin_uv_auth_protocol;
} make_credential_request_t;

typedef struct {
    uint8_t client_data_hash[32];
    char rp_id[MEOWKEY_RP_ID_SIZE];
    uint8_t allow_credential_id[MEOWKEY_CREDENTIAL_ID_SIZE];
    size_t rp_id_length;
    size_t allow_credential_id_length;
    bool has_allow_credential;
    bool require_user_verification;
    bool hmac_secret_requested;
    bool hmac_secret_have_protocol;
    bool hmac_secret_have_key_agreement;
    bool hmac_secret_have_salt_enc;
    bool hmac_secret_have_salt_auth;
    bool user_verified;
    bool have_pin_uv_auth_param;
    uint8_t pin_uv_auth_param[32];
    size_t pin_uv_auth_param_length;
    uint8_t pin_uv_auth_protocol;
    uint8_t hmac_secret_protocol;
    uint8_t hmac_secret_key_agreement[65];
    uint8_t hmac_secret_salt_enc[64];
    size_t hmac_secret_salt_enc_length;
    uint8_t hmac_secret_salt_auth[32];
    size_t hmac_secret_salt_auth_length;
} get_assertion_request_t;

typedef struct {
    bool active;
    get_assertion_request_t request;
    uint32_t slot_indices[MEOWKEY_PENDING_ASSERTION_MAX_CREDENTIALS];
    uint32_t credential_count;
    uint32_t next_index;
    uint32_t last_used_ms;
} pending_assertion_state_t;

typedef struct {
    bool active;
    uint8_t request_match_key[32];
    uint32_t granted_ms;
} recent_assertion_up_state_t;

static const uint8_t k_aaguid[16] = {
    0x4d, 0x65, 0x6f, 0x77, 0x4b, 0x65, 0x79, 0x00,
    0x52, 0x50, 0x32, 0x33, 0x35, 0x30, 0x00, 0x01,
};

static pending_assertion_state_t s_pending_assertion;
static recent_assertion_up_state_t s_recent_assertion_up;

static bool sha256_bytes(const uint8_t *data, size_t length, uint8_t output[32]);

static int webauthn_random(void *context, unsigned char *output, size_t output_length) {
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

static void clear_pending_assertion(void) {
    memset(&s_pending_assertion, 0, sizeof(s_pending_assertion));
}

static void clear_recent_assertion_up(void) {
    memset(&s_recent_assertion_up, 0, sizeof(s_recent_assertion_up));
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

static bool recent_assertion_up_is_current(void) {
    if (!s_recent_assertion_up.active) {
        return false;
    }
    if ((board_millis() - s_recent_assertion_up.granted_ms) > MEOWKEY_ASSERTION_UP_REUSE_WINDOW_MS) {
        clear_recent_assertion_up();
        return false;
    }
    return true;
}

// Match retries on the parsed request so semantically identical CBOR retries can reuse one UP.
static bool get_assertion_retry_match_key(const get_assertion_request_t *request, uint8_t output[32]) {
    return sha256_bytes((const uint8_t *)request, sizeof(*request), output);
}

static bool consume_recent_assertion_up_if_matching(const uint8_t request_match_key[32]) {
    uint32_t age_ms;

    if (!s_recent_assertion_up.active) {
        meowkey_diag_logf("getAssertion recent UP unavailable reason=inactive");
        return false;
    }
    age_ms = board_millis() - s_recent_assertion_up.granted_ms;
    if (!recent_assertion_up_is_current()) {
        meowkey_diag_logf("getAssertion recent UP unavailable reason=expired ageMs=%u windowMs=%u",
                          (unsigned int)age_ms,
                          (unsigned int)MEOWKEY_ASSERTION_UP_REUSE_WINDOW_MS);
        return false;
    }
    if (memcmp(s_recent_assertion_up.request_match_key,
               request_match_key,
               sizeof(s_recent_assertion_up.request_match_key)) != 0) {
        char current_prefix[17];
        char requested_prefix[17];

        (void)hex_prefix(s_recent_assertion_up.request_match_key,
                         sizeof(s_recent_assertion_up.request_match_key),
                         current_prefix,
                         sizeof(current_prefix));
        (void)hex_prefix(request_match_key, 32u, requested_prefix, sizeof(requested_prefix));
        meowkey_diag_logf("getAssertion recent UP unavailable reason=retryKeyMismatch ageMs=%u current=%s requested=%s",
                          (unsigned int)age_ms,
                          current_prefix,
                          requested_prefix);
        return false;
    }

    clear_recent_assertion_up();
    meowkey_diag_logf("getAssertion reused recent UP ageMs=%u", (unsigned int)age_ms);
    return true;
}

static void arm_recent_assertion_up(const uint8_t request_match_key[32]) {
    char key_prefix[17];

    s_recent_assertion_up.active = true;
    memcpy(s_recent_assertion_up.request_match_key,
           request_match_key,
           sizeof(s_recent_assertion_up.request_match_key));
    s_recent_assertion_up.granted_ms = board_millis();
    (void)hex_prefix(request_match_key, 32u, key_prefix, sizeof(key_prefix));
    meowkey_diag_logf("getAssertion armed one-shot UP reuse windowMs=%u key=%s",
                      (unsigned int)MEOWKEY_ASSERTION_UP_REUSE_WINDOW_MS,
                      key_prefix);
}

static bool pending_assertion_is_current(void) {
    if (!s_pending_assertion.active) {
        return false;
    }
    if ((board_millis() - s_pending_assertion.last_used_ms) > MEOWKEY_PENDING_ASSERTION_TIMEOUT_MS) {
        clear_pending_assertion();
        return false;
    }
    return true;
}

static bool copy_view_to_string(meowkey_cbor_view_t view, char *output, size_t output_capacity, size_t *output_length) {
    if (view.length >= output_capacity) {
        return false;
    }
    memcpy(output, view.data, view.length);
    output[view.length] = '\0';
    *output_length = view.length;
    return true;
}

static bool copy_view_to_bytes(meowkey_cbor_view_t view, uint8_t *output, size_t output_capacity, size_t *output_length) {
    if (view.length > output_capacity) {
        return false;
    }
    memcpy(output, view.data, view.length);
    *output_length = view.length;
    return true;
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

static bool parse_hmac_secret_key_agreement(meowkey_cbor_reader_t *reader, uint8_t public_key[65]) {
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

static bool parse_make_credential_extensions(meowkey_cbor_reader_t *reader, make_credential_request_t *request) {
    size_t count;
    size_t index;

    if (!meowkey_cbor_read_map_start(reader, &count)) {
        return false;
    }

    for (index = 0; index < count; ++index) {
        meowkey_cbor_view_t key;
        if (!meowkey_cbor_read_text(reader, &key)) {
            return false;
        }
        if (key.length == 11u && memcmp(key.data, "hmac-secret", 11u) == 0) {
            bool enabled = false;
            if (!meowkey_cbor_read_bool(reader, &enabled)) {
                return false;
            }
            request->hmac_secret_requested = enabled;
        } else {
            if (!meowkey_cbor_skip(reader)) {
                return false;
            }
        }
    }

    return true;
}

static bool parse_get_assertion_hmac_secret(meowkey_cbor_reader_t *reader, get_assertion_request_t *request) {
    size_t count;
    size_t index;

    if (!meowkey_cbor_read_map_start(reader, &count)) {
        return false;
    }

    request->hmac_secret_requested = true;
    for (index = 0; index < count; ++index) {
        int64_t key = 0;
        if (!meowkey_cbor_read_int(reader, &key)) {
            return false;
        }

        switch (key) {
        case 1: {
            int64_t value = 0;
            if (!meowkey_cbor_read_int(reader, &value)) {
                return false;
            }
            request->hmac_secret_protocol = (uint8_t)value;
            request->hmac_secret_have_protocol = true;
            break;
        }

        case 2:
            if (!parse_hmac_secret_key_agreement(reader, request->hmac_secret_key_agreement)) {
                return false;
            }
            request->hmac_secret_have_key_agreement = true;
            break;

        case 3: {
            meowkey_cbor_view_t value;
            if (!meowkey_cbor_read_bytes(reader, &value) ||
                !copy_view_to_bytes(value,
                                    request->hmac_secret_salt_enc,
                                    sizeof(request->hmac_secret_salt_enc),
                                    &request->hmac_secret_salt_enc_length)) {
                return false;
            }
            request->hmac_secret_have_salt_enc = true;
            break;
        }

        case 4: {
            meowkey_cbor_view_t value;
            if (!meowkey_cbor_read_bytes(reader, &value) ||
                !copy_view_to_bytes(value,
                                    request->hmac_secret_salt_auth,
                                    sizeof(request->hmac_secret_salt_auth),
                                    &request->hmac_secret_salt_auth_length)) {
                return false;
            }
            request->hmac_secret_have_salt_auth = true;
            break;
        }

        default:
            if (!meowkey_cbor_skip(reader)) {
                return false;
            }
            break;
        }
    }

    return true;
}

static bool parse_get_assertion_extensions(meowkey_cbor_reader_t *reader, get_assertion_request_t *request) {
    size_t count;
    size_t index;

    if (!meowkey_cbor_read_map_start(reader, &count)) {
        return false;
    }

    for (index = 0; index < count; ++index) {
        meowkey_cbor_view_t key;
        if (!meowkey_cbor_read_text(reader, &key)) {
            return false;
        }
        if (key.length == 11u && memcmp(key.data, "hmac-secret", 11u) == 0) {
            if (!parse_get_assertion_hmac_secret(reader, request)) {
                return false;
            }
        } else {
            if (!meowkey_cbor_skip(reader)) {
                return false;
            }
        }
    }

    return true;
}

static bool parse_rp_entity(meowkey_cbor_reader_t *reader, make_credential_request_t *request) {
    size_t count;
    size_t index;

    if (!meowkey_cbor_read_map_start(reader, &count)) {
        return false;
    }

    for (index = 0; index < count; ++index) {
        meowkey_cbor_view_t key;
        if (!meowkey_cbor_read_text(reader, &key)) {
            return false;
        }
        if (key.length == 2u && memcmp(key.data, "id", 2u) == 0) {
            meowkey_cbor_view_t value;
            if (!meowkey_cbor_read_text(reader, &value) ||
                !copy_view_to_string(value, request->rp_id, sizeof(request->rp_id), &request->rp_id_length)) {
                return false;
            }
        } else {
            if (!meowkey_cbor_skip(reader)) {
                return false;
            }
        }
    }

    return request->rp_id_length > 0u;
}

static bool parse_user_entity(meowkey_cbor_reader_t *reader, make_credential_request_t *request) {
    size_t count;
    size_t index;

    if (!meowkey_cbor_read_map_start(reader, &count)) {
        return false;
    }

    for (index = 0; index < count; ++index) {
        meowkey_cbor_view_t key;
        if (!meowkey_cbor_read_text(reader, &key)) {
            return false;
        }

        if (key.length == 2u && memcmp(key.data, "id", 2u) == 0) {
            meowkey_cbor_view_t value;
            if (!meowkey_cbor_read_bytes(reader, &value) ||
                !copy_view_to_bytes(value, request->user_id, sizeof(request->user_id), &request->user_id_length)) {
                return false;
            }
        } else if (key.length == 4u && memcmp(key.data, "name", 4u) == 0) {
            meowkey_cbor_view_t value;
            if (!meowkey_cbor_read_text(reader, &value) ||
                !copy_view_to_string(value, request->user_name, sizeof(request->user_name), &request->user_name_length)) {
                return false;
            }
        } else if (key.length == 11u && memcmp(key.data, "displayName", 11u) == 0) {
            meowkey_cbor_view_t value;
            if (!meowkey_cbor_read_text(reader, &value) ||
                !copy_view_to_string(value, request->display_name, sizeof(request->display_name), &request->display_name_length)) {
                return false;
            }
        } else {
            if (!meowkey_cbor_skip(reader)) {
                return false;
            }
        }
    }

    return request->user_id_length > 0u;
}

static bool parse_pub_key_cred_params(meowkey_cbor_reader_t *reader, make_credential_request_t *request) {
    size_t count;
    size_t index;

    if (!meowkey_cbor_read_array_start(reader, &count)) {
        return false;
    }

    for (index = 0; index < count; ++index) {
        size_t pair_count;
        size_t pair_index;
        int64_t alg = 0;
        bool has_alg = false;
        bool is_public_key = false;

        if (!meowkey_cbor_read_map_start(reader, &pair_count)) {
            return false;
        }

        for (pair_index = 0; pair_index < pair_count; ++pair_index) {
            meowkey_cbor_view_t key;
            if (!meowkey_cbor_read_text(reader, &key)) {
                return false;
            }

            if (key.length == 3u && memcmp(key.data, "alg", 3u) == 0) {
                if (!meowkey_cbor_read_int(reader, &alg)) {
                    return false;
                }
                has_alg = true;
            } else if (key.length == 4u && memcmp(key.data, "type", 4u) == 0) {
                meowkey_cbor_view_t value;
                if (!meowkey_cbor_read_text(reader, &value)) {
                    return false;
                }
                is_public_key = value.length == 10u && memcmp(value.data, "public-key", 10u) == 0;
            } else {
                if (!meowkey_cbor_skip(reader)) {
                    return false;
                }
            }
        }

        if (has_alg && is_public_key && alg == CTAP2_COSE_ALG_ES256) {
            request->supports_es256 = true;
        }
    }

    return true;
}

static bool parse_options_map(meowkey_cbor_reader_t *reader, bool *rk, bool *uv) {
    size_t count;
    size_t index;

    if (!meowkey_cbor_read_map_start(reader, &count)) {
        return false;
    }

    for (index = 0; index < count; ++index) {
        meowkey_cbor_view_t key;
        if (!meowkey_cbor_read_text(reader, &key)) {
            return false;
        }
        if (key.length == 2u && memcmp(key.data, "rk", 2u) == 0) {
            if (!meowkey_cbor_read_bool(reader, rk)) {
                return false;
            }
        } else if (key.length == 2u && memcmp(key.data, "uv", 2u) == 0) {
            if (!meowkey_cbor_read_bool(reader, uv)) {
                return false;
            }
        } else {
            if (!meowkey_cbor_skip(reader)) {
                return false;
            }
        }
    }

    return true;
}

static bool parse_exclude_list(meowkey_cbor_reader_t *reader, make_credential_request_t *request) {
    size_t count;
    size_t index;

    if (!meowkey_cbor_read_array_start(reader, &count)) {
        return false;
    }

    for (index = 0; index < count; ++index) {
        size_t pair_count;
        size_t pair_index;

        if (!meowkey_cbor_read_map_start(reader, &pair_count)) {
            return false;
        }

        for (pair_index = 0; pair_index < pair_count; ++pair_index) {
            meowkey_cbor_view_t key;
            if (!meowkey_cbor_read_text(reader, &key)) {
                return false;
            }

            if (key.length == 2u && memcmp(key.data, "id", 2u) == 0) {
                meowkey_cbor_view_t value;
                if (!meowkey_cbor_read_bytes(reader, &value)) {
                    return false;
                }
                if (meowkey_store_find_by_credential_id(value.data, value.length, NULL, NULL)) {
                    request->credential_excluded = true;
                }
            } else {
                if (!meowkey_cbor_skip(reader)) {
                    return false;
                }
            }
        }
    }

    return true;
}

static uint8_t parse_make_credential_request(const uint8_t *request_data,
                                             size_t request_length,
                                             make_credential_request_t *request) {
    meowkey_cbor_reader_t reader;
    size_t count;
    size_t index;
    bool pin_configured = false;
    bool have_client_data_hash = false;
    bool have_rp = false;
    bool have_user = false;

    memset(request, 0, sizeof(*request));
    pin_configured = meowkey_client_pin_is_configured();
    meowkey_cbor_reader_init(&reader, request_data, request_length);
    if (!meowkey_cbor_read_map_start(&reader, &count)) {
        return CTAP2_ERR_INVALID_CBOR;
    }

    for (index = 0; index < count; ++index) {
        int64_t key = 0;
        if (!meowkey_cbor_read_int(&reader, &key)) {
            return CTAP2_ERR_INVALID_CBOR;
        }

        switch (key) {
        case 1: {
            meowkey_cbor_view_t value;
            if (!meowkey_cbor_read_bytes(&reader, &value) || value.length != sizeof(request->client_data_hash)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            memcpy(request->client_data_hash, value.data, sizeof(request->client_data_hash));
            have_client_data_hash = true;
            break;
        }

        case 2:
            if (!parse_rp_entity(&reader, request)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            have_rp = true;
            break;

        case 3:
            if (!parse_user_entity(&reader, request)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            have_user = true;
            break;

        case 4:
            if (!parse_pub_key_cred_params(&reader, request)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            break;

        case 5:
            if (!parse_exclude_list(&reader, request)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            break;

        case 6:
            if (!parse_make_credential_extensions(&reader, request)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            break;

        case 7:
            if (!parse_options_map(&reader, &request->require_resident_key, &request->require_user_verification)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            break;

        case 8: {
            meowkey_cbor_view_t value;
            if (!meowkey_cbor_read_bytes(&reader, &value) || value.length > sizeof(request->pin_uv_auth_param)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            memcpy(request->pin_uv_auth_param, value.data, value.length);
            request->pin_uv_auth_param_length = value.length;
            request->have_pin_uv_auth_param = true;
            break;
        }

        case 9: {
            int64_t value = 0;
            if (!meowkey_cbor_read_int(&reader, &value)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            request->pin_uv_auth_protocol = (uint8_t)value;
            break;
        }

        default:
            if (!meowkey_cbor_skip(&reader)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            break;
        }
    }

    if (!have_client_data_hash || !have_rp || !have_user) {
        return CTAP2_ERR_MISSING_PARAMETER;
    }
    if (!request->supports_es256) {
        return CTAP2_ERR_UNSUPPORTED_ALGORITHM;
    }
    meowkey_diag_logf("makeCredential opts rk=%u uv=%u pinParam=%u proto=%u pinCfg=%u",
                      request->require_resident_key ? 1u : 0u,
                      request->require_user_verification ? 1u : 0u,
                      request->have_pin_uv_auth_param ? 1u : 0u,
                      request->pin_uv_auth_protocol,
                      pin_configured ? 1u : 0u);
    if (request->require_user_verification) {
        uint8_t pin_status;

        if (!pin_configured) {
            return CTAP2_ERR_PIN_NOT_SET;
        }
        pin_status = meowkey_client_pin_verify_auth(
            request->client_data_hash,
            request->pin_uv_auth_protocol,
            request->have_pin_uv_auth_param ? request->pin_uv_auth_param : NULL,
            request->pin_uv_auth_param_length);
        if (pin_status != CTAP2_STATUS_OK) {
            return pin_status;
        }
    } else if (request->have_pin_uv_auth_param) {
        uint8_t pin_status = meowkey_client_pin_verify_auth(
            request->client_data_hash,
            request->pin_uv_auth_protocol,
            request->pin_uv_auth_param,
            request->pin_uv_auth_param_length);
        if (pin_status != CTAP2_STATUS_OK) {
            return pin_status;
        }
    }
    request->user_verified = pin_configured && request->have_pin_uv_auth_param;
    if (request->credential_excluded) {
        return CTAP2_ERR_CREDENTIAL_EXCLUDED;
    }

    return CTAP2_STATUS_OK;
}

static bool parse_allow_list(meowkey_cbor_reader_t *reader, get_assertion_request_t *request) {
    size_t count;
    size_t index;

    if (!meowkey_cbor_read_array_start(reader, &count)) {
        return false;
    }

    for (index = 0; index < count; ++index) {
        size_t pair_count;
        size_t pair_index;

        if (!meowkey_cbor_read_map_start(reader, &pair_count)) {
            return false;
        }

        for (pair_index = 0; pair_index < pair_count; ++pair_index) {
            meowkey_cbor_view_t key;
            if (!meowkey_cbor_read_text(reader, &key)) {
                return false;
            }
            if (key.length == 2u && memcmp(key.data, "id", 2u) == 0) {
                meowkey_cbor_view_t value;
                if (!meowkey_cbor_read_bytes(reader, &value) ||
                    !copy_view_to_bytes(value,
                                        request->allow_credential_id,
                                        sizeof(request->allow_credential_id),
                                        &request->allow_credential_id_length)) {
                    return false;
                }
                request->has_allow_credential = true;
            } else {
                if (!meowkey_cbor_skip(reader)) {
                    return false;
                }
            }
        }

        if (request->has_allow_credential) {
            return true;
        }
    }

    return true;
}

static uint8_t parse_get_assertion_request(const uint8_t *request_data,
                                           size_t request_length,
                                           get_assertion_request_t *request) {
    meowkey_cbor_reader_t reader;
    size_t count;
    size_t index;
    bool pin_configured = false;
    bool have_client_data_hash = false;
    bool have_rp_id = false;
    bool ignored_rk = false;

    memset(request, 0, sizeof(*request));
    pin_configured = meowkey_client_pin_is_configured();
    meowkey_cbor_reader_init(&reader, request_data, request_length);
    if (!meowkey_cbor_read_map_start(&reader, &count)) {
        return CTAP2_ERR_INVALID_CBOR;
    }

    for (index = 0; index < count; ++index) {
        int64_t key = 0;
        if (!meowkey_cbor_read_int(&reader, &key)) {
            return CTAP2_ERR_INVALID_CBOR;
        }

        switch (key) {
        case 1: {
            meowkey_cbor_view_t value;
            if (!meowkey_cbor_read_text(&reader, &value) ||
                !copy_view_to_string(value, request->rp_id, sizeof(request->rp_id), &request->rp_id_length)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            have_rp_id = true;
            break;
        }

        case 2: {
            meowkey_cbor_view_t value;
            if (!meowkey_cbor_read_bytes(&reader, &value) || value.length != sizeof(request->client_data_hash)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            memcpy(request->client_data_hash, value.data, sizeof(request->client_data_hash));
            have_client_data_hash = true;
            break;
        }

        case 3:
            if (!parse_allow_list(&reader, request)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            break;

        case 4:
            if (!parse_get_assertion_extensions(&reader, request)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            break;

        case 5:
            if (!parse_options_map(&reader, &ignored_rk, &request->require_user_verification)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            break;

        case 6: {
            meowkey_cbor_view_t value;
            if (!meowkey_cbor_read_bytes(&reader, &value) || value.length > sizeof(request->pin_uv_auth_param)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            memcpy(request->pin_uv_auth_param, value.data, value.length);
            request->pin_uv_auth_param_length = value.length;
            request->have_pin_uv_auth_param = true;
            break;
        }

        case 7: {
            int64_t value = 0;
            if (!meowkey_cbor_read_int(&reader, &value)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            request->pin_uv_auth_protocol = (uint8_t)value;
            break;
        }

        default:
            if (!meowkey_cbor_skip(&reader)) {
                return CTAP2_ERR_INVALID_CBOR;
            }
            break;
        }
    }

    if (!have_rp_id || !have_client_data_hash) {
        return CTAP2_ERR_MISSING_PARAMETER;
    }
    meowkey_diag_logf("getAssertion opts uv=%u pinParam=%u proto=%u pinCfg=%u hmac=%u",
                      request->require_user_verification ? 1u : 0u,
                      request->have_pin_uv_auth_param ? 1u : 0u,
                      request->pin_uv_auth_protocol,
                      pin_configured ? 1u : 0u,
                      request->hmac_secret_requested ? 1u : 0u);
    if (request->require_user_verification && !pin_configured) {
        return CTAP2_ERR_PIN_NOT_SET;
    }
    if (request->hmac_secret_requested &&
        (!request->hmac_secret_have_key_agreement ||
         !request->hmac_secret_have_salt_enc ||
         !request->hmac_secret_have_salt_auth)) {
        return CTAP2_ERR_MISSING_PARAMETER;
    }
    if (request->require_user_verification) {
        uint8_t pin_status = meowkey_client_pin_verify_auth(
            request->client_data_hash,
            request->pin_uv_auth_protocol,
            request->have_pin_uv_auth_param ? request->pin_uv_auth_param : NULL,
            request->pin_uv_auth_param_length);
        if (pin_status != CTAP2_STATUS_OK) {
            return pin_status;
        }
    } else if (request->have_pin_uv_auth_param) {
        uint8_t pin_status = meowkey_client_pin_verify_auth(
            request->client_data_hash,
            request->pin_uv_auth_protocol,
            request->pin_uv_auth_param,
            request->pin_uv_auth_param_length);
        if (pin_status != CTAP2_STATUS_OK) {
            return pin_status;
        }
    }
    request->user_verified = pin_configured && request->have_pin_uv_auth_param;

    return CTAP2_STATUS_OK;
}

static bool sha256_bytes(const uint8_t *data, size_t length, uint8_t output[32]) {
    return mbedtls_sha256(data, length, output, 0) == 0;
}

static bool build_cose_public_key(const uint8_t public_key[65], uint8_t *output, size_t *output_length) {
    meowkey_cbor_writer_t writer;
    meowkey_cbor_writer_init(&writer, output, *output_length);

    meowkey_cbor_write_map_start(&writer, 5u);
    meowkey_cbor_write_int(&writer, 1);
    meowkey_cbor_write_int(&writer, CTAP2_COSE_KTY_EC2);
    meowkey_cbor_write_int(&writer, 3);
    meowkey_cbor_write_int(&writer, CTAP2_COSE_ALG_ES256);
    meowkey_cbor_write_int(&writer, -1);
    meowkey_cbor_write_int(&writer, CTAP2_COSE_CRV_P256);
    meowkey_cbor_write_int(&writer, -2);
    meowkey_cbor_write_bytes(&writer, &public_key[1], 32u);
    meowkey_cbor_write_int(&writer, -3);
    meowkey_cbor_write_bytes(&writer, &public_key[33], 32u);

    if (writer.failed) {
        return false;
    }

    *output_length = writer.length;
    return true;
}

static bool build_hmac_secret_create_extensions(const make_credential_request_t *request,
                                                uint8_t *output,
                                                size_t *output_length) {
    meowkey_cbor_writer_t writer;

    if (!request->hmac_secret_requested) {
        *output_length = 0u;
        return true;
    }

    meowkey_cbor_writer_init(&writer, output, *output_length);
    meowkey_cbor_write_map_start(&writer, 1u);
    meowkey_cbor_write_text(&writer, "hmac-secret", 11u);
    meowkey_cbor_write_bool(&writer, true);
    if (writer.failed) {
        return false;
    }

    *output_length = writer.length;
    return true;
}

static bool build_attested_auth_data(const meowkey_credential_record_t *record,
                                     const uint8_t public_key[65],
                                     const uint8_t *extensions,
                                     size_t extensions_length,
                                     bool user_verified,
                                     uint8_t *output,
                                     size_t *output_length) {
    uint8_t rp_id_hash[32];
    uint8_t cose_key[96];
    size_t cose_key_length = sizeof(cose_key);
    uint8_t flags = 0x41u;
    size_t offset = 0u;
    bool ok = false;

    if (!sha256_bytes((const uint8_t *)record->rp_id, record->rp_id_length, rp_id_hash) ||
        !build_cose_public_key(public_key, cose_key, &cose_key_length) ||
        *output_length < (32u + 1u + 4u + 16u + 2u + record->credential_id_length + cose_key_length + extensions_length)) {
        goto cleanup;
    }

    if (user_verified) {
        flags |= 0x04u;
    }
    if (extensions_length > 0u) {
        flags |= 0x80u;
    }

    memcpy(&output[offset], rp_id_hash, sizeof(rp_id_hash));
    offset += sizeof(rp_id_hash);
    output[offset++] = flags;
    output[offset++] = 0x00u;
    output[offset++] = 0x00u;
    output[offset++] = 0x00u;
    output[offset++] = 0x00u;
    memcpy(&output[offset], k_aaguid, sizeof(k_aaguid));
    offset += sizeof(k_aaguid);
    output[offset++] = (uint8_t)(record->credential_id_length >> 8u);
    output[offset++] = (uint8_t)record->credential_id_length;
    memcpy(&output[offset], record->credential_id, record->credential_id_length);
    offset += record->credential_id_length;
    memcpy(&output[offset], cose_key, cose_key_length);
    offset += cose_key_length;
    if (extensions_length > 0u) {
        memcpy(&output[offset], extensions, extensions_length);
        offset += extensions_length;
    }

    *output_length = offset;
    ok = true;

cleanup:
    secure_zero(rp_id_hash, sizeof(rp_id_hash));
    secure_zero(cose_key, sizeof(cose_key));
    return ok;
}

static uint8_t generate_credential(meowkey_credential_record_t *record, uint8_t public_key[65]) {
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
        result = mbedtls_ecp_gen_keypair(&group, &private_key, &public_point, webauthn_random, NULL);
    }
    if (result == 0) {
        result = mbedtls_mpi_write_binary(&private_key, record->private_key, MEOWKEY_PRIVATE_KEY_SIZE);
    }
    if (result == 0) {
        result = mbedtls_ecp_point_write_binary(
            &group,
            &public_point,
            MBEDTLS_ECP_PF_UNCOMPRESSED,
            &public_key_length,
            public_key,
            65u);
    }
    if (result == 0) {
        webauthn_random(NULL, record->credential_id, MEOWKEY_CREDENTIAL_ID_SIZE);
        webauthn_random(NULL, record->cred_random_with_uv, MEOWKEY_CRED_RANDOM_SIZE);
        webauthn_random(NULL, record->cred_random_without_uv, MEOWKEY_CRED_RANDOM_SIZE);
        record->private_key_length = MEOWKEY_PRIVATE_KEY_SIZE;
        record->credential_id_length = MEOWKEY_CREDENTIAL_ID_SIZE;
        record->sign_count = 0u;
        record->discoverable = true;
        record->cred_random_ready = true;
    }

    mbedtls_mpi_free(&private_key);
    mbedtls_ecp_point_free(&public_point);
    mbedtls_ecp_group_free(&group);
    return result == 0 && public_key_length == 65u ? CTAP2_STATUS_OK : CTAP2_ERR_INVALID_CBOR;
}

static uint8_t build_make_credential_response(const meowkey_credential_record_t *record,
                                              const make_credential_request_t *request,
                                              const uint8_t public_key[65],
                                              uint8_t *response,
                                              size_t *response_length) {
    uint8_t auth_data[256] = {0};
    uint8_t extensions[32] = {0};
    size_t extensions_length = sizeof(extensions);
    size_t auth_data_length = sizeof(auth_data);
    meowkey_cbor_writer_t writer;
    uint8_t status = CTAP2_ERR_INVALID_CBOR;

    if (!build_hmac_secret_create_extensions(request, extensions, &extensions_length) ||
        !build_attested_auth_data(record,
                                  public_key,
                                  extensions,
                                  extensions_length,
                                  request->user_verified,
                                  auth_data,
                                  &auth_data_length)) {
        goto cleanup;
    }

    meowkey_cbor_writer_init(&writer, response, *response_length);
    meowkey_cbor_write_map_start(&writer, 3u);
    meowkey_cbor_write_int(&writer, 1);
    meowkey_cbor_write_text(&writer, "none", 4u);
    meowkey_cbor_write_int(&writer, 2);
    meowkey_cbor_write_bytes(&writer, auth_data, auth_data_length);
    meowkey_cbor_write_int(&writer, 3);
    meowkey_cbor_write_map_start(&writer, 0u);

    if (writer.failed) {
        goto cleanup;
    }

    *response_length = writer.length;
    status = CTAP2_STATUS_OK;

cleanup:
    secure_zero(auth_data, sizeof(auth_data));
    secure_zero(extensions, sizeof(extensions));
    return status;
}

static uint8_t build_hmac_secret_assertion_extensions(const meowkey_credential_record_t *record,
                                                      const get_assertion_request_t *request,
                                                      uint8_t *output,
                                                      size_t *output_length) {
    const uint8_t *cred_random = NULL;
    uint8_t shared_secret[MEOWKEY_SHARED_SECRET_SIZE] = {0};
    uint8_t decrypted_salts[64] = {0};
    uint8_t secrets[64] = {0};
    size_t secret_length = 0u;
    meowkey_cbor_writer_t writer;
    uint8_t protocol;
    uint8_t status = CTAP2_STATUS_OK;

    if (!request->hmac_secret_requested || !record->cred_random_ready) {
        *output_length = 0u;
        return CTAP2_STATUS_OK;
    }

    protocol = request->hmac_secret_have_protocol ? request->hmac_secret_protocol : MEOWKEY_PIN_UV_AUTH_PROTOCOL_1;
    if (protocol != MEOWKEY_PIN_UV_AUTH_PROTOCOL_1) {
        status = CTAP1_ERR_INVALID_PARAMETER;
        goto cleanup;
    }
    if ((request->hmac_secret_salt_enc_length != 32u && request->hmac_secret_salt_enc_length != 64u) ||
        (request->hmac_secret_salt_enc_length % 16u) != 0u) {
        status = CTAP1_ERR_INVALID_PARAMETER;
        goto cleanup;
    }
    if (!meowkey_client_pin_get_shared_secret(request->hmac_secret_key_agreement, shared_secret)) {
        status = CTAP1_ERR_INVALID_PARAMETER;
        goto cleanup;
    }
    {
        uint8_t auth_status = meowkey_client_pin_verify_shared_secret_auth(shared_secret,
                                                                           request->hmac_secret_salt_enc,
                                                                           request->hmac_secret_salt_enc_length,
                                                                           request->hmac_secret_salt_auth,
                                                                           request->hmac_secret_salt_auth_length);
        if (auth_status != CTAP2_STATUS_OK) {
            status = auth_status;
            goto cleanup;
        }
    }
    if (!meowkey_client_pin_decrypt_with_shared_secret(shared_secret,
                                                       request->hmac_secret_salt_enc,
                                                       request->hmac_secret_salt_enc_length,
                                                       decrypted_salts)) {
        status = CTAP1_ERR_INVALID_PARAMETER;
        goto cleanup;
    }

    cred_random = request->user_verified ? record->cred_random_with_uv : record->cred_random_without_uv;
    if (!hmac_sha256(cred_random, MEOWKEY_CRED_RANDOM_SIZE, decrypted_salts, 32u, secrets)) {
        status = CTAP2_ERR_INVALID_CBOR;
        goto cleanup;
    }
    secret_length = 32u;
    if (request->hmac_secret_salt_enc_length == 64u) {
        if (!hmac_sha256(cred_random,
                         MEOWKEY_CRED_RANDOM_SIZE,
                         &decrypted_salts[32],
                         32u,
                         &secrets[32])) {
            status = CTAP2_ERR_INVALID_CBOR;
            goto cleanup;
        }
        secret_length = 64u;
    }
    if (!meowkey_client_pin_encrypt_with_shared_secret(shared_secret, secrets, secret_length, secrets)) {
        status = CTAP1_ERR_INVALID_PARAMETER;
        goto cleanup;
    }

    meowkey_cbor_writer_init(&writer, output, *output_length);
    meowkey_cbor_write_map_start(&writer, 1u);
    meowkey_cbor_write_text(&writer, "hmac-secret", 11u);
    meowkey_cbor_write_bytes(&writer, secrets, secret_length);
    if (writer.failed) {
        status = CTAP2_ERR_INVALID_CBOR;
        goto cleanup;
    }

    *output_length = writer.length;
    status = CTAP2_STATUS_OK;

cleanup:
    secure_zero(shared_secret, sizeof(shared_secret));
    secure_zero(decrypted_salts, sizeof(decrypted_salts));
    secure_zero(secrets, sizeof(secrets));
    return status;
}

static bool build_assertion_auth_data(const char *rp_id,
                                      size_t rp_id_length,
                                      uint32_t sign_count,
                                      const uint8_t *extensions,
                                      size_t extensions_length,
                                      bool user_verified,
                                      uint8_t *output,
                                      size_t *output_length) {
    uint8_t rp_id_hash[32];
    uint8_t flags = 0x01u;

    if (*output_length < (37u + extensions_length) ||
        !sha256_bytes((const uint8_t *)rp_id, rp_id_length, rp_id_hash)) {
        return false;
    }

    if (user_verified) {
        flags |= 0x04u;
    }
    if (extensions_length > 0u) {
        flags |= 0x80u;
    }

    memcpy(output, rp_id_hash, sizeof(rp_id_hash));
    output[32] = flags;
    output[33] = (uint8_t)(sign_count >> 24u);
    output[34] = (uint8_t)(sign_count >> 16u);
    output[35] = (uint8_t)(sign_count >> 8u);
    output[36] = (uint8_t)sign_count;
    if (extensions_length > 0u) {
        memcpy(&output[37], extensions, extensions_length);
    }
    *output_length = 37u + extensions_length;
    return true;
}

static uint8_t sign_assertion(const meowkey_credential_record_t *record,
                              const uint8_t *auth_data,
                              size_t auth_data_length,
                              const uint8_t client_data_hash[32],
                              uint8_t *signature,
                              size_t *signature_length) {
    uint8_t message[192] = {0};
    uint8_t message_hash[32] = {0};
    mbedtls_ecdsa_context context;
    int result;
    uint8_t status = CTAP2_ERR_INVALID_CREDENTIAL;

    mbedtls_ecdsa_init(&context);
    if ((auth_data_length + 32u) > sizeof(message)) {
        status = CTAP2_ERR_INVALID_CBOR;
        goto cleanup;
    }
    memcpy(message, auth_data, auth_data_length);
    memcpy(&message[auth_data_length], client_data_hash, 32u);
    if (!sha256_bytes(message, auth_data_length + 32u, message_hash)) {
        status = CTAP2_ERR_INVALID_CBOR;
        goto cleanup;
    }

    result = mbedtls_ecp_group_load(&context.MBEDTLS_PRIVATE(grp), MBEDTLS_ECP_DP_SECP256R1);
    if (result == 0) {
        result = mbedtls_mpi_read_binary(
            &context.MBEDTLS_PRIVATE(d),
            record->private_key,
            record->private_key_length);
    }
    if (result == 0) {
        result = mbedtls_ecp_mul(
            &context.MBEDTLS_PRIVATE(grp),
            &context.MBEDTLS_PRIVATE(Q),
            &context.MBEDTLS_PRIVATE(d),
            &context.MBEDTLS_PRIVATE(grp).G,
            webauthn_random,
            NULL);
    }
    if (result == 0) {
        result = mbedtls_ecdsa_write_signature(
            &context,
            MBEDTLS_MD_SHA256,
            message_hash,
            sizeof(message_hash),
            signature,
            *signature_length,
            signature_length,
            webauthn_random,
            NULL);
    }

    status = result == 0 ? CTAP2_STATUS_OK : CTAP2_ERR_INVALID_CREDENTIAL;
cleanup:
    mbedtls_ecdsa_free(&context);
    secure_zero(message, sizeof(message));
    secure_zero(message_hash, sizeof(message_hash));
    return status;
}

static uint8_t build_get_assertion_response(const meowkey_credential_record_t *record,
                                            uint8_t *auth_data,
                                            size_t auth_data_length,
                                            const uint8_t *signature,
                                            size_t signature_length,
                                            bool include_number_of_credentials,
                                            uint32_t number_of_credentials,
                                            uint8_t *response,
                                            size_t *response_length) {
    meowkey_cbor_writer_t writer;
    size_t map_count = record->user_id_length > 0u ? 4u : 3u;
    if (include_number_of_credentials) {
        map_count += 1u;
    }

    meowkey_cbor_writer_init(&writer, response, *response_length);
    meowkey_cbor_write_map_start(&writer, map_count);
    meowkey_cbor_write_int(&writer, 1);
    meowkey_cbor_write_map_start(&writer, 2u);
    meowkey_cbor_write_text(&writer, "id", 2u);
    meowkey_cbor_write_bytes(&writer, record->credential_id, record->credential_id_length);
    meowkey_cbor_write_text(&writer, "type", 4u);
    meowkey_cbor_write_text(&writer, "public-key", 10u);
    meowkey_cbor_write_int(&writer, 2);
    meowkey_cbor_write_bytes(&writer, auth_data, auth_data_length);
    meowkey_cbor_write_int(&writer, 3);
    meowkey_cbor_write_bytes(&writer, signature, signature_length);

    if (record->user_id_length > 0u) {
        size_t user_map_count = 1u;
        meowkey_cbor_write_int(&writer, 4);
        if (record->user_name_length > 0u) {
            user_map_count += 1u;
        }
        if (record->display_name_length > 0u) {
            user_map_count += 1u;
        }
        meowkey_cbor_write_map_start(&writer, user_map_count);
        meowkey_cbor_write_text(&writer, "id", 2u);
        meowkey_cbor_write_bytes(&writer, record->user_id, record->user_id_length);
        if (record->user_name_length > 0u) {
            meowkey_cbor_write_text(&writer, "name", 4u);
            meowkey_cbor_write_text(&writer, record->user_name, record->user_name_length);
        }
        if (record->display_name_length > 0u) {
            meowkey_cbor_write_text(&writer, "displayName", 11u);
            meowkey_cbor_write_text(&writer, record->display_name, record->display_name_length);
        }
    }

    if (include_number_of_credentials) {
        meowkey_cbor_write_int(&writer, 5);
        meowkey_cbor_write_int(&writer, number_of_credentials);
    }

    if (writer.failed) {
        return CTAP2_ERR_INVALID_CBOR;
    }

    *response_length = writer.length;
    return CTAP2_STATUS_OK;
}

static uint32_t collect_matching_slots(const get_assertion_request_t *request, uint32_t *slot_indices, uint32_t slot_capacity) {
    uint32_t count = 0u;
    uint32_t capacity = meowkey_store_get_credential_capacity();
    uint32_t slot_index;

    for (slot_index = 0u; slot_index < capacity && count < slot_capacity; ++slot_index) {
        meowkey_credential_record_t record;
        if (!meowkey_store_get_credential_by_slot(slot_index, &record)) {
            continue;
        }
        if (!record.discoverable) {
            continue;
        }
        if (record.rp_id_length != request->rp_id_length ||
            memcmp(record.rp_id, request->rp_id, request->rp_id_length) != 0) {
            continue;
        }
        slot_indices[count++] = slot_index;
    }

    return count;
}

static uint8_t respond_with_assertion(const get_assertion_request_t *request,
                                      uint32_t slot_index,
                                      bool include_number_of_credentials,
                                      uint32_t number_of_credentials,
                                      uint8_t *response,
                                      size_t *response_length) {
    meowkey_credential_record_t record;
    uint8_t auth_data[160] = {0};
    uint8_t extensions[128] = {0};
    size_t extensions_length = sizeof(extensions);
    size_t auth_data_length = sizeof(auth_data);
    uint8_t signature[MBEDTLS_ECDSA_MAX_LEN] = {0};
    size_t signature_length = sizeof(signature);
    uint8_t status = CTAP2_ERR_INVALID_CREDENTIAL;

    memset(&record, 0, sizeof(record));

    if (!meowkey_store_get_credential_by_slot(slot_index, &record)) {
        goto cleanup;
    }

    record.sign_count += 1u;
    status = build_hmac_secret_assertion_extensions(&record, request, extensions, &extensions_length);
    if (status != CTAP2_STATUS_OK) {
        meowkey_diag_logf("getAssertion hmac-secret failed status=0x%02x", status);
        goto cleanup;
    }
    if (!build_assertion_auth_data(record.rp_id,
                                   record.rp_id_length,
                                   record.sign_count,
                                   extensions,
                                   extensions_length,
                                   request->user_verified,
                                   auth_data,
                                   &auth_data_length)) {
        meowkey_diag_logf("getAssertion authData build failed");
        status = CTAP2_ERR_INVALID_CBOR;
        goto cleanup;
    }

    status = sign_assertion(&record, auth_data, auth_data_length, request->client_data_hash, signature, &signature_length);
    if (status != CTAP2_STATUS_OK) {
        meowkey_diag_logf("getAssertion signing failed status=0x%02x", status);
        goto cleanup;
    }

    if (!meowkey_store_update_sign_count(slot_index, record.sign_count)) {
        meowkey_diag_logf("getAssertion signCount update failed");
        status = CTAP2_ERR_INVALID_CREDENTIAL;
        goto cleanup;
    }

    meowkey_diag_logf("getAssertion success rp=%s signCount=%lu slot=%lu",
                      record.rp_id,
                      (unsigned long)record.sign_count,
                      (unsigned long)slot_index);

    status = build_get_assertion_response(&record,
                                          auth_data,
                                          auth_data_length,
                                          signature,
                                          signature_length,
                                          include_number_of_credentials,
                                          number_of_credentials,
                                          response,
                                          response_length);

cleanup:
    secure_zero(&record, sizeof(record));
    secure_zero(auth_data, sizeof(auth_data));
    secure_zero(extensions, sizeof(extensions));
    secure_zero(signature, sizeof(signature));
    return status;
}

uint8_t meowkey_webauthn_make_credential(const uint8_t *request,
                                         size_t request_length,
                                         uint8_t *response,
                                         size_t *response_length) {
    make_credential_request_t parsed;
    meowkey_credential_record_t record;
    uint8_t public_key[65] = {0};
    uint8_t status = CTAP2_ERR_INVALID_CBOR;

    memset(&parsed, 0, sizeof(parsed));
    memset(&record, 0, sizeof(record));

    meowkey_store_init();
    status = parse_make_credential_request(request, request_length, &parsed);
    if (status != CTAP2_STATUS_OK) {
        meowkey_diag_logf("makeCredential rejected status=0x%02x", status);
        goto cleanup;
    }
    clear_pending_assertion();
    clear_recent_assertion_up();
    status = meowkey_user_presence_wait_for_confirmation("makeCredential");
    if (status != CTAP2_STATUS_OK) {
        meowkey_diag_logf("makeCredential userPresence status=0x%02x", status);
        goto cleanup;
    }

    memcpy(record.rp_id, parsed.rp_id, parsed.rp_id_length + 1u);
    memcpy(record.user_id, parsed.user_id, parsed.user_id_length);
    memcpy(record.user_name, parsed.user_name, parsed.user_name_length + (parsed.user_name_length > 0u ? 1u : 0u));
    memcpy(record.display_name, parsed.display_name, parsed.display_name_length + (parsed.display_name_length > 0u ? 1u : 0u));
    record.rp_id_length = parsed.rp_id_length;
    record.user_id_length = parsed.user_id_length;
    record.user_name_length = parsed.user_name_length;
    record.display_name_length = parsed.display_name_length;

    status = generate_credential(&record, public_key);
    if (status != CTAP2_STATUS_OK) {
        meowkey_diag_logf("makeCredential key generation failed status=0x%02x", status);
        goto cleanup;
    }

    if (!meowkey_store_add_credential(&record, NULL)) {
        meowkey_diag_logf("makeCredential store full");
        status = CTAP2_ERR_KEY_STORE_FULL;
        goto cleanup;
    }

    meowkey_diag_logf(
        "makeCredential created rp=%s user=%s credentialLen=%lu",
        record.rp_id,
        record.user_name_length > 0u ? record.user_name : "(anonymous)",
        (unsigned long)record.credential_id_length);

    status = build_make_credential_response(&record, &parsed, public_key, response, response_length);

cleanup:
    secure_zero(&parsed, sizeof(parsed));
    secure_zero(&record, sizeof(record));
    secure_zero(public_key, sizeof(public_key));
    return status;
}

uint8_t meowkey_webauthn_get_assertion(const uint8_t *request,
                                       size_t request_length,
                                       uint8_t *response,
                                       size_t *response_length) {
    get_assertion_request_t parsed;
    meowkey_credential_record_t record;
    uint32_t slot_index = 0u;
    uint32_t matching_slots[MEOWKEY_PENDING_ASSERTION_MAX_CREDENTIALS];
    uint32_t matching_count = 0u;
    uint8_t request_match_key[32];
    bool reused_recent_up = false;
    uint8_t status;

    meowkey_store_init();
    clear_pending_assertion();
    status = parse_get_assertion_request(request, request_length, &parsed);
    if (status != CTAP2_STATUS_OK) {
        meowkey_diag_logf("getAssertion rejected status=0x%02x", status);
        return status;
    }
    if (!get_assertion_retry_match_key(&parsed, request_match_key)) {
        meowkey_diag_logf("getAssertion retry key failed");
        return CTAP2_ERR_INVALID_CBOR;
    }

    if (parsed.has_allow_credential) {
        if (!meowkey_store_find_by_credential_id(
                parsed.allow_credential_id,
                parsed.allow_credential_id_length,
                &record,
                &slot_index)) {
            meowkey_diag_logf("getAssertion allowList miss");
            return CTAP2_ERR_INVALID_CREDENTIAL;
        }
        if (record.rp_id_length != parsed.rp_id_length ||
            memcmp(record.rp_id, parsed.rp_id, parsed.rp_id_length) != 0) {
            meowkey_diag_logf("getAssertion rpId mismatch");
            return CTAP2_ERR_INVALID_CREDENTIAL;
        }
        reused_recent_up = consume_recent_assertion_up_if_matching(request_match_key);
        if (!reused_recent_up) {
            status = meowkey_user_presence_wait_for_confirmation("getAssertion");
            if (status != CTAP2_STATUS_OK) {
                meowkey_diag_logf("getAssertion userPresence status=0x%02x", status);
                return status;
            }
        }
        status = respond_with_assertion(&parsed, slot_index, false, 0u, response, response_length);
        if (status == CTAP2_STATUS_OK && !reused_recent_up) {
            arm_recent_assertion_up(request_match_key);
        }
        return status;
    }

    matching_count = collect_matching_slots(&parsed, matching_slots, MEOWKEY_PENDING_ASSERTION_MAX_CREDENTIALS);
    if (matching_count == 0u) {
        meowkey_diag_logf("getAssertion no credential for rp=%s", parsed.rp_id);
        return CTAP2_ERR_NO_CREDENTIALS;
    }

    reused_recent_up = consume_recent_assertion_up_if_matching(request_match_key);
    if (!reused_recent_up) {
        status = meowkey_user_presence_wait_for_confirmation("getAssertion");
        if (status != CTAP2_STATUS_OK) {
            meowkey_diag_logf("getAssertion userPresence status=0x%02x", status);
            return status;
        }
    }
    status = respond_with_assertion(&parsed,
                                    matching_slots[0],
                                    matching_count > 1u,
                                    matching_count,
                                    response,
                                    response_length);
    if (status != CTAP2_STATUS_OK) {
        clear_pending_assertion();
        return status;
    }
    if (!reused_recent_up) {
        arm_recent_assertion_up(request_match_key);
    }

    if (matching_count > 1u) {
        s_pending_assertion.active = true;
        s_pending_assertion.request = parsed;
        memcpy(s_pending_assertion.slot_indices, matching_slots, matching_count * sizeof(matching_slots[0]));
        s_pending_assertion.credential_count = matching_count;
        s_pending_assertion.next_index = 1u;
        s_pending_assertion.last_used_ms = board_millis();
    }
    return status;
}

uint8_t meowkey_webauthn_get_next_assertion(const uint8_t *request,
                                            size_t request_length,
                                            uint8_t *response,
                                            size_t *response_length) {
    uint8_t status;

    (void)request;
    if (request_length != 0u) {
        return CTAP2_ERR_INVALID_CBOR;
    }
    if (!pending_assertion_is_current() || s_pending_assertion.next_index >= s_pending_assertion.credential_count) {
        clear_pending_assertion();
        return CTAP2_ERR_NOT_ALLOWED;
    }

    status = respond_with_assertion(&s_pending_assertion.request,
                                    s_pending_assertion.slot_indices[s_pending_assertion.next_index],
                                    false,
                                    0u,
                                    response,
                                    response_length);
    if (status != CTAP2_STATUS_OK) {
        clear_pending_assertion();
        return status;
    }

    s_pending_assertion.next_index += 1u;
    s_pending_assertion.last_used_ms = board_millis();
    if (s_pending_assertion.next_index >= s_pending_assertion.credential_count) {
        clear_pending_assertion();
    }
    return CTAP2_STATUS_OK;
}
