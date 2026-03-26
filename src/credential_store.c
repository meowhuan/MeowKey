#include "credential_store.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "meowkey_build_config.h"

enum {
    MEOWKEY_STORE_MAGIC = 0x4d4b5331u,
    MEOWKEY_STORE_VERSION = 6u,
    MEOWKEY_STORE_TOTAL_SIZE = FLASH_SECTOR_SIZE * MEOWKEY_CREDENTIAL_STORE_SECTORS,
    MEOWKEY_STORE_SIGN_COUNT_JOURNAL_SECTORS = 2u,
    MEOWKEY_STORE_SIGN_COUNT_JOURNAL_SIZE = FLASH_SECTOR_SIZE * MEOWKEY_STORE_SIGN_COUNT_JOURNAL_SECTORS,
    MEOWKEY_STORE_DATA_SECTORS = MEOWKEY_CREDENTIAL_STORE_SECTORS - MEOWKEY_STORE_SIGN_COUNT_JOURNAL_SECTORS,
    MEOWKEY_STORE_SLOT_COUNT = 2u,
    MEOWKEY_STORE_SLOT_SECTORS = MEOWKEY_STORE_DATA_SECTORS / MEOWKEY_STORE_SLOT_COUNT,
    MEOWKEY_STORE_SLOT_SIZE = FLASH_SECTOR_SIZE * MEOWKEY_STORE_SLOT_SECTORS,
    MEOWKEY_STORE_DATA_SIZE = MEOWKEY_STORE_SLOT_SIZE * MEOWKEY_STORE_SLOT_COUNT,
    MEOWKEY_STORE_OFFSET = PICO_FLASH_SIZE_BYTES - MEOWKEY_STORE_TOTAL_SIZE,
    MEOWKEY_STORE_SLOT0_OFFSET = MEOWKEY_STORE_OFFSET,
    MEOWKEY_STORE_SLOT1_OFFSET = MEOWKEY_STORE_SLOT0_OFFSET + MEOWKEY_STORE_SLOT_SIZE,
    MEOWKEY_STORE_SIGN_COUNT_JOURNAL_OFFSET = MEOWKEY_STORE_OFFSET + MEOWKEY_STORE_DATA_SIZE,
    MEOWKEY_STORE_HEADER_SIZE = 64u,
    MEOWKEY_STORE_SLOT_PAYLOAD_SIZE = MEOWKEY_STORE_SLOT_SIZE - MEOWKEY_STORE_HEADER_SIZE,
    MEOWKEY_STORE_LEGACY_MAX_CREDENTIALS = 8u,
    MEOWKEY_STORE_LEGACY_SIZE = FLASH_SECTOR_SIZE,
    MEOWKEY_STORE_LEGACY_OFFSET = PICO_FLASH_SIZE_BYTES - MEOWKEY_STORE_LEGACY_SIZE,
    MEOWKEY_STORE_OLD_4MB_LEGACY_OFFSET = (4u * 1024u * 1024u) - FLASH_SECTOR_SIZE,
    MEOWKEY_STORE_V3_MEDIUM_MAX_CREDENTIALS = 32u,
    MEOWKEY_STORE_V3_MEDIUM_SIZE = FLASH_SECTOR_SIZE * 4u,
    MEOWKEY_STORE_V3_MEDIUM_OFFSET = PICO_FLASH_SIZE_BYTES - MEOWKEY_STORE_V3_MEDIUM_SIZE,
    MEOWKEY_STORE_OLD_4MB_V3_MEDIUM_OFFSET = (4u * 1024u * 1024u) - MEOWKEY_STORE_V3_MEDIUM_SIZE,
    MEOWKEY_STORE_V4_V5_LEGACY_SIZE = MEOWKEY_STORE_DATA_SIZE,
    MEOWKEY_STORE_OLD_4MB_V4_OFFSET = (4u * 1024u * 1024u) - MEOWKEY_STORE_V4_V5_LEGACY_SIZE,
    MEOWKEY_PIN_RETRIES_DEFAULT = 8u,
    MEOWKEY_SIGN_COUNT_JOURNAL_MAGIC = 0x4d4b5343u,
    MEOWKEY_SIGN_COUNT_JOURNAL_VERSION = 2u,
    MEOWKEY_SIGN_COUNT_LEGACY_JOURNAL_VERSION = 1u,
    MEOWKEY_SIGN_COUNT_ENTRY_MAGIC = 0x53434e54u,
};

typedef struct {
    uint8_t in_use;
    uint8_t discoverable;
    uint8_t cred_random_ready;
    uint8_t credential_id_length;
    uint8_t private_key_length;
    uint8_t rp_id_length;
    uint8_t user_id_length;
    uint8_t user_name_length;
    uint8_t display_name_length;
    uint8_t reserved[3];
    uint32_t sign_count;
    uint8_t credential_id[MEOWKEY_CREDENTIAL_ID_SIZE];
    uint8_t private_key[MEOWKEY_PRIVATE_KEY_SIZE];
    uint8_t cred_random_with_uv[MEOWKEY_CRED_RANDOM_SIZE];
    uint8_t cred_random_without_uv[MEOWKEY_CRED_RANDOM_SIZE];
    char rp_id[MEOWKEY_RP_ID_SIZE];
    uint8_t user_id[MEOWKEY_USER_ID_SIZE];
    char user_name[MEOWKEY_USER_NAME_SIZE];
    char display_name[MEOWKEY_DISPLAY_NAME_SIZE];
} meowkey_store_slot_t;

typedef struct {
    uint8_t in_use;
    uint8_t discoverable;
    uint8_t credential_id_length;
    uint8_t private_key_length;
    uint8_t rp_id_length;
    uint8_t user_id_length;
    uint8_t user_name_length;
    uint8_t display_name_length;
    uint32_t sign_count;
    uint8_t credential_id[MEOWKEY_CREDENTIAL_ID_SIZE];
    uint8_t private_key[MEOWKEY_PRIVATE_KEY_SIZE];
    char rp_id[MEOWKEY_RP_ID_SIZE];
    uint8_t user_id[MEOWKEY_USER_ID_SIZE];
    char user_name[MEOWKEY_USER_NAME_SIZE];
    char display_name[MEOWKEY_DISPLAY_NAME_SIZE];
} meowkey_store_slot_v2_t;

typedef struct {
    uint32_t credential_count;
    uint8_t pin_configured;
    uint8_t pin_retries;
    uint8_t reserved0[2];
    uint8_t pin_hash[16];
    meowkey_user_presence_config_t user_presence;
} meowkey_store_payload_fixed_t;

enum {
    MEOWKEY_STORE_MAX_CREDENTIALS_BY_REGION =
        (MEOWKEY_STORE_SLOT_PAYLOAD_SIZE - sizeof(meowkey_store_payload_fixed_t)) / sizeof(meowkey_store_slot_t),
    MEOWKEY_STORE_V4_V5_LEGACY_MAX_CREDENTIALS =
        (MEOWKEY_STORE_V4_V5_LEGACY_SIZE - 64u) / sizeof(meowkey_store_slot_t),
#if MEOWKEY_CREDENTIAL_CAPACITY_LIMIT > 0
    MEOWKEY_STORE_MAX_CREDENTIALS =
        (MEOWKEY_CREDENTIAL_CAPACITY_LIMIT < MEOWKEY_STORE_MAX_CREDENTIALS_BY_REGION)
            ? MEOWKEY_CREDENTIAL_CAPACITY_LIMIT
            : MEOWKEY_STORE_MAX_CREDENTIALS_BY_REGION,
#else
    MEOWKEY_STORE_MAX_CREDENTIALS = MEOWKEY_STORE_MAX_CREDENTIALS_BY_REGION,
#endif
};

typedef struct {
    meowkey_store_payload_fixed_t fixed;
    meowkey_store_slot_t slots[MEOWKEY_STORE_MAX_CREDENTIALS];
} meowkey_store_payload_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t generation;
    uint32_t payload_size;
    uint32_t payload_crc32;
    uint32_t header_crc32;
    uint8_t reserved[MEOWKEY_STORE_HEADER_SIZE - 24u];
} meowkey_store_header_t;

typedef union {
    meowkey_store_payload_t data;
    uint8_t raw[MEOWKEY_STORE_SLOT_PAYLOAD_SIZE];
} meowkey_store_payload_image_t;

typedef union {
    struct {
        meowkey_store_header_t header;
        uint8_t payload[MEOWKEY_STORE_SLOT_PAYLOAD_SIZE];
    } parts;
    uint8_t raw[MEOWKEY_STORE_SLOT_SIZE];
} meowkey_store_slot_image_t;

typedef union {
    struct {
        uint32_t magic;
        uint32_t version;
        uint32_t credential_count;
        uint32_t reserved;
        meowkey_store_slot_v2_t slots[MEOWKEY_STORE_LEGACY_MAX_CREDENTIALS];
    } data;
    uint8_t raw[MEOWKEY_STORE_LEGACY_SIZE];
} meowkey_store_image_v1_t;

typedef union {
    struct {
        uint32_t magic;
        uint32_t version;
        uint32_t credential_count;
        uint8_t pin_configured;
        uint8_t pin_retries;
        uint8_t reserved[2];
        uint8_t pin_hash[16];
        uint8_t pin_token[32];
        meowkey_store_slot_v2_t slots[MEOWKEY_STORE_LEGACY_MAX_CREDENTIALS];
    } data;
    uint8_t raw[MEOWKEY_STORE_LEGACY_SIZE];
} meowkey_store_image_v2_t;

typedef union {
    struct {
        uint32_t magic;
        uint32_t version;
        uint32_t credential_count;
        uint8_t pin_configured;
        uint8_t pin_retries;
        uint8_t reserved[2];
        uint8_t pin_hash[16];
        uint8_t pin_token[32];
        meowkey_store_slot_t slots[MEOWKEY_STORE_LEGACY_MAX_CREDENTIALS];
    } data;
    uint8_t raw[MEOWKEY_STORE_LEGACY_SIZE];
} meowkey_store_image_v3_small_t;

typedef union {
    struct {
        uint32_t magic;
        uint32_t version;
        uint32_t credential_count;
        uint8_t pin_configured;
        uint8_t pin_retries;
        uint8_t reserved[2];
        uint8_t pin_hash[16];
        uint8_t pin_token[32];
        meowkey_store_slot_t slots[MEOWKEY_STORE_V3_MEDIUM_MAX_CREDENTIALS];
    } data;
    uint8_t raw[MEOWKEY_STORE_V3_MEDIUM_SIZE];
} meowkey_store_image_v3_medium_t;

typedef union {
    struct {
        uint32_t magic;
        uint32_t version;
        uint32_t credential_count;
        uint8_t pin_configured;
        uint8_t pin_retries;
        uint8_t reserved[2];
        uint8_t pin_hash[16];
        uint8_t pin_token[32];
        meowkey_store_slot_t slots[MEOWKEY_STORE_V4_V5_LEGACY_MAX_CREDENTIALS];
    } data;
    uint8_t raw[MEOWKEY_STORE_V4_V5_LEGACY_SIZE];
} meowkey_store_image_v4_v5_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_size;
    uint32_t entry_capacity;
    uint32_t store_version;
    uint32_t store_generation;
    uint32_t store_payload_crc32;
    uint32_t reserved;
} meowkey_sign_count_journal_header_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_size;
    uint32_t entry_capacity;
    uint32_t reserved[4];
} meowkey_legacy_sign_count_journal_header_t;

typedef struct {
    uint32_t magic;
    uint16_t slot_index;
    uint16_t slot_index_check;
    uint32_t sign_count;
    uint32_t sequence;
} meowkey_sign_count_entry_t;

enum {
    MEOWKEY_SIGN_COUNT_JOURNAL_ENTRY_CAPACITY =
        (MEOWKEY_STORE_SIGN_COUNT_JOURNAL_SIZE - FLASH_PAGE_SIZE) / sizeof(meowkey_sign_count_entry_t),
};

typedef struct {
    bool ready;
    uint32_t next_sequence;
    size_t next_entry_index;
} meowkey_sign_count_journal_state_t;

_Static_assert(MEOWKEY_STORE_DATA_SIZE >= (FLASH_SECTOR_SIZE * 2u), "credential store must reserve at least two data sectors");
_Static_assert(sizeof(meowkey_user_presence_config_t) == 8u, "user-presence config must stay compact");
_Static_assert(sizeof(meowkey_store_payload_fixed_t) == 32u, "store fixed payload must stay stable");
_Static_assert(sizeof(meowkey_store_header_t) == MEOWKEY_STORE_HEADER_SIZE, "store header size must stay page-friendly");
_Static_assert(sizeof(meowkey_store_payload_image_t) == MEOWKEY_STORE_SLOT_PAYLOAD_SIZE, "payload image must fill a slot payload");
_Static_assert(sizeof(meowkey_store_slot_image_t) == MEOWKEY_STORE_SLOT_SIZE, "slot image must fill a slot");
_Static_assert(sizeof(meowkey_store_image_v1_t) <= MEOWKEY_STORE_LEGACY_SIZE, "legacy v1 image exceeds legacy flash region");
_Static_assert(sizeof(meowkey_store_image_v2_t) <= MEOWKEY_STORE_LEGACY_SIZE, "legacy v2 image exceeds legacy flash region");
_Static_assert(sizeof(meowkey_store_image_v3_small_t) <= MEOWKEY_STORE_LEGACY_SIZE, "legacy v3 small image exceeds legacy flash region");
_Static_assert(sizeof(meowkey_store_image_v3_medium_t) <= MEOWKEY_STORE_V3_MEDIUM_SIZE, "legacy v3 medium image exceeds reserved flash region");
_Static_assert(sizeof(meowkey_store_image_v4_v5_t) <= MEOWKEY_STORE_V4_V5_LEGACY_SIZE, "legacy v4/v5 image exceeds reserved flash region");
_Static_assert(sizeof(meowkey_sign_count_journal_header_t) <= FLASH_PAGE_SIZE, "sign count journal header must fit in one page");
_Static_assert(sizeof(meowkey_sign_count_entry_t) == 16u, "sign count journal entries must stay page-friendly");
_Static_assert(MEOWKEY_SIGN_COUNT_JOURNAL_ENTRY_CAPACITY > 0u, "sign count journal must reserve at least one entry");
_Static_assert(MEOWKEY_STORE_MAX_CREDENTIALS > 0u, "credential store must reserve space for at least one credential");

static meowkey_store_payload_image_t s_store;
static bool s_store_loaded = false;
static uint32_t s_store_generation = 0u;
static uint32_t s_store_payload_crc32 = 0u;
static int s_active_slot_index = -1;
static meowkey_sign_count_journal_state_t s_sign_count_journal;

static uint32_t crc32_bytes(const uint8_t *data, size_t length) {
    uint32_t crc = 0xffffffffu;
    size_t index;

    for (index = 0u; index < length; ++index) {
        uint32_t value = (crc ^ data[index]) & 0xffu;
        size_t bit;
        for (bit = 0u; bit < 8u; ++bit) {
            value = (value & 1u) != 0u ? (0xedb88320u ^ (value >> 1u)) : (value >> 1u);
        }
        crc = value ^ (crc >> 8u);
    }

    return ~crc;
}

static uint32_t store_header_crc(const meowkey_store_header_t *header) {
    meowkey_store_header_t copy;
    memcpy(&copy, header, sizeof(copy));
    copy.header_crc32 = 0u;
    return crc32_bytes((const uint8_t *)&copy, sizeof(copy));
}

static size_t store_slot_offset(size_t slot_index) {
    return slot_index == 0u ? MEOWKEY_STORE_SLOT0_OFFSET : MEOWKEY_STORE_SLOT1_OFFSET;
}

static const meowkey_store_slot_image_t *store_slot_flash_image(size_t slot_index) {
    return (const meowkey_store_slot_image_t *)(XIP_BASE + store_slot_offset(slot_index));
}

static void store_default_user_presence_config(meowkey_user_presence_config_t *config) {
    memset(config, 0, sizeof(*config));
    config->source = (uint8_t)MEOWKEY_DEFAULT_UP_SOURCE;
    config->gpio_pin = (int8_t)MEOWKEY_DEFAULT_UP_GPIO_PIN;
    config->gpio_active_low = (uint8_t)MEOWKEY_DEFAULT_UP_GPIO_ACTIVE_LOW;
    config->tap_count = (uint8_t)MEOWKEY_DEFAULT_UP_TAP_COUNT;
    config->gesture_window_ms = (uint16_t)MEOWKEY_DEFAULT_UP_GESTURE_WINDOW_MS;
    config->request_timeout_ms = (uint16_t)MEOWKEY_DEFAULT_UP_REQUEST_TIMEOUT_MS;
}

static bool store_user_presence_config_is_valid(const meowkey_user_presence_config_t *config) {
    if (config == NULL) {
        return false;
    }
    if (config->source > MEOWKEY_UP_SOURCE_GPIO) {
        return false;
    }
    if (config->source == MEOWKEY_UP_SOURCE_GPIO && (config->gpio_pin < 0 || config->gpio_pin > 47)) {
        return false;
    }
    if (config->tap_count < 1u || config->tap_count > 4u) {
        return false;
    }
    if (config->gesture_window_ms < 100u || config->gesture_window_ms > 5000u) {
        return false;
    }
    if (config->request_timeout_ms < 500u || config->request_timeout_ms > 30000u) {
        return false;
    }
    return true;
}

static bool store_sanitize_user_presence_config(void) {
    meowkey_user_presence_config_t defaults;
    if (store_user_presence_config_is_valid(&s_store.data.fixed.user_presence)) {
        return false;
    }

    store_default_user_presence_config(&defaults);
    s_store.data.fixed.user_presence = defaults;
    return true;
}

static bool store_slot_is_valid(const meowkey_store_slot_t *slot) {
    if (slot->in_use != 1u) {
        return false;
    }
    if (slot->credential_id_length == 0u || slot->credential_id_length > MEOWKEY_CREDENTIAL_ID_SIZE) {
        return false;
    }
    if (slot->private_key_length != MEOWKEY_PRIVATE_KEY_SIZE) {
        return false;
    }
    if (slot->rp_id_length == 0u || slot->rp_id_length >= MEOWKEY_RP_ID_SIZE) {
        return false;
    }
    if (slot->user_id_length > MEOWKEY_USER_ID_SIZE ||
        slot->user_name_length >= MEOWKEY_USER_NAME_SIZE ||
        slot->display_name_length >= MEOWKEY_DISPLAY_NAME_SIZE) {
        return false;
    }
    return true;
}

static bool store_sanitize_slots(void) {
    bool changed = false;
    uint32_t valid_count = 0u;
    size_t index;

    for (index = 0u; index < MEOWKEY_STORE_MAX_CREDENTIALS; ++index) {
        meowkey_store_slot_t *slot = &s_store.data.slots[index];
        if (store_slot_is_valid(slot)) {
            valid_count += 1u;
            continue;
        }
        if (slot->in_use != 0u) {
            memset(slot, 0, sizeof(*slot));
            changed = true;
        }
    }

    if (s_store.data.fixed.credential_count != valid_count) {
        s_store.data.fixed.credential_count = valid_count;
        changed = true;
    }

    return changed;
}

static void sign_count_journal_reset_state(void) {
    memset(&s_sign_count_journal, 0, sizeof(s_sign_count_journal));
    s_sign_count_journal.next_sequence = 1u;
}

static void store_reset_image(void) {
    meowkey_user_presence_config_t defaults;

    memset(&s_store, 0, sizeof(s_store));
    store_default_user_presence_config(&defaults);
    s_store.data.fixed.credential_count = 0u;
    s_store.data.fixed.pin_retries = MEOWKEY_PIN_RETRIES_DEFAULT;
    s_store.data.fixed.user_presence = defaults;
    s_store_generation = 0u;
    s_store_payload_crc32 = crc32_bytes(s_store.raw, sizeof(s_store.raw));
    s_active_slot_index = -1;
    sign_count_journal_reset_state();
}

static void store_copy_current_slots(const meowkey_store_slot_t *slots, size_t slot_count) {
    size_t index;
    size_t imported = 0u;
    size_t limit = slot_count < MEOWKEY_STORE_MAX_CREDENTIALS ? slot_count : MEOWKEY_STORE_MAX_CREDENTIALS;

    memset(s_store.data.slots, 0, sizeof(s_store.data.slots));
    for (index = 0u; index < limit; ++index) {
        if (slots[index].in_use != 0u) {
            memcpy(&s_store.data.slots[index], &slots[index], sizeof(s_store.data.slots[index]));
            imported += 1u;
        }
    }
    s_store.data.fixed.credential_count = (uint32_t)imported;
}

static void store_copy_slot_to_record(const meowkey_store_slot_t *slot, meowkey_credential_record_t *record) {
    memset(record, 0, sizeof(*record));
    record->sign_count = slot->sign_count;
    record->discoverable = slot->discoverable != 0u;
    record->cred_random_ready = slot->cred_random_ready != 0u;
    record->credential_id_length = slot->credential_id_length;
    record->private_key_length = slot->private_key_length;
    record->rp_id_length = slot->rp_id_length;
    record->user_id_length = slot->user_id_length;
    record->user_name_length = slot->user_name_length;
    record->display_name_length = slot->display_name_length;
    memcpy(record->credential_id, slot->credential_id, sizeof(record->credential_id));
    memcpy(record->private_key, slot->private_key, sizeof(record->private_key));
    memcpy(record->cred_random_with_uv, slot->cred_random_with_uv, sizeof(record->cred_random_with_uv));
    memcpy(record->cred_random_without_uv, slot->cred_random_without_uv, sizeof(record->cred_random_without_uv));
    memcpy(record->rp_id, slot->rp_id, sizeof(record->rp_id));
    memcpy(record->user_id, slot->user_id, sizeof(record->user_id));
    memcpy(record->user_name, slot->user_name, sizeof(record->user_name));
    memcpy(record->display_name, slot->display_name, sizeof(record->display_name));
}

static void store_copy_record_to_slot(const meowkey_credential_record_t *record, meowkey_store_slot_t *slot) {
    memset(slot, 0, sizeof(*slot));
    slot->in_use = 1u;
    slot->discoverable = (uint8_t)(record->discoverable ? 1u : 0u);
    slot->cred_random_ready = (uint8_t)(record->cred_random_ready ? 1u : 0u);
    slot->credential_id_length = (uint8_t)record->credential_id_length;
    slot->private_key_length = (uint8_t)record->private_key_length;
    slot->rp_id_length = (uint8_t)record->rp_id_length;
    slot->user_id_length = (uint8_t)record->user_id_length;
    slot->user_name_length = (uint8_t)record->user_name_length;
    slot->display_name_length = (uint8_t)record->display_name_length;
    slot->sign_count = record->sign_count;
    memcpy(slot->credential_id, record->credential_id, sizeof(slot->credential_id));
    memcpy(slot->private_key, record->private_key, sizeof(slot->private_key));
    memcpy(slot->cred_random_with_uv, record->cred_random_with_uv, sizeof(slot->cred_random_with_uv));
    memcpy(slot->cred_random_without_uv, record->cred_random_without_uv, sizeof(slot->cred_random_without_uv));
    memcpy(slot->rp_id, record->rp_id, sizeof(slot->rp_id));
    memcpy(slot->user_id, record->user_id, sizeof(slot->user_id));
    memcpy(slot->user_name, record->user_name, sizeof(slot->user_name));
    memcpy(slot->display_name, record->display_name, sizeof(slot->display_name));
}

static void store_copy_v2_slot_to_slot(const meowkey_store_slot_v2_t *source, meowkey_store_slot_t *target) {
    memset(target, 0, sizeof(*target));
    target->in_use = source->in_use;
    target->discoverable = source->discoverable;
    target->credential_id_length = source->credential_id_length;
    target->private_key_length = source->private_key_length;
    target->rp_id_length = source->rp_id_length;
    target->user_id_length = source->user_id_length;
    target->user_name_length = source->user_name_length;
    target->display_name_length = source->display_name_length;
    target->sign_count = source->sign_count;
    memcpy(target->credential_id, source->credential_id, sizeof(target->credential_id));
    memcpy(target->private_key, source->private_key, sizeof(target->private_key));
    memcpy(target->rp_id, source->rp_id, sizeof(target->rp_id));
    memcpy(target->user_id, source->user_id, sizeof(target->user_id));
    memcpy(target->user_name, source->user_name, sizeof(target->user_name));
    memcpy(target->display_name, source->display_name, sizeof(target->display_name));
}

static void store_prepare_slot_image(meowkey_store_slot_image_t *image, uint32_t generation) {
    memset(image, 0xff, sizeof(*image));
    memcpy(image->parts.payload, s_store.raw, sizeof(s_store.raw));
    memset(&image->parts.header, 0, sizeof(image->parts.header));
    image->parts.header.magic = MEOWKEY_STORE_MAGIC;
    image->parts.header.version = MEOWKEY_STORE_VERSION;
    image->parts.header.generation = generation;
    image->parts.header.payload_size = MEOWKEY_STORE_SLOT_PAYLOAD_SIZE;
    image->parts.header.payload_crc32 = crc32_bytes(image->parts.payload, sizeof(s_store.raw));
    image->parts.header.header_crc32 = store_header_crc(&image->parts.header);
}

static bool store_header_is_valid(const meowkey_store_header_t *header) {
    return header->magic == MEOWKEY_STORE_MAGIC &&
           header->version == MEOWKEY_STORE_VERSION &&
           header->generation > 0u &&
           header->payload_size == MEOWKEY_STORE_SLOT_PAYLOAD_SIZE &&
           header->header_crc32 == store_header_crc(header);
}

static bool store_load_slot(size_t slot_index,
                            meowkey_store_payload_image_t *payload,
                            uint32_t *generation,
                            uint32_t *payload_crc32) {
    const meowkey_store_slot_image_t *image = store_slot_flash_image(slot_index);
    uint32_t actual_payload_crc32;

    if (!store_header_is_valid(&image->parts.header)) {
        return false;
    }

    actual_payload_crc32 = crc32_bytes(image->parts.payload, MEOWKEY_STORE_SLOT_PAYLOAD_SIZE);
    if (actual_payload_crc32 != image->parts.header.payload_crc32) {
        return false;
    }

    if (payload != NULL) {
        memcpy(payload->raw, image->parts.payload, sizeof(payload->raw));
    }
    if (generation != NULL) {
        *generation = image->parts.header.generation;
    }
    if (payload_crc32 != NULL) {
        *payload_crc32 = actual_payload_crc32;
    }
    return true;
}

static bool sign_count_journal_header_basic_is_valid(const meowkey_sign_count_journal_header_t *header) {
    return header->magic == MEOWKEY_SIGN_COUNT_JOURNAL_MAGIC &&
           header->version == MEOWKEY_SIGN_COUNT_JOURNAL_VERSION &&
           header->entry_size == sizeof(meowkey_sign_count_entry_t) &&
           header->entry_capacity == MEOWKEY_SIGN_COUNT_JOURNAL_ENTRY_CAPACITY &&
           header->store_version == MEOWKEY_STORE_VERSION;
}

static bool sign_count_journal_header_is_valid(const meowkey_sign_count_journal_header_t *header) {
    return sign_count_journal_header_basic_is_valid(header) &&
           header->store_generation == s_store_generation &&
           header->store_payload_crc32 == s_store_payload_crc32;
}

static bool legacy_sign_count_journal_header_is_valid(const meowkey_legacy_sign_count_journal_header_t *header) {
    return header->magic == MEOWKEY_SIGN_COUNT_JOURNAL_MAGIC &&
           header->version == MEOWKEY_SIGN_COUNT_LEGACY_JOURNAL_VERSION &&
           header->entry_size == sizeof(meowkey_sign_count_entry_t) &&
           header->entry_capacity == MEOWKEY_SIGN_COUNT_JOURNAL_ENTRY_CAPACITY;
}

static bool sign_count_journal_entry_is_blank(const meowkey_sign_count_entry_t *entry) {
    const uint8_t *raw = (const uint8_t *)entry;
    size_t index;

    for (index = 0u; index < sizeof(*entry); ++index) {
        if (raw[index] != 0xffu) {
            return false;
        }
    }
    return true;
}

static bool sign_count_journal_entry_is_valid(const meowkey_sign_count_entry_t *entry) {
    return entry->magic == MEOWKEY_SIGN_COUNT_ENTRY_MAGIC &&
           entry->slot_index_check == (uint16_t)~entry->slot_index;
}

static bool sign_count_journal_erase_and_initialize(void) {
    meowkey_sign_count_journal_header_t header;
    uint8_t first_page[FLASH_PAGE_SIZE];
    uint32_t interrupts;

    if (s_active_slot_index < 0) {
        sign_count_journal_reset_state();
        return true;
    }

    memset(&header, 0, sizeof(header));
    header.magic = MEOWKEY_SIGN_COUNT_JOURNAL_MAGIC;
    header.version = MEOWKEY_SIGN_COUNT_JOURNAL_VERSION;
    header.entry_size = sizeof(meowkey_sign_count_entry_t);
    header.entry_capacity = MEOWKEY_SIGN_COUNT_JOURNAL_ENTRY_CAPACITY;
    header.store_version = MEOWKEY_STORE_VERSION;
    header.store_generation = s_store_generation;
    header.store_payload_crc32 = s_store_payload_crc32;

    memset(first_page, 0xff, sizeof(first_page));
    memcpy(first_page, &header, sizeof(header));

    interrupts = save_and_disable_interrupts();
    flash_range_erase(MEOWKEY_STORE_SIGN_COUNT_JOURNAL_OFFSET, MEOWKEY_STORE_SIGN_COUNT_JOURNAL_SIZE);
    flash_range_program(MEOWKEY_STORE_SIGN_COUNT_JOURNAL_OFFSET, first_page, sizeof(first_page));
    restore_interrupts(interrupts);

    s_sign_count_journal.ready = true;
    s_sign_count_journal.next_sequence = 1u;
    s_sign_count_journal.next_entry_index = 0u;
    return true;
}

static bool store_program_slot(size_t slot_index, const meowkey_store_slot_image_t *image) {
    size_t slot_offset = store_slot_offset(slot_index);
    size_t page_offset;
    uint32_t interrupts;

    interrupts = save_and_disable_interrupts();
    flash_range_erase(slot_offset, MEOWKEY_STORE_SLOT_SIZE);
    for (page_offset = FLASH_PAGE_SIZE; page_offset < MEOWKEY_STORE_SLOT_SIZE; page_offset += FLASH_PAGE_SIZE) {
        flash_range_program(slot_offset + page_offset, &image->raw[page_offset], FLASH_PAGE_SIZE);
    }
    restore_interrupts(interrupts);

    if (memcmp((const void *)(XIP_BASE + slot_offset + FLASH_PAGE_SIZE),
               &image->raw[FLASH_PAGE_SIZE],
               MEOWKEY_STORE_SLOT_SIZE - FLASH_PAGE_SIZE) != 0) {
        return false;
    }

    interrupts = save_and_disable_interrupts();
    flash_range_program(slot_offset, image->raw, FLASH_PAGE_SIZE);
    restore_interrupts(interrupts);

    return memcmp((const void *)(XIP_BASE + slot_offset), image->raw, MEOWKEY_STORE_SLOT_SIZE) == 0;
}

static bool store_commit_payload(void) {
    meowkey_store_slot_image_t image;
    uint32_t generation = s_store_generation + 1u;
    size_t target_slot_index = s_active_slot_index == 0 ? 1u : 0u;

    store_prepare_slot_image(&image, generation);
    if (!store_program_slot(target_slot_index, &image)) {
        return false;
    }

    s_active_slot_index = (int)target_slot_index;
    s_store_generation = generation;
    s_store_payload_crc32 = image.parts.header.payload_crc32;
    return true;
}

static bool store_commit_transaction(void) {
    if (!store_commit_payload()) {
        return false;
    }
    return sign_count_journal_erase_and_initialize();
}

static void sign_count_journal_load_if_present(void) {
    const meowkey_sign_count_journal_header_t *header;
    size_t entry_index;

    sign_count_journal_reset_state();
    if (s_active_slot_index < 0) {
        return;
    }

    header = (const meowkey_sign_count_journal_header_t *)(XIP_BASE + MEOWKEY_STORE_SIGN_COUNT_JOURNAL_OFFSET);
    if (!sign_count_journal_header_is_valid(header)) {
        return;
    }

    s_sign_count_journal.ready = true;
    for (entry_index = 0u; entry_index < MEOWKEY_SIGN_COUNT_JOURNAL_ENTRY_CAPACITY; ++entry_index) {
        size_t entry_offset = MEOWKEY_STORE_SIGN_COUNT_JOURNAL_OFFSET + FLASH_PAGE_SIZE +
                              (entry_index * sizeof(meowkey_sign_count_entry_t));
        const meowkey_sign_count_entry_t *entry = (const meowkey_sign_count_entry_t *)(XIP_BASE + entry_offset);

        if (sign_count_journal_entry_is_blank(entry)) {
            s_sign_count_journal.next_entry_index = entry_index;
            return;
        }
        if (!sign_count_journal_entry_is_valid(entry)) {
            s_sign_count_journal.next_entry_index = entry_index;
            return;
        }

        if (entry->sequence >= s_sign_count_journal.next_sequence) {
            s_sign_count_journal.next_sequence = entry->sequence + 1u;
        }
        if (entry->slot_index < MEOWKEY_STORE_MAX_CREDENTIALS &&
            store_slot_is_valid(&s_store.data.slots[entry->slot_index])) {
            s_store.data.slots[entry->slot_index].sign_count = entry->sign_count;
        }
        s_sign_count_journal.next_entry_index = entry_index + 1u;
    }
}

static void legacy_sign_count_journal_apply_if_present(void) {
    const meowkey_legacy_sign_count_journal_header_t *header;
    size_t entry_index;

    header = (const meowkey_legacy_sign_count_journal_header_t *)(XIP_BASE + MEOWKEY_STORE_SIGN_COUNT_JOURNAL_OFFSET);
    if (!legacy_sign_count_journal_header_is_valid(header)) {
        return;
    }

    for (entry_index = 0u; entry_index < MEOWKEY_SIGN_COUNT_JOURNAL_ENTRY_CAPACITY; ++entry_index) {
        size_t entry_offset = MEOWKEY_STORE_SIGN_COUNT_JOURNAL_OFFSET + FLASH_PAGE_SIZE +
                              (entry_index * sizeof(meowkey_sign_count_entry_t));
        const meowkey_sign_count_entry_t *entry = (const meowkey_sign_count_entry_t *)(XIP_BASE + entry_offset);

        if (sign_count_journal_entry_is_blank(entry) || !sign_count_journal_entry_is_valid(entry)) {
            return;
        }
        if (entry->slot_index < MEOWKEY_STORE_MAX_CREDENTIALS &&
            store_slot_is_valid(&s_store.data.slots[entry->slot_index])) {
            s_store.data.slots[entry->slot_index].sign_count = entry->sign_count;
        }
    }
}

static bool sign_count_journal_ensure_ready(void) {
    if (s_sign_count_journal.ready) {
        return true;
    }
    return sign_count_journal_erase_and_initialize();
}

static bool sign_count_journal_compact(void) {
    return store_commit_transaction();
}

static bool sign_count_journal_append(uint16_t slot_index, uint32_t sign_count) {
    uint8_t page_buffer[FLASH_PAGE_SIZE];
    meowkey_sign_count_entry_t entry;
    size_t entry_offset;
    size_t page_offset;
    size_t in_page_offset;
    uint32_t interrupts;

    if (!sign_count_journal_ensure_ready()) {
        return false;
    }

    if (s_sign_count_journal.next_entry_index >= MEOWKEY_SIGN_COUNT_JOURNAL_ENTRY_CAPACITY) {
        if (!sign_count_journal_compact()) {
            return false;
        }
    }

    entry.magic = MEOWKEY_SIGN_COUNT_ENTRY_MAGIC;
    entry.slot_index = slot_index;
    entry.slot_index_check = (uint16_t)~slot_index;
    entry.sign_count = sign_count;
    entry.sequence = s_sign_count_journal.next_sequence;

    entry_offset = MEOWKEY_STORE_SIGN_COUNT_JOURNAL_OFFSET + FLASH_PAGE_SIZE +
                   (s_sign_count_journal.next_entry_index * sizeof(meowkey_sign_count_entry_t));
    page_offset = entry_offset - (entry_offset % FLASH_PAGE_SIZE);
    in_page_offset = entry_offset - page_offset;

    memcpy(page_buffer, (const void *)(XIP_BASE + page_offset), sizeof(page_buffer));
    if (!sign_count_journal_entry_is_blank((const meowkey_sign_count_entry_t *)&page_buffer[in_page_offset])) {
        return false;
    }
    memcpy(&page_buffer[in_page_offset], &entry, sizeof(entry));

    interrupts = save_and_disable_interrupts();
    flash_range_program(page_offset, page_buffer, sizeof(page_buffer));
    restore_interrupts(interrupts);

    s_sign_count_journal.next_entry_index += 1u;
    s_sign_count_journal.next_sequence += 1u;
    return true;
}

static bool store_import_v4_v5_image_at_offset(size_t offset, bool apply_legacy_journal) {
    const meowkey_store_image_v4_v5_t *legacy;

    legacy = (const meowkey_store_image_v4_v5_t *)(XIP_BASE + offset);
    if (legacy->data.magic == MEOWKEY_STORE_MAGIC &&
        (legacy->data.version == 4u || legacy->data.version == 5u)) {
        store_reset_image();
        s_store.data.fixed.pin_configured = legacy->data.pin_configured;
        s_store.data.fixed.pin_retries = legacy->data.pin_retries;
        memcpy(s_store.data.fixed.pin_hash, legacy->data.pin_hash, sizeof(s_store.data.fixed.pin_hash));
        store_copy_current_slots(legacy->data.slots, MEOWKEY_STORE_V4_V5_LEGACY_MAX_CREDENTIALS);
        if (apply_legacy_journal && legacy->data.version == 5u) {
            legacy_sign_count_journal_apply_if_present();
        }
        return true;
    }

    return false;
}

static bool store_import_legacy_image_at_offset(size_t offset) {
    const meowkey_store_image_v1_t *legacy_v1;
    const meowkey_store_image_v2_t *legacy_v2;
    const meowkey_store_image_v3_small_t *legacy_v3_small;
    size_t index;

    legacy_v1 = (const meowkey_store_image_v1_t *)(XIP_BASE + offset);
    if (legacy_v1->data.magic == MEOWKEY_STORE_MAGIC && legacy_v1->data.version == 1u) {
        store_reset_image();
        for (index = 0u; index < MEOWKEY_STORE_LEGACY_MAX_CREDENTIALS; ++index) {
            store_copy_v2_slot_to_slot(&legacy_v1->data.slots[index], &s_store.data.slots[index]);
        }
        s_store.data.fixed.credential_count = legacy_v1->data.credential_count;
        return true;
    }

    legacy_v2 = (const meowkey_store_image_v2_t *)(XIP_BASE + offset);
    if (legacy_v2->data.magic == MEOWKEY_STORE_MAGIC && legacy_v2->data.version == 2u) {
        store_reset_image();
        s_store.data.fixed.pin_configured = legacy_v2->data.pin_configured;
        s_store.data.fixed.pin_retries = legacy_v2->data.pin_retries;
        memcpy(s_store.data.fixed.pin_hash, legacy_v2->data.pin_hash, sizeof(s_store.data.fixed.pin_hash));
        for (index = 0u; index < MEOWKEY_STORE_LEGACY_MAX_CREDENTIALS; ++index) {
            store_copy_v2_slot_to_slot(&legacy_v2->data.slots[index], &s_store.data.slots[index]);
        }
        s_store.data.fixed.credential_count = legacy_v2->data.credential_count;
        return true;
    }

    legacy_v3_small = (const meowkey_store_image_v3_small_t *)(XIP_BASE + offset);
    if (legacy_v3_small->data.magic == MEOWKEY_STORE_MAGIC && legacy_v3_small->data.version == 3u) {
        store_reset_image();
        s_store.data.fixed.pin_configured = legacy_v3_small->data.pin_configured;
        s_store.data.fixed.pin_retries = legacy_v3_small->data.pin_retries;
        memcpy(s_store.data.fixed.pin_hash, legacy_v3_small->data.pin_hash, sizeof(s_store.data.fixed.pin_hash));
        store_copy_current_slots(legacy_v3_small->data.slots, MEOWKEY_STORE_LEGACY_MAX_CREDENTIALS);
        return true;
    }

    return false;
}

static bool store_import_v3_medium_image_at_offset(size_t offset) {
    const meowkey_store_image_v3_medium_t *legacy_v3_medium;

    legacy_v3_medium = (const meowkey_store_image_v3_medium_t *)(XIP_BASE + offset);
    if (legacy_v3_medium->data.magic == MEOWKEY_STORE_MAGIC && legacy_v3_medium->data.version == 3u) {
        store_reset_image();
        s_store.data.fixed.pin_configured = legacy_v3_medium->data.pin_configured;
        s_store.data.fixed.pin_retries = legacy_v3_medium->data.pin_retries;
        memcpy(s_store.data.fixed.pin_hash, legacy_v3_medium->data.pin_hash, sizeof(s_store.data.fixed.pin_hash));
        store_copy_current_slots(legacy_v3_medium->data.slots, MEOWKEY_STORE_V3_MEDIUM_MAX_CREDENTIALS);
        return true;
    }

    return false;
}

static bool store_load_from_legacy_if_present(void) {
    if (store_import_v4_v5_image_at_offset(MEOWKEY_STORE_OFFSET, true)) {
        return true;
    }
    if (store_import_legacy_image_at_offset(MEOWKEY_STORE_OFFSET)) {
        return true;
    }
    if (MEOWKEY_STORE_OLD_4MB_V4_OFFSET != MEOWKEY_STORE_OFFSET &&
        store_import_v4_v5_image_at_offset(MEOWKEY_STORE_OLD_4MB_V4_OFFSET, false)) {
        return true;
    }
    if (store_import_legacy_image_at_offset(MEOWKEY_STORE_LEGACY_OFFSET)) {
        return true;
    }
    if (MEOWKEY_STORE_OLD_4MB_LEGACY_OFFSET != MEOWKEY_STORE_LEGACY_OFFSET &&
        store_import_legacy_image_at_offset(MEOWKEY_STORE_OLD_4MB_LEGACY_OFFSET)) {
        return true;
    }
    if (MEOWKEY_STORE_V3_MEDIUM_OFFSET != MEOWKEY_STORE_OFFSET &&
        store_import_v3_medium_image_at_offset(MEOWKEY_STORE_V3_MEDIUM_OFFSET)) {
        return true;
    }
    if (MEOWKEY_STORE_OLD_4MB_V3_MEDIUM_OFFSET != MEOWKEY_STORE_V3_MEDIUM_OFFSET &&
        store_import_v3_medium_image_at_offset(MEOWKEY_STORE_OLD_4MB_V3_MEDIUM_OFFSET)) {
        return true;
    }
    return false;
}

static void store_load_if_needed(void) {
    bool imported = false;
    bool changed = false;
    size_t slot_index;
    bool have_slot = false;
    uint32_t best_generation = 0u;
    uint32_t best_payload_crc32 = 0u;
    meowkey_store_payload_image_t best_payload;
    size_t best_slot_index = 0u;

    if (s_store_loaded) {
        return;
    }

    store_reset_image();
    for (slot_index = 0u; slot_index < MEOWKEY_STORE_SLOT_COUNT; ++slot_index) {
        meowkey_store_payload_image_t candidate_payload;
        uint32_t candidate_generation = 0u;
        uint32_t candidate_payload_crc32 = 0u;

        if (!store_load_slot(slot_index, &candidate_payload, &candidate_generation, &candidate_payload_crc32)) {
            continue;
        }
        if (!have_slot || candidate_generation > best_generation) {
            have_slot = true;
            best_generation = candidate_generation;
            best_payload_crc32 = candidate_payload_crc32;
            best_slot_index = slot_index;
            best_payload = candidate_payload;
        }
    }

    if (have_slot) {
        s_store = best_payload;
        s_store_generation = best_generation;
        s_store_payload_crc32 = best_payload_crc32;
        s_active_slot_index = (int)best_slot_index;
    } else if (store_load_from_legacy_if_present()) {
        imported = true;
    }

    changed |= store_sanitize_slots();
    changed |= store_sanitize_user_presence_config();
    if (s_store.data.fixed.pin_retries == 0u && s_store.data.fixed.pin_configured == 0u) {
        s_store.data.fixed.pin_retries = MEOWKEY_PIN_RETRIES_DEFAULT;
        changed = true;
    }

    if (have_slot) {
        sign_count_journal_load_if_present();
    } else {
        sign_count_journal_reset_state();
    }

    s_store_loaded = true;
    if (have_slot) {
        s_store_payload_crc32 = crc32_bytes(s_store.raw, sizeof(s_store.raw));
    }

    if (imported || changed) {
        (void)store_commit_transaction();
    }
}

void meowkey_store_init(void) {
    store_load_if_needed();
}

bool meowkey_store_add_credential(const meowkey_credential_record_t *record, uint32_t *slot_index) {
    size_t index;

    store_load_if_needed();
    for (index = 0u; index < MEOWKEY_STORE_MAX_CREDENTIALS; ++index) {
        if (!store_slot_is_valid(&s_store.data.slots[index])) {
            store_copy_record_to_slot(record, &s_store.data.slots[index]);
            s_store.data.fixed.credential_count += 1u;
            if (!store_commit_transaction()) {
                memset(&s_store.data.slots[index], 0, sizeof(s_store.data.slots[index]));
                s_store.data.fixed.credential_count -= 1u;
                return false;
            }
            if (slot_index != NULL) {
                *slot_index = (uint32_t)index;
            }
            return true;
        }
    }

    return false;
}

bool meowkey_store_find_by_credential_id(const uint8_t *credential_id,
                                         size_t credential_id_length,
                                         meowkey_credential_record_t *record,
                                         uint32_t *slot_index) {
    size_t index;

    store_load_if_needed();
    for (index = 0u; index < MEOWKEY_STORE_MAX_CREDENTIALS; ++index) {
        meowkey_store_slot_t *slot = &s_store.data.slots[index];
        if (!store_slot_is_valid(slot) || slot->credential_id_length != credential_id_length) {
            continue;
        }
        if (memcmp(slot->credential_id, credential_id, credential_id_length) == 0) {
            if (record != NULL) {
                store_copy_slot_to_record(slot, record);
            }
            if (slot_index != NULL) {
                *slot_index = (uint32_t)index;
            }
            return true;
        }
    }
    return false;
}

bool meowkey_store_find_by_rp_id(const char *rp_id,
                                 meowkey_credential_record_t *record,
                                 uint32_t *slot_index) {
    size_t index;
    size_t rp_id_length;

    store_load_if_needed();
    rp_id_length = strlen(rp_id);
    for (index = 0u; index < MEOWKEY_STORE_MAX_CREDENTIALS; ++index) {
        meowkey_store_slot_t *slot = &s_store.data.slots[index];
        if (!store_slot_is_valid(slot) || slot->rp_id_length != rp_id_length) {
            continue;
        }
        if (memcmp(slot->rp_id, rp_id, rp_id_length) == 0) {
            if (record != NULL) {
                store_copy_slot_to_record(slot, record);
            }
            if (slot_index != NULL) {
                *slot_index = (uint32_t)index;
            }
            return true;
        }
    }
    return false;
}

uint32_t meowkey_store_get_credential_count(void) {
    store_load_if_needed();
    return s_store.data.fixed.credential_count;
}

uint32_t meowkey_store_get_credential_capacity(void) {
    return MEOWKEY_STORE_MAX_CREDENTIALS;
}

bool meowkey_store_get_credential_by_slot(uint32_t slot_index, meowkey_credential_record_t *record) {
    store_load_if_needed();
    if (slot_index >= MEOWKEY_STORE_MAX_CREDENTIALS || !store_slot_is_valid(&s_store.data.slots[slot_index])) {
        return false;
    }
    if (record != NULL) {
        store_copy_slot_to_record(&s_store.data.slots[slot_index], record);
    }
    return true;
}

bool meowkey_store_update_sign_count(uint32_t slot_index, uint32_t sign_count) {
    uint32_t previous_sign_count;

    store_load_if_needed();
    if (slot_index >= MEOWKEY_STORE_MAX_CREDENTIALS || !store_slot_is_valid(&s_store.data.slots[slot_index])) {
        return false;
    }

    previous_sign_count = s_store.data.slots[slot_index].sign_count;
    s_store.data.slots[slot_index].sign_count = sign_count;
    if (!sign_count_journal_append((uint16_t)slot_index, sign_count)) {
        s_store.data.slots[slot_index].sign_count = previous_sign_count;
        return false;
    }
    return true;
}

bool meowkey_store_clear_credentials(void) {
    store_load_if_needed();
    memset(s_store.data.slots, 0, sizeof(s_store.data.slots));
    s_store.data.fixed.credential_count = 0u;
    return store_commit_transaction();
}

void meowkey_store_get_pin_state(meowkey_pin_state_t *state) {
    store_load_if_needed();
    memset(state, 0, sizeof(*state));
    state->configured = s_store.data.fixed.pin_configured != 0u;
    state->retries = s_store.data.fixed.pin_retries;
    memcpy(state->pin_hash, s_store.data.fixed.pin_hash, sizeof(state->pin_hash));
}

bool meowkey_store_set_pin_state(const meowkey_pin_state_t *state) {
    store_load_if_needed();
    s_store.data.fixed.pin_configured = (uint8_t)(state->configured ? 1u : 0u);
    s_store.data.fixed.pin_retries = state->retries;
    memcpy(s_store.data.fixed.pin_hash, state->pin_hash, sizeof(s_store.data.fixed.pin_hash));
    return store_commit_transaction();
}

void meowkey_store_get_user_presence_config(meowkey_user_presence_config_t *config) {
    store_load_if_needed();
    if (config != NULL) {
        *config = s_store.data.fixed.user_presence;
    }
}

bool meowkey_store_set_user_presence_config(const meowkey_user_presence_config_t *config) {
    if (!store_user_presence_config_is_valid(config)) {
        return false;
    }

    store_load_if_needed();
    s_store.data.fixed.user_presence = *config;
    return store_commit_transaction();
}

uint32_t meowkey_store_get_format_version(void) {
    return MEOWKEY_STORE_VERSION;
}

size_t meowkey_store_write_summary(char *output, size_t output_capacity) {
    if (output_capacity == 0u) {
        return 0u;
    }

    store_load_if_needed();
    return (size_t)snprintf(
        output,
        output_capacity,
        "store-version=%lu slot-size=%luKB generation=%lu journal=%lu/%lu entries active-slot=%d\n",
        (unsigned long)MEOWKEY_STORE_VERSION,
        (unsigned long)(MEOWKEY_STORE_SLOT_SIZE / 1024u),
        (unsigned long)s_store_generation,
        (unsigned long)s_sign_count_journal.next_entry_index,
        (unsigned long)MEOWKEY_SIGN_COUNT_JOURNAL_ENTRY_CAPACITY,
        s_active_slot_index);
}
