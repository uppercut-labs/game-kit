/* save-block: the checksummed two-slot record. Part of save-block 1.0.0 (MIT). */
#include "sb/record.h"

#include <string.h>

#include "det/hash.h"
#include "sb/codec.h"

typedef struct slot_state {
    sb_result status;
    uint32_t sequence;
} slot_state;

static int format_fits(const sb_format* format) {
    return format && format->payload_bytes >= 1u && format->payload_bytes <= SB_MAX_PAYLOAD_BYTES;
}

size_t sb_record_bytes(const sb_format* format) {
    if (!format_fits(format)) return 0u;
    return (size_t)SB_HEADER_BYTES + format->payload_bytes + SB_CHECKSUM_BYTES;
}

sb_result sb_encode(const sb_format* format, uint32_t sequence, const uint8_t* payload, uint8_t* out, size_t capacity) {
    const size_t total = sb_record_bytes(format);
    if (total == 0u || !payload || !out || capacity < total) return SB_BAD_ARGUMENT;
    memcpy(out, format->magic, 4u);
    sb_put_u16le(out + 4, format->version);
    sb_put_u16le(out + 6, format->payload_bytes);
    sb_put_u32le(out + 8, sequence);
    memcpy(out + SB_HEADER_BYTES, payload, format->payload_bytes);
    sb_put_u32le(out + total - SB_CHECKSUM_BYTES, det_fnv1a32(out, total - SB_CHECKSUM_BYTES));
    return SB_OK;
}

sb_result sb_decode(const sb_format* format, const uint8_t* bytes, size_t length, uint8_t* payload_out, uint32_t* sequence_out) {
    const size_t total = sb_record_bytes(format);
    if (total == 0u || !bytes) return SB_BAD_ARGUMENT;
    if (length < 6u || memcmp(bytes, format->magic, 4u) != 0) return SB_INVALID;
    if (sb_get_u16le(bytes + 4) != format->version) return SB_UNSUPPORTED_VERSION;
    if (length != total || sb_get_u16le(bytes + 6) != format->payload_bytes) return SB_INVALID;
    if (det_fnv1a32(bytes, total - SB_CHECKSUM_BYTES) != sb_get_u32le(bytes + total - SB_CHECKSUM_BYTES)) return SB_INVALID;
    if (payload_out) memcpy(payload_out, bytes + SB_HEADER_BYTES, format->payload_bytes);
    if (sequence_out) *sequence_out = sb_get_u32le(bytes + 8);
    return SB_OK;
}

static void read_slot(const sb_store* store, uint8_t slot, uint8_t* record, size_t total, slot_state* state) {
    size_t length = 0u;
    state->sequence = 0u;
    state->status = store->storage.read(store->storage.context, slot, record, total, &length);
    if (state->status != SB_OK) return;
    state->status = sb_decode(&store->format, record, length, NULL, &state->sequence);
    if (state->status == SB_OK && store->validate && !store->validate(store->validate_context, record + SB_HEADER_BYTES)) {
        state->status = SB_INVALID;
    }
}

/* The slot holding the newest valid record (0 or 1), or -1 when neither is valid. */
static int newest_slot(const slot_state state[SB_SLOT_COUNT]) {
    uint32_t distance;
    if (state[0].status != SB_OK) return state[1].status == SB_OK ? 1 : -1;
    if (state[1].status != SB_OK) return 0;
    /* Serial-number arithmetic: a sequence just past UINT32_MAX (zero) is newer than UINT32_MAX. A tie prefers slot 0. */
    distance = state[1].sequence - state[0].sequence;
    return (distance != 0u && distance < 0x80000000u) ? 1 : 0;
}

static sb_result worse_of(sb_result a, sb_result b) {
    return a > b ? a : b;
}

sb_result sb_load(const sb_store* store, uint8_t* payload_out, uint32_t* sequence_out) {
    uint8_t record[SB_SLOT_COUNT][SB_MAX_RECORD_BYTES];
    slot_state state[SB_SLOT_COUNT];
    size_t total;
    int newest;
    uint8_t slot;
    if (!store || !store->storage.read) return SB_BAD_ARGUMENT;
    total = sb_record_bytes(&store->format);
    if (total == 0u) return SB_BAD_ARGUMENT;
    for (slot = 0u; slot < SB_SLOT_COUNT; slot++) read_slot(store, slot, record[slot], total, &state[slot]);
    newest = newest_slot(state);
    if (newest < 0) return worse_of(state[0].status, state[1].status);
    if (payload_out) memcpy(payload_out, record[newest] + SB_HEADER_BYTES, store->format.payload_bytes);
    if (sequence_out) *sequence_out = state[newest].sequence;
    return SB_OK;
}

sb_result sb_save(const sb_store* store, const uint8_t* payload) {
    uint8_t record[SB_SLOT_COUNT][SB_MAX_RECORD_BYTES];
    slot_state state[SB_SLOT_COUNT];
    size_t total;
    size_t length = 0u;
    int newest;
    uint8_t slot;
    uint8_t target;
    uint32_t sequence;
    sb_result result;
    if (!store || !store->storage.read || !store->storage.write || !payload) return SB_BAD_ARGUMENT;
    total = sb_record_bytes(&store->format);
    if (total == 0u) return SB_BAD_ARGUMENT;
    if (store->validate && !store->validate(store->validate_context, payload)) return SB_INVALID;

    for (slot = 0u; slot < SB_SLOT_COUNT; slot++) read_slot(store, slot, record[slot], total, &state[slot]);
    for (slot = 0u; slot < SB_SLOT_COUNT; slot++) {
        /* A device error, or a record of another version, stops the save: do not write over what may be a good save. */
        if (state[slot].status > SB_INVALID) return state[slot].status;
    }
    newest = newest_slot(state);
    target = newest < 0 ? 0u : (uint8_t)(newest ^ 1);
    sequence = newest < 0 ? 1u : state[newest].sequence + 1u;

    /* Both slots have been read, so their buffers are free: build in the first, read back into the second. */
    result = sb_encode(&store->format, sequence, payload, record[0], total);
    if (result != SB_OK) return result;
    result = store->storage.write(store->storage.context, target, record[0], total);
    if (result != SB_OK) return result;
    result = store->storage.read(store->storage.context, target, record[1], total, &length);
    if (result != SB_OK || length != total || memcmp(record[0], record[1], total) != 0) return SB_VERIFY;
    return SB_OK;
}

const char* sb_result_text(sb_result result) {
    switch (result) {
        case SB_OK: return "Complete";
        case SB_EMPTY: return "No saved data";
        case SB_INVALID: return "Saved data is damaged";
        case SB_UNSUPPORTED_VERSION: return "Saved data is from an unsupported version";
        case SB_NO_MEDIA: return "No storage device found";
        case SB_UNFORMATTED: return "Storage device is unformatted";
        case SB_FULL: return "Storage device is full";
        case SB_UNAVAILABLE: return "Storage service is unavailable";
        case SB_IO: return "Storage access failed";
        case SB_VERIFY: return "Could not verify the save";
        case SB_BAD_ARGUMENT: return "Invalid argument";
        default: return "Storage access failed";
    }
}
