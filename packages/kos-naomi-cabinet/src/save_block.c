#include "kcab/save_block.h"

#include <string.h>

static uint8_t sizes_match(const uint8_t *image, uint8_t record_bytes)
{
    return image[38] == record_bytes && image[39] == record_bytes && image[42] == record_bytes && image[43] == record_bytes;
}

static uint8_t all_ff(const uint8_t *bytes, size_t length)
{
    size_t i;

    for (i = 0; i < length; i++) {
        if (bytes[i] != 0xFFu) {
            return 0;
        }
    }
    return 1;
}

uint8_t kcab_sb_fits(const sb_format *format)
{
    size_t bytes = sb_record_bytes(format);

    return bytes != 0 && bytes <= KCAB_SB_MAX_RECORD_BYTES;
}

static sb_result sb_read(void *context, uint8_t slot, uint8_t *buffer, size_t capacity, size_t *length)
{
    const KcabSbStorage *s = context;
    uint8_t image[KCAB_EEPROM_BYTES];
    const uint8_t *record;

    if (slot >= SB_SLOT_COUNT || !buffer || !length) {
        return SB_BAD_ARGUMENT;
    }
    memset(image, 0, sizeof(image));
    if (!s->eeprom->read(s->eeprom->context, image)) {
        return SB_NO_MEDIA;
    }
    record = image + KCAB_GAME_OFFSET + (size_t)slot * s->record_bytes;
    if (!sizes_match(image, s->record_bytes) || all_ff(record, s->record_bytes)) {
        return SB_EMPTY;
    }
    memcpy(buffer, record, capacity < s->record_bytes ? capacity : s->record_bytes);
    *length = s->record_bytes;
    return SB_OK;
}

static sb_result sb_write(void *context, uint8_t slot, const uint8_t *buffer, size_t length)
{
    const KcabSbStorage *s = context;
    uint8_t image[KCAB_EEPROM_BYTES], back[KCAB_EEPROM_BYTES];
    uint16_t crc;

    if (slot >= SB_SLOT_COUNT || !buffer || length != s->record_bytes) {
        return SB_BAD_ARGUMENT;
    }
    memset(image, 0, sizeof(image));
    if (!s->eeprom->read(s->eeprom->context, image)) {
        return SB_NO_MEDIA;
    }
    if (!sizes_match(image, s->record_bytes)) {
        /* another layout (or a blank chip): start this one with both slots empty */
        memset(image + KCAB_GAME_OFFSET, 0xFF, (size_t)s->record_bytes * SB_SLOT_COUNT);
    }
    memcpy(image + KCAB_GAME_OFFSET + (size_t)slot * s->record_bytes, buffer, length);
    crc = kcab_crc16(image + KCAB_GAME_OFFSET, s->record_bytes); /* the BIOS checksum covers the first slot */
    image[36] = image[40] = (uint8_t)(crc & 0xFFu);
    image[37] = image[41] = (uint8_t)(crc >> 8);
    image[38] = image[39] = image[42] = image[43] = s->record_bytes;
    if (!s->eeprom->write(s->eeprom->context, image)) {
        return SB_IO;
    }
    memset(back, 0, sizeof(back));
    if (!s->eeprom->read(s->eeprom->context, back)) {
        return SB_IO;
    }
    return memcmp(image, back, sizeof(image)) == 0 ? SB_OK : SB_VERIFY;
}

sb_storage kcab_sb_storage(KcabSbStorage *context, const KcabEeprom *eeprom, const sb_format *format)
{
    sb_storage storage;

    context->eeprom = eeprom;
    context->record_bytes = kcab_sb_fits(format) ? (uint8_t)sb_record_bytes(format) : 0; /* 0: every write is refused */
    storage.context = context;
    storage.read = sb_read;
    storage.write = sb_write;
    return storage;
}
