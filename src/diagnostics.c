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
    size_t count = 0u;
    size_t included_count = 0u;
    size_t included_bytes = 0u;
    size_t omitted_count = 0u;
    int omitted_written = 0;
    size_t available_capacity;

    if (output_capacity == 0u) {
        return 0u;
    }

    if (s_entry_count == 0u) {
        snprintf(output, output_capacity, "诊断日志为空。\n");
        return strlen(output);
    }

    available_capacity = output_capacity - 1u;
    while (included_count < s_entry_count) {
        size_t reverse_index = included_count;
        size_t index = (s_next_index + MEOWKEY_DIAG_ENTRY_COUNT - 1u - reverse_index) % MEOWKEY_DIAG_ENTRY_COUNT;
        size_t entry_length = strnlen(s_entries[index], MEOWKEY_DIAG_ENTRY_SIZE) + 1u;
        size_t candidate_included_count = included_count + 1u;
        size_t candidate_omitted_count = s_entry_count - candidate_included_count;
        size_t required_capacity = included_bytes + entry_length;

        if (candidate_omitted_count > 0u) {
            omitted_written = snprintf(NULL, 0, "olderEntriesOmitted=%lu\n", (unsigned long)candidate_omitted_count);
            if (omitted_written > 0) {
                required_capacity += (size_t)omitted_written;
            }
        }

        if (required_capacity > available_capacity) {
            break;
        }

        included_count = candidate_included_count;
        included_bytes += entry_length;
    }

    omitted_count = s_entry_count - included_count;
    if (omitted_count > 0u) {
        omitted_written = snprintf(
            &output[offset],
            output_capacity - offset,
            "olderEntriesOmitted=%lu\n",
            (unsigned long)omitted_count);
        if (omitted_written > 0 && (size_t)omitted_written < (output_capacity - offset)) {
            offset += (size_t)omitted_written;
        }
    }

    for (count = 0u; count < included_count; ++count) {
        size_t chronological_index = included_count - count - 1u;
        size_t index = (s_next_index + MEOWKEY_DIAG_ENTRY_COUNT - 1u - chronological_index) % MEOWKEY_DIAG_ENTRY_COUNT;
        int written = snprintf(&output[offset], output_capacity - offset, "%s\n", s_entries[index]);
        if (written <= 0 || (size_t)written >= (output_capacity - offset)) {
            break;
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
