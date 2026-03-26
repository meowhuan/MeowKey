#include "board_probe.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"

enum {
    PROBE_GPIO_PIN_COUNT = 30,
    PROBE_GPIO_SAMPLE_DELAY_US = 50,
    PROBE_I2C_TIMEOUT_US = 1000,
    PROBE_I2C_EEPROM_SAMPLE_LENGTH = 8,
    PROBE_I2C_HEADER_SCHEMA_VERSION = 1,
    PROBE_I2C_MAX_DEVICES_PER_PAIR = 16,
    PROBE_I2C_MAX_EEPROM_CANDIDATES = 16,
};

typedef struct {
    uint8_t pin;
    bool raw_level;
    bool pull_up_level;
    bool pull_down_level;
} gpio_pin_probe_t;

typedef struct {
    uint8_t instance;
    uint8_t sda_pin;
    uint8_t scl_pin;
} i2c_pin_pair_t;

typedef struct {
    i2c_pin_pair_t pair;
    uint8_t devices[PROBE_I2C_MAX_DEVICES_PER_PAIR];
    size_t device_count;
} i2c_pair_probe_t;

typedef struct {
    i2c_pin_pair_t pair;
    uint8_t address;
    bool read_with_8bit_offset;
    bool read_with_16bit_offset;
    uint8_t address_width;
    uint8_t raw[PROBE_I2C_EEPROM_SAMPLE_LENGTH];
    size_t raw_length;
} i2c_eeprom_candidate_t;

static const i2c_pin_pair_t k_i2c_pairs[] = {
    {0u, 0u, 1u},
    {1u, 2u, 3u},
    {0u, 4u, 5u},
    {1u, 6u, 7u},
    {0u, 8u, 9u},
    {1u, 10u, 11u},
    {0u, 12u, 13u},
    {1u, 14u, 15u},
    {0u, 16u, 17u},
    {1u, 18u, 19u},
    {0u, 20u, 21u},
    {1u, 22u, 23u},
    {0u, 24u, 25u},
    {1u, 26u, 27u},
    {0u, 28u, 29u},
};

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

static const char *gpio_classification(const gpio_pin_probe_t *probe) {
    if (probe->pull_up_level && probe->pull_down_level) {
        return "forced-high";
    }
    if (!probe->pull_up_level && !probe->pull_down_level) {
        return "forced-low";
    }
    if (probe->pull_up_level && !probe->pull_down_level) {
        return "switchable";
    }
    return "unstable";
}

static void sample_gpio_pin(uint8_t pin, gpio_pin_probe_t *probe) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);

    gpio_disable_pulls(pin);
    sleep_us(PROBE_GPIO_SAMPLE_DELAY_US);
    probe->raw_level = gpio_get(pin);

    gpio_pull_up(pin);
    sleep_us(PROBE_GPIO_SAMPLE_DELAY_US);
    probe->pull_up_level = gpio_get(pin);

    gpio_disable_pulls(pin);
    gpio_pull_down(pin);
    sleep_us(PROBE_GPIO_SAMPLE_DELAY_US);
    probe->pull_down_level = gpio_get(pin);

    gpio_disable_pulls(pin);
}

static bool i2c_probe_address(i2c_inst_t *i2c, uint8_t address) {
    uint8_t scratch = 0u;
    int read_result = i2c_read_timeout_us(
        i2c,
        address,
        &scratch,
        1u,
        false,
        PROBE_I2C_TIMEOUT_US);
    return read_result >= 0;
}

static bool i2c_try_eeprom_read(i2c_inst_t *i2c,
                                uint8_t address,
                                uint8_t address_width,
                                uint8_t *output,
                                size_t output_length) {
    uint8_t offset_buffer[2] = {0u, 0u};
    int write_result;
    int read_result;

    write_result = i2c_write_timeout_us(
        i2c,
        address,
        offset_buffer,
        address_width,
        true,
        PROBE_I2C_TIMEOUT_US);
    if (write_result != address_width) {
        return false;
    }

    read_result = i2c_read_timeout_us(
        i2c,
        address,
        output,
        output_length,
        false,
        PROBE_I2C_TIMEOUT_US);
    return read_result == (int)output_length;
}

static void reset_i2c_pair(const i2c_pin_pair_t *pair) {
    gpio_set_function(pair->sda_pin, GPIO_FUNC_SIO);
    gpio_set_function(pair->scl_pin, GPIO_FUNC_SIO);
    gpio_set_dir(pair->sda_pin, GPIO_IN);
    gpio_set_dir(pair->scl_pin, GPIO_IN);
    gpio_disable_pulls(pair->sda_pin);
    gpio_disable_pulls(pair->scl_pin);
}

static void scan_i2c_pair(const i2c_pin_pair_t *pair,
                          i2c_pair_probe_t *pair_probe,
                          i2c_eeprom_candidate_t *candidates,
                          size_t *candidate_count) {
    i2c_inst_t *i2c = pair->instance == 0u ? i2c0 : i2c1;
    uint8_t address;

    memset(pair_probe, 0, sizeof(*pair_probe));
    pair_probe->pair = *pair;

    i2c_init(i2c, 100000u);
    gpio_set_function(pair->sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(pair->scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(pair->sda_pin);
    gpio_pull_up(pair->scl_pin);
    sleep_us(100u);

    for (address = 0x08u; address <= 0x77u; ++address) {
        bool detected = i2c_probe_address(i2c, address);

        if (!detected) {
            continue;
        }

        if (pair_probe->device_count < PROBE_I2C_MAX_DEVICES_PER_PAIR) {
            pair_probe->devices[pair_probe->device_count++] = address;
        }

        if (*candidate_count < PROBE_I2C_MAX_EEPROM_CANDIDATES) {
            i2c_eeprom_candidate_t *candidate = &candidates[*candidate_count];
            uint8_t sample_8bit[PROBE_I2C_EEPROM_SAMPLE_LENGTH];
            uint8_t sample_16bit[PROBE_I2C_EEPROM_SAMPLE_LENGTH];

            memset(candidate, 0, sizeof(*candidate));
            candidate->pair = *pair;
            candidate->address = address;
            candidate->read_with_8bit_offset = i2c_try_eeprom_read(
                i2c,
                address,
                1u,
                sample_8bit,
                sizeof(sample_8bit));
            candidate->read_with_16bit_offset = i2c_try_eeprom_read(
                i2c,
                address,
                2u,
                sample_16bit,
                sizeof(sample_16bit));

            if (candidate->read_with_16bit_offset) {
                candidate->address_width = 2u;
                candidate->raw_length = sizeof(sample_16bit);
                memcpy(candidate->raw, sample_16bit, sizeof(sample_16bit));
                *candidate_count += 1u;
            } else if (candidate->read_with_8bit_offset) {
                candidate->address_width = 1u;
                candidate->raw_length = sizeof(sample_8bit);
                memcpy(candidate->raw, sample_8bit, sizeof(sample_8bit));
                *candidate_count += 1u;
            }
        }
    }

    i2c_deinit(i2c);
    reset_i2c_pair(pair);
}

static void emit_gpio_report(void) {
    gpio_pin_probe_t probes[PROBE_GPIO_PIN_COUNT];
    size_t index;
    bool first_forced_pin = true;

    for (index = 0u; index < PROBE_GPIO_PIN_COUNT; ++index) {
        probes[index].pin = (uint8_t)index;
        sample_gpio_pin((uint8_t)index, &probes[index]);
    }

    printf("  \"gpio\": {\n");
    printf("    \"pinCount\": %u,\n", PROBE_GPIO_PIN_COUNT);
    printf("    \"forcedPinCandidates\": [");
    for (index = 0u; index < PROBE_GPIO_PIN_COUNT; ++index) {
        const char *classification = gpio_classification(&probes[index]);
        if (strcmp(classification, "forced-high") != 0 && strcmp(classification, "forced-low") != 0) {
            continue;
        }
        printf("%s%u", first_forced_pin ? "" : ", ", probes[index].pin);
        first_forced_pin = false;
    }
    printf("],\n");
    printf("    \"pins\": [\n");
    for (index = 0u; index < PROBE_GPIO_PIN_COUNT; ++index) {
        const char *classification = gpio_classification(&probes[index]);
        printf("      {\"pin\": %u, \"raw\": %s, \"pullUp\": %s, \"pullDown\": %s, \"classification\": \"%s\"}%s\n",
               probes[index].pin,
               probes[index].raw_level ? "true" : "false",
               probes[index].pull_up_level ? "true" : "false",
               probes[index].pull_down_level ? "true" : "false",
               classification,
               index + 1u == PROBE_GPIO_PIN_COUNT ? "" : ",");
    }
    printf("    ]\n");
    printf("  }");
}

static const char *suggested_preset_label(const i2c_eeprom_candidate_t *candidate) {
    if (candidate->address_width == 1u) {
        return "24c02";
    }
    return "custom";
}

static void emit_i2c_report(void) {
    i2c_pair_probe_t pair_probes[sizeof(k_i2c_pairs) / sizeof(k_i2c_pairs[0])];
    i2c_eeprom_candidate_t candidates[PROBE_I2C_MAX_EEPROM_CANDIDATES];
    size_t pair_index;
    size_t candidate_count = 0u;

    memset(pair_probes, 0, sizeof(pair_probes));
    memset(candidates, 0, sizeof(candidates));

    for (pair_index = 0u; pair_index < (sizeof(k_i2c_pairs) / sizeof(k_i2c_pairs[0])); ++pair_index) {
        scan_i2c_pair(&k_i2c_pairs[pair_index], &pair_probes[pair_index], candidates, &candidate_count);
    }

    printf("  \"i2c\": {\n");
    printf("    \"pairs\": [\n");
    for (pair_index = 0u; pair_index < (sizeof(k_i2c_pairs) / sizeof(k_i2c_pairs[0])); ++pair_index) {
        size_t device_index;
        printf("      {\"instance\": %u, \"sdaPin\": %u, \"sclPin\": %u, \"devices\": [",
               pair_probes[pair_index].pair.instance,
               pair_probes[pair_index].pair.sda_pin,
               pair_probes[pair_index].pair.scl_pin);
        for (device_index = 0u; device_index < pair_probes[pair_index].device_count; ++device_index) {
            printf("%s\"0x%02x\"",
                   device_index == 0u ? "" : ", ",
                   pair_probes[pair_index].devices[device_index]);
        }
        printf("]}%s\n",
               pair_index + 1u == (sizeof(k_i2c_pairs) / sizeof(k_i2c_pairs[0])) ? "" : ",");
    }
    printf("    ],\n");
    printf("    \"eepromCandidates\": [\n");
    for (pair_index = 0u; pair_index < candidate_count; ++pair_index) {
        char raw_hex[PROBE_I2C_EEPROM_SAMPLE_LENGTH * 2u + 1u];
        bytes_to_hex(candidates[pair_index].raw, candidates[pair_index].raw_length, raw_hex, sizeof(raw_hex));
        printf("      {\"instance\": %u, \"sdaPin\": %u, \"sclPin\": %u, \"address\": \"0x%02x\", \"readWith8BitOffset\": %s, \"readWith16BitOffset\": %s, \"addressWidth\": %u, \"suggestedPreset\": \"%s\", \"rawHex\": \"%s\"}%s\n",
               candidates[pair_index].pair.instance,
               candidates[pair_index].pair.sda_pin,
               candidates[pair_index].pair.scl_pin,
               candidates[pair_index].address,
               candidates[pair_index].read_with_8bit_offset ? "true" : "false",
               candidates[pair_index].read_with_16bit_offset ? "true" : "false",
               candidates[pair_index].address_width,
               suggested_preset_label(&candidates[pair_index]),
               raw_hex,
               pair_index + 1u == candidate_count ? "" : ",");
    }
    printf("    ]\n");
    printf("  }\n");
}

void meowkey_board_probe_emit_report(void) {
    char unique_id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1u];

    pico_get_unique_board_id_string(unique_id, sizeof(unique_id));

    printf("MEOWKEY_PROBE_JSON_BEGIN\n");
    printf("{\n");
    printf("  \"schemaVersion\": %u,\n", PROBE_I2C_HEADER_SCHEMA_VERSION);
    printf("  \"tool\": \"meowkey-probe\",\n");
    printf("  \"uniqueId\": \"%s\",\n", unique_id);
    printf("  \"flashSizeBytes\": %u,\n", (unsigned int)PICO_FLASH_SIZE_BYTES);
    emit_gpio_report();
    printf(",\n");
    emit_i2c_report();
    printf("}\n");
    printf("MEOWKEY_PROBE_JSON_END\n");
    fflush(stdout);
}
