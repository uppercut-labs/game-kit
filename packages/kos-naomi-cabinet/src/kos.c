/* KallistiOS glue. Builds only with KOS; on the NAOMI subarchitecture it uses the MIE driver. */
#include "kcab/kos.h"

#include <kos.h>
#ifdef _arch_sub_naomi
#include <dc/maple/mie.h>
#endif

#ifdef _arch_sub_naomi

static const mie_state_t *board(void)
{
    maple_device_t *device;

    if (mie_port0_mode() != MIE_PORT0_JVS) {
        return NULL;
    }
    device = maple_enum_type(0, MAPLE_FUNC_MIE);
    return device ? (const mie_state_t *)maple_dev_status(device) : NULL;
}

uint8_t kcab_kos_ready(void)
{
    return board() != NULL;
}

static uint8_t eeprom_read(void *context, uint8_t image[KCAB_EEPROM_BYTES])
{
    (void)context;
    return mie_get_eeprom(image) ? 1 : 0;
}

static uint8_t eeprom_write(void *context, const uint8_t image[KCAB_EEPROM_BYTES])
{
    uint8_t copy[KCAB_EEPROM_BYTES]; /* mie_set_eeprom takes a non-const buffer */
    size_t i;

    (void)context;
    for (i = 0; i < KCAB_EEPROM_BYTES; i++) {
        copy[i] = image[i];
    }
    return mie_set_eeprom(copy) ? 1 : 0;
}

KcabEeprom kcab_kos_eeprom(void)
{
    KcabEeprom e;

    e.context = NULL;
    e.read = eeprom_read;
    e.write = eeprom_write;
    return e;
}

uint8_t kcab_kos_read_inputs(KcabKosInputs *out)
{
    const mie_state_t *state = board();
    uint32_t switches = 0;

    if (!state) {
        return 0;
    }
    out->meter[0] = state->jvs.coin[0].count;
    out->meter[1] = state->jvs.coin[1].count;
    out->coin_pulse = state->jvs.coin_pulse;
    if (state->jvs.system.test || state->jvs.panel.psw.test) {
        switches |= KCAB_SW_TEST;
    }
    if (state->jvs.panel.psw.service || state->jvs.p1.service || state->jvs.p2.service) {
        switches |= KCAB_SW_SERVICE;
    }
    out->switches = switches;
    return 1;
}

#else /* a KOS target with no MIE bridge: nothing to read */

uint8_t kcab_kos_ready(void)
{
    return 0;
}

static uint8_t no_read(void *context, uint8_t image[KCAB_EEPROM_BYTES])
{
    (void)context;
    (void)image;
    return 0;
}

static uint8_t no_write(void *context, const uint8_t image[KCAB_EEPROM_BYTES])
{
    (void)context;
    (void)image;
    return 0;
}

KcabEeprom kcab_kos_eeprom(void)
{
    KcabEeprom e;

    e.context = NULL;
    e.read = no_read;
    e.write = no_write;
    return e;
}

uint8_t kcab_kos_read_inputs(KcabKosInputs *out)
{
    (void)out;
    return 0;
}

#endif
