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
    PROBE_I2C_HEADER_SCHEMA_VERSION = 2,
    PROBE_I2C_MAX_DEVICES_PER_PAIR = 16,
    PROBE_I2C_MAX_EEPROM_CANDIDATES = 16,
    PROBE_USER_PRESENCE_OBSERVATION_WINDOW_MS = 1200,
    PROBE_USER_PRESENCE_SAMPLE_INTERVAL_MS = 20,
    PROBE_USER_PRESENCE_MAX_CANDIDATES = 8,
    PROBE_USER_VERIFICATION_MAX_CANDIDATES = 16,
    PROBE_SECURE_ELEMENT_MAX_CANDIDATES = 24,
    PROBE_SECURE_ELEMENT_MAX_DETECTIONS = 8,
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

typedef struct {
    uint8_t pin;
    bool have_active_low;
    bool active_low;
    bool ambiguous_active_state;
    uint16_t transitions;
} user_presence_candidate_t;

typedef struct {
    i2c_pin_pair_t pair;
    uint8_t address;
    const char *hint;
    const char *confidence;
} user_verification_candidate_t;

typedef struct {
    i2c_pin_pair_t pair;
    uint8_t address;
    const char *family;
    const char *hint;
    const char *confidence;
    const char *probe_method;
} secure_element_candidate_t;

typedef struct {
    i2c_pin_pair_t pair;
    uint8_t address;
    const char *family;
    char model[32];
    char revision_hex[9];
    const char *config_zone_lock;
    const char *data_zone_lock;
    const char *device_certificate;
    bool supports_p256;
    bool supports_ecdh;
    bool supports_rng;
    bool supports_counters;
} secure_element_detection_t;

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

static bool gpio_is_switchable(const gpio_pin_probe_t *probe) {
    return probe->pull_up_level && !probe->pull_down_level;
}

static uint32_t probe_now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
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

static void observe_gpio_button_phase(const gpio_pin_probe_t probes[PROBE_GPIO_PIN_COUNT],
                                      bool active_low,
                                      user_presence_candidate_t *candidates,
                                      size_t *candidate_count) {
    bool enabled[PROBE_GPIO_PIN_COUNT] = {0};
    bool last_level[PROBE_GPIO_PIN_COUNT] = {0};
    bool saw_low[PROBE_GPIO_PIN_COUNT] = {0};
    bool saw_high[PROBE_GPIO_PIN_COUNT] = {0};
    uint16_t transitions[PROBE_GPIO_PIN_COUNT] = {0};
    uint32_t start_ms;
    size_t index;

    for (index = 0u; index < PROBE_GPIO_PIN_COUNT; ++index) {
        if (!gpio_is_switchable(&probes[index])) {
            continue;
        }

        enabled[index] = true;
        gpio_init((uint)probes[index].pin);
        gpio_set_dir((uint)probes[index].pin, GPIO_IN);
        gpio_disable_pulls((uint)probes[index].pin);
        if (active_low) {
            gpio_pull_up((uint)probes[index].pin);
        } else {
            gpio_pull_down((uint)probes[index].pin);
        }
    }

    sleep_ms(10u);
    for (index = 0u; index < PROBE_GPIO_PIN_COUNT; ++index) {
        bool level;
        if (!enabled[index]) {
            continue;
        }

        level = gpio_get((uint)probes[index].pin);
        last_level[index] = level;
        saw_low[index] = !level;
        saw_high[index] = level;
    }

    start_ms = probe_now_ms();
    while ((probe_now_ms() - start_ms) < PROBE_USER_PRESENCE_OBSERVATION_WINDOW_MS) {
        for (index = 0u; index < PROBE_GPIO_PIN_COUNT; ++index) {
            bool level;
            if (!enabled[index]) {
                continue;
            }

            level = gpio_get((uint)probes[index].pin);
            if (level != last_level[index]) {
                transitions[index] += 1u;
                last_level[index] = level;
            }
            saw_low[index] = saw_low[index] || !level;
            saw_high[index] = saw_high[index] || level;
        }
        sleep_ms(PROBE_USER_PRESENCE_SAMPLE_INTERVAL_MS);
    }

    for (index = 0u; index < PROBE_GPIO_PIN_COUNT; ++index) {
        size_t candidate_index;
        if (!enabled[index]) {
            continue;
        }

        gpio_disable_pulls((uint)probes[index].pin);
        if (!(saw_low[index] && saw_high[index]) || transitions[index] == 0u) {
            continue;
        }

        for (candidate_index = 0u; candidate_index < *candidate_count; ++candidate_index) {
            if (candidates[candidate_index].pin == probes[index].pin) {
                if (candidates[candidate_index].have_active_low &&
                    candidates[candidate_index].active_low != active_low) {
                    candidates[candidate_index].ambiguous_active_state = true;
                } else {
                    candidates[candidate_index].have_active_low = true;
                    candidates[candidate_index].active_low = active_low;
                }
                if (transitions[index] > candidates[candidate_index].transitions) {
                    candidates[candidate_index].transitions = transitions[index];
                }
                break;
            }
        }

        if (candidate_index >= *candidate_count && *candidate_count < PROBE_USER_PRESENCE_MAX_CANDIDATES) {
            candidates[*candidate_count].pin = probes[index].pin;
            candidates[*candidate_count].have_active_low = true;
            candidates[*candidate_count].active_low = active_low;
            candidates[*candidate_count].ambiguous_active_state = false;
            candidates[*candidate_count].transitions = transitions[index];
            *candidate_count += 1u;
        }
    }
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

static void init_i2c_pair(const i2c_pin_pair_t *pair) {
    i2c_inst_t *i2c = pair->instance == 0u ? i2c0 : i2c1;

    i2c_init(i2c, 100000u);
    gpio_set_function(pair->sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(pair->scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(pair->sda_pin);
    gpio_pull_up(pair->scl_pin);
    sleep_us(100u);
}

static void scan_i2c_pair(const i2c_pin_pair_t *pair,
                          i2c_pair_probe_t *pair_probe,
                          i2c_eeprom_candidate_t *candidates,
                          size_t *candidate_count) {
    i2c_inst_t *i2c = pair->instance == 0u ? i2c0 : i2c1;
    uint8_t address;

    memset(pair_probe, 0, sizeof(*pair_probe));
    pair_probe->pair = *pair;

    init_i2c_pair(pair);

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

static void emit_gpio_report(const gpio_pin_probe_t probes[PROBE_GPIO_PIN_COUNT]) {
    size_t index;
    bool first_forced_pin = true;

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

static void emit_user_presence_report(const gpio_pin_probe_t probes[PROBE_GPIO_PIN_COUNT]) {
    user_presence_candidate_t candidates[PROBE_USER_PRESENCE_MAX_CANDIDATES];
    size_t candidate_count = 0u;
    size_t index;

    memset(candidates, 0, sizeof(candidates));
    observe_gpio_button_phase(probes, true, candidates, &candidate_count);
    observe_gpio_button_phase(probes, false, candidates, &candidate_count);

    printf("  \"userPresence\": {\n");
    printf("    \"observationWindowMs\": %u,\n", PROBE_USER_PRESENCE_OBSERVATION_WINDOW_MS);
    printf("    \"sampleIntervalMs\": %u,\n", PROBE_USER_PRESENCE_SAMPLE_INTERVAL_MS);
    printf("    \"gpioButtonCandidates\": [\n");
    for (index = 0u; index < candidate_count; ++index) {
        const char *active_state = candidates[index].ambiguous_active_state
            ? "ambiguous"
            : (candidates[index].active_low ? "low" : "high");
        const char *confidence = candidates[index].ambiguous_active_state ? "low" : "medium";
        printf("      {\"pin\": %u, \"activeState\": \"%s\", \"transitions\": %u, \"confidence\": \"%s\"}%s\n",
               candidates[index].pin,
               active_state,
               candidates[index].transitions,
               confidence,
               index + 1u == candidate_count ? "" : ",");
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

static bool i2c_address_is_eeprom_candidate(const i2c_pin_pair_t *pair,
                                            uint8_t address,
                                            const i2c_eeprom_candidate_t *candidates,
                                            size_t candidate_count) {
    size_t index;

    for (index = 0u; index < candidate_count; ++index) {
        if (candidates[index].pair.instance == pair->instance &&
            candidates[index].pair.sda_pin == pair->sda_pin &&
            candidates[index].pair.scl_pin == pair->scl_pin &&
            candidates[index].address == address) {
            return true;
        }
    }
    return false;
}

static void collect_user_verification_candidates(const i2c_pair_probe_t *pair_probes,
                                                 size_t pair_probe_count,
                                                 const i2c_eeprom_candidate_t *eeprom_candidates,
                                                 size_t eeprom_candidate_count,
                                                 user_verification_candidate_t *uv_candidates,
                                                 size_t *uv_candidate_count) {
    size_t pair_index;

    for (pair_index = 0u; pair_index < pair_probe_count; ++pair_index) {
        size_t device_index;
        for (device_index = 0u; device_index < pair_probes[pair_index].device_count; ++device_index) {
            uint8_t address = pair_probes[pair_index].devices[device_index];
            if (*uv_candidate_count >= PROBE_USER_VERIFICATION_MAX_CANDIDATES) {
                return;
            }
            if (i2c_address_is_eeprom_candidate(&pair_probes[pair_index].pair,
                                                address,
                                                eeprom_candidates,
                                                eeprom_candidate_count)) {
                continue;
            }

            uv_candidates[*uv_candidate_count].pair = pair_probes[pair_index].pair;
            uv_candidates[*uv_candidate_count].address = address;
            uv_candidates[*uv_candidate_count].hint = "manual-review-non-eeprom-i2c-device";
            uv_candidates[*uv_candidate_count].confidence = "low";
            *uv_candidate_count += 1u;
        }
    }
}

static uint16_t atecc_crc16(const uint8_t *data, size_t length) {
    uint16_t crc = 0u;
    size_t index;

    for (index = 0u; index < length; ++index) {
        uint8_t shift_register;
        for (shift_register = 0x01u; shift_register != 0u; shift_register <<= 1u) {
            uint8_t data_bit = (data[index] & shift_register) != 0u ? 1u : 0u;
            uint8_t crc_bit = (uint8_t)((crc >> 15u) & 0x01u);
            crc <<= 1u;
            if (data_bit != crc_bit) {
                crc ^= 0x8005u;
            }
        }
    }

    return crc;
}

static bool atecc_response_is_valid(const uint8_t *response, size_t length) {
    uint16_t expected_crc;

    if (response == NULL || length < 4u || response[0] != length) {
        return false;
    }

    expected_crc = (uint16_t)response[length - 2u] | ((uint16_t)response[length - 1u] << 8u);
    return atecc_crc16(response, length - 2u) == expected_crc;
}

static void atecc_wake_device(const i2c_pin_pair_t *pair) {
    reset_i2c_pair(pair);
    gpio_init(pair->sda_pin);
    gpio_set_dir(pair->sda_pin, GPIO_OUT);
    gpio_put(pair->sda_pin, 0u);
    sleep_us(80u);
    gpio_put(pair->sda_pin, 1u);
    sleep_us(10u);
    init_i2c_pair(pair);
    sleep_ms(3u);
}

static void atecc_sleep_device(i2c_inst_t *i2c, uint8_t address) {
    uint8_t word_address = 0x01u;
    (void)i2c_write_timeout_us(i2c, address, &word_address, 1u, false, PROBE_I2C_TIMEOUT_US);
}

static bool atecc_send_command(i2c_inst_t *i2c,
                               uint8_t address,
                               uint8_t opcode,
                               uint8_t param1,
                               uint16_t param2,
                               const uint8_t *data,
                               size_t data_length,
                               uint8_t *response,
                               size_t response_length,
                               uint32_t execution_delay_ms) {
    uint8_t packet[1u + 1u + 1u + 1u + 2u + 32u + 2u];
    size_t packet_length = 1u + 7u + data_length;
    uint16_t crc;
    int write_result;
    int read_result;

    if (data_length > 32u || response == NULL || response_length < 4u) {
        return false;
    }

    memset(packet, 0, sizeof(packet));
    packet[0] = 0x03u;
    packet[1] = (uint8_t)(7u + data_length);
    packet[2] = opcode;
    packet[3] = param1;
    packet[4] = (uint8_t)(param2 & 0xffu);
    packet[5] = (uint8_t)(param2 >> 8u);
    if (data_length > 0u) {
        memcpy(&packet[6], data, data_length);
    }
    crc = atecc_crc16(&packet[1], 5u + data_length);
    packet[packet_length - 2u] = (uint8_t)(crc & 0xffu);
    packet[packet_length - 1u] = (uint8_t)(crc >> 8u);

    write_result = i2c_write_timeout_us(i2c, address, packet, packet_length, false, PROBE_I2C_TIMEOUT_US);
    if (write_result != (int)packet_length) {
        return false;
    }

    sleep_ms(execution_delay_ms);
    read_result = i2c_read_timeout_us(i2c, address, response, response_length, false, PROBE_I2C_TIMEOUT_US);
    return read_result == (int)response_length && atecc_response_is_valid(response, response_length);
}

static bool atecc_info_revision(i2c_inst_t *i2c, uint8_t address, uint8_t revision[4]) {
    uint8_t response[7];

    if (!atecc_send_command(i2c, address, 0x30u, 0x00u, 0x0000u, NULL, 0u, response, sizeof(response), 4u)) {
        return false;
    }

    memcpy(revision, &response[1], 4u);
    return true;
}

static bool atecc_read_word(i2c_inst_t *i2c, uint8_t address, uint8_t zone, uint16_t word_address, uint8_t output[4]) {
    uint8_t response[7];

    if (!atecc_send_command(i2c, address, 0x02u, zone, word_address, NULL, 0u, response, sizeof(response), 4u)) {
        return false;
    }

    memcpy(output, &response[1], 4u);
    return true;
}

static const char *atecc_lock_state_name(uint8_t value) {
    if (value == 0x00u) {
        return "locked";
    }
    if (value == 0x55u) {
        return "unlocked";
    }
    return "unknown";
}

static const char *atecc_device_certificate_state(i2c_inst_t *i2c, uint8_t address) {
    static const uint8_t k_certificate_slots[] = {10u, 11u, 12u, 13u};
    bool any_readable = false;
    size_t index;

    for (index = 0u; index < sizeof(k_certificate_slots); ++index) {
        uint8_t word[4];
        size_t byte_index;
        bool non_blank = false;
        bool non_zero = false;

        if (!atecc_read_word(i2c, address, 0x02u, (uint16_t)(k_certificate_slots[index] << 3u), word)) {
            continue;
        }

        any_readable = true;
        for (byte_index = 0u; byte_index < sizeof(word); ++byte_index) {
            non_blank = non_blank || (word[byte_index] != 0xffu);
            non_zero = non_zero || (word[byte_index] != 0x00u);
        }
        if (non_blank && non_zero) {
            return "present";
        }
    }

    return any_readable ? "absent" : "unknown";
}

static void format_atecc_model_name(const uint8_t revision[4], char *output, size_t output_capacity) {
    if (output_capacity == 0u) {
        return;
    }

    if (revision[2] == 0x60u) {
        (void)snprintf(output, output_capacity, "ATECC608-compatible");
    } else if (revision[2] == 0x50u) {
        (void)snprintf(output, output_capacity, "ATECC508-compatible");
    } else {
        (void)snprintf(output, output_capacity, "ATECC ECC-compatible");
    }
}

static void add_secure_element_candidate(secure_element_candidate_t *candidates,
                                         size_t *candidate_count,
                                         const i2c_pin_pair_t *pair,
                                         uint8_t address,
                                         const char *family,
                                         const char *hint,
                                         const char *confidence,
                                         const char *probe_method) {
    size_t index;

    for (index = 0u; index < *candidate_count; ++index) {
        if (candidates[index].pair.instance == pair->instance &&
            candidates[index].pair.sda_pin == pair->sda_pin &&
            candidates[index].pair.scl_pin == pair->scl_pin &&
            candidates[index].address == address) {
            return;
        }
    }

    if (*candidate_count >= PROBE_SECURE_ELEMENT_MAX_CANDIDATES) {
        return;
    }

    candidates[*candidate_count].pair = *pair;
    candidates[*candidate_count].address = address;
    candidates[*candidate_count].family = family;
    candidates[*candidate_count].hint = hint;
    candidates[*candidate_count].confidence = confidence;
    candidates[*candidate_count].probe_method = probe_method;
    *candidate_count += 1u;
}

static bool maybe_add_whitelisted_secure_element_candidate(const i2c_pin_pair_t *pair,
                                                           uint8_t address,
                                                           secure_element_candidate_t *candidates,
                                                           size_t *candidate_count) {
    if (address >= 0x60u && address <= 0x67u) {
        add_secure_element_candidate(candidates,
                                     candidate_count,
                                     pair,
                                     address,
                                     "microchip-atecc-ecc",
                                     "atecc-wake-required-address",
                                     "medium",
                                     "whitelist-address");
        return true;
    }
    if (address == 0x30u) {
        add_secure_element_candidate(candidates,
                                     candidate_count,
                                     pair,
                                     address,
                                     "infineon-optiga-family",
                                     "common-optiga-i2c-address",
                                     "low",
                                     "whitelist-address");
        return true;
    }
    if (address == 0x48u) {
        add_secure_element_candidate(candidates,
                                     candidate_count,
                                     pair,
                                     address,
                                     "nxp-se05x-family",
                                     "common-se05x-i2c-address",
                                     "low",
                                     "whitelist-address");
        return true;
    }
    if (address == 0x2eu) {
        add_secure_element_candidate(candidates,
                                     candidate_count,
                                     pair,
                                     address,
                                     "tpm-i2c-family",
                                     "common-tpm-i2c-address",
                                     "low",
                                     "whitelist-address");
        return true;
    }

    return false;
}

static bool probe_atecc_detection(const i2c_pin_pair_t *pair,
                                  uint8_t address,
                                  secure_element_detection_t *detection) {
    i2c_inst_t *i2c = pair->instance == 0u ? i2c0 : i2c1;
    uint8_t revision[4];
    uint8_t lock_bytes[4];
    bool have_lock_bytes = false;
    bool detected = false;

    atecc_wake_device(pair);
    detected = atecc_info_revision(i2c, address, revision);
    if (!detected) {
        reset_i2c_pair(pair);
        return false;
    }

    memset(detection, 0, sizeof(*detection));
    detection->pair = *pair;
    detection->address = address;
    detection->family = "microchip-atecc-ecc";
    bytes_to_hex(revision, sizeof(revision), detection->revision_hex, sizeof(detection->revision_hex));
    format_atecc_model_name(revision, detection->model, sizeof(detection->model));

    have_lock_bytes = atecc_read_word(i2c, address, 0x00u, 21u, lock_bytes);
    detection->config_zone_lock = have_lock_bytes ? atecc_lock_state_name(lock_bytes[3]) : "unknown";
    detection->data_zone_lock = have_lock_bytes ? atecc_lock_state_name(lock_bytes[2]) : "unknown";
    detection->device_certificate = atecc_device_certificate_state(i2c, address);
    detection->supports_p256 = true;
    detection->supports_ecdh = true;
    detection->supports_rng = true;
    detection->supports_counters = true;

    atecc_sleep_device(i2c, address);
    reset_i2c_pair(pair);
    return true;
}

static void emit_secure_element_report(const i2c_pair_probe_t *pair_probes, size_t pair_probe_count) {
    secure_element_candidate_t candidates[PROBE_SECURE_ELEMENT_MAX_CANDIDATES];
    secure_element_detection_t detections[PROBE_SECURE_ELEMENT_MAX_DETECTIONS];
    size_t candidate_count = 0u;
    size_t detection_count = 0u;
    size_t pair_index;

    memset(candidates, 0, sizeof(candidates));
    memset(detections, 0, sizeof(detections));

    for (pair_index = 0u; pair_index < pair_probe_count; ++pair_index) {
        size_t device_index;
        for (device_index = 0u; device_index < pair_probes[pair_index].device_count; ++device_index) {
            (void)maybe_add_whitelisted_secure_element_candidate(&pair_probes[pair_index].pair,
                                                                 pair_probes[pair_index].devices[device_index],
                                                                 candidates,
                                                                 &candidate_count);
        }
    }

    for (pair_index = 0u; pair_index < pair_probe_count; ++pair_index) {
        static const uint8_t k_atecc_addresses[] = {0x60u, 0x61u, 0x62u, 0x63u, 0x64u, 0x65u, 0x66u, 0x67u};
        size_t address_index;

        for (address_index = 0u; address_index < sizeof(k_atecc_addresses); ++address_index) {
            secure_element_detection_t detection;
            if (detection_count >= PROBE_SECURE_ELEMENT_MAX_DETECTIONS) {
                break;
            }
            if (!probe_atecc_detection(&pair_probes[pair_index].pair, k_atecc_addresses[address_index], &detection)) {
                continue;
            }

            detections[detection_count++] = detection;
            add_secure_element_candidate(candidates,
                                         &candidate_count,
                                         &pair_probes[pair_index].pair,
                                         k_atecc_addresses[address_index],
                                         "microchip-atecc-ecc",
                                         "atecc-info-revision-probe",
                                         "high",
                                         "wake+info+read");
        }
    }

    printf("  ,\"secureElement\": {\n");
    printf("    \"probeMethod\": \"whitelist-safe-readonly\",\n");
    printf("    \"candidates\": [\n");
    for (pair_index = 0u; pair_index < candidate_count; ++pair_index) {
        printf("      {\"instance\": %u, \"sdaPin\": %u, \"sclPin\": %u, \"address\": \"0x%02x\", \"family\": \"%s\", \"hint\": \"%s\", \"confidence\": \"%s\", \"probeMethod\": \"%s\"}%s\n",
               candidates[pair_index].pair.instance,
               candidates[pair_index].pair.sda_pin,
               candidates[pair_index].pair.scl_pin,
               candidates[pair_index].address,
               candidates[pair_index].family,
               candidates[pair_index].hint,
               candidates[pair_index].confidence,
               candidates[pair_index].probe_method,
               pair_index + 1u == candidate_count ? "" : ",");
    }
    printf("    ],\n");
    printf("    \"detections\": [\n");
    for (pair_index = 0u; pair_index < detection_count; ++pair_index) {
        printf("      {\"instance\": %u, \"sdaPin\": %u, \"sclPin\": %u, \"address\": \"0x%02x\", \"family\": \"%s\", \"model\": \"%s\", \"revisionHex\": \"%s\", \"configZoneLock\": \"%s\", \"dataZoneLock\": \"%s\", \"deviceCertificate\": \"%s\", \"capabilities\": {\"p256\": %s, \"ecdh\": %s, \"rng\": %s, \"counters\": %s}}%s\n",
               detections[pair_index].pair.instance,
               detections[pair_index].pair.sda_pin,
               detections[pair_index].pair.scl_pin,
               detections[pair_index].address,
               detections[pair_index].family,
               detections[pair_index].model,
               detections[pair_index].revision_hex,
               detections[pair_index].config_zone_lock,
               detections[pair_index].data_zone_lock,
               detections[pair_index].device_certificate,
               detections[pair_index].supports_p256 ? "true" : "false",
               detections[pair_index].supports_ecdh ? "true" : "false",
               detections[pair_index].supports_rng ? "true" : "false",
               detections[pair_index].supports_counters ? "true" : "false",
               pair_index + 1u == detection_count ? "" : ",");
    }
    printf("    ]\n");
    printf("  }\n");
}

static void emit_i2c_report(void) {
    i2c_pair_probe_t pair_probes[sizeof(k_i2c_pairs) / sizeof(k_i2c_pairs[0])];
    i2c_eeprom_candidate_t candidates[PROBE_I2C_MAX_EEPROM_CANDIDATES];
    user_verification_candidate_t uv_candidates[PROBE_USER_VERIFICATION_MAX_CANDIDATES];
    size_t pair_index;
    size_t candidate_count = 0u;
    size_t uv_candidate_count = 0u;

    memset(pair_probes, 0, sizeof(pair_probes));
    memset(candidates, 0, sizeof(candidates));
    memset(uv_candidates, 0, sizeof(uv_candidates));

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

    collect_user_verification_candidates(pair_probes,
                                         sizeof(k_i2c_pairs) / sizeof(k_i2c_pairs[0]),
                                         candidates,
                                         candidate_count,
                                         uv_candidates,
                                         &uv_candidate_count);

    printf("  ,\"userVerification\": {\n");
    printf("    \"probeMethod\": \"heuristic-non-eeprom-i2c-review\",\n");
    printf("    \"i2cCandidates\": [\n");
    for (pair_index = 0u; pair_index < uv_candidate_count; ++pair_index) {
        printf("      {\"instance\": %u, \"sdaPin\": %u, \"sclPin\": %u, \"address\": \"0x%02x\", \"hint\": \"%s\", \"confidence\": \"%s\"}%s\n",
               uv_candidates[pair_index].pair.instance,
               uv_candidates[pair_index].pair.sda_pin,
               uv_candidates[pair_index].pair.scl_pin,
               uv_candidates[pair_index].address,
               uv_candidates[pair_index].hint,
               uv_candidates[pair_index].confidence,
               pair_index + 1u == uv_candidate_count ? "" : ",");
    }
    printf("    ]\n");
    printf("  }\n");
    emit_secure_element_report(pair_probes, sizeof(k_i2c_pairs) / sizeof(k_i2c_pairs[0]));
}

void meowkey_board_probe_emit_report(void) {
    char unique_id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1u];
    gpio_pin_probe_t gpio_probes[PROBE_GPIO_PIN_COUNT];
    size_t index;

    pico_get_unique_board_id_string(unique_id, sizeof(unique_id));
    for (index = 0u; index < PROBE_GPIO_PIN_COUNT; ++index) {
        gpio_probes[index].pin = (uint8_t)index;
        sample_gpio_pin((uint8_t)index, &gpio_probes[index]);
    }

    printf("MEOWKEY_PROBE_JSON_BEGIN\n");
    printf("{\n");
    printf("  \"schemaVersion\": %u,\n", PROBE_I2C_HEADER_SCHEMA_VERSION);
    printf("  \"tool\": \"meowkey-probe\",\n");
    printf("  \"uniqueId\": \"%s\",\n", unique_id);
    printf("  \"flashSizeBytes\": %u,\n", (unsigned int)PICO_FLASH_SIZE_BYTES);
    emit_gpio_report(gpio_probes);
    printf(",\n");
    emit_user_presence_report(gpio_probes);
    printf(",\n");
    emit_i2c_report();
    printf("}\n");
    printf("MEOWKEY_PROBE_JSON_END\n");
    fflush(stdout);
}
