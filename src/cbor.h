#ifndef MEOWKEY_CBOR_H
#define MEOWKEY_CBOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    size_t length;
} meowkey_cbor_view_t;

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
} meowkey_cbor_reader_t;

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t length;
    bool failed;
} meowkey_cbor_writer_t;

void meowkey_cbor_reader_init(meowkey_cbor_reader_t *reader, const uint8_t *data, size_t length);
bool meowkey_cbor_read_map_start(meowkey_cbor_reader_t *reader, size_t *count);
bool meowkey_cbor_read_array_start(meowkey_cbor_reader_t *reader, size_t *count);
bool meowkey_cbor_read_int(meowkey_cbor_reader_t *reader, int64_t *value);
bool meowkey_cbor_read_bool(meowkey_cbor_reader_t *reader, bool *value);
bool meowkey_cbor_read_bytes(meowkey_cbor_reader_t *reader, meowkey_cbor_view_t *view);
bool meowkey_cbor_read_text(meowkey_cbor_reader_t *reader, meowkey_cbor_view_t *view);
bool meowkey_cbor_skip(meowkey_cbor_reader_t *reader);

void meowkey_cbor_writer_init(meowkey_cbor_writer_t *writer, uint8_t *data, size_t capacity);
bool meowkey_cbor_write_map_start(meowkey_cbor_writer_t *writer, size_t count);
bool meowkey_cbor_write_array_start(meowkey_cbor_writer_t *writer, size_t count);
bool meowkey_cbor_write_uint(meowkey_cbor_writer_t *writer, uint64_t value);
bool meowkey_cbor_write_int(meowkey_cbor_writer_t *writer, int64_t value);
bool meowkey_cbor_write_bool(meowkey_cbor_writer_t *writer, bool value);
bool meowkey_cbor_write_bytes(meowkey_cbor_writer_t *writer, const uint8_t *data, size_t length);
bool meowkey_cbor_write_text(meowkey_cbor_writer_t *writer, const char *text, size_t length);
bool meowkey_cbor_write_null(meowkey_cbor_writer_t *writer);

#endif

