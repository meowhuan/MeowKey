#ifndef MEOWKEY_WEBAUTHN_H
#define MEOWKEY_WEBAUTHN_H

#include <stddef.h>
#include <stdint.h>

uint8_t meowkey_webauthn_make_credential(const uint8_t *request,
                                         size_t request_length,
                                         uint8_t *response,
                                         size_t *response_length);

uint8_t meowkey_webauthn_get_assertion(const uint8_t *request,
                                       size_t request_length,
                                       uint8_t *response,
                                       size_t *response_length);

#endif

