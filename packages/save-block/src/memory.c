/* save-block: a two-slot storage in RAM with fault injection, for tests. Part of save-block 0.1.0 (MIT). */
#include "sb/memory.h"

#include <string.h>

static sb_result memory_read(void* context, uint8_t slot, uint8_t* buffer, size_t capacity, size_t* length) {
    sb_memory* memory = (sb_memory*)context;
    size_t copy;
    if (!memory || slot >= SB_SLOT_COUNT || !buffer || !length) return SB_BAD_ARGUMENT;
    memory->reads++;
    if (memory->read_error[slot] != SB_OK) return memory->read_error[slot];
    if (memory->length[slot] == 0u) return SB_EMPTY;
    copy = memory->length[slot] < capacity ? memory->length[slot] : capacity;
    memcpy(buffer, memory->data[slot], copy);
    *length = memory->length[slot];
    return SB_OK;
}

static sb_result memory_write(void* context, uint8_t slot, const uint8_t* buffer, size_t length) {
    sb_memory* memory = (sb_memory*)context;
    if (!memory || slot >= SB_SLOT_COUNT || !buffer) return SB_BAD_ARGUMENT;
    memory->writes++;
    if (memory->write_error != SB_OK) return memory->write_error;
    if (length == 0u || length > SB_MAX_RECORD_BYTES) return SB_FULL;
    memcpy(memory->data[slot], buffer, length);
    if (memory->truncate_to != 0u && memory->truncate_to < length) {
        memory->length[slot] = memory->truncate_to;
        return SB_IO;
    }
    memory->length[slot] = length;
    if (memory->corrupt_at != 0u && memory->corrupt_at <= length) memory->data[slot][memory->corrupt_at - 1u] ^= 0x40u;
    return SB_OK;
}

void sb_memory_init(sb_memory* memory) {
    memset(memory, 0, sizeof(*memory));
}

sb_storage sb_memory_storage(sb_memory* memory) {
    sb_storage storage;
    storage.context = memory;
    storage.read = memory_read;
    storage.write = memory_write;
    return storage;
}
