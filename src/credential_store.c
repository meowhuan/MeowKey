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
    MEOWKEY_STORE_VERSION = 5u,
    MEOWKEY_STORE_TOTAL_SIZE = FLASH_SECTOR_SIZE * MEOWKEY_CREDENTIAL_STORE_SECTORS,
    MEOWKEY_STORE_SIGN_COUNT_JOURNAL_SECTORS = 2u,
    MEOWKEY_STORE_SIGN_COUNT_JOURNAL_SIZE = FLASH_SECTOR_SIZE * MEOWKEY_STORE_SIGN_COUNT_JOURNAL_SECTORS,
    MEOWKEY_STORE_SIZE = MEOWKEY_STORE_TOTAL_SIZE - MEOWKEY_STORE_SIGN_COUNT_JOURNAL_SIZE,
    MEOWKEY_STORE_OFFSET = PICO_FLASH_SIZE_BYTES - MEOWKEY_STORE_TOTAL_SIZE,
    MEOWKEY_STORE_SIGN_COUNT_JOURNAL_OFFSET = MEOWKEY_STORE_OFFSET + MEOWKEY_STORE_SIZE,
    MEOWKEY_STORE_LEGACY_MAX_CREDENTIALS = 8u,
    MEOWKEY_STORE_LEGACY_SIZE = FLASH_SECTOR_SIZE,
    MEOWKEY_STORE_LEGACY_OFFSET = PICO_FLASH_SIZE_BYTES - MEOWKEY_STORE_LEGACY_SIZE,
    MEOWKEY_STORE_OLD_4MB_LEGACY_OFFSET = (4u * 1024u * 1024u) - FLASH_SECTOR_SIZE,
    MEOWKEY_STORE_V3_MEDIUM_MAX_CREDENTIALS = 32u,
    MEOWKEY_STORE_V3_MEDIUM_SIZE = FLASH_SECTOR_SIZE * 4u,
    MEOWKEY_STORE_V3_MEDIUM_OFFSET = PICO_FLASH_SIZE_BYTES - MEOWKEY_STORE_V3_MEDIUM_SIZE,
    MEOWKEY_STORE_OLD_4MB_V3_MEDIUM_OFFSET = (4u * 1024u * 1024u) - MEOWKEY_STORE_V3_MEDIUM_SIZE,
    MEOWKEY_STORE_V4_LEGACY_SIZE = MEOWKEY_STORE_TOTAL_SIZE,
    MEOWKEY_STORE_OLD_4MB_V4_OFFSET = (4u * 1024u * 1024u) - MEOWKEY_STORE_V4_LEGACY_SIZE,
    MEOWKEY_PIN_RETRIES_DEFAULT = 8u,
    MEOWKEY_SIGN_COUNT_JOURNAL_MAGIC = 0x4d4b5343u,
    MEOWKEY_SIGN_COUNT_JOURNAL_VERSION = 1u,
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

enum {
    MEOWKEY_STORE_HEADER_SIZE = 64u,
    MEOWKEY_STORE_MAX_CREDENTIALS_BY_REGION =
        (MEOWKEY_STORE_SIZE - MEOWKEY_STORE_HEADER_SIZE) / sizeof(meowkey_store_slot_t),
    MEOWKEY_STORE_V4_LEGACY_MAX_CREDENTIALS =
        (MEOWKEY_STORE_V4_LEGACY_SIZE - MEOWKEY_STORE_HEADER_SIZE) / sizeof(meowkey_store_slot_t),
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
        meowkey_store_slot_t slots[MEOWKEY_STORE_MAX_CREDENTIALS];
    } data;
    uint8_t raw[MEOWKEY_STORE_SIZE];
} meowkey_store_image_t;

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
        meowkey_store_slot_t slots[MEOWKEY_STORE_V4_LEGACY_MAX_CREDENTIALS];
    } data;
    uint8_t raw[MEOWKEY_STORE_V4_LEGACY_SIZE];
} meowkey_store_image_v4_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_size;
    uint32_t entry_capacity;
    uint32_t reserved[4];
} meowkey_sign_count_journal_header_t;

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

_Static_assert(MEOWKEY_STORE_SIZE >= FLASH_SECTOR_SIZE, "credential store must reserve at least one data sector");
_Static_assert(sizeof(meowkey_store_image_t) <= MEOWKEY_STORE_SIZE, "credential store image exceeds reserved flash region");
_Static_assert(sizeof(meowkey_store_image_v1_t) <= MEOWKEY_STORE_LEGACY_SIZE, "legacy v1 image exceeds legacy flash region");
_Static_assert(sizeof(meowkey_store_image_v2_t) <= MEOWKEY_STORE_LEGACY_SIZE, "legacy v2 image exceeds legacy flash region");
_Static_assert(sizeof(meowkey_store_image_v3_small_t) <= MEOWKEY_STORE_LEGACY_SIZE, "legacy v3 small image exceeds legacy flash region");
_Static_assert(sizeof(meowkey_store_image_v3_medium_t) <= MEOWKEY_STORE_V3_MEDIUM_SIZE, "legacy v3 medium image exceeds reserved flash region");
_Static_assert(sizeof(meowkey_store_image_v4_t) <= MEOWKEY_STORE_V4_LEGACY_SIZE, "legacy v4 image exceeds legacy flash region");
_Static_assert(sizeof(meowkey_sign_count_journal_header_t) <= FLASH_PAGE_SIZE, "sign count journal header must fit in one page");
_Static_assert(sizeof(meowkey_sign_count_entry_t) == 16u, "sign count journal entries must stay page-friendly");
_Static_assert(MEOWKEY_SIGN_COUNT_JOURNAL_ENTRY_CAPACITY > 0u, "sign count journal must reserve at least one entry");
_Static_assert(MEOWKEY_STORE_MAX_CREDENTIALS > 0u, "credential store must reserve space for at least one credential");

static meowkey_store_image_t s_store;
static bool s_store_loaded = false;
static meowkey_sign_count_journal_state_t s_sign_count_journal;

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

    for (index = 0; index < MEOWKEY_STORE_MAX_CREDENTIALS; ++index) {
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

    if (s_store.data.credential_count != valid_count) {
        s_store.data.credential_count = valid_count;
        changed = true;
    }

    return changed;
}

static void sign_count_journal_reset_state(void) {
    memset(&s_sign_count_journal, 0, sizeof(s_sign_count_journal));
    s_sign_count_journal.next_sequence = 1u;
}

static void store_reset_image(void) {
    memset(&s_store, 0, sizeof(s_store));
    s_store.data.magic = MEOWKEY_STORE_MAGIC;
    s_store.data.version = MEOWKEY_STORE_VERSION;
    s_store.data.credential_count = 0u;
    s_store.data.pin_retries = MEOWKEY_PIN_RETRIES_DEFAULT;
    sign_count_journal_reset_state();
}

static void store_copy_current_slots(const meowkey_store_slot_t *slots, size_t slot_count) {
    size_t index;
    size_t imported = 0u;
    size_t limit = slot_count < MEOWKEY_STORE_MAX_CREDENTIALS ? slot_count : MEOWKEY_STORE_MAX_CREDENTIALS;

    memset(s_store.data.slots, 0, sizeof(s_store.data.slots));
    for (index = 0; index < limit; ++index) {
        if (slots[index].in_use) {
            memcpy(&s_store.data.slots[index], &slots[index], sizeof(s_store.data.slots[index]));
            imported += 1u;
        }
    }
    s_store.data.credential_count = (uint32_t)imported;
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

static bool sign_count_journal_header_is_valid(const meowkey_sign_count_journal_header_t *header) {
    return header->magic == MEOWKEY_SIGN_COUNT_JOURNAL_MAGIC &&
           header->version == MEOWKEY_SIGN_COUNT_JOURNAL_VERSION &&
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
    uint32_t interrupts = save_and_disable_interrupts();

    memset(&header, 0, sizeof(header));
    header.magic = MEOWKEY_SIGN_COUNT_JOURNAL_MAGIC;
    header.version = MEOWKEY_SIGN_COUNT_JOURNAL_VERSION;
    header.entry_size = sizeof(meowkey_sign_count_entry_t);
    header.entry_capacity = MEOWKEY_SIGN_COUNT_JOURNAL_ENTRY_CAPACITY;

    memset(first_page, 0xff, sizeof(first_page));
    memcpy(first_page, &header, sizeof(header));

    flash_range_erase(MEOWKEY_STORE_SIGN_COUNT_JOURNAL_OFFSET, MEOWKEY_STORE_SIGN_COUNT_JOURNAL_SIZE);
    flash_range_program(MEOWKEY_STORE_SIGN_COUNT_JOURNAL_OFFSET, first_page, sizeof(first_page));
    restore_interrupts(interrupts);

    s_sign_count_journal.ready = true;
    s_sign_count_journal.next_sequence = 1u;
    s_sign_count_journal.next_entry_index = 0u;
    return true;
}

static bool store_commit(void) {
    uint32_t interrupts = save_and_disable_interrupts();
    size_t offset;

    flash_range_erase(MEOWKEY_STORE_OFFSET, MEOWKEY_STORE_SIZE);
    for (offset = 0; offset < MEOWKEY_STORE_SIZE; offset += FLASH_PAGE_SIZE) {
        flash_range_program(
            MEOWKEY_STORE_OFFSET + offset,
            &s_store.raw[offset],
            FLASH_PAGE_SIZE);
    }
    restore_interrupts(interrupts);
    return true;
}

static void sign_count_journal_load_if_present(void) {
    const meowkey_sign_count_journal_header_t *header;
    size_t entry_index;

    sign_count_journal_reset_state();
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

static bool sign_count_journal_ensure_ready(void) {
    if (s_sign_count_journal.ready) {
        return true;
    }
    return sign_count_journal_erase_and_initialize();
}

static bool sign_count_journal_compact(void) {
    if (!store_commit()) {
        return false;
    }
    return sign_count_journal_erase_and_initialize();
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

static bool store_import_v4_image_at_offset(size_t offset) {
    const meowkey_store_image_v4_t *legacy_v4;

    legacy_v4 = (const meowkey_store_image_v4_t *)(XIP_BASE + offset);
    if (legacy_v4->data.magic == MEOWKEY_STORE_MAGIC && legacy_v4->data.version == 4u) {
        store_reset_image();
        s_store.data.pin_configured = legacy_v4->data.pin_configured;
        s_store.data.pin_retries = legacy_v4->data.pin_retries;
        memcpy(s_store.data.pin_hash, legacy_v4->data.pin_hash, sizeof(s_store.data.pin_hash));
        memcpy(s_store.data.pin_token, legacy_v4->data.pin_token, sizeof(s_store.data.pin_token));
        store_copy_current_slots(legacy_v4->data.slots, MEOWKEY_STORE_V4_LEGACY_MAX_CREDENTIALS);
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
        for (index = 0; index < MEOWKEY_STORE_LEGACY_MAX_CREDENTIALS; ++index) {
            store_copy_v2_slot_to_slot(&legacy_v1->data.slots[index], &s_store.data.slots[index]);
        }
        s_store.data.credential_count = legacy_v1->data.credential_count;
        return true;
    }

    legacy_v2 = (const meowkey_store_image_v2_t *)(XIP_BASE + offset);
    if (legacy_v2->data.magic == MEOWKEY_STORE_MAGIC && legacy_v2->data.version == 2u) {
        store_reset_image();
        s_store.data.pin_configured = legacy_v2->data.pin_configured;
        s_store.data.pin_retries = legacy_v2->data.pin_retries;
        memcpy(s_store.data.pin_hash, legacy_v2->data.pin_hash, sizeof(s_store.data.pin_hash));
        memcpy(s_store.data.pin_token, legacy_v2->data.pin_token, sizeof(s_store.data.pin_token));
        for (index = 0; index < MEOWKEY_STORE_LEGACY_MAX_CREDENTIALS; ++index) {
            store_copy_v2_slot_to_slot(&legacy_v2->data.slots[index], &s_store.data.slots[index]);
        }
        s_store.data.credential_count = legacy_v2->data.credential_count;
        return true;
    }

    legacy_v3_small = (const meowkey_store_image_v3_small_t *)(XIP_BASE + offset);
    if (legacy_v3_small->data.magic == MEOWKEY_STORE_MAGIC && legacy_v3_small->data.version == 3u) {
        store_reset_image();
        s_store.data.pin_configured = legacy_v3_small->data.pin_configured;
        s_store.data.pin_retries = legacy_v3_small->data.pin_retries;
        memcpy(s_store.data.pin_hash, legacy_v3_small->data.pin_hash, sizeof(s_store.data.pin_hash));
        memcpy(s_store.data.pin_token, legacy_v3_small->data.pin_token, sizeof(s_store.data.pin_token));
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
        s_store.data.pin_configured = legacy_v3_medium->data.pin_configured;
        s_store.data.pin_retries = legacy_v3_medium->data.pin_retries;
        memcpy(s_store.data.pin_hash, legacy_v3_medium->data.pin_hash, sizeof(s_store.data.pin_hash));
        memcpy(s_store.data.pin_token, legacy_v3_medium->data.pin_token, sizeof(s_store.data.pin_token));
        store_copy_current_slots(legacy_v3_medium->data.slots, MEOWKEY_STORE_V3_MEDIUM_MAX_CREDENTIALS);
        return true;
    }

    return false;
}

static bool store_load_from_legacy_if_present(void) {
    if (store_import_legacy_image_at_offset(MEOWKEY_STORE_LEGACY_OFFSET)) {
        return true;
    }
    if (MEOWKEY_STORE_OLD_4MB_LEGACY_OFFSET != MEOWKEY_STORE_LEGACY_OFFSET) {
        if (store_import_legacy_image_at_offset(MEOWKEY_STORE_OLD_4MB_LEGACY_OFFSET)) {
            return true;
        }
    }
    if (MEOWKEY_STORE_V3_MEDIUM_OFFSET != MEOWKEY_STORE_OFFSET &&
        store_import_v3_medium_image_at_offset(MEOWKEY_STORE_V3_MEDIUM_OFFSET)) {
        return true;
    }
    if (MEOWKEY_STORE_OLD_4MB_V3_MEDIUM_OFFSET != MEOWKEY_STORE_V3_MEDIUM_OFFSET) {
        if (store_import_v3_medium_image_at_offset(MEOWKEY_STORE_OLD_4MB_V3_MEDIUM_OFFSET)) {
            return true;
        }
    }
    if (MEOWKEY_STORE_OLD_4MB_V4_OFFSET != MEOWKEY_STORE_OFFSET) {
        if (store_import_v4_image_at_offset(MEOWKEY_STORE_OLD_4MB_V4_OFFSET)) {
            return true;
        }
    }
    return false;
}

static void store_load_if_needed(void) {
    const meowkey_store_image_t *flash_store;
    size_t index;

    if (s_store_loaded) {
        return;
    }

    sign_count_journal_reset_state();
    flash_store = (const meowkey_store_image_t *)(XIP_BASE + MEOWKEY_STORE_OFFSET);
    memcpy(&s_store, flash_store, sizeof(s_store));

    if (s_store.data.magic == MEOWKEY_STORE_MAGIC && s_store.data.version == 1u) {
        const meowkey_store_image_v1_t *old_store = (const meowkey_store_image_v1_t *)(XIP_BASE + MEOWKEY_STORE_OFFSET);
        store_reset_image();
        for (index = 0; index < MEOWKEY_STORE_LEGACY_MAX_CREDENTIALS; ++index) {
            store_copy_v2_slot_to_slot(&old_store->data.slots[index], &s_store.data.slots[index]);
        }
        s_store.data.credential_count = old_store->data.credential_count;
    } else if (s_store.data.magic == MEOWKEY_STORE_MAGIC && s_store.data.version == 2u) {
        const meowkey_store_image_v2_t *old_store = (const meowkey_store_image_v2_t *)(XIP_BASE + MEOWKEY_STORE_OFFSET);
        store_reset_image();
        s_store.data.pin_configured = old_store->data.pin_configured;
        s_store.data.pin_retries = old_store->data.pin_retries;
        memcpy(s_store.data.pin_hash, old_store->data.pin_hash, sizeof(s_store.data.pin_hash));
        memcpy(s_store.data.pin_token, old_store->data.pin_token, sizeof(s_store.data.pin_token));
        for (index = 0; index < MEOWKEY_STORE_LEGACY_MAX_CREDENTIALS; ++index) {
            store_copy_v2_slot_to_slot(&old_store->data.slots[index], &s_store.data.slots[index]);
        }
        s_store.data.credential_count = old_store->data.credential_count;
    } else if (s_store.data.magic == MEOWKEY_STORE_MAGIC && s_store.data.version == 3u) {
        if (!store_import_legacy_image_at_offset(MEOWKEY_STORE_OFFSET)) {
            (void)store_import_v3_medium_image_at_offset(MEOWKEY_STORE_OFFSET);
        }
    } else if (s_store.data.magic == MEOWKEY_STORE_MAGIC && s_store.data.version == 4u) {
        (void)store_import_v4_image_at_offset(MEOWKEY_STORE_OFFSET);
    } else if (s_store.data.magic != MEOWKEY_STORE_MAGIC || s_store.data.version != MEOWKEY_STORE_VERSION) {
        if (!store_load_from_legacy_if_present()) {
            store_reset_image();
        }
    }

    (void)store_sanitize_slots();
    sign_count_journal_load_if_present();

    s_store_loaded = true;
}

void meowkey_store_init(void) {
    store_load_if_needed();
}

bool meowkey_store_add_credential(const meowkey_credential_record_t *record, uint32_t *slot_index) {
    size_t index;

    store_load_if_needed();
    for (index = 0; index < MEOWKEY_STORE_MAX_CREDENTIALS; ++index) {
        if (!store_slot_is_valid(&s_store.data.slots[index])) {
            store_copy_record_to_slot(record, &s_store.data.slots[index]);
            s_store.data.credential_count += 1u;
            if (!store_commit()) {
                return false;
            }
            if (slot_index) {
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
    for (index = 0; index < MEOWKEY_STORE_MAX_CREDENTIALS; ++index) {
        meowkey_store_slot_t *slot = &s_store.data.slots[index];
        if (!store_slot_is_valid(slot) || slot->credential_id_length != credential_id_length) {
            continue;
        }
        if (memcmp(slot->credential_id, credential_id, credential_id_length) == 0) {
            if (record) {
                store_copy_slot_to_record(slot, record);
            }
            if (slot_index) {
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
    for (index = 0; index < MEOWKEY_STORE_MAX_CREDENTIALS; ++index) {
        meowkey_store_slot_t *slot = &s_store.data.slots[index];
        if (!store_slot_is_valid(slot) || slot->rp_id_length != rp_id_length) {
            continue;
        }
        if (memcmp(slot->rp_id, rp_id, rp_id_length) == 0) {
            if (record) {
                store_copy_slot_to_record(slot, record);
            }
            if (slot_index) {
                *slot_index = (uint32_t)index;
            }
            return true;
        }
    }
    return false;
}

uint32_t meowkey_store_get_credential_count(void) {
    store_load_if_needed();
    return s_store.data.credential_count;
}

uint32_t meowkey_store_get_credential_capacity(void) {
    return MEOWKEY_STORE_MAX_CREDENTIALS;
}

bool meowkey_store_get_credential_by_slot(uint32_t slot_index, meowkey_credential_record_t *record) {
    store_load_if_needed();
    if (slot_index >= MEOWKEY_STORE_MAX_CREDENTIALS || !store_slot_is_valid(&s_store.data.slots[slot_index])) {
        return false;
    }
    if (record) {
        store_copy_slot_to_record(&s_store.data.slots[slot_index], record);
    }
    return true;
}

bool meowkey_store_update_sign_count(uint32_t slot_index, uint32_t sign_count) {
    store_load_if_needed();
    if (slot_index >= MEOWKEY_STORE_MAX_CREDENTIALS || !store_slot_is_valid(&s_store.data.slots[slot_index])) {
        return false;
    }
    s_store.data.slots[slot_index].sign_count = sign_count;
    return sign_count_journal_append((uint16_t)slot_index, sign_count);
}

bool meowkey_store_clear_credentials(void) {
    store_load_if_needed();
    memset(s_store.data.slots, 0, sizeof(s_store.data.slots));
    s_store.data.credential_count = 0u;
    if (!store_commit()) {
        return false;
    }
    return sign_count_journal_erase_and_initialize();
}

void meowkey_store_get_pin_state(meowkey_pin_state_t *state) {
    store_load_if_needed();
    memset(state, 0, sizeof(*state));
    state->configured = s_store.data.pin_configured != 0u;
    state->retries = s_store.data.pin_retries;
    memcpy(state->pin_hash, s_store.data.pin_hash, sizeof(state->pin_hash));
    memcpy(state->pin_token, s_store.data.pin_token, sizeof(state->pin_token));
}

bool meowkey_store_set_pin_state(const meowkey_pin_state_t *state) {
    store_load_if_needed();
    s_store.data.pin_configured = (uint8_t)(state->configured ? 1u : 0u);
    s_store.data.pin_retries = state->retries;
    memcpy(s_store.data.pin_hash, state->pin_hash, sizeof(s_store.data.pin_hash));
    memcpy(s_store.data.pin_token, state->pin_token, sizeof(s_store.data.pin_token));
    return store_commit();
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
        "store-version=%lu data-region=%luKB sign-journal=%lu/%lu entries journal-region=%luKB\n",
        (unsigned long)MEOWKEY_STORE_VERSION,
        (unsigned long)(MEOWKEY_STORE_SIZE / 1024u),
        (unsigned long)s_sign_count_journal.next_entry_index,
        (unsigned long)MEOWKEY_SIGN_COUNT_JOURNAL_ENTRY_CAPACITY,
        (unsigned long)(MEOWKEY_STORE_SIGN_COUNT_JOURNAL_SIZE / 1024u));
}
