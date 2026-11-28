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

#define LGP_VERSION "1.1.0"
#define LGP_MAX_PORTS 8u

/* Exit codes of lgp_run() and of the command line. */
enum {
    LGP_OK = 0,
    LGP_EXPECT_FAILED = 1, /* the run finished but an --expect was never seen */
    LGP_ERR_ARGS = 2,
    LGP_ERR_CORE = 3,  /* the core would not load, or lacks a libretro function */
    LGP_ERR_LOAD = 4,  /* retro_load_game said no */
    LGP_ERR_IO = 5,    /* the record file or a capture output could not be created or written */
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

/* ---- guest addresses ------------------------------------------------------------------------------------------- */

/* Where a guest's RAM appears in its own address space, so a symbol address from the toolchain's nm can be turned into an
 * offset into the memory the core exposes. An address is accepted when it is already an offset (no higher than `mask`)
 * or when its bits above `mask` are one of `bases`; the offset is then `address & mask`. With no bases, every address
 * is accepted and masked, as --address-mask does.
 *
 * SH-4 (Dreamcast, NAOMI) is one example: RAM is mirrored at 0x0c000000 (physical), 0x8c000000 (cached) and 0xac000000
 * (uncached), and NAOMI's second 16 MiB bank follows the first, so mask 0x01ffffff with those three bases takes
 * 0x8c0e0100 to 0x000e0100 and refuses 0x0e000000. */
#define LGP_MAX_RAM_BASES 8u

typedef struct LgpRamWindow {
    uint32_t mask;
    uint32_t bases[LGP_MAX_RAM_BASES];
    unsigned base_count;
} LgpRamWindow;

void lgp_ram_window_init(LgpRamWindow *window, uint32_t mask);
int lgp_ram_window_add_base(LgpRamWindow *window, uint32_t base); /* 1, or 0 when full or `base` has bits in `mask` */
int lgp_ram_offset(const LgpRamWindow *window, uint32_t address, uint32_t *offset);
/* The same from text: a number in C syntax ("0x8c0e0100", "917760"), optional leading spaces and '+'. 1 or 0. */
int lgp_ram_parse_offset(const LgpRamWindow *window, const char *text, uint32_t *offset);

/* The address of `symbol` in a symbol listing written by nm ("8c0e0100 B _symbol", one per line). Returns 1 and sets
 * `*address`, or 0 when the file cannot be read or has no such symbol. */
int lgp_nm_lookup(const char *nm_path, const char *symbol, uint32_t *address);

/* Copy `count` 32-bit words at `offset` in a block of guest memory into `out` (host order, so a record of 32-bit fields,
 * floats included, can be read straight into a struct). The block need not be aligned. Returns 1, or 0 when the words
 * do not fit inside the block (nothing is copied then). */
int lgp_read_words(const uint8_t *memory, size_t size, uint32_t offset, void *out, size_t count, int big_endian);

/* ---- video and audio ------------------------------------------------------------------------------------------- */

/* What a run counts, from the first retro_run. */
typedef struct LgpCounters {
    unsigned runs;           /* retro_run calls */
    unsigned presented;      /* video callbacks with a new frame: pixels, or RETRO_HW_FRAME_BUFFER_VALID */
    unsigned repeated;       /* video callbacks with no data: the core says the previous frame stands (a dupe) */
    unsigned no_video;       /* retro_run calls that made no video callback at all */
    unsigned guest_stalls;   /* runs in which the guest's tick did not move (only with a probe tick) */
    unsigned guest_leaps;    /* guest ticks skipped, when the tick moved by more than one in a run */
    unsigned max_run_us;     /* the longest retro_run, in microseconds */
    uint64_t audio_frames;   /* stereo sample frames the core sent */
    unsigned captured_video; /* frames written to the video output */
    uint64_t captured_audio; /* stereo sample frames written to the audio output */
} LgpCounters;

/* What the last retro_run did. */
typedef struct LgpRunStats {
    unsigned presented, repeated; /* video callbacks of each kind in this run */
    unsigned audio_frames;
    unsigned run_us;
    unsigned tick_step; /* how far the guest's tick moved (0 without a probe tick, or before the guest started) */
} LgpRunStats;

/* Turn one software frame into top-down RGB24. `format` is RETRO_PIXEL_FORMAT_* (0 0RGB1555, 1 XRGB8888, 2 RGB565).
 * Returns 1, or 0 for an unknown format. */
int lgp_convert_rgb24(unsigned format, const void *pixels, unsigned width, unsigned height, size_t pitch, uint8_t *rgb);

/* ---- the frame hook -------------------------------------------------------------------------------------------- */

/* Given to the hook after every retro_run, with the guest's memory as that run left it. */
typedef struct LgpFrameInfo {
    unsigned host_frame;       /* the index of the retro_run just made, from 0 */
    int ready;                 /* the probe's guard: 0 while the guest's tick reads 0 or its memory is not there */
    unsigned tick;             /* the guest's tick (the host frame without a probe tick) */
    const uint8_t *memory;     /* the memory block config->memory_id after this run, or NULL */
    size_t memory_size;
    LgpRunStats run;
    const LgpCounters *total;  /* everything so far, this run included */
    double fps, sample_rate;   /* from the core's AV info (0 when it gave none) */
    unsigned ports;
} LgpFrameInfo;

/* What the hook decides for the next retro_run. */
typedef struct LgpFrameControl {
    /* Each port's buttons, one bit per RETRO_DEVICE_ID_JOYPAD_* id (bit 3 Start, bit 13 R2). Filled before the call with
     * what the replay would give (through the button map), or zero with no replay. A pressed L2 or R2 also reads as a
     * fully pressed analog trigger (see lgp_joypad_state). The left stick still comes from the replay. */
    uint16_t joypad[LGP_MAX_PORTS];
    /* Write the next run's video and audio to the capture outputs. Filled with the current state: capture starts by
     * itself the first time the guest is ready (when an output is set) and ends after capture_max_frames frames. */
    int capture;
    int stop; /* end the run after this frame */
} LgpFrameControl;

typedef void (*LgpFrameHook)(void *user, const LgpFrameInfo *info, LgpFrameControl *control);

/* What the core reads from a port driven by the hook: `joypad` as above. Answers retro_input_state for the joypad
 * (single buttons and RETRO_DEVICE_ID_JOYPAD_MASK) and for the analog buttons (RETRO_DEVICE_INDEX_ANALOG_BUTTON, which
 * cores read for the L2 and R2 triggers), where a pressed button reads 0x7fff. 0 for anything else. */
int16_t lgp_joypad_state(uint16_t joypad, unsigned device, unsigned index, unsigned id);

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

    /* added in 1.1.0; lgp_config_defaults leaves them off */
    LgpFrameHook frame_hook;  /* NULL, or called after every retro_run (see LgpFrameControl) */
    void *frame_user;         /* passed to frame_hook */
    const char *video_path;   /* NULL, or a file or named pipe for raw RGB24 frames, top-down, one per video callback */
    const char *audio_path;   /* NULL, or a file or named pipe for raw signed 16-bit little-endian stereo samples */
    FILE *video_out;          /* instead of video_path: an open stream (the caller closes it) */
    FILE *audio_out;          /* instead of audio_path */
    unsigned capture_width, capture_height; /* the frame size written; 0 takes the first frame's. A frame of another
                                               size is a capture error */
    unsigned capture_max_frames; /* stop capturing after this many video frames (0: no limit) */
} LgpConfig;

typedef struct LgpResult {
    unsigned frames_run;
    unsigned not_ready_frames; /* frames skipped because the guest had not started */
    unsigned last_tick;
    int stopped;               /* a stop rule ended the run */
    int expectations_met;
    int hw_render_requested;   /* the core asked for an OpenGL context */

    /* added in 1.1.0 */
    LgpCounters counters;
    double fps, sample_rate;   /* the core's AV info, 0 when it gave none */
    unsigned video_width, video_height; /* the size of the frames captured (0 when nothing was) */
    int capture_error;         /* a capture output could not be opened or written, or a frame changed size */
    int hw_context;            /* the core was given an OpenGL context */
} LgpResult;

void lgp_config_defaults(LgpConfig *config);

/* Load the core, run up to max_frames frames or until a stop rule or the hook says stop, unload. Returns an LGP_* code.
 * A hardware-render core gets a hidden window, an OpenGL core context and a framebuffer object to draw into, made with
 * SDL3 or SDL2 when the library was built with one; without a display or GPU that fails and the core is told it cannot
 * have one. A capture error makes the result LGP_ERR_IO once the run is over. */
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
