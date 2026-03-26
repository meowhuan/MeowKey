#ifndef MEOWKEY_CTAP2_H
#define MEOWKEY_CTAP2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool ctap2_handle_cbor(uint8_t const *request,
                       size_t request_length,
                       uint8_t *response,
                       size_t *response_length);

#endif

