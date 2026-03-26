#ifndef MEOWKEY_CTAP_HID_H
#define MEOWKEY_CTAP_HID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CTAP_HID_PACKET_SIZE 64u
#define CTAP_HID_MAX_MESSAGE_SIZE 1024u
#define CTAP_HID_FIDO_INSTANCE 0u
#define CTAP_HID_DEBUG_INSTANCE 1u

void ctap_hid_init(void);
void ctap_hid_task(void);
void ctap_hid_handle_report(uint8_t instance, uint8_t const *report, size_t report_length);

bool ctap_hid_is_configured(void);

#endif
