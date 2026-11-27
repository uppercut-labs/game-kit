/* sgdkstub: what the host stub of genesis.h recorded. A test resets it, runs game code, and reads the state.
 *
 * This is a call recorder, not a Mega Drive emulator: nothing here runs 68000 or Z80 code, times a DMA, draws a frame
 * or plays a sample. It records what the game asked for and checks the calls that real hardware or SGDK would
 * misbehave on (a write outside the plane, a palette index past 63, two tile sets loaded over each other). */
#ifndef SGDKSTUB_H
#define SGDKSTUB_H

#include <stdio.h>

#include "genesis.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SGDKSTUB_VERSION "1.0.0"

#define SGS_PLANE_COUNT 3
#define SGS_MAX_PLANE_W 64
#define SGS_MAX_PLANE_H 64
#define SGS_MAX_TILES 2048
#define SGS_MAX_SPRITES 80
#define SGS_MAX_AUDIO_CALLS 64
#define SGS_PSG_LOG_SIZE 1024
#define SGS_SRAM_SIZE 65536
#define SGS_MAX_JOY_SCRIPT 64
#define SGS_XGM2_CHANNELS 3
#define SGS_MAX_ALERTS 16
#define SGS_ALERT_LEN 128

typedef struct {
    bool stopped; /* true for an XGM2_stopPCM entry */
    const u8 *sample;
    u32 length;
    s8 channel; /* -1 is SOUND_PCM_CH_AUTO */
    u8 priority;
    bool half_rate;
    bool loop;
} SgsAudioCall;

typedef struct {
    u16 tiles[SGS_PLANE_COUNT][SGS_MAX_PLANE_H][SGS_MAX_PLANE_W]; /* the attribute word last written to each cell */
    char text[SGS_PLANE_COUNT][SGS_MAX_PLANE_H][SGS_MAX_PLANE_W]; /* the character VDP_drawText put there, else 0 */
    u32 tile_pixels[SGS_MAX_TILES][8];                            /* loaded tile data, 8 words a tile */
    u8 tile_loaded[SGS_MAX_TILES];
    u16 palette[64];
    u16 background_index;
    u16 screen_width; /* 320 or 256 */
    u16 plane_w, plane_h; /* from VDP_setPlaneSize; 64 x 32 until a game sets it */
    s16 hscroll[SGS_PLANE_COUNT];
    s16 vscroll[SGS_PLANE_COUNT];
    VDPPlane text_plane;
    u16 text_palette;
    bool text_priority;

    Sprite sprites[SGS_MAX_SPRITES];
    u16 sprite_count;
    u32 sprite_updates;

    SgsAudioCall audio[SGS_MAX_AUDIO_CALLS];
    u16 audio_count;
    const u8 *channel_sample[SGS_XGM2_CHANNELS];
    bool channel_loop[SGS_XGM2_CHANNELS];
    u16 z80_driver;

    u8 psg_log[SGS_PSG_LOG_SIZE]; /* every byte written to the PSG, in order */
    u16 psg_log_length;

    u8 sram[SGS_SRAM_SIZE];
    u8 sram_mode; /* 0 off, 1 read only, 2 read and write */

    u16 joy_state[2];                  /* what JOY_readJoypad gives once its script runs out */
    u16 joy_script[2][SGS_MAX_JOY_SCRIPT];
    u16 joy_script_length[2];
    u16 joy_script_index[2];

    char alerts[SGS_MAX_ALERTS][SGS_ALERT_LEN]; /* KDebug_Alert text, oldest first */
    u16 alert_count;

    u32 tile_writes;
    u16 max_tile_x, max_tile_y; /* the largest cell written so far */
    u32 palette_writes;
    u32 clears;
    u32 vblanks;

    u32 violations; /* calls that broke a check; see sgs_last_violation() */
} SgsState;

extern SgsState sgs;

/* Checks that are a game's own policy rather than a hardware fault. They are off (zero) until a test turns them on,
 * and sgs_reset() leaves them as they are, so a test sets them once. */
typedef struct {
    u16 write_limit_w, write_limit_h;   /* tile and text writes at or past these cells are violations, e.g. 40 x 28 for the
                                           screen; 0 means the plane size */
    s16 sprite_bound_w, sprite_bound_h; /* a sprite placed outside 0..w-1, 0..h-1 is a violation; 0 turns it off */
    bool require_loaded_tiles;          /* VDP_setTileMapXY must name a tile loaded with VDP_loadTileSet (or tile 0) */
    bool forbid_tile_overlap;           /* VDP_loadTileSet over a tile that is already loaded is a violation */
    FILE *alert_stream;                 /* KDebug_Alert also prints "ROM: <text>" here */
} SgsOptions;

extern SgsOptions sgs_opt;

/* The state starts as after a power-on, so a test that never calls this works. This puts the recorded state back: empty planes, nothing loaded, 320 wide, 64 x 32 planes, the
 * joypads at rest, the random seed at its start. Keeps sgs_opt and the failure handler. */
void sgs_reset(void);

/* What happens at a violation. The default prints "sgdkstub: <what>" to stderr and aborts, so a test that breaks a
 * rule stops at the call. A handler gets the text instead; NULL records the violation (sgs.violations and
 * sgs_last_violation()) and carries on, which is how the stub's own test checks that a check fires. */
typedef void (*SgsFailHandler)(const char *what);
void sgs_set_fail_handler(SgsFailHandler handler);
void sgs_record_violations_only(void);
const char *sgs_last_violation(void);

/* Planes and tiles */
u16 sgs_tile_at(VDPPlane plane, u16 x, u16 y);
/* The text drawn on a row, and ' ' elsewhere. out needs plane width + 1 bytes (SGS_MAX_PLANE_W + 1 is always enough). */
void sgs_row_text(VDPPlane plane, u16 y, char *out);
bool sgs_row_contains(VDPPlane plane, u16 y, const char *text);
/* True when the tile the attribute word names has a non-zero pixel word. */
bool sgs_tile_has_ink(u16 attr);

/* Joypads: a script is read one word per JOY_readJoypad call, then the held state takes over. */
void sgs_joy_set(u16 port, u16 state);
void sgs_joy_script(u16 port, const u16 *states, u16 count);

/* Sound */
void sgs_clear_audio_calls(void);

/* The PSG log read as the chip reads it: a first byte with bit 7 set picks channel and register, a tone's second byte
 * (bit 7 clear) holds its high six bits. */
typedef struct {
    u8 channel;   /* 0 to 2 tone, 3 noise */
    bool volume;  /* false: tone period (or the noise control on channel 3) */
    u16 value;    /* attenuation 0 to 15 for volume; the 10-bit period for a tone once its second byte arrived */
} SgsPsgEvent;
/* Fills out with up to max events, oldest first, and returns how many the log holds. */
u16 sgs_psg_events(SgsPsgEvent *out, u16 max);
bool sgs_psg_has_tone(u8 channel);                  /* any tone write for the channel */
bool sgs_psg_has_envelope(u8 channel, u8 value);    /* a volume write of exactly value */
bool sgs_psg_tone_changed(u8 channel);              /* two different complete tone periods were written */

#ifdef __cplusplus
}
#endif

#endif
