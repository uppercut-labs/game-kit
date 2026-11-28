/* det-core: 32-bit FNV-1a. Part of det-core 1.1.0 (MIT). */
#ifndef DET_HASH_H
#define DET_HASH_H

#include <stddef.h>
#include <stdint.h>
#include "det/types.h"

/*
 * Canonical 32-bit FNV-1a (offset basis 2166136261, prime 16777619).
 *
 * Values are folded in byte by byte, and the multi-byte helpers use a fixed little-endian order, so a result does
 * not depend on the target's endianness, struct padding, enum size or compiler. The hash is for checksums,
 * diagnostics and regression pins. It is not a cryptographic signature.
 *
 * A hash is a plain uint32_t that is started with det_hash32_begin() and passed through each call, so a value can
 * be built up in any number of pieces and the pieces give the same answer as one call over the joined bytes.
 */
static inline uint32_t det_hash32_begin(void) {
    return 2166136261u;
}

static inline uint32_t det_hash32_u8(uint32_t hash, uint8_t value) {
    hash ^= value;
    return hash * 16777619u;
}

static inline uint32_t det_hash32_u16(uint32_t hash, uint16_t value) {
    hash = det_hash32_u8(hash, (uint8_t)(value & 0xffu));
    hash = det_hash32_u8(hash, (uint8_t)((value >> 8) & 0xffu));
    return hash;
}

static inline uint32_t det_hash32_u32(uint32_t hash, uint32_t value) {
    hash = det_hash32_u8(hash, (uint8_t)(value & 0xffu));
    hash = det_hash32_u8(hash, (uint8_t)((value >> 8) & 0xffu));
    hash = det_hash32_u8(hash, (uint8_t)((value >> 16) & 0xffu));
    hash = det_hash32_u8(hash, (uint8_t)((value >> 24) & 0xffu));
    return hash;
}

static inline uint32_t det_hash32_i32(uint32_t hash, int32_t value) {
    return det_hash32_u32(hash, (uint32_t)value);
}

/* Folds `length` bytes into `hash`. `data` may be NULL only when `length` is zero. */
static inline uint32_t det_hash32_bytes(uint32_t hash, const uint8_t* data, size_t length) {
    size_t i;
    for (i = 0u; i < length; i++) hash = det_hash32_u8(hash, data[i]);
    return hash;
}

/* The FNV-1a of a whole buffer: "" gives 0x811c9dc5, "a" gives 0xe40c292c, "foobar" gives 0xbf9cf968. */
static inline uint32_t det_fnv1a32(const uint8_t* data, size_t length) {
    return det_hash32_bytes(det_hash32_begin(), data, length);
}

#endif
