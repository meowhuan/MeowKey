#include "security_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "diagnostics.h"
#include "hardware/regs/otp_data.h"
#include "meowkey_build_config.h"
#include "pico/bootrom.h"

static bool read_otp_row24(uint16_t row, uint32_t *value) {
    uint32_t raw = 0u;
    otp_cmd_t command = {
        .flags = row,
    };
    int status;

    if (value == NULL) {
        return false;
    }

    status = rom_func_otp_access((uint8_t *)&raw, sizeof(raw), command);
    if (status != BOOTROM_OK) {
        return false;
    }

    *value = raw & 0x00ffffffu;
    return true;
}

void meowkey_security_status_get(meowkey_security_status_t *status) {
    uint32_t boot_flags0 = 0u;
    uint32_t boot_flags1 = 0u;

    if (status == NULL) {
        return;
    }

    memset(status, 0, sizeof(*status));
    status->signed_boot_enabled = MEOWKEY_ENABLE_SIGNED_BOOT != 0;
    status->anti_rollback_enabled = MEOWKEY_ENABLE_ANTI_ROLLBACK != 0;
    status->anti_rollback_version = MEOWKEY_ANTI_ROLLBACK_VERSION;

    status->boot_flags0_available = read_otp_row24((uint16_t)OTP_DATA_BOOT_FLAGS0_ROW, &boot_flags0);
    if (status->boot_flags0_available) {
        status->boot_flags0_raw = boot_flags0;
        status->rollback_required = (boot_flags0 & OTP_DATA_BOOT_FLAGS0_ROLLBACK_REQUIRED_BITS) != 0u;
        status->flash_boot_disabled = (boot_flags0 & OTP_DATA_BOOT_FLAGS0_DISABLE_FLASH_BOOT_BITS) != 0u;
        status->picoboot_disabled = (boot_flags0 & OTP_DATA_BOOT_FLAGS0_DISABLE_BOOTSEL_USB_PICOBOOT_IFC_BITS) != 0u;
    }

    status->boot_flags1_available = read_otp_row24((uint16_t)OTP_DATA_BOOT_FLAGS1_ROW, &boot_flags1);
    if (status->boot_flags1_available) {
        status->boot_flags1_raw = boot_flags1;
        status->key_valid_mask = boot_flags1 & OTP_DATA_BOOT_FLAGS1_KEY_VALID_BITS;
        status->key_invalid_mask =
            (boot_flags1 & OTP_DATA_BOOT_FLAGS1_KEY_INVALID_BITS) >> OTP_DATA_BOOT_FLAGS1_KEY_INVALID_LSB;
    }
}

void meowkey_security_status_log_summary(void) {
    meowkey_security_status_t status;

    meowkey_security_status_get(&status);

    meowkey_diag_logf("security build signed=%u antiRollback=%u rollbackVersion=%lu",
                      status.signed_boot_enabled ? 1u : 0u,
                      status.anti_rollback_enabled ? 1u : 0u,
                      (unsigned long)status.anti_rollback_version);

    if (status.boot_flags0_available) {
        meowkey_diag_logf("security otp bootFlags0=0x%06lx rollbackRequired=%u flashBootDisabled=%u picobootDisabled=%u",
                          (unsigned long)status.boot_flags0_raw,
                          status.rollback_required ? 1u : 0u,
                          status.flash_boot_disabled ? 1u : 0u,
                          status.picoboot_disabled ? 1u : 0u);
    } else {
        meowkey_diag_logf("security otp bootFlags0=unavailable");
    }

    if (status.boot_flags1_available) {
        meowkey_diag_logf("security otp bootFlags1=0x%06lx keyValidMask=0x%lx keyInvalidMask=0x%lx",
                          (unsigned long)status.boot_flags1_raw,
                          (unsigned long)status.key_valid_mask,
                          (unsigned long)status.key_invalid_mask);
    } else {
        meowkey_diag_logf("security otp bootFlags1=unavailable");
    }
}
