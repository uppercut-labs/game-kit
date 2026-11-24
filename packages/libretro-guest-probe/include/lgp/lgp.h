/* libretro-guest-probe: run a libretro core with no window and no audio device, feed it scripted pad input, answer its
 * option queries, and read the guest's memory to see what the game did. See README.md.
 *
 * The parts are separate so they can be used (and tested) without a core: the option table, the input bridge, the memory
 * probe and the argument parser need no libretro core and no display. Only lgp_run() loads a core.
 *
 * C11. Depends on pad-replay (PadrSample, PadrScript). Not thread safe; lgp_run() keeps its state in statics because
 * libretro's callbacks carry no user pointer, so run one core at a time per process.
 */
#ifndef LGP_LGP_H
#define LGP_LGP_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "padr/padr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LGP_VERSION "0.1.0"
#define LGP_MAX_PORTS 8u

/* Exit codes of lgp_run() and of the command line. */
enum {
    LGP_OK = 0,
    LGP_EXPECT_FAILED = 1, /* the run finished but an --expect was never seen */
    LGP_ERR_ARGS = 2,
    LGP_ERR_CORE = 3,  /* the core would not load, or lacks a libretro function */
    LGP_ERR_LOAD = 4,  /* retro_load_game said no */
    LGP_ERR_IO = 5,    /* the record file could not be created or written */
    LGP_HELP = 100     /* lgp_args_parse() printed help or the version: not an error, nothing to run */
};

/* ---- core options ---------------------------------------------------------------------------------------------- */

/* The answers to a core's option queries (RETRO_ENVIRONMENT_GET_VARIABLE). Each entry is a key pattern and a value;
 * `*` in a pattern matches any run of characters (including none), so "*serial*" answers every key with "serial" in it.
 * The first entry that matches wins. A key nothing matches is not answered (the core uses its default). */
typedef struct LgpOptions LgpOptions;

LgpOptions *lgp_options_new(void);
void lgp_options_free(LgpOptions *options);
int lgp_options_add(LgpOptions *options, const char *pattern, const char *value); /* 1, or 0 on bad input / memory */
int lgp_options_parse(LgpOptions *options, const char *text);                     /* "pattern=value" */
/* The value for `key`, or NULL. `*first` (may be NULL) is set to 1 the first time that key is answered, so a caller can
 * log each answer once. The returned string stays valid until the table is freed. */
const char *lgp_options_get(LgpOptions *options, const char *key, int *first);
size_t lgp_options_count(const LgpOptions *options);
int lgp_glob_match(const char *pattern, const char *text);

/* ---- the input bridge ------------------------------------------------------------------------------------------ */

/* Which of the port's button bits means which libretro joypad button. The bits are the caller's (the same bit set the
 * replay file uses); the package knows no console. */
typedef struct LgpButtonMap {
    unsigned retro_id; /* RETRO_DEVICE_ID_JOYPAD_*: 0 b, 1 y, 2 select, 3 start, 4 up, 5 down, 6 left, 7 right, 8 a, 9 x,
                          10 l, 11 r, 12 l2, 13 r2, 14 l3, 15 r3 */
    uint16_t mask;     /* the button is down when any of these bits is set */
} LgpButtonMap;

#define LGP_MAX_MAP 16u

/* The libretro joypad id for a lower-case name ("start", "b", "l2"), or -1. */
int lgp_button_id(const char *name);

/* "start=0x0008" (name, then a number in C syntax) into an entry. Returns 1, or 0 when the name or number is bad. */
int lgp_map_parse(const char *text, LgpButtonMap *entry);

/* What the core sees from each port. Fill `pad[port]` before each retro_input_poll; lgp_pads_state answers
 * retro_input_state. A port that is not connected reads as all zeros. The left analog stick is (axis - 128) * 256. */
typedef struct LgpPads {
    LgpButtonMap map[LGP_MAX_MAP];
    unsigned map_count;
    PadrSample pad[LGP_MAX_PORTS];
    unsigned ports;
} LgpPads;

void lgp_pads_init(LgpPads *pads, const LgpButtonMap *map, size_t map_count, unsigned ports);
int16_t lgp_pads_state(const LgpPads *pads, unsigned port, unsigned device, unsigned index, unsigned id);

/* ---- the memory probe ------------------------------------------------------------------------------------------ */

/* Watches values in the guest's memory by address, and decides when to stop and whether the run saw what was expected.
 * An address is whatever the guest uses (a symbol address from the toolchain's nm); `address_mask` turns it into an
 * offset into the memory the core exposes (0x00ffffff for a console that mirrors RAM at 0x8c000000 and 0xac000000). */
typedef struct LgpProbe LgpProbe;

LgpProbe *lgp_probe_new(void);
void lgp_probe_free(LgpProbe *probe);
void lgp_probe_set_mask(LgpProbe *probe, uint32_t address_mask); /* default 0xffffffff */

/* A value of 1, 2 or 4 bytes, little endian (the libretro cores' hosts are little endian; an optional flag below swaps
 * for a big-endian guest). Names are up to 31 characters. */
int lgp_probe_watch(LgpProbe *probe, const char *name, uint32_t address, unsigned size, int big_endian);
/* The guest's own frame counter. While it reads 0 (or the memory is not there yet) the guest has not started and a run
 * skips those frames; the script's frames are this counter. Without it the script's frames are the host's. */
int lgp_probe_tick(LgpProbe *probe, uint32_t address, unsigned size, int big_endian);
/* The run passes only if the named watch equalled `value` at some sample. */
int lgp_probe_expect(LgpProbe *probe, const char *name, uint32_t value);
/* The run ends early when the named watch equals `value` and the guest's tick is at least `min_tick`. */
int lgp_probe_stop(LgpProbe *probe, const char *name, uint32_t value, unsigned min_tick);

/* Command line forms: "name=ADDR[:SIZE]" (SIZE 1, 2 or 4, default 4), "ADDR[:SIZE]", "name=VALUE",
 * "name=VALUE[@MIN_TICK]". Numbers take C syntax (0x for hex). They return 1, or 0 for text that does not fit. */
int lgp_probe_parse_watch(LgpProbe *probe, const char *text);
int lgp_probe_parse_tick(LgpProbe *probe, const char *text);
int lgp_probe_parse_expect(LgpProbe *probe, const char *text);
int lgp_probe_parse_stop(LgpProbe *probe, const char *text);

/* Read `width` bytes at `address` (after the mask) from a block of memory. Returns 1, or 0 when it does not fit. */
int lgp_memory_read(const uint8_t *memory, size_t size, uint32_t address, uint32_t mask, unsigned width, int big_endian,
                    uint32_t *value);

typedef struct LgpSample {
    int ready;      /* 0: the guest has not started or its memory is not there; skip this frame */
    unsigned tick;  /* the guest's frame counter (the host frame when no tick is set) */
    int stop;       /* a stop rule is met */
} LgpSample;

/* Read every watch once. Logs "WATCH name host_frame=N tick=T value=0x..." when a value changes and "GUEST MEMORY
 * unavailable ..." once when it cannot be read. `log` may be NULL. */
LgpSample lgp_probe_sample(LgpProbe *probe, const uint8_t *memory, size_t size, unsigned host_frame, FILE *log);

/* Prints one "EXPECT name=value PASS|FAIL" line per expectation. Returns 1 when all were met (also with none set). */
int lgp_probe_report(const LgpProbe *probe, FILE *out);
int lgp_probe_has_tick(const LgpProbe *probe);

/* ---- the run --------------------------------------------------------------------------------------------------- */

typedef struct LgpConfig {
    const char *core_path;    /* the libretro core (a shared library) */
    const char *content_path; /* the content to load; NULL for a core that runs with no game */
    const char *system_dir;   /* the core's system and save folder; give each run its own */
    const char *record_path;  /* NULL, or a new file to write the pad states the core asked for (pad-replay format) */
    PadrScript *script;       /* NULL, or the replay (the run advances it): slot N feeds port N */
    LgpOptions *options;      /* NULL, or the core option answers */
    LgpProbe *probe;          /* NULL, or the memory probe */
    LgpButtonMap map[LGP_MAX_MAP];
    unsigned map_count;
    unsigned ports;           /* ports fed, 1 to LGP_MAX_PORTS (default 1) */
    unsigned memory_id;       /* RETRO_MEMORY_* to probe (default 2, system RAM) */
    unsigned max_frames;      /* the bound on host frames (default 20000) */
    unsigned not_ready_delay_ms; /* sleep when the guest has not started, so a slow core is given time (default 1) */
    unsigned frame_delay_ms;  /* sleep after every frame (default 0) */
    unsigned gl_major, gl_minor; /* the hidden OpenGL core context made if the core asks for one (default 3.2) */
    FILE *log;                /* default stderr */
} LgpConfig;

typedef struct LgpResult {
    unsigned frames_run;
    unsigned not_ready_frames; /* frames skipped because the guest had not started */
    unsigned last_tick;
    int stopped;               /* a stop rule ended the run */
    int expectations_met;
    int hw_render_requested;   /* the core asked for an OpenGL context */
} LgpResult;

void lgp_config_defaults(LgpConfig *config);

/* Load the core, run up to max_frames frames or until a stop rule, unload. Returns an LGP_* code. A hardware-render core
 * gets a hidden OpenGL context made with SDL3 when the library was built with it; without a display or GPU that fails
 * and the core is told it cannot have one. */
int lgp_run(const LgpConfig *config, LgpResult *result);

/* ---- the command line ------------------------------------------------------------------------------------------ */

/* Everything a command line builds, owned by the setup. */
typedef struct LgpSetup {
    LgpConfig config;
    LgpOptions *options;
    LgpProbe *probe;
    PadrScript *script;
} LgpSetup;

/* Parse argv (see `lgp_usage`) into a setup. Returns LGP_OK, LGP_ERR_ARGS (a message went to `err`), or LGP_HELP. The
 * setup must be freed with lgp_setup_free in every case. `--replay` is read with the strict pad-replay reader. */
int lgp_args_parse(int argc, char **argv, LgpSetup *setup, FILE *err);
void lgp_setup_free(LgpSetup *setup);
const char *lgp_usage(void);

#ifdef __cplusplus
}
#endif

#endif
