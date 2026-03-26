#include "user_presence.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bsp/board_api.h"
#include "ctap_hid.h"
#include "diagnostics.h"
#include "hardware/gpio.h"
#include "meowkey_build_config.h"
#include "pico/stdlib.h"

enum {
    CTAP2_STATUS_OK = 0x00,
    CTAP2_ERR_UNSUPPORTED_OPTION = 0x2b,
    CTAP2_ERR_USER_ACTION_TIMEOUT = 0x2f,
};

enum {
    GPIO_PIN_UNINITIALIZED = -128,
    USER_PRESENCE_POLL_INTERVAL_MS = 5u,
};

static meowkey_user_presence_config_t s_cached_config;
static bool s_cached_config_ready = false;
static int8_t s_configured_gpio_pin = GPIO_PIN_UNINITIALIZED;

static void load_default_config(meowkey_user_presence_config_t *config) {
    memset(config, 0, sizeof(*config));
    config->source = MEOWKEY_DEFAULT_UP_SOURCE;
    config->gpio_pin = (int8_t)MEOWKEY_DEFAULT_UP_GPIO_PIN;
    config->gpio_active_low = (uint8_t)MEOWKEY_DEFAULT_UP_GPIO_ACTIVE_LOW;
    config->tap_count = (uint8_t)MEOWKEY_DEFAULT_UP_TAP_COUNT;
    config->gesture_window_ms = (uint16_t)MEOWKEY_DEFAULT_UP_GESTURE_WINDOW_MS;
    config->request_timeout_ms = (uint16_t)MEOWKEY_DEFAULT_UP_REQUEST_TIMEOUT_MS;
}

static bool config_is_valid(const meowkey_user_presence_config_t *config) {
    if (config == NULL) {
        return false;
    }
    if (config->source > MEOWKEY_USER_PRESENCE_SOURCE_GPIO) {
        return false;
    }
    if (config->source == MEOWKEY_USER_PRESENCE_SOURCE_GPIO &&
        (config->gpio_pin < 0 || config->gpio_pin > 47)) {
        return false;
    }
    if (config->tap_count < 1u || config->tap_count > 4u) {
        return false;
    }
    if (config->gesture_window_ms < 100u || config->gesture_window_ms > 5000u) {
        return false;
    }
    if (config->request_timeout_ms < 500u || config->request_timeout_ms > 30000u) {
        return false;
    }
    return true;
}

static void configure_gpio_source(const meowkey_user_presence_config_t *config) {
    if (s_configured_gpio_pin >= 0) {
        gpio_disable_pulls((uint)s_configured_gpio_pin);
        s_configured_gpio_pin = GPIO_PIN_UNINITIALIZED;
    }
    if (config->source != MEOWKEY_USER_PRESENCE_SOURCE_GPIO || config->gpio_pin < 0) {
        return;
    }

    gpio_init((uint)config->gpio_pin);
    gpio_set_dir((uint)config->gpio_pin, GPIO_IN);
    gpio_disable_pulls((uint)config->gpio_pin);
    if (config->gpio_active_low != 0u) {
        gpio_pull_up((uint)config->gpio_pin);
    } else {
        gpio_pull_down((uint)config->gpio_pin);
    }
    s_configured_gpio_pin = config->gpio_pin;
}

static void cache_config_if_needed(void) {
    meowkey_user_presence_config_t config;

    if (s_cached_config_ready) {
        return;
    }

    load_default_config(&config);
    meowkey_store_get_user_presence_config(&config);
    if (!config_is_valid(&config)) {
        load_default_config(&config);
    }

    s_cached_config = config;
    configure_gpio_source(&s_cached_config);
    s_cached_config_ready = true;
}

static bool read_presence_signal(const meowkey_user_presence_config_t *config) {
    if (config->source == MEOWKEY_USER_PRESENCE_SOURCE_BOOTSEL) {
        return board_button_read() != 0u;
    }
    if (config->source == MEOWKEY_USER_PRESENCE_SOURCE_GPIO && config->gpio_pin >= 0) {
        bool level = gpio_get((uint)config->gpio_pin);
        return config->gpio_active_low != 0u ? !level : level;
    }
    return false;
}

static uint32_t elapsed_ms(uint32_t start_ms, uint32_t now_ms) {
    return now_ms - start_ms;
}

void meowkey_user_presence_init(void) {
    s_cached_config_ready = false;
    cache_config_if_needed();
}

bool meowkey_user_presence_is_enabled(void) {
    cache_config_if_needed();
    return s_cached_config.source != MEOWKEY_USER_PRESENCE_SOURCE_NONE;
}

uint8_t meowkey_user_presence_wait_for_confirmation(const char *reason) {
    uint32_t start_ms;
    uint32_t first_tap_ms = 0u;
    uint8_t taps_seen = 0u;
    bool last_pressed;

    cache_config_if_needed();
    if (s_cached_config.source == MEOWKEY_USER_PRESENCE_SOURCE_NONE) {
        meowkey_diag_logf("userPresence bypassed for %s", reason != NULL ? reason : "operation");
        return CTAP2_STATUS_OK;
    }

    start_ms = board_millis();
    last_pressed = read_presence_signal(&s_cached_config);
    meowkey_diag_logf("userPresence waiting source=%u taps=%u reason=%s",
                      s_cached_config.source,
                      s_cached_config.tap_count,
                      reason != NULL ? reason : "operation");

    while (elapsed_ms(start_ms, board_millis()) < s_cached_config.request_timeout_ms) {
        uint32_t now_ms = board_millis();
        bool pressed = read_presence_signal(&s_cached_config);

        ctap_hid_keepalive_up_needed();

        if (pressed && !last_pressed) {
            if (s_cached_config.tap_count <= 1u) {
                meowkey_diag_logf("userPresence confirmed reason=%s", reason != NULL ? reason : "operation");
                return CTAP2_STATUS_OK;
            }
            if (taps_seen == 0u || elapsed_ms(first_tap_ms, now_ms) > s_cached_config.gesture_window_ms) {
                taps_seen = 1u;
                first_tap_ms = now_ms;
            } else {
                taps_seen += 1u;
                if (taps_seen >= s_cached_config.tap_count) {
                    meowkey_diag_logf("userPresence confirmed reason=%s", reason != NULL ? reason : "operation");
                    return CTAP2_STATUS_OK;
                }
            }
        }

        if (taps_seen > 0u && elapsed_ms(first_tap_ms, now_ms) > s_cached_config.gesture_window_ms) {
            taps_seen = 0u;
        }

        last_pressed = pressed;
        sleep_ms(USER_PRESENCE_POLL_INTERVAL_MS);
    }

    meowkey_diag_logf("userPresence timeout reason=%s", reason != NULL ? reason : "operation");
    return CTAP2_ERR_USER_ACTION_TIMEOUT;
}

void meowkey_user_presence_get_config(meowkey_user_presence_config_t *config) {
    cache_config_if_needed();
    if (config != NULL) {
        *config = s_cached_config;
    }
}

bool meowkey_user_presence_set_config(const meowkey_user_presence_config_t *config) {
    if (!config_is_valid(config) || !meowkey_store_set_user_presence_config(config)) {
        return false;
    }

    s_cached_config = *config;
    configure_gpio_source(&s_cached_config);
    s_cached_config_ready = true;
    meowkey_diag_logf("userPresence config updated source=%u gpio=%d taps=%u timeoutMs=%u",
                      s_cached_config.source,
                      (int)s_cached_config.gpio_pin,
                      s_cached_config.tap_count,
                      s_cached_config.request_timeout_ms);
    return true;
}
