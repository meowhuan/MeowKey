#include "diagnostics.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum {
    MEOWKEY_DIAG_ENTRY_COUNT = 48,
    MEOWKEY_DIAG_ENTRY_SIZE = 120,
};

static char s_entries[MEOWKEY_DIAG_ENTRY_COUNT][MEOWKEY_DIAG_ENTRY_SIZE];
static size_t s_next_index = 0u;
static size_t s_entry_count = 0u;
static unsigned long s_sequence = 1u;

void meowkey_diag_init(void) {
    memset(s_entries, 0, sizeof(s_entries));
    s_next_index = 0u;
    s_entry_count = 0u;
    s_sequence = 1u;
}

void meowkey_diag_clear(void) {
    meowkey_diag_init();
}

void meowkey_diag_logf(const char *format, ...) {
    va_list args;
    char body[MEOWKEY_DIAG_ENTRY_SIZE];

    va_start(args, format);
    vsnprintf(body, sizeof(body), format, args);
    va_end(args);

    snprintf(
        s_entries[s_next_index],
        sizeof(s_entries[s_next_index]),
        "[%03lu] %s",
        s_sequence++,
        body);

    s_next_index = (s_next_index + 1u) % MEOWKEY_DIAG_ENTRY_COUNT;
    if (s_entry_count < MEOWKEY_DIAG_ENTRY_COUNT) {
        s_entry_count += 1u;
    }
}

size_t meowkey_diag_snapshot(char *output, size_t output_capacity) {
    size_t offset = 0u;
    size_t count;

    if (output_capacity == 0u) {
        return 0u;
    }

    if (s_entry_count == 0u) {
        snprintf(output, output_capacity, "诊断日志为空。\n");
        return strlen(output);
    }

    for (count = 0u; count < s_entry_count; ++count) {
        size_t index = (s_next_index + MEOWKEY_DIAG_ENTRY_COUNT - s_entry_count + count) % MEOWKEY_DIAG_ENTRY_COUNT;
        int written = snprintf(
            &output[offset],
            output_capacity - offset,
            "%s\n",
            s_entries[index]);
        if (written <= 0) {
            break;
        }

        if ((size_t)written >= (output_capacity - offset)) {
            offset = output_capacity - 1u;
            output[offset] = '\0';
            return offset;
        }
        offset += (size_t)written;
    }

    if (offset < output_capacity) {
        output[offset] = '\0';
    } else {
        output[output_capacity - 1u] = '\0';
        offset = output_capacity - 1u;
    }

    return offset;
}
