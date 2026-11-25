/* The settings block in a NAOMI EEPROM image. See include/kcab/eeprom.h. */
#include "kcab/eeprom.h"

#include <string.h>

enum {
    CRC_LO = 36, /* the game block's checksum, twice */
    CRC_HI = 37,
    SIZE_A = 38, /* its size, twice */
    SIZE_B = 39,
    CRC_LO2 = 40,
    CRC_HI2 = 41,
    SIZE_A2 = 42,
    SIZE_B2 = 43
};

/*
 * The checksum is the one the BIOS and KallistiOS use: a 32-bit register that starts at 0xDEBDEB00; for each byte the
 * low 8 bits of the register are replaced by the byte and the register is shifted left 8 times, each time adding
 * 0x10210000 after the shift when the top bit was set; then 8 more shifts with no byte. The result is the top 16 bits.
 */
uint16_t kcab_crc16(const uint8_t *data, size_t length)
{
    uint32_t n = 0xDEBDEB00u;
    size_t i;
    int bit;

    for (i = 0; i < length; i++) {
        n = (n & 0xFFFFFF00u) + data[i];
        for (bit = 0; bit < 8; bit++) {
            n = (n & 0x80000000u) ? (n << 1) + 0x10210000u : n << 1;
        }
    }
    for (bit = 0; bit < 8; bit++) {
        n = (n & 0x80000000u) ? (n << 1) + 0x10210000u : n << 1;
    }
    return (uint16_t)(n >> 16);
}

uint8_t kcab_block_fits(const KcabBlock *block)
{
    if (!block || block->size == 0 || block->copies < 1 || block->copies > 2 || block->magic_len > block->size) {
        return 0;
    }
    if (block->magic_len && !block->magic) {
        return 0;
    }
    return (size_t)block->size * block->copies <= KCAB_GAME_AREA;
}

static uint8_t magic_ok(const uint8_t *data, const KcabBlock *block)
{
    return block->magic_len == 0 || memcmp(data, block->magic, block->magic_len) == 0;
}

kcab_result kcab_image_store(uint8_t image[KCAB_EEPROM_BYTES], const KcabBlock *block, const uint8_t *data)
{
    uint16_t crc;

    if (!image || !data || !kcab_block_fits(block) || !magic_ok(data, block)) {
        return KCAB_BAD_ARGUMENT;
    }
    memcpy(image + KCAB_GAME_OFFSET, data, block->size);
    if (block->copies == 2) {
        memcpy(image + KCAB_GAME_OFFSET + block->size, data, block->size);
    }
    crc = kcab_crc16(image + KCAB_GAME_OFFSET, block->size);
    image[CRC_LO] = image[CRC_LO2] = (uint8_t)(crc & 0xFFu);
    image[CRC_HI] = image[CRC_HI2] = (uint8_t)(crc >> 8);
    image[SIZE_A] = image[SIZE_B] = image[SIZE_A2] = image[SIZE_B2] = block->size;
    return KCAB_OK;
}

/* a copy is good when it starts with the magic and its checksum is one of the two stored */
static uint8_t copy_good(const uint8_t *copy, const KcabBlock *block, uint16_t crc_a, uint16_t crc_b, uint8_t *both)
{
    uint16_t crc;

    if (!magic_ok(copy, block)) {
        return 0;
    }
    crc = kcab_crc16(copy, block->size);
    *both = crc == crc_a && crc == crc_b;
    return crc == crc_a || crc == crc_b;
}

kcab_result kcab_image_fetch(const uint8_t image[KCAB_EEPROM_BYTES], const KcabBlock *block, uint8_t *data)
{
    uint8_t sizes, both = 0;
    uint16_t crc_a, crc_b;
    const uint8_t *first, *second;

    if (!image || !data || !kcab_block_fits(block)) {
        return KCAB_BAD_ARGUMENT;
    }
    sizes = (uint8_t)((image[SIZE_A] == block->size) + (image[SIZE_B] == block->size) + (image[SIZE_A2] == block->size) +
                      (image[SIZE_B2] == block->size));
    if (sizes == 0) {
        return KCAB_EMPTY; /* nothing of this layout: a blank chip, or another game's block */
    }
    crc_a = (uint16_t)(image[CRC_LO] | image[CRC_HI] << 8);
    crc_b = (uint16_t)(image[CRC_LO2] | image[CRC_HI2] << 8);
    first = image + KCAB_GAME_OFFSET;
    second = first + block->size;

    if (copy_good(first, block, crc_a, crc_b, &both)) {
        memcpy(data, first, block->size);
        /* a second copy that differs, a header byte that is wrong, or one checksum that is wrong: the data is good, but
           something was damaged */
        if (block->copies == 2 && memcmp(first, second, block->size) != 0) {
            both = 0;
        }
        return (both && sizes == 4) ? KCAB_OK : KCAB_RECOVERED;
    }
    if (block->copies == 2 && copy_good(second, block, crc_a, crc_b, &both)) {
        memcpy(data, second, block->size);
        return KCAB_RECOVERED;
    }
    return KCAB_INVALID;
}

kcab_result kcab_eeprom_write_verified(const KcabEeprom *eeprom, const uint8_t image[KCAB_EEPROM_BYTES])
{
    uint8_t back[KCAB_EEPROM_BYTES];

    if (!eeprom || !eeprom->read || !eeprom->write || !image) {
        return KCAB_BAD_ARGUMENT;
    }
    if (!eeprom->write(eeprom->context, image)) {
        return KCAB_WRITE_FAILED;
    }
    memset(back, 0, sizeof(back));
    if (!eeprom->read(eeprom->context, back) || memcmp(image, back, KCAB_EEPROM_BYTES) != 0) {
        return KCAB_VERIFY_FAILED;
    }
    return KCAB_OK;
}

kcab_result kcab_block_load(const KcabEeprom *eeprom, const KcabBlock *block, uint8_t *data)
{
    uint8_t image[KCAB_EEPROM_BYTES];

    if (!eeprom || !eeprom->read || !data || !kcab_block_fits(block)) {
        return KCAB_BAD_ARGUMENT;
    }
    memset(image, 0, sizeof(image));
    if (!eeprom->read(eeprom->context, image)) {
        return KCAB_NO_EEPROM;
    }
    return kcab_image_fetch(image, block, data);
}

kcab_result kcab_block_save(const KcabEeprom *eeprom, const KcabBlock *block, const uint8_t *data)
{
    uint8_t image[KCAB_EEPROM_BYTES];
    kcab_result result;

    if (!eeprom || !eeprom->read || !eeprom->write || !data || !kcab_block_fits(block)) {
        return KCAB_BAD_ARGUMENT;
    }
    memset(image, 0, sizeof(image));
    if (!eeprom->read(eeprom->context, image)) {
        return KCAB_NO_EEPROM;
    }
    result = kcab_image_store(image, block, data); /* only bytes 36 and up change: the BIOS bytes are what the chip gave */
    if (result != KCAB_OK) {
        return result;
    }
    return kcab_eeprom_write_verified(eeprom, image);
}

const char *kcab_result_text(kcab_result result)
{
    switch (result) {
    case KCAB_OK:
        return "done";
    case KCAB_RECOVERED:
        return "recovered from a damaged block";
    case KCAB_EMPTY:
        return "no settings stored";
    case KCAB_INVALID:
        return "stored settings are damaged";
    case KCAB_NO_EEPROM:
        return "no EEPROM";
    case KCAB_WRITE_FAILED:
        return "EEPROM write failed";
    case KCAB_VERIFY_FAILED:
        return "EEPROM write did not verify";
    case KCAB_BAD_ARGUMENT:
        return "bad argument";
    default:
        return "unknown result";
    }
}
