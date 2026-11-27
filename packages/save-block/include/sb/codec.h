/* save-block: little- and big-endian byte codec and bounded writer and reader. Part of save-block 1.0.0 (MIT). */
#ifndef SB_CODEC_H
#define SB_CODEC_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Saved data is a byte layout, not a struct dump: struct padding, enum size and byte order differ between compilers
 * and consoles. These helpers write and read each field at a named offset, in a stated byte order, so a block means
 * the same thing on every machine.
 */

/* Single values. `bytes` must have 2 or 4 bytes. */
static inline void sb_put_u16le(uint8_t* bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static inline void sb_put_u32le(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static inline void sb_put_u16be(uint8_t* bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static inline void sb_put_u32be(uint8_t* bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static inline uint16_t sb_get_u16le(const uint8_t* bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static inline uint32_t sb_get_u32le(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static inline uint16_t sb_get_u16be(const uint8_t* bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
}

static inline uint32_t sb_get_u32be(const uint8_t* bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

/*
 * A bounded writer. Writing past the end writes nothing and sets a flag that stays set, so a layout is written
 * with no checks between fields and tested once at the end:
 *
 *   sb_writer w;
 *   sb_writer_init(&w, buffer, sizeof(buffer));
 *   sb_writer_u8(&w, volume);
 *   sb_writer_u32le(&w, score);
 *   if (!sb_writer_ok(&w) || w.length != PAYLOAD_BYTES) return SB_BAD_ARGUMENT;
 */
typedef struct sb_writer {
    uint8_t* data;
    size_t capacity;
    size_t length; /* bytes written so far */
    uint8_t overflow; /* set once a write did not fit */
} sb_writer;

static inline void sb_writer_init(sb_writer* writer, uint8_t* data, size_t capacity) {
    writer->data = data;
    writer->capacity = capacity;
    writer->length = 0u;
    writer->overflow = 0u;
}

static inline uint8_t sb_writer_ok(const sb_writer* writer) {
    return writer->overflow ? 0u : 1u;
}

/* Reserves `count` bytes and returns where they go, or NULL (and sets the flag) when they do not fit. */
static inline uint8_t* sb_writer_take(sb_writer* writer, size_t count) {
    uint8_t* at;
    if (writer->overflow || count > writer->capacity - writer->length) {
        writer->overflow = 1u;
        return NULL;
    }
    at = writer->data + writer->length;
    writer->length += count;
    return at;
}

static inline void sb_writer_u8(sb_writer* writer, uint8_t value) {
    uint8_t* at = sb_writer_take(writer, 1u);
    if (at) at[0] = value;
}

static inline void sb_writer_u16le(sb_writer* writer, uint16_t value) {
    uint8_t* at = sb_writer_take(writer, 2u);
    if (at) sb_put_u16le(at, value);
}

static inline void sb_writer_u32le(sb_writer* writer, uint32_t value) {
    uint8_t* at = sb_writer_take(writer, 4u);
    if (at) sb_put_u32le(at, value);
}

static inline void sb_writer_u16be(sb_writer* writer, uint16_t value) {
    uint8_t* at = sb_writer_take(writer, 2u);
    if (at) sb_put_u16be(at, value);
}

static inline void sb_writer_u32be(sb_writer* writer, uint32_t value) {
    uint8_t* at = sb_writer_take(writer, 4u);
    if (at) sb_put_u32be(at, value);
}

static inline void sb_writer_bytes(sb_writer* writer, const uint8_t* bytes, size_t count) {
    uint8_t* at = sb_writer_take(writer, count);
    if (at && count) memcpy(at, bytes, count);
}

/* `count` zero bytes, for reserved or padding fields. */
static inline void sb_writer_zeros(sb_writer* writer, size_t count) {
    uint8_t* at = sb_writer_take(writer, count);
    if (at && count) memset(at, 0, count);
}

/*
 * A bounded reader, the mirror of the writer. Reading past the end returns zero, reads nothing and sets a flag that
 * stays set.
 */
typedef struct sb_reader {
    const uint8_t* data;
    size_t length;
    size_t position; /* bytes read so far */
    uint8_t underflow; /* set once a read went past the end */
} sb_reader;

static inline void sb_reader_init(sb_reader* reader, const uint8_t* data, size_t length) {
    reader->data = data;
    reader->length = length;
    reader->position = 0u;
    reader->underflow = 0u;
}

static inline uint8_t sb_reader_ok(const sb_reader* reader) {
    return reader->underflow ? 0u : 1u;
}

static inline const uint8_t* sb_reader_take(sb_reader* reader, size_t count) {
    const uint8_t* at;
    if (reader->underflow || count > reader->length - reader->position) {
        reader->underflow = 1u;
        return NULL;
    }
    at = reader->data + reader->position;
    reader->position += count;
    return at;
}

static inline uint8_t sb_reader_u8(sb_reader* reader) {
    const uint8_t* at = sb_reader_take(reader, 1u);
    return at ? at[0] : 0u;
}

static inline uint16_t sb_reader_u16le(sb_reader* reader) {
    const uint8_t* at = sb_reader_take(reader, 2u);
    return at ? sb_get_u16le(at) : 0u;
}

static inline uint32_t sb_reader_u32le(sb_reader* reader) {
    const uint8_t* at = sb_reader_take(reader, 4u);
    return at ? sb_get_u32le(at) : 0u;
}

static inline uint16_t sb_reader_u16be(sb_reader* reader) {
    const uint8_t* at = sb_reader_take(reader, 2u);
    return at ? sb_get_u16be(at) : 0u;
}

static inline uint32_t sb_reader_u32be(sb_reader* reader) {
    const uint8_t* at = sb_reader_take(reader, 4u);
    return at ? sb_get_u32be(at) : 0u;
}

/* Copies `count` bytes out. On underflow nothing is copied. */
static inline void sb_reader_bytes(sb_reader* reader, uint8_t* out, size_t count) {
    const uint8_t* at = sb_reader_take(reader, count);
    if (at && count) memcpy(out, at, count);
}

static inline void sb_reader_skip(sb_reader* reader, size_t count) {
    (void)sb_reader_take(reader, count);
}

#endif
