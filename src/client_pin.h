#ifndef MEOWKEY_CLIENT_PIN_H
#define MEOWKEY_CLIENT_PIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MEOWKEY_PIN_UV_AUTH_PROTOCOL_1 1u
#define MEOWKEY_SHARED_SECRET_SIZE 32u

bool meowkey_client_pin_is_configured(void);
uint8_t meowkey_client_pin_handle(const uint8_t *request,
                                  size_t request_length,
                                  uint8_t *response,
                                  size_t *response_length);
uint8_t meowkey_client_pin_verify_auth(const uint8_t client_data_hash[32],
                                       uint8_t protocol,
                                       const uint8_t *pin_uv_auth_param,
                                       size_t pin_uv_auth_param_length);
bool meowkey_client_pin_get_shared_secret(const uint8_t peer_public_key[65],
                                          uint8_t output[MEOWKEY_SHARED_SECRET_SIZE]);
bool meowkey_client_pin_encrypt_with_shared_secret(const uint8_t shared_secret[MEOWKEY_SHARED_SECRET_SIZE],
                                                   const uint8_t *input,
                                                   size_t input_length,
                                                   uint8_t *output);
bool meowkey_client_pin_decrypt_with_shared_secret(const uint8_t shared_secret[MEOWKEY_SHARED_SECRET_SIZE],
                                                   const uint8_t *input,
                                                   size_t input_length,
                                                   uint8_t *output);
uint8_t meowkey_client_pin_verify_shared_secret_auth(const uint8_t shared_secret[MEOWKEY_SHARED_SECRET_SIZE],
                                                     const uint8_t *message,
                                                     size_t message_length,
                                                     const uint8_t *provided_param,
                                                     size_t provided_param_length);

#endif
