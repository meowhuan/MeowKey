#ifndef MEOWKEY_SECURITY_STATUS_H
#define MEOWKEY_SECURITY_STATUS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool signed_boot_enabled;
    bool anti_rollback_enabled;
    uint32_t anti_rollback_version;

    bool boot_flags0_available;
    uint32_t boot_flags0_raw;
    bool rollback_required;
    bool flash_boot_disabled;
    bool picoboot_disabled;

    bool boot_flags1_available;
    uint32_t boot_flags1_raw;
    uint32_t key_valid_mask;
    uint32_t key_invalid_mask;
} meowkey_security_status_t;

void meowkey_security_status_get(meowkey_security_status_t *status);
void meowkey_security_status_log_summary(void);

#endif
