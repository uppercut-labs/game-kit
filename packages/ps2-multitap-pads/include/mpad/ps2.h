/* The PS2SDK side of ps2-multitap-pads: libpad and libmtap behind the MpadOps table. Part of ps2-multitap-pads 1.0.0 (MIT).
 *
 * Compile src/ps2.c only with the PS2SDK (the pad poller itself, src/mpad.c, builds anywhere). Link -lpadx -lmtap
 * (or -lpad -lmtap).
 *
 * WHO OWNS WHAT. This glue does not reset the IOP, initialise SIF RPC or patch the loader, because those change the
 * whole machine and every IOP service (memory card, sound, files) shares them. The program does, once, before this:
 *
 *     sceSifInitRpc(0);
 *     while (!SifIopReset("", 0)) {}
 *     while (!SifIopSync()) {}
 *     sceSifInitRpc(0);
 *     sbv_patch_enable_lmb();      // lets SifExecModuleBuffer load a module from memory
 *
 * then calls mpad_ps2_start(), then loads whatever other IOP modules it wants. This glue loads three modules, sio2man,
 * mtapman and padman, from buffers the caller supplies. It carries no IRX binary: they come from the PS2SDK
 * (the .irx files under iop/irx), usually turned into C arrays by `bin2c`.
 */
#ifndef MPAD_PS2_H
#define MPAD_PS2_H

#include "mpad/mpad.h"

/* The three IOP modules, as buffers in memory. */
typedef struct MpadPs2Drivers {
    const void *sio2man;
    unsigned size_sio2man;
    const void *mtapman;
    unsigned size_mtapman;
    const void *padman;
    unsigned size_padman;
} MpadPs2Drivers;

/* What mpad_ps2_start returns when it is negative. Zero or more is the number of endpoints that opened. */
enum {
    MPAD_PS2_SIO2MAN_FAILED = -1, /* the module did not load or did not start */
    MPAD_PS2_MTAPMAN_FAILED = -2,
    MPAD_PS2_PADMAN_FAILED = -3,
    MPAD_PS2_RPC_FAILED = -4, /* mtapInit or padInit did not answer */
    MPAD_PS2_BAD_ARGUMENT = -5
};

/* Loads the three modules and starts the multitap and pad services. Does not open any pad. */
int mpad_ps2_load_drivers(const MpadPs2Drivers *drivers);

/* The libpad and libmtap calls as an MpadOps table (context unused). */
MpadOps mpad_ps2_ops(void);

/* mpad_ps2_load_drivers, then mtapInit and padInit(0), then mpad_init with mpad_ps2_ops(). */
int mpad_ps2_start(MpadPoller *poller, const MpadPs2Drivers *drivers, uint8_t search_grace);

/*
 * Drivers from `bin2c` arrays named as bin2c names them (`bin2c sio2man.irx out.c sio2man_irx` gives `sio2man_irx` and
 * `size_sio2man_irx`). The arrays are declared here and defined by the program: compile src/ps2_bin2c.c only in a
 * program that defines all six, or fill an MpadPs2Drivers yourself.
 */
extern unsigned char sio2man_irx[];
extern unsigned char mtapman_irx[];
extern unsigned char padman_irx[];
extern unsigned int size_sio2man_irx;
extern unsigned int size_mtapman_irx;
extern unsigned int size_padman_irx;
int mpad_ps2_start_bin2c(MpadPoller *poller, uint8_t search_grace);

#endif
