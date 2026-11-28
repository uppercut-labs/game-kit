/* The KallistiOS side of kos-naomi-cabinet: the MIE bridge's EEPROM, coin meters and switches behind the package's
 * types. Part of kos-naomi-cabinet 1.0.1 (MIT).
 *
 * Compile src/kos.c and src/kos_naomi_mode.c only with KallistiOS (the NAOMI subarchitecture; on another KOS target
 * kcab_kos_ready reports 0 and the rest do nothing). The rest of the package builds anywhere.
 */
#ifndef KCAB_KOS_H
#define KCAB_KOS_H

#include "kcab/cabinet.h"
#include "kcab/eeprom.h"

/*
 * 1 when the cabinet's I/O board is there: port A is wired to the JVS bridge and the MIE device has been
 * enumerated. The Maple scan takes a moment after boot, so a game polls this until it is 1 before it loads its
 * settings (a game that gives up after a few seconds treats the cabinet as having no EEPROM).
 */
uint8_t kcab_kos_ready(void);

/* The bridge's EEPROM as a KcabEeprom (mie_get_eeprom and mie_set_eeprom). */
KcabEeprom kcab_kos_eeprom(void);

/* What one poll of the I/O board says. */
typedef struct KcabKosInputs {
    uint16_t meter[2]; /* the two coin slots' running meters (for kcab_coins_update) */
    uint8_t coin_pulse; /* one-shot insert flags, bit per slot (for kcab_coins_pulse): use the meters or this, not both */
    uint32_t switches; /* KCAB_SW_TEST and KCAB_SW_SERVICE: the system or panel test switch; the panel or either player's service switch */
} KcabKosInputs;

/* Reads the latest state the driver has cached (no bus traffic). Returns 0, leaving `out` alone, when the board is not ready. */
uint8_t kcab_kos_read_inputs(KcabKosInputs *out);

/*
 * The hardware type KallistiOS read, before kos_naomi_mode.c's override (HW_TYPE_*; 0 if unknown). Emulators that
 * load a cartridge as NAOMI can still report a retail system mode; src/kos_naomi_mode.c wraps hardware_sys_mode so
 * that a retail answer becomes NAOMI, in a program built only for NAOMI. Link with -Wl,--wrap=hardware_sys_mode to use
 * it.
 */
int kcab_kos_raw_hardware_mode(void);

#endif
