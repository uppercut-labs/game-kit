/* An NAOMI EEPROM as the storage of a save-block store: its two slots are the two copies of the game block. Part of
 * kos-naomi-cabinet 1.0.1 (MIT). Needs the save-block package. */
#ifndef KCAB_SAVE_BLOCK_H
#define KCAB_SAVE_BLOCK_H

#include "kcab/eeprom.h"
#include "sb/record.h"

/*
 * save-block keeps a save in two alternating slots. On a cabinet the two slots are the two copies the BIOS layout
 * already has room for: slot 0 is the block at offset 44 and slot 1 follows it, each `record_bytes` long (a
 * save-block record is 16 bytes longer than its payload, so a payload of at most 26 bytes fits). The chip's header
 * (sizes and the BIOS checksum, which covers slot 0 only) is kept right on every write; bytes 0..35 are never touched.
 * A game with more to keep than 26 bytes uses kcab_block_save with one copy instead (up to 84 bytes).
 */
#define KCAB_SB_MAX_RECORD_BYTES (KCAB_GAME_AREA / 2u)

typedef struct KcabSbStorage {
    const KcabEeprom *eeprom;
    uint8_t record_bytes;
} KcabSbStorage;

/* 1 when a store of this format fits on the chip. */
uint8_t kcab_sb_fits(const sb_format *format);

/*
 * The storage for sb_store. `context` is filled in and must outlive the store. Returns an sb_storage whose callbacks
 * report: SB_EMPTY for a chip with no block of this record size (blank, or another layout); SB_NO_MEDIA when the chip
 * cannot be read; SB_IO and SB_VERIFY for a write that fails or does not read back.
 */
sb_storage kcab_sb_storage(KcabSbStorage *context, const KcabEeprom *eeprom, const sb_format *format);

#endif
