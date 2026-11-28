/* An EEPROM in RAM, with faults on demand: for a game's tests, and for a build that has no chip (an emulator without
 * one). Part of kos-naomi-cabinet 1.0.1 (MIT). */
#ifndef KCAB_RAM_H
#define KCAB_RAM_H

#include "kcab/eeprom.h"

typedef struct KcabRam {
    uint8_t image[KCAB_EEPROM_BYTES];
    /* faults: set before a call */
    uint8_t fail_reads; /* this many reads fail (return 0), then reads work again */
    uint8_t fail_writes; /* the same for writes: nothing is stored */
    uint8_t drop_write_after; /* non-zero: a write stores only this many bytes but says it worked (a torn write) */
    uint8_t corrupt_write_byte; /* non-zero: a write flips one bit of byte (this - 1) after storing, and says it worked */
    uint32_t reads; /* how many calls were made */
    uint32_t writes;
} KcabRam;

/* A chip with every byte `fill` (0xFF is a blank chip) and no faults. */
void kcab_ram_init(KcabRam *ram, uint8_t fill);

/* The callbacks over `ram`. */
KcabEeprom kcab_ram_eeprom(KcabRam *ram);

#endif
