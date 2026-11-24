#include "kcab/ram.h"

#include <string.h>

static uint8_t ram_read(void *context, uint8_t image[KCAB_EEPROM_BYTES])
{
    KcabRam *ram = context;

    ram->reads++;
    if (ram->fail_reads) {
        ram->fail_reads--;
        return 0;
    }
    memcpy(image, ram->image, KCAB_EEPROM_BYTES);
    return 1;
}

static uint8_t ram_write(void *context, const uint8_t image[KCAB_EEPROM_BYTES])
{
    KcabRam *ram = context;
    size_t count = KCAB_EEPROM_BYTES;

    ram->writes++;
    if (ram->fail_writes) {
        ram->fail_writes--;
        return 0;
    }
    if (ram->drop_write_after && ram->drop_write_after < count) {
        count = ram->drop_write_after;
    }
    memcpy(ram->image, image, count);
    if (ram->corrupt_write_byte && ram->corrupt_write_byte <= KCAB_EEPROM_BYTES) {
        ram->image[ram->corrupt_write_byte - 1] ^= 1u;
    }
    return 1;
}

void kcab_ram_init(KcabRam *ram, uint8_t fill)
{
    memset(ram, 0, sizeof(*ram));
    memset(ram->image, fill, sizeof(ram->image));
}

KcabEeprom kcab_ram_eeprom(KcabRam *ram)
{
    KcabEeprom e;

    e.context = ram;
    e.read = ram_read;
    e.write = ram_write;
    return e;
}
