/* The PS2SDK side of ps2-audsrv-stream: audsrv behind the PasOps table, and its start. Part of ps2-audsrv-stream 1.0.0
 * (MIT).
 *
 * Compile src/ps2.c only with the PS2SDK (the stream itself, src/pas.c, builds anywhere). Link -laudsrv.
 *
 * WHO OWNS WHAT. This glue does not reset the IOP or patch the loader; the program does that once, before any IOP
 * module is loaded, and the loader patch (sbv_patch_enable_lmb) must be in place before pas_ps2_start, because audsrv
 * is started from a buffer in memory. The glue carries no IRX binary: audsrv comes from the PS2SDK (iop/irx/audsrv.irx),
 * usually turned into a C array by `bin2c`. LIBSD, which audsrv needs, is loaded from the console's ROM (rom0:LIBSD).
 */
#ifndef PAS_PS2_H
#define PAS_PS2_H

#include "pas/pas.h"

/* What pas_ps2_start returns when it is negative. 0 is success. */
enum {
    PAS_PS2_LIBSD_FAILED = -1, /* rom0:LIBSD did not load */
    PAS_PS2_AUDSRV_FAILED = -2, /* the audsrv module did not load or did not start */
    PAS_PS2_RPC_FAILED = -3, /* audsrv_init did not answer */
    PAS_PS2_FORMAT_FAILED = -4, /* audsrv_set_format refused the rate */
    PAS_PS2_VOLUME_FAILED = -5, /* audsrv_set_volume refused the volume */
    PAS_PS2_BAD_ARGUMENT = -6
};

/* How to start. The format is always 16-bit stereo. */
typedef struct PasPs2Config {
    const void *audsrv; /* the audsrv IRX in memory */
    unsigned size_audsrv;
    int rate; /* samples per second, e.g. 22050, 44100 or 48000 */
    uint8_t volume; /* audsrv volume, 0..100 */
    uint16_t chunk_frames; /* the most frames rendered at once; 0 = PAS_MAX_FRAMES */
    PasRender render;
    void *user;
} PasPs2Config;

/* The SDK's own numbers for the step that failed: its return value, and for the module load the module's status. */
typedef struct PasPs2Failure {
    int result;
    int status;
} PasPs2Failure;

/* The audsrv calls as a PasOps table (context unused). */
PasOps pas_ps2_ops(void);

/*
 * Loads rom0:LIBSD, then audsrv from config->audsrv, then audsrv_init, audsrv_set_format(rate, 16 bits, 2 channels)
 * and audsrv_set_volume(volume), and sets the stream up over pas_ps2_ops(). Returns 0, or a PAS_PS2_* failure with the
 * stream left not ready (so pas_feed does nothing) and, when `failure` is not NULL, the SDK's numbers in it.
 */
int pas_ps2_start(PasStream *stream, const PasPs2Config *config, PasPs2Failure *failure);

/* A few words for a PAS_PS2_* code, for a log line: "LIBSD load", "audsrv module", "audio RPC init", "audio format",
 * "audio volume", "bad argument"; "" for anything else. */
const char *pas_ps2_step(int code);

/*
 * A config with the audsrv module from a `bin2c` array named as bin2c names it (`bin2c audsrv.irx out.c audsrv_irx`
 * gives `audsrv_irx` and `size_audsrv_irx`). The array is declared here and defined by the program: compile
 * src/ps2_bin2c.c only in a program that defines both, or fill a PasPs2Config yourself.
 */
extern unsigned char audsrv_irx[];
extern unsigned int size_audsrv_irx;
PasPs2Config pas_ps2_config_bin2c(int rate, uint8_t volume, PasRender render, void *user);

#endif
