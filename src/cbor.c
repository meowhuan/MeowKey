#include "cbor.h"

#include <string.h>

static bool cbor_reader_take(meowkey_cbor_reader_t *reader, size_t length, meowkey_cbor_view_t *view) {
    if ((reader->offset + length) > reader->length) {
        return false;
    }

    view->data = &reader->data[reader->offset];
    view->length = length;
    reader->offset += length;
    return true;
}

static bool cbor_read_length(meowkey_cbor_reader_t *reader, uint8_t additional, size_t *value) {
    meowkey_cbor_view_t view;
    uint32_t tmp = 0u;

    if (additional < 24u) {
        *value = additional;
        return true;
    }

    if (additional == 24u) {
        if (!cbor_reader_take(reader, 1u, &view)) {
            return false;
        }
        *value = view.data[0];
        return true;
    }

    if (additional == 25u) {
        if (!cbor_reader_take(reader, 2u, &view)) {
            return false;
        }
        *value = (size_t)((view.data[0] << 8u) | view.data[1]);
        return true;
    }

    if (additional == 26u) {
        if (!cbor_reader_take(reader, 4u, &view)) {
            return false;
        }
        tmp = ((uint32_t)view.data[0] << 24u) |
              ((uint32_t)view.data[1] << 16u) |
              ((uint32_t)view.data[2] << 8u) |
              (uint32_t)view.data[3];
        *value = tmp;
        return true;
    }

    return false;
}

static bool cbor_read_initial(meowkey_cbor_reader_t *reader, uint8_t *major, uint8_t *additional) {
    meowkey_cbor_view_t view;
    if (!cbor_reader_take(reader, 1u, &view)) {
        return false;
    }
    *major = (uint8_t)(view.data[0] >> 5u);
    *additional = (uint8_t)(view.data[0] & 0x1fu);
    return true;
}

static bool cbor_skip_items(meowkey_cbor_reader_t *reader, size_t count) {
    size_t index;
    for (index = 0; index < count; ++index) {
        if (!meowkey_cbor_skip(reader)) {
            return false;
        }
    }
    return true;
}

void meowkey_cbor_reader_init(meowkey_cbor_reader_t *reader, const uint8_t *data, size_t length) {
    reader->data = data;
    reader->length = length;
    reader->offset = 0u;
}

bool meowkey_cbor_read_map_start(meowkey_cbor_reader_t *reader, size_t *count) {
    uint8_t major;
    uint8_t additional;
    if (!cbor_read_initial(reader, &major, &additional) || major != 5u) {
        return false;
    }
    return cbor_read_length(reader, additional, count);
}

bool meowkey_cbor_read_array_start(meowkey_cbor_reader_t *reader, size_t *count) {
    uint8_t major;
    uint8_t additional;
    if (!cbor_read_initial(reader, &major, &additional) || major != 4u) {
        return false;
    }
    return cbor_read_length(reader, additional, count);
}

bool meowkey_cbor_read_int(meowkey_cbor_reader_t *reader, int64_t *value) {
    uint8_t major;
    uint8_t additional;
    size_t raw = 0u;

    if (!cbor_read_initial(reader, &major, &additional)) {
        return false;
    }
    if ((major != 0u && major != 1u) || !cbor_read_length(reader, additional, &raw)) {
        return false;
    }

    if (major == 0u) {
        *value = (int64_t)raw;
    } else {
        *value = -(int64_t)raw - 1;
    }
    return true;
}

bool meowkey_cbor_read_bool(meowkey_cbor_reader_t *reader, bool *value) {
    uint8_t major;
    uint8_t additional;
    if (!cbor_read_initial(reader, &major, &additional) || major != 7u) {
        return false;
    }
    if (additional == 20u) {
        *value = false;
        return true;
    }
    if (additional == 21u) {
        *value = true;
        return true;
    }
    return false;
}

bool meowkey_cbor_read_bytes(meowkey_cbor_reader_t *reader, meowkey_cbor_view_t *view) {
    uint8_t major;
    uint8_t additional;
    size_t length = 0u;
    if (!cbor_read_initial(reader, &major, &additional) || major != 2u) {
        return false;
    }
    if (!cbor_read_length(reader, additional, &length)) {
        return false;
    }
    return cbor_reader_take(reader, length, view);
}

bool meowkey_cbor_read_text(meowkey_cbor_reader_t *reader, meowkey_cbor_view_t *view) {
    uint8_t major;
    uint8_t additional;
    size_t length = 0u;
    if (!cbor_read_initial(reader, &major, &additional) || major != 3u) {
        return false;
    }
    if (!cbor_read_length(reader, additional, &length)) {
        return false;
    }
    return cbor_reader_take(reader, length, view);
}

bool meowkey_cbor_skip(meowkey_cbor_reader_t *reader) {
    uint8_t major;
    uint8_t additional;
    size_t count = 0u;
    meowkey_cbor_view_t view;

    if (!cbor_read_initial(reader, &major, &additional)) {
        return false;
    }

    switch (major) {
    case 0u:
    case 1u:
        return cbor_read_length(reader, additional, &count);

    case 2u:
    case 3u:
        return cbor_read_length(reader, additional, &count) && cbor_reader_take(reader, count, &view);

    case 4u:
        return cbor_read_length(reader, additional, &count) && cbor_skip_items(reader, count);

    case 5u:
        return cbor_read_length(reader, additional, &count) && cbor_skip_items(reader, count * 2u);

    case 7u:
        if (additional <= 23u) {
            return true;
        }
        if (additional == 24u) {
            return cbor_reader_take(reader, 1u, &view);
        }
        if (additional == 25u) {
            return cbor_reader_take(reader, 2u, &view);
        }
        if (additional == 26u) {
            return cbor_reader_take(reader, 4u, &view);
        }
        return false;

    default:
        return false;
    }
}

static bool cbor_writer_put(meowkey_cbor_writer_t *writer, const void *data, size_t length) {
    if (writer->failed || (writer->length + length) > writer->capacity) {
        writer->failed = true;
        return false;
    }
    memcpy(&writer->data[writer->length], data, length);
    writer->length += length;
    return true;
}

static bool cbor_write_type_and_value(meowkey_cbor_writer_t *writer, uint8_t major, uint64_t value) {
    uint8_t header[9];
    size_t length = 1u;

    if (value < 24u) {
        header[0] = (uint8_t)((major << 5u) | value);
    } else if (value <= 0xffu) {
        header[0] = (uint8_t)((major << 5u) | 24u);
        header[1] = (uint8_t)value;
        length = 2u;
    } else if (value <= 0xffffu) {
        header[0] = (uint8_t)((major << 5u) | 25u);
        header[1] = (uint8_t)(value >> 8u);
        header[2] = (uint8_t)value;
        length = 3u;
    } else {
        header[0] = (uint8_t)((major << 5u) | 26u);
        header[1] = (uint8_t)(value >> 24u);
        header[2] = (uint8_t)(value >> 16u);
        header[3] = (uint8_t)(value >> 8u);
        header[4] = (uint8_t)value;
        length = 5u;
    }

    return cbor_writer_put(writer, header, length);
}

void meowkey_cbor_writer_init(meowkey_cbor_writer_t *writer, uint8_t *data, size_t capacity) {
    writer->data = data;
    writer->capacity = capacity;
    writer->length = 0u;
    writer->failed = false;
}

bool meowkey_cbor_write_map_start(meowkey_cbor_writer_t *writer, size_t count) {
    return cbor_write_type_and_value(writer, 5u, count);
}

bool meowkey_cbor_write_array_start(meowkey_cbor_writer_t *writer, size_t count) {
    return cbor_write_type_and_value(writer, 4u, count);
}

bool meowkey_cbor_write_uint(meowkey_cbor_writer_t *writer, uint64_t value) {
    return cbor_write_type_and_value(writer, 0u, value);
}

bool meowkey_cbor_write_int(meowkey_cbor_writer_t *writer, int64_t value) {
    if (value >= 0) {
        return meowkey_cbor_write_uint(writer, (uint64_t)value);
    }
    return cbor_write_type_and_value(writer, 1u, (uint64_t)(-value - 1));
}

bool meowkey_cbor_write_bool(meowkey_cbor_writer_t *writer, bool value) {
    uint8_t raw = (uint8_t)(0xe0u | (value ? 21u : 20u));
    return cbor_writer_put(writer, &raw, 1u);
}

bool meowkey_cbor_write_bytes(meowkey_cbor_writer_t *writer, const uint8_t *data, size_t length) {
    return cbor_write_type_and_value(writer, 2u, length) && cbor_writer_put(writer, data, length);
}

bool meowkey_cbor_write_text(meowkey_cbor_writer_t *writer, const char *text, size_t length) {
    return cbor_write_type_and_value(writer, 3u, length) && cbor_writer_put(writer, text, length);
}

bool meowkey_cbor_write_null(meowkey_cbor_writer_t *writer) {
    const uint8_t raw = 0xf6u;
    return cbor_writer_put(writer, &raw, 1u);
}

