/* save-block: a two-slot storage in RAM with fault injection, for tests. Part of save-block 1.0.0 (MIT). */
#ifndef SB_MEMORY_H
#define SB_MEMORY_H

#include "sb/record.h"

/*
 * Storage for a test: two slots in memory, the callbacks sb_save and sb_load need, and switches that make the
 * "device" fail the way real ones do. Zero it (or call sb_memory_init) for two empty slots.
 */
typedef struct sb_memory {
    uint8_t data[SB_SLOT_COUNT][SB_MAX_RECORD_BYTES];
    size_t length[SB_SLOT_COUNT]; /* 0 means the slot was never written */
    sb_result read_error[SB_SLOT_COUNT]; /* a result other than SB_OK makes reading that slot fail with it */
    sb_result write_error; /* a result other than SB_OK makes every write fail with it, storing nothing */
    size_t truncate_to; /* non-zero: a write stores only this many bytes and fails with SB_IO (power cut) */
    size_t corrupt_at; /* non-zero: a write succeeds but flips a bit of byte (corrupt_at - 1) (a bad medium) */
    unsigned reads; /* how many reads and writes were made */
    unsigned writes;
} sb_memory;

void sb_memory_init(sb_memory* memory);

/* The callbacks, ready for sb_store.storage. */
sb_storage sb_memory_storage(sb_memory* memory);

#endif
