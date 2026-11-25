/* genesis.h for host tests (sgdk-host-stub). Our own declarations of the call shapes a Mega Drive game takes from
 * SGDK, so game logic can be built with a host compiler and run under ctest. It holds no SGDK source and no SGDK
 * header text; each function records what it was given in the state declared in sgdkstub/sgdkstub.h.
 *
 * A game that does `#include <genesis.h>` finds this file when the stub's include folder comes first on the include
 * path. SGDK's own header gives a game the C string and print functions without a further include, so this one
 * includes <stdio.h>, <stdlib.h> and <string.h>. Only the calls the games used so far are here; a missing call is a
 * link error, which is how to find out that the stub needs one more. */
#ifndef SGDKSTUB_GENESIS_H
#define SGDKSTUB_GENESIS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* ---- video ---- */

/* The numbers are the stub's own; code should name the planes. */
typedef enum { BG_A = 0, BG_B = 1, WINDOW = 2 } VDPPlane;
typedef enum { CPU, DMA, DMA_QUEUE, DMA_QUEUE_COPY } TransferMethod;

#define PAL0 0
#define PAL1 1
#define PAL2 2
#define PAL3 3

/* The first tile index a game may load into. The stub fixes it at 16; the real value follows the VRAM layout. */
#define TILE_USER_INDEX 16
#define TILE_INDEX_MASK 0x07ffu
#define TILE_ATTR(pal, priority, vflip, hflip) \
    ((u16)(((pal) << 13) | ((priority) << 15) | ((vflip) << 12) | ((hflip) << 11)))
#define TILE_ATTR_FULL(pal, priority, vflip, hflip, index) \
    ((u16)(TILE_ATTR(pal, priority, vflip, hflip) | (index)))

/* RGB888 constants to a 3-bit-per-channel Mega Drive colour, rounding to the nearest level (255 stays the top level).
 * This is the rounding the games' runtime code relies on; a palette built from a PNG by the resource compiler is a
 * separate conversion and is not modelled here. */
#define SGS_RGB3_(value) (((value) > 239 ? 255 : (value) + 16) >> 5)
#define RGB8_8_8_TO_VDPCOLOR(r, g, b) \
    ((u16)((SGS_RGB3_(r) << 1) | (SGS_RGB3_(g) << 5) | (SGS_RGB3_(b) << 9)))
#define RGB24_TO_VDPCOLOR(rgb24) \
    RGB8_8_8_TO_VDPCOLOR(((rgb24) >> 16) & 0xff, ((rgb24) >> 8) & 0xff, (rgb24) & 0xff)

typedef struct {
    u16 compression;
    u16 numTile;
    const u32 *tiles; /* 8 words per tile, 4 bits a pixel */
} TileSet;

typedef struct {
    u16 length;
    const u16 *data;
} Palette;

bool VDP_loadTileSet(const TileSet *tileset, u16 index, TransferMethod transfer);
void VDP_setTileMapXY(VDPPlane plane, u16 attr, u16 x, u16 y);
void VDP_fillTileMapRect(VDPPlane plane, u16 attr, u16 x, u16 y, u16 w, u16 h);
void VDP_clearPlane(VDPPlane plane, bool wait);
void VDP_setPlaneSize(u16 w, u16 h, bool setupVram);
void VDP_setHorizontalScroll(VDPPlane plane, s16 value);
void VDP_setVerticalScroll(VDPPlane plane, s16 value);
void VDP_drawText(const char *text, u16 x, u16 y);
void VDP_setTextPlane(VDPPlane plane);
void VDP_setTextPalette(u16 palette);
void VDP_setTextPriority(u16 priority);
void VDP_setBackgroundColor(u16 index);
void VDP_setScreenWidth320(void);
void VDP_setScreenWidth256(void);

void PAL_setPalette(u16 palette, const u16 *data, TransferMethod transfer);
void PAL_setColor(u16 index, u16 value);

/* ---- sprites ---- */

typedef enum { VISIBLE, HIDDEN, AUTO_FAST, AUTO_SLOW } SpriteVisibility;

/* numAnimation and numFrame are the stub's own: a test that fills them in has SPR_setAnim and SPR_setFrame checked
 * against them (frames per animation); left at 0 they are not checked. */
typedef struct {
    const Palette *palette;
    u16 numAnimation;
    u16 numFrame;
} SpriteDefinition;

typedef struct {
    const SpriteDefinition *definition;
    s16 x;
    s16 y;
    u16 attr;
    u16 anim;
    u16 frame;
    u16 visibility;
} Sprite;

void SPR_init(void);
Sprite *SPR_addSprite(const SpriteDefinition *definition, s16 x, s16 y, u16 attr);
void SPR_setPosition(Sprite *sprite, s16 x, s16 y);
void SPR_setAnim(Sprite *sprite, s16 anim);
void SPR_setFrame(Sprite *sprite, s16 frame);
void SPR_setVisibility(Sprite *sprite, SpriteVisibility visibility);
void SPR_update(void);

/* ---- input ---- */

#define JOY_1 0
#define JOY_2 1

#define BUTTON_UP 0x0001u
#define BUTTON_DOWN 0x0002u
#define BUTTON_LEFT 0x0004u
#define BUTTON_RIGHT 0x0008u
#define BUTTON_B 0x0010u
#define BUTTON_C 0x0020u
#define BUTTON_A 0x0040u
#define BUTTON_START 0x0080u
#define BUTTON_Z 0x0100u
#define BUTTON_Y 0x0200u
#define BUTTON_X 0x0400u
#define BUTTON_MODE 0x0800u

void JOY_init(void);
u16 JOY_readJoypad(u16 port);

/* ---- sound ---- */

#define Z80_DRIVER_XGM2 5

typedef enum {
    SOUND_PCM_CH_AUTO = -1,
    SOUND_PCM_CH1,
    SOUND_PCM_CH2,
    SOUND_PCM_CH3,
    SOUND_PCM_CH4
} SoundPCMChannel;

void Z80_loadDriver(u16 driver, bool wait);
bool XGM2_playPCMEx(const u8 *sample, u32 length, SoundPCMChannel channel, u8 priority, bool halfRate, bool loop);
void XGM2_stopPCM(SoundPCMChannel channel);

#define PSG_ENVELOPE_MIN 15
#define PSG_ENVELOPE_MAX 0
#define PSG_NOISE_TYPE_PERIODIC 0
#define PSG_NOISE_TYPE_WHITE 1
#define PSG_NOISE_FREQ_CLOCK2 0
#define PSG_NOISE_FREQ_CLOCK4 1
#define PSG_NOISE_FREQ_CLOCK8 2
#define PSG_NOISE_FREQ_TONE3 3

void PSG_write(u8 value);
void PSG_reset(void);
void PSG_setEnvelope(u8 channel, u8 value);
void PSG_setTone(u8 channel, u16 value);
void PSG_setNoise(u8 type, u8 frequency);

/* ---- save RAM ---- */

void SRAM_enable(void);
void SRAM_enableRO(void);
void SRAM_disable(void);
u8 SRAM_readByte(u32 offset);
void SRAM_writeByte(u32 offset, u8 value);

/* ---- system ---- */

/* Returns TRUE so a caller that tests the result and a caller that ignores it both build. */
bool SYS_doVBlankProcess(void);
void KDebug_Alert(const char *text);
/* SGDK's random() would clash with the C library's on a host, so it is a macro for sgs_random(): a small fixed
 * generator, the same sequence on every system. <stdlib.h> is included above so its own declaration comes first. */
u16 sgs_random(void);
#define random sgs_random
void setRandomSeed(u16 seed);

#ifdef __cplusplus
}
#endif

#endif
