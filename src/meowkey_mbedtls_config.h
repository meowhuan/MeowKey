#ifndef MEOWKEY_MBEDTLS_CONFIG_H
#define MEOWKEY_MBEDTLS_CONFIG_H

#include "mbedtls/mbedtls_config.h"

/*
 * MeowKey only uses a narrow crypto subset for CTAP/WebAuthn.
 * Disable host-oriented timing and time/date support to keep the
 * pico-sdk bundled mbedtls sources buildable on-device.
 */
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_HAVE_TIME
#undef MBEDTLS_HAVE_TIME_DATE
#undef MBEDTLS_SHA3_C
#undef MBEDTLS_SHAKE256_C

#define MBEDTLS_NO_PLATFORM_ENTROPY

#endif
