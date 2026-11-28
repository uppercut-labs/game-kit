/* The whole-program helpers in mpad/ps2.h: the IOP reset, and all eight pads on one static poller. Builds only with the
 * PS2SDK (or the test stubs in tests/ps2sdk-stub). Needs src/ps2_bin2c.c and the program's bin2c module arrays. */
#include "mpad/ps2.h"

#include <iopcontrol.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>

/* Static: the pad driver writes into its DMA buffers for as long as the pads are open. Zeroed, it polls as absent. */
static MpadPoller g_poller;

void mpad_ps2_reset_iop(void)
{
    /* Start with clean IOP services even when the ELF came from a launcher. */
    sceSifInitRpc(0);
    while (!SifIopReset("", 0)) {
    }
    while (!SifIopSync()) {
    }
    sceSifInitRpc(0);
    sbv_patch_enable_lmb(); /* lets SifExecModuleBuffer start a module from memory */
}

int mpad_ps2_open_all(uint8_t search_grace)
{
    uint8_t endpoint;
    int opened = mpad_ps2_start_bin2c(&g_poller, search_grace);

    if (opened < 0) {
        printf("Controller start failed (code %d)\n", opened);
        return opened;
    }
    for (endpoint = 0; endpoint < MPAD_ENDPOINTS; ++endpoint) {
        if (!mpad_is_open(&g_poller, endpoint)) {
            printf("Controller port %u slot %u failed to open\n", (unsigned)(endpoint / MPAD_SLOTS + 1),
                   (unsigned)(endpoint % MPAD_SLOTS + 1));
        }
    }
    return opened;
}

void mpad_ps2_poll_all(MpadSample out[MPAD_ENDPOINTS])
{
    mpad_poll(&g_poller, out);
}

MpadPoller *mpad_ps2_poller(void)
{
    return &g_poller;
}
