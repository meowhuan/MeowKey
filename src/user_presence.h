#ifndef MEOWKEY_USER_PRESENCE_H
#define MEOWKEY_USER_PRESENCE_H

#include <stdbool.h>
#include <stdint.h>

#include "credential_store.h"

enum {
    MEOWKEY_USER_PRESENCE_SOURCE_NONE = 0,
    MEOWKEY_USER_PRESENCE_SOURCE_BOOTSEL = 1,
    MEOWKEY_USER_PRESENCE_SOURCE_GPIO = 2,
};

void meowkey_user_presence_init(void);
bool meowkey_user_presence_is_enabled(void);
uint8_t meowkey_user_presence_wait_for_confirmation(const char *reason);
void meowkey_user_presence_get_config(meowkey_user_presence_config_t *config);
bool meowkey_user_presence_set_config(const meowkey_user_presence_config_t *config);

#endif
