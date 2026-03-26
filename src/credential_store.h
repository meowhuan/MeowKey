#ifndef MEOWKEY_CREDENTIAL_STORE_H
#define MEOWKEY_CREDENTIAL_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MEOWKEY_CREDENTIAL_ID_SIZE 32u
#define MEOWKEY_PRIVATE_KEY_SIZE 32u
#define MEOWKEY_RP_ID_SIZE 64u
#define MEOWKEY_USER_ID_SIZE 64u
#define MEOWKEY_USER_NAME_SIZE 64u
#define MEOWKEY_DISPLAY_NAME_SIZE 96u
#define MEOWKEY_CRED_RANDOM_SIZE 32u

typedef struct {
    uint32_t sign_count;
    bool discoverable;
    bool cred_random_ready;
    size_t credential_id_length;
    size_t private_key_length;
    size_t rp_id_length;
    size_t user_id_length;
    size_t user_name_length;
    size_t display_name_length;
    uint8_t credential_id[MEOWKEY_CREDENTIAL_ID_SIZE];
    uint8_t private_key[MEOWKEY_PRIVATE_KEY_SIZE];
    uint8_t cred_random_with_uv[MEOWKEY_CRED_RANDOM_SIZE];
    uint8_t cred_random_without_uv[MEOWKEY_CRED_RANDOM_SIZE];
    char rp_id[MEOWKEY_RP_ID_SIZE];
    uint8_t user_id[MEOWKEY_USER_ID_SIZE];
    char user_name[MEOWKEY_USER_NAME_SIZE];
    char display_name[MEOWKEY_DISPLAY_NAME_SIZE];
} meowkey_credential_record_t;

typedef struct {
    bool configured;
    uint8_t retries;
    uint8_t pin_hash[16];
} meowkey_pin_state_t;

typedef struct {
    uint8_t source;
    int8_t gpio_pin;
    uint8_t gpio_active_low;
    uint8_t tap_count;
    uint16_t gesture_window_ms;
    uint16_t request_timeout_ms;
} meowkey_user_presence_config_t;

void meowkey_store_init(void);
bool meowkey_store_add_credential(const meowkey_credential_record_t *record, uint32_t *slot_index);
bool meowkey_store_find_by_credential_id(const uint8_t *credential_id,
                                         size_t credential_id_length,
                                         meowkey_credential_record_t *record,
                                         uint32_t *slot_index);
bool meowkey_store_find_by_rp_id(const char *rp_id,
                                 meowkey_credential_record_t *record,
                                 uint32_t *slot_index);
uint32_t meowkey_store_get_credential_count(void);
uint32_t meowkey_store_get_credential_capacity(void);
bool meowkey_store_get_credential_by_slot(uint32_t slot_index, meowkey_credential_record_t *record);
bool meowkey_store_update_sign_count(uint32_t slot_index, uint32_t sign_count);
bool meowkey_store_clear_credentials(void);
void meowkey_store_get_pin_state(meowkey_pin_state_t *state);
bool meowkey_store_set_pin_state(const meowkey_pin_state_t *state);
void meowkey_store_get_user_presence_config(meowkey_user_presence_config_t *config);
bool meowkey_store_set_user_presence_config(const meowkey_user_presence_config_t *config);
uint32_t meowkey_store_get_format_version(void);
size_t meowkey_store_write_summary(char *output, size_t output_capacity);

#endif
