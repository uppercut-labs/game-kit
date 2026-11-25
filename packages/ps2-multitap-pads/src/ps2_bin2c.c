/* mpad_ps2_start_bin2c: for a program that embeds the three modules with bin2c under the names in mpad/ps2.h. */
#include "mpad/ps2.h"

int mpad_ps2_start_bin2c(MpadPoller *poller, uint8_t search_grace)
{
    MpadPs2Drivers drivers;
    drivers.sio2man = sio2man_irx;
    drivers.size_sio2man = size_sio2man_irx;
    drivers.mtapman = mtapman_irx;
    drivers.size_mtapman = size_mtapman_irx;
    drivers.padman = padman_irx;
    drivers.size_padman = size_padman_irx;
    return mpad_ps2_start(poller, &drivers, search_grace);
}
