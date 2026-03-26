#include "board_id.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "diagnostics.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "meowkey_build_config.h"
#include "pico/stdlib.h"

enum {
    BOARD_ID_GPIO_PIN_COUNT = 4,
    BOARD_ID_MAX_RAW_BYTES = 16,
    BOARD_ID_I2C_TIMEOUT_US = 20000,
};

typedef struct {
    bool initialized;
    bool detected;
    uint32_t code;
    uint8_t raw[BOARD_ID_MAX_RAW_BYTES];
    size_t raw_length;
    char summary[128];
} meowkey_board_id_state_t;

static meowkey_board_id_state_t s_board_id;

static size_t bytes_to_hex(const uint8_t *data, size_t length, char *output, size_t output_capacity) {
    static const char k_hex[] = "0123456789abcdef";
    size_t index;

    if (output_capacity == 0u) {
        return 0u;
    }

    if ((length * 2u + 1u) > output_capacity) {
        length = (output_capacity - 1u) / 2u;
    }

    for (index = 0; index < length; ++index) {
        output[index * 2u] = k_hex[data[index] >> 4u];
        output[index * 2u + 1u] = k_hex[data[index] & 0x0fu];
    }
    output[length * 2u] = '\0';
    return length * 2u;
}

static uint32_t board_id_code_from_bytes(const uint8_t *data, size_t length) {
    size_t index;
    size_t byte_count = length < 4u ? length : 4u;
    uint32_t code = 0u;

    for (index = 0; index < byte_count; ++index) {
        code = (code << 8u) | data[index];
    }
    return code;
}

static void detect_gpio_board_id(void) {
    static const int8_t pins[BOARD_ID_GPIO_PIN_COUNT] = {
        (int8_t)MEOWKEY_BOARD_ID_GPIO_PIN0,
        (int8_t)MEOWKEY_BOARD_ID_GPIO_PIN1,
        (int8_t)MEOWKEY_BOARD_ID_GPIO_PIN2,
        (int8_t)MEOWKEY_BOARD_ID_GPIO_PIN3,
    };
    uint32_t code = 0u;
    size_t configured = 0u;
    size_t index;
    char pins_summary[32];
    size_t pins_length = 0u;

    memset(pins_summary, 0, sizeof(pins_summary));
    for (index = 0; index < BOARD_ID_GPIO_PIN_COUNT; ++index) {
        bool bit_set;
        int8_t pin = pins[index];

        if (pin < 0) {
            continue;
        }

        gpio_init((uint)pin);
        gpio_set_dir((uint)pin, GPIO_IN);
#if MEOWKEY_BOARD_ID_GPIO_ACTIVE_LOW
        gpio_pull_up((uint)pin);
#else
        gpio_pull_down((uint)pin);
#endif
        sleep_us(50);
        bit_set =
#if MEOWKEY_BOARD_ID_GPIO_ACTIVE_LOW
            !gpio_get((uint)pin);
#else
            gpio_get((uint)pin);
#endif
        if (bit_set) {
            code |= (1u << configured);
        }

        if (pins_length < sizeof(pins_summary)) {
            pins_length += (size_t)snprintf(&pins_summary[pins_length],
                                            sizeof(pins_summary) - pins_length,
                                            "%s%d",
                                            configured > 0u ? "," : "",
                                            (int)pin);
        }
        configured += 1u;
    }

    s_board_id.detected = configured > 0u;
    s_board_id.code = code;
    (void)snprintf(s_board_id.summary,
                   sizeof(s_board_id.summary),
                   "board-id mode=gpio detected=%u code=0x%08lx pins=%s active=%s",
                   s_board_id.detected ? 1u : 0u,
                   (unsigned long)s_board_id.code,
                   configured > 0u ? pins_summary : "(none)",
#if MEOWKEY_BOARD_ID_GPIO_ACTIVE_LOW
                   "low"
#else
                   "high"
#endif
    );
}

static void detect_i2c_board_id(void) {
    i2c_inst_t *i2c = MEOWKEY_BOARD_ID_I2C_INSTANCE == 0 ? i2c0 : i2c1;
    uint8_t offset_buffer[2];
    uint8_t raw[BOARD_ID_MAX_RAW_BYTES];
    char raw_hex[BOARD_ID_MAX_RAW_BYTES * 2u + 1u];
    size_t raw_length = MEOWKEY_BOARD_ID_I2C_READ_LENGTH;
    size_t index;
    int write_result;
    int read_result;

    if (raw_length > sizeof(raw)) {
        raw_length = sizeof(raw);
    }

    i2c_init(i2c, 100000u);
    gpio_set_function(MEOWKEY_BOARD_ID_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(MEOWKEY_BOARD_ID_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(MEOWKEY_BOARD_ID_I2C_SDA_PIN);
    gpio_pull_up(MEOWKEY_BOARD_ID_I2C_SCL_PIN);

    for (index = 0; index < (size_t)MEOWKEY_BOARD_ID_I2C_MEM_ADDRESS_WIDTH; ++index) {
        offset_buffer[MEOWKEY_BOARD_ID_I2C_MEM_ADDRESS_WIDTH - index - 1u] =
            (uint8_t)(MEOWKEY_BOARD_ID_I2C_MEM_OFFSET >> (index * 8u));
    }

    write_result = i2c_write_timeout_us(i2c,
                                        MEOWKEY_BOARD_ID_I2C_ADDRESS,
                                        offset_buffer,
                                        (size_t)MEOWKEY_BOARD_ID_I2C_MEM_ADDRESS_WIDTH,
                                        true,
                                        BOARD_ID_I2C_TIMEOUT_US);
    read_result = write_result == MEOWKEY_BOARD_ID_I2C_MEM_ADDRESS_WIDTH
        ? i2c_read_timeout_us(i2c,
                              MEOWKEY_BOARD_ID_I2C_ADDRESS,
                              raw,
                              raw_length,
                              false,
                              BOARD_ID_I2C_TIMEOUT_US)
        : -1;

    if (read_result == (int)raw_length) {
        s_board_id.detected = true;
        s_board_id.raw_length = raw_length;
        memcpy(s_board_id.raw, raw, raw_length);
        s_board_id.code = board_id_code_from_bytes(raw, raw_length);
        (void)bytes_to_hex(raw, raw_length, raw_hex, sizeof(raw_hex));
        (void)snprintf(s_board_id.summary,
                       sizeof(s_board_id.summary),
                       "board-id mode=i2c-eeprom preset=%s addr=0x%02x offset=0x%04x code=0x%08lx raw=%s",
                       MEOWKEY_BOARD_ID_I2C_PRESET,
                       (unsigned int)MEOWKEY_BOARD_ID_I2C_ADDRESS,
                       (unsigned int)MEOWKEY_BOARD_ID_I2C_MEM_OFFSET,
                       (unsigned long)s_board_id.code,
                       raw_hex);
    } else {
        s_board_id.detected = false;
        s_board_id.code = 0u;
        s_board_id.raw_length = 0u;
        (void)snprintf(s_board_id.summary,
                       sizeof(s_board_id.summary),
                       "board-id mode=i2c-eeprom preset=%s addr=0x%02x read-failed write=%d read=%d",
                       MEOWKEY_BOARD_ID_I2C_PRESET,
                       (unsigned int)MEOWKEY_BOARD_ID_I2C_ADDRESS,
                       write_result,
                       read_result);
    }
}

void meowkey_board_id_init(void) {
    if (s_board_id.initialized) {
        return;
    }

    memset(&s_board_id, 0, sizeof(s_board_id));
    switch (MEOWKEY_BOARD_ID_MODE) {
    case MEOWKEY_BOARD_ID_MODE_GPIO:
        detect_gpio_board_id();
        break;

    case MEOWKEY_BOARD_ID_MODE_I2C_EEPROM:
        detect_i2c_board_id();
        break;

    case MEOWKEY_BOARD_ID_MODE_NONE:
    default:
        (void)snprintf(s_board_id.summary,
                       sizeof(s_board_id.summary),
                       "board-id mode=none");
        break;
    }

    s_board_id.initialized = true;
}

bool meowkey_board_id_is_detected(void) {
    meowkey_board_id_init();
    return s_board_id.detected;
}

uint32_t meowkey_board_id_get_code(void) {
    meowkey_board_id_init();
    return s_board_id.code;
}

const char *meowkey_board_id_summary(void) {
    meowkey_board_id_init();
    return s_board_id.summary;
}

void meowkey_board_id_log_summary(void) {
    meowkey_diag_logf("%s", meowkey_board_id_summary());
}
