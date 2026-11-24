/* Host test of the made-up game in game.c: script the pad, run frames, read what the stub recorded. */
#include <stdio.h>

#include <sgdkstub/sgdkstub.h>

#include "game.h"

static int failures;
#define CHECK(cond)                                           \
    do {                                                      \
        if (!(cond)) {                                        \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
            failures++;                                       \
        }                                                     \
    } while (0)

int main(void)
{
    /* the screen is 40 x 28 cells; a write outside it stops the test at the call (the default is to abort) */
    sgs_reset();
    sgs_opt.write_limit_w = 40;
    sgs_opt.write_limit_h = 28;
    sgs_opt.require_loaded_tiles = true;

    {
        /* frames: nothing, right, right held, A, nothing */
        const u16 pad[] = {0, BUTTON_RIGHT, BUTTON_RIGHT, BUTTON_RIGHT | BUTTON_A, 0};
        int frame;
        game_init();
        sgs_joy_script(JOY_1, pad, 5);
        CHECK(sgs.tile_loaded[TILE_USER_INDEX + 1] && sgs_tile_has_ink(TILE_USER_INDEX + 1));
        CHECK(sgs_row_contains(BG_A, 1, "SCORE 0"));
        for (frame = 0; frame < 5; ++frame) game_frame();
        CHECK(sgs_tile_at(BG_A, 1, 5) == (u16)(TILE_USER_INDEX + 1)); /* one step right: holding is not a second press */
        CHECK(sgs_tile_at(BG_A, 0, 5) == 0);
        CHECK(game_score() == 1 && sgs_row_contains(BG_A, 1, "SCORE 1"));
        CHECK(sgs.audio_count == 1 && sgs.audio[0].sample == beep_sample && sgs.audio[0].channel == 0 && sgs.audio[0].priority == 4);
        CHECK(sgs.vblanks == 5);
        CHECK(sgs.palette[1] == 0x0eee);
        CHECK(sgs.violations == 0);
    }
    if (failures) return 1;
    puts("my-game-test: ok");
    return 0;
}
