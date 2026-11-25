/*
 * Builds only with KallistiOS, in a program made for NAOMI, and only with -Wl,--wrap=hardware_sys_mode on the link.
 *
 * An emulator can load a cartridge in NAOMI mode while KOS's system-mode register still reads as a retail console.
 * This wraps hardware_sys_mode so that a retail answer reads as NAOMI and keeps the region the real call gave. Do not
 * link it into a program that also runs on a retail console.
 */
#include "kcab/kos.h"

#include <kos.h>

int __real_hardware_sys_mode(int *region);

int __wrap_hardware_sys_mode(int *region)
{
    int detected = __real_hardware_sys_mode(region);

    return detected == HW_TYPE_RETAIL ? HW_TYPE_NAOMI : detected;
}

int kcab_kos_raw_hardware_mode(void)
{
    return __real_hardware_sys_mode(NULL);
}
