#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp/board_api.h"
#include "board_id.h"
#include "ctap_hid.h"
#include "diagnostics.h"
#include "pico/stdlib.h"
#include "pico/status_led.h"
#include "security_status.h"
#include "tusb.h"
#include "user_presence.h"

enum {
    BLINK_NOT_MOUNTED_MS = 250,
    BLINK_MOUNTED_MS = 1000,
    BLINK_CONFIGURED_MS = 100,
    BLINK_SUSPENDED_MS = 2500,
};

enum {
    LED_COLOR_BLUE = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(0x00, 0x00, 0x08),
    LED_COLOR_YELLOW = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(0x08, 0x06, 0x00),
    LED_COLOR_GREEN = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(0x00, 0x08, 0x00),
    LED_COLOR_WHITE = PICO_COLORED_STATUS_LED_COLOR_FROM_RGB(0x06, 0x06, 0x06),
};

#ifndef MEOWKEY_LED_COLOR_DISCONNECTED
#define MEOWKEY_LED_COLOR_DISCONNECTED LED_COLOR_BLUE
#endif

#ifndef MEOWKEY_LED_COLOR_ENUMERATED
#define MEOWKEY_LED_COLOR_ENUMERATED LED_COLOR_YELLOW
#endif

#ifndef MEOWKEY_LED_COLOR_ACTIVE
#define MEOWKEY_LED_COLOR_ACTIVE LED_COLOR_GREEN
#endif

#ifndef MEOWKEY_LED_COLOR_SUSPENDED
#define MEOWKEY_LED_COLOR_SUSPENDED LED_COLOR_WHITE
#endif

#ifndef MEOWKEY_LED_ENABLE_ACTIVE_STATE
#define MEOWKEY_LED_ENABLE_ACTIVE_STATE 1
#endif

static uint32_t s_blink_interval_ms = BLINK_NOT_MOUNTED_MS;
static uint32_t s_led_color = MEOWKEY_LED_COLOR_DISCONNECTED;

static void meowkey_led_init(void) {
    status_led_init();
    if (colored_status_led_supported()) {
        // WS2812 state can survive MCU reset until another frame is written.
        (void)colored_status_led_set_on_with_color(0u);
        (void)colored_status_led_set_state(false);
    } else if (status_led_supported()) {
        (void)status_led_set_state(false);
    }
}

static void meowkey_led_write(bool on) {
    if (colored_status_led_supported()) {
        if (on) {
            colored_status_led_set_on_with_color(s_led_color);
        } else {
            colored_status_led_set_state(false);
        }
    } else {
        status_led_set_state(on);
    }
}

static void meowkey_led_set_color(uint32_t color) {
    s_led_color = color;
    if (colored_status_led_supported() && status_led_get_state()) {
        colored_status_led_set_state(false);
        colored_status_led_set_on_with_color(s_led_color);
    }
}

static void led_blinking_task(void) {
    static uint32_t last_toggle_ms = 0;
    static bool led_state = false;
    uint32_t now_ms = board_millis();

    if ((now_ms - last_toggle_ms) < s_blink_interval_ms) {
        return;
    }

    last_toggle_ms = now_ms;
    led_state = !led_state;
    meowkey_led_write(led_state);
}

static void sync_blink_interval(void) {
    if (tud_suspended()) {
        s_blink_interval_ms = BLINK_SUSPENDED_MS;
        meowkey_led_set_color(MEOWKEY_LED_COLOR_SUSPENDED);
    } else if (!tud_mounted()) {
        s_blink_interval_ms = BLINK_NOT_MOUNTED_MS;
        meowkey_led_set_color(MEOWKEY_LED_COLOR_DISCONNECTED);
    } else if (MEOWKEY_LED_ENABLE_ACTIVE_STATE && ctap_hid_is_configured()) {
        s_blink_interval_ms = BLINK_CONFIGURED_MS;
        meowkey_led_set_color(MEOWKEY_LED_COLOR_ACTIVE);
    } else {
        s_blink_interval_ms = BLINK_MOUNTED_MS;
        meowkey_led_set_color(MEOWKEY_LED_COLOR_ENUMERATED);
    }
}

int main(void) {
    stdio_init_all();
    board_init();
    meowkey_led_init();
    meowkey_diag_init();
    meowkey_security_status_log_summary();
    meowkey_board_id_init();
    meowkey_board_id_log_summary();
    meowkey_user_presence_init();
    ctap_hid_init();
    tusb_init();

    while (true) {
        tud_task();
        ctap_hid_task();
        sync_blink_interval();
        led_blinking_task();
    }
}

void tud_mount_cb(void) {
    sync_blink_interval();
}

void tud_umount_cb(void) {
    ctap_hid_init();
    sync_blink_interval();
}

void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    sync_blink_interval();
}

void tud_resume_cb(void) {
    sync_blink_interval();
}

uint16_t tud_hid_get_report_cb(uint8_t instance,
                               uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer,
                               uint16_t request_length) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)request_length;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer,
                           uint16_t buffer_size) {
    (void)report_id;
    (void)report_type;
    ctap_hid_handle_report(instance, buffer, buffer_size);
}
