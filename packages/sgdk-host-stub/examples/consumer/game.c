#include "game.h"

#define CURSOR_TILE (TILE_USER_INDEX + 1)

const u8 beep_sample[64] = {1, 2, 3};

static const u32 cursor_pixels[8] = {0x11111111, 0x10000001, 0x10000001, 0x10000001, 0x10000001, 0x10000001, 0x10000001, 0x11111111};
static const TileSet cursor_tileset = {0, 1, cursor_pixels};
static u16 score;
static u16 cursor_x;
static u16 previous_pad;

void game_init(void)
{
    char line[32];
    JOY_init();
    VDP_loadTileSet(&cursor_tileset, CURSOR_TILE, DMA);
    PAL_setColor(1, RGB8_8_8_TO_VDPCOLOR(255, 255, 255));
    VDP_clearPlane(BG_A, TRUE);
    sprintf(line, "SCORE %u", (unsigned)score);
    VDP_drawText(line, 2, 1);
    cursor_x = 0;
    previous_pad = 0;
}

void game_frame(void)
{
    char line[32];
    const u16 pad = JOY_readJoypad(JOY_1);
    const u16 pressed = (u16)(pad & ~previous_pad);
    previous_pad = pad;
    VDP_setTileMapXY(BG_A, 0, cursor_x, 5);
    if ((pressed & BUTTON_RIGHT) && cursor_x < 39) ++cursor_x;
    if ((pressed & BUTTON_LEFT) && cursor_x > 0) --cursor_x;
    if (pressed & BUTTON_A) {
        ++score;
        XGM2_playPCMEx(beep_sample, sizeof beep_sample, SOUND_PCM_CH1, 4, FALSE, FALSE);
    }
    VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, 0, 0, 0, CURSOR_TILE), cursor_x, 5);
    sprintf(line, "SCORE %u", (unsigned)score);
    VDP_drawText(line, 2, 1);
    SYS_doVBlankProcess();
}

u16 game_score(void) { return score; }
