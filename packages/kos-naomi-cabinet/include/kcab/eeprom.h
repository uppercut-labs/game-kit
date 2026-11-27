/* kos-naomi-cabinet: a game's settings block in a NAOMI cabinet's 128-byte EEPROM, written the way the BIOS expects it.
 * Part of kos-naomi-cabinet 1.0.0 (MIT). No platform headers: the EEPROM is two callbacks. */
#ifndef KCAB_EEPROM_H
#define KCAB_EEPROM_H

#include <stddef.h>
#include <stdint.h>

#define KCAB_VERSION "1.0.0"

#define KCAB_EEPROM_BYTES 128u
#define KCAB_SYSTEM_BYTES 36u /* bytes 0..35: the BIOS system settings, two copies with their checksums. Never touched. */
#define KCAB_GAME_OFFSET 44u /* the game's block starts here; bytes 36..43 are its header */
#define KCAB_GAME_AREA (KCAB_EEPROM_BYTES - KCAB_GAME_OFFSET) /* 84 bytes for the block and its second copy */

/*
 * What happened. The numeric order matters: a result of KCAB_NO_EEPROM or more means the chip or the arguments need
 * attention.
 */
typedef uint8_t kcab_result;
enum {
    KCAB_OK = 0,
    KCAB_RECOVERED = 1, /* the data is good, but something in the block was damaged (a copy, a header byte or one checksum): save again to heal it */
    KCAB_EMPTY = 2, /* no block of this layout is there (a blank chip, or another game's) */
    KCAB_INVALID = 3, /* a block of this layout is there, and no copy of it is good */
    KCAB_NO_EEPROM = 4, /* the chip could not be read */
    KCAB_WRITE_FAILED = 5,
    KCAB_VERIFY_FAILED = 6, /* the write was accepted but the chip reads back different */
    KCAB_BAD_ARGUMENT = 7 /* a missing pointer or callback, or a layout that does not fit */
};

/* The EEPROM the game provides. Each callback returns 1 on success. `image` is always KCAB_EEPROM_BYTES bytes. */
typedef struct KcabEeprom {
    void *context;
    uint8_t (*read)(void *context, uint8_t image[KCAB_EEPROM_BYTES]);
    uint8_t (*write)(void *context, const uint8_t image[KCAB_EEPROM_BYTES]);
} KcabEeprom;

/*
 * A game's layout, chosen by the game: the package knows no fields.
 *
 *   size       bytes in one copy of the block, magic included (1 .. 84, or 1 .. 42 with two copies)
 *   copies     1, or 2 to store the block twice in a row (the BIOS's own convention)
 *   magic      the first `magic_len` bytes of every block (0 .. size); how a block is told from another game's
 *
 * The EEPROM then holds, after the BIOS bytes 0..35:
 *
 *   36,37  CRC-16 of the first copy, low byte first        40,41  the same, again
 *   38,39  size                                            42,43  the same, again
 *   44 ..  the block, then (with two copies) the block again
 */
typedef struct KcabBlock {
    const uint8_t *magic;
    uint8_t magic_len;
    uint8_t size;
    uint8_t copies;
} KcabBlock;

/* The checksum the NAOMI EEPROM uses (CRC-16-CCITT variant, seed 0xDEBDEB00 and a closing zero round). */
uint16_t kcab_crc16(const uint8_t *data, size_t length);

/* 1 when the layout fits the chip. */
uint8_t kcab_block_fits(const KcabBlock *block);

/*
 * The pure part, on an image in memory. kcab_image_store writes `data` (block->size bytes, starting with the magic)
 * into the block area, sets the size and checksum header, and leaves bytes 0..35 and everything after the block as
 * they were. kcab_image_fetch checks an image and copies the first good copy of the block out to `data`
 * (block->size bytes, untouched unless the result is KCAB_OK or KCAB_RECOVERED).
 */
kcab_result kcab_image_store(uint8_t image[KCAB_EEPROM_BYTES], const KcabBlock *block, const uint8_t *data);
kcab_result kcab_image_fetch(const uint8_t image[KCAB_EEPROM_BYTES], const KcabBlock *block, uint8_t *data);

/*
 * Over the EEPROM. kcab_block_load reads the chip and fetches. kcab_block_save reads the chip, stores into what it
 * read (so the BIOS bytes and anything else on the chip are kept byte for byte), writes, reads back and compares all
 * 128 bytes. `data` must start with the magic.
 */
kcab_result kcab_block_load(const KcabEeprom *eeprom, const KcabBlock *block, uint8_t *data);
kcab_result kcab_block_save(const KcabEeprom *eeprom, const KcabBlock *block, const uint8_t *data);

/* Writes a whole image, reads it back and compares. For restoring a chip after a test. */
kcab_result kcab_eeprom_write_verified(const KcabEeprom *eeprom, const uint8_t image[KCAB_EEPROM_BYTES]);

/* A short, neutral description of a result. Never NULL. */
const char *kcab_result_text(kcab_result result);

#endif
