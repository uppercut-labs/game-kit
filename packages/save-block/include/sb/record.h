/* save-block: a checksummed, versioned record in two alternating slots. Part of save-block 1.0.0 (MIT). */
#ifndef SB_RECORD_H
#define SB_RECORD_H

#include <stddef.h>
#include <stdint.h>

#define SB_PACKAGE_VERSION "1.0.0"

/*
 * The biggest payload a format may have. It sizes the buffers on the stack (about 2 * (this + 16) bytes in a load,
 * the same in a save). Build the library with -DSB_MAX_PAYLOAD_BYTES=<n> (or set it in CMake) to change it.
 */
#ifndef SB_MAX_PAYLOAD_BYTES
#define SB_MAX_PAYLOAD_BYTES 512u
#endif

#define SB_HEADER_BYTES 12u /* magic 4, version 2, payload size 2, sequence 4 */
#define SB_CHECKSUM_BYTES 4u
#define SB_MAX_RECORD_BYTES (SB_HEADER_BYTES + SB_MAX_PAYLOAD_BYTES + SB_CHECKSUM_BYTES)
#define SB_SLOT_COUNT 2u

/*
 * What happened. The numeric order matters: a result greater than SB_INVALID means the storage or the data needs
 * attention, and a save stops on such a result instead of overwriting.
 */
typedef uint8_t sb_result;
enum {
    SB_OK = 0,
    SB_EMPTY = 1, /* nothing has been saved */
    SB_INVALID = 2, /* the data is damaged, or from something else */
    SB_UNSUPPORTED_VERSION = 3, /* a record with another format version (maybe from a newer game) */
    SB_NO_MEDIA = 4, /* no card, cartridge or device */
    SB_UNFORMATTED = 5,
    SB_FULL = 6,
    SB_UNAVAILABLE = 7, /* the storage service is not running */
    SB_IO = 8, /* a read or write failed */
    SB_VERIFY = 9, /* the write was reported as done but reads back different */
    SB_BAD_ARGUMENT = 10 /* a NULL pointer, a missing callback, or a format that does not fit */
};

/*
 * The record layout (every number little-endian):
 *
 *   offset 0    magic          4 bytes, chosen by the game
 *   offset 4    version        u16, chosen by the game
 *   offset 6    payload size   u16, the same for every record of a format
 *   offset 8    sequence       u32, counts saves; compared with wrap-around arithmetic
 *   offset 12   payload        payload_bytes bytes
 *   at the end  checksum       u32, FNV-1a over every byte before it
 */
typedef struct sb_format {
    uint8_t magic[4];
    uint16_t version;
    uint16_t payload_bytes; /* 1 .. SB_MAX_PAYLOAD_BYTES */
} sb_format;

/*
 * The storage the game provides, a file, a memory-card slot, EEPROM or RAM, as two callbacks over `slot` 0 and 1.
 *
 * read:  copy up to `capacity` bytes of the slot into `buffer`, set `*length` to the slot's full size, and return
 *        SB_OK. A slot that was never written returns SB_EMPTY. Device trouble returns the matching result.
 * write: store `length` bytes in the slot, replacing it, and return SB_OK or the matching result.
 */
typedef struct sb_storage {
    void* context;
    sb_result (*read)(void* context, uint8_t slot, uint8_t* buffer, size_t capacity, size_t* length);
    sb_result (*write)(void* context, uint8_t slot, const uint8_t* buffer, size_t length);
} sb_storage;

/* A payload check, run on a loaded record and before a save. Return non-zero (1) when the payload is acceptable. */
typedef uint8_t (*sb_validate_fn)(void* context, const uint8_t* payload);

typedef struct sb_store {
    sb_format format;
    sb_storage storage;
    sb_validate_fn validate; /* optional */
    void* validate_context;
} sb_store;

/* The size of a record of this format, or 0 if the format does not fit. */
size_t sb_record_bytes(const sb_format* format);

/* Builds a record: header, payload, checksum. `out` must hold sb_record_bytes(format) bytes. */
sb_result sb_encode(const sb_format* format, uint32_t sequence, const uint8_t* payload, uint8_t* out, size_t capacity);

/*
 * Checks a record and copies its payload and sequence out (either may be NULL). Order of the checks: too short or
 * the wrong magic is SB_INVALID; a different version is SB_UNSUPPORTED_VERSION; a wrong length, payload size or
 * checksum is SB_INVALID. Outputs are written only on SB_OK.
 */
sb_result sb_decode(const sb_format* format, const uint8_t* bytes, size_t length, uint8_t* payload_out, uint32_t* sequence_out);

/*
 * Loads the newest valid record of the two slots into `payload_out` (payload_bytes bytes) and its sequence into
 * `sequence_out` (may be NULL). A slot whose payload fails `validate` counts as damaged. When neither slot is
 * valid, the result is the more serious of the two slots' results (SB_EMPTY < SB_INVALID < ... < SB_VERIFY), and
 * the outputs are untouched.
 */
sb_result sb_load(const sb_store* store, uint8_t* payload_out, uint32_t* sequence_out);

/*
 * Saves a payload into the slot that does not hold the newest valid record, with the next sequence number, then
 * reads it back and compares. The older slot is never touched until the new one verifies, so power loss or a failed
 * write leaves the last good save loadable. Stops without writing when a slot reports a device error or a record
 * of another version (so an older game does not overwrite a newer game's save).
 */
sb_result sb_save(const sb_store* store, const uint8_t* payload);

/* A short, neutral description of a result, for a message on screen or in a log. Never NULL. */
const char* sb_result_text(sb_result result);

#endif
