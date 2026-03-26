#include "security_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

void meowkey_security_status_log_summary(void) {
    uint32_t boot_flags0 = 0u;
    uint32_t boot_flags1 = 0u;
    bool have_boot_flags0 = read_otp_row24((uint16_t)OTP_DATA_BOOT_FLAGS0_ROW, &boot_flags0);
    bool have_boot_flags1 = read_otp_row24((uint16_t)OTP_DATA_BOOT_FLAGS1_ROW, &boot_flags1);

    meowkey_diag_logf("security build signed=%u antiRollback=%u rollbackVersion=%lu",
                      MEOWKEY_ENABLE_SIGNED_BOOT,
                      MEOWKEY_ENABLE_ANTI_ROLLBACK,
                      (unsigned long)MEOWKEY_ANTI_ROLLBACK_VERSION);

    if (have_boot_flags0) {
        meowkey_diag_logf("security otp bootFlags0=0x%06lx rollbackRequired=%u flashBootDisabled=%u picobootDisabled=%u",
                          (unsigned long)boot_flags0,
                          (boot_flags0 & OTP_DATA_BOOT_FLAGS0_ROLLBACK_REQUIRED_BITS) != 0u ? 1u : 0u,
                          (boot_flags0 & OTP_DATA_BOOT_FLAGS0_DISABLE_FLASH_BOOT_BITS) != 0u ? 1u : 0u,
                          (boot_flags0 & OTP_DATA_BOOT_FLAGS0_DISABLE_BOOTSEL_USB_PICOBOOT_IFC_BITS) != 0u ? 1u : 0u);
    } else {
        meowkey_diag_logf("security otp bootFlags0=unavailable");
    }

    if (have_boot_flags1) {
        meowkey_diag_logf("security otp bootFlags1=0x%06lx keyValidMask=0x%lx keyInvalidMask=0x%lx",
                          (unsigned long)boot_flags1,
                          (unsigned long)(boot_flags1 & OTP_DATA_BOOT_FLAGS1_KEY_VALID_BITS),
                          (unsigned long)((boot_flags1 & OTP_DATA_BOOT_FLAGS1_KEY_INVALID_BITS) >> OTP_DATA_BOOT_FLAGS1_KEY_INVALID_LSB));
    } else {
        meowkey_diag_logf("security otp bootFlags1=unavailable");
    }
}
