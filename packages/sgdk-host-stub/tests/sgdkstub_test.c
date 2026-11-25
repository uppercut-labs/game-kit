/* sgdkstub-test: the stub with made-up tile data, palettes and calls. No SGDK, no display, no game. */
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "sgdkstub/sgdkstub.h"

static int checks, failures;

static void on_abort(int sig)
{
    (void)sig;
    _Exit(0); /* the abort the default failure handler makes is what this run is for */
}

#define CHECK(cond, ...)                                 \
    do {                                                 \
        checks++;                                        \
        if (!(cond)) {                                   \
            failures++;                                  \
            fprintf(stderr, "FAIL line %d: ", __LINE__); \
            fprintf(stderr, __VA_ARGS__);                \
            fputc('\n', stderr);                         \
        }                                                \
    } while (0)

/* Runs with violations recorded instead of aborting, from a clean state; returns the violations the call made. */
static u32 violations_before;
static void begin(void)
{
    memset(&sgs_opt, 0, sizeof sgs_opt);
    sgs_record_violations_only();
    sgs_reset();
    violations_before = 0;
}
static u32 violations(void) { return sgs.violations - violations_before; }

static const u32 tile_a[8] = {0x11111111, 0, 0, 0, 0, 0, 0, 0};
static const u32 tile_blank[8] = {0};
static u32 two_tiles[16];

static void test_constants(void)
{
    /* the button bits and the colour rounding the games rely on */
    CHECK(BUTTON_UP == 0x0001 && BUTTON_DOWN == 0x0002 && BUTTON_LEFT == 0x0004 && BUTTON_RIGHT == 0x0008, "d-pad bits");
    CHECK(BUTTON_B == 0x0010 && BUTTON_C == 0x0020 && BUTTON_A == 0x0040 && BUTTON_START == 0x0080, "face button bits");
    CHECK(BUTTON_Z == 0x0100 && BUTTON_Y == 0x0200 && BUTTON_X == 0x0400 && BUTTON_MODE == 0x0800, "six-button bits");
    CHECK(RGB8_8_8_TO_VDPCOLOR(0, 0, 0) == 0, "black");
    CHECK(RGB8_8_8_TO_VDPCOLOR(255, 255, 255) == 0x0eee, "white is 0x0eee");
    CHECK(RGB8_8_8_TO_VDPCOLOR(255, 0, 0) == 0x000e, "red is in the low nibble");
    CHECK(RGB8_8_8_TO_VDPCOLOR(0, 255, 0) == 0x00e0, "green is in the middle nibble");
    CHECK(RGB8_8_8_TO_VDPCOLOR(0, 0, 255) == 0x0e00, "blue is in the high nibble");
    /* 15 is still level 0, 16 is the first value that rounds to level 1, and 239 and 240 are level 7 */
    CHECK(RGB8_8_8_TO_VDPCOLOR(15, 0, 0) == 0, "15 rounds to level 0");
    CHECK(RGB8_8_8_TO_VDPCOLOR(16, 0, 0) == (1 << 1), "16 rounds to level 1");
    CHECK(RGB8_8_8_TO_VDPCOLOR(239, 0, 0) == (7 << 1), "239 is level 7");
    CHECK(RGB8_8_8_TO_VDPCOLOR(240, 0, 0) == (7 << 1), "240 is level 7");
    CHECK(RGB24_TO_VDPCOLOR(0xff8000) == (u16)(RGB8_8_8_TO_VDPCOLOR(255, 128, 0)), "RGB24 matches RGB888");
    CHECK(TILE_ATTR_FULL(PAL2, 1, 0, 1, 20) == (u16)((2 << 13) | (1 << 15) | (1 << 11) | 20), "tile attribute packing");
}

static void test_planes_and_tiles(void)
{
    char row[SGS_MAX_PLANE_W + 1];
    begin();
    CHECK(sgs.screen_width == 320 && sgs.plane_w == 64 && sgs.plane_h == 32, "power-on video state");
    VDP_setTileMapXY(BG_A, 5, 3, 4);
    VDP_setTileMapXY(BG_B, 6, 3, 4);
    CHECK(sgs.tiles[BG_A][4][3] == 5 && sgs.tiles[BG_B][4][3] == 6 && sgs_tile_at(BG_A, 3, 4) == 5, "tile writes land per plane");
    CHECK(sgs.tile_writes == 2 && sgs.max_tile_x == 3 && sgs.max_tile_y == 4, "write counters");
    CHECK(violations() == 0, "an in-plane write is fine");

    VDP_setTileMapXY(BG_A, 1, 64, 0);
    CHECK(violations() == 1 && strstr(sgs_last_violation(), "outside"), "a write past the plane width is a violation: %s", sgs_last_violation());
    CHECK(sgs.tiles[BG_A][0][63] == 0, "and nothing is written");
    VDP_setTileMapXY(BG_A, 1, 0, 32);
    CHECK(violations() == 2, "a write past the default plane height is a violation");
    VDP_setPlaneSize(64, 64, TRUE);
    VDP_setTileMapXY(BG_A, 1, 0, 32);
    CHECK(violations() == 2 && sgs.tiles[BG_A][32][0] == 1, "after VDP_setPlaneSize(64, 64) row 32 is in the plane");
    VDP_setPlaneSize(128, 64, TRUE);
    CHECK(violations() == 3 && sgs.plane_w == 64, "a plane size the stub does not hold is refused");

    sgs_opt.write_limit_w = 40;
    sgs_opt.write_limit_h = 28;
    VDP_setTileMapXY(BG_A, 1, 39, 27);
    CHECK(violations() == 3, "the last screen cell is inside a 40x28 limit");
    VDP_setTileMapXY(BG_A, 1, 40, 0);
    VDP_setTileMapXY(BG_A, 1, 0, 28);
    CHECK(violations() == 5, "cells just outside a 40x28 limit are violations");
    sgs_opt.write_limit_w = sgs_opt.write_limit_h = 0;

    VDP_setTileMapXY((VDPPlane)3, 1, 0, 0);
    CHECK(violations() == 6, "a fourth plane does not exist");

    VDP_fillTileMapRect(BG_B, 9, 2, 2, 3, 2);
    CHECK(sgs.tiles[BG_B][2][2] == 9 && sgs.tiles[BG_B][3][4] == 9 && sgs.tiles[BG_B][3][5] == 0 && sgs.tiles[BG_B][4][2] == 0,
          "a filled rectangle covers exactly its cells");
    VDP_fillTileMapRect(BG_B, 9, 62, 0, 4, 1);
    CHECK(violations() == 7 && sgs.tiles[BG_B][0][62] == 0, "a rectangle past the edge is refused whole");

    VDP_setTextPlane(BG_B);
    VDP_drawText("HELLO", 4, 1);
    CHECK(sgs_row_contains(BG_B, 1, "HELLO") && !sgs_row_contains(BG_A, 1, "HELLO"), "text lands on the text plane");
    sgs_row_text(BG_B, 1, row);
    CHECK(strlen(row) == 64 && row[4] == 'H' && row[8] == 'O' && row[9] == ' ', "a row reads back with spaces elsewhere");
    VDP_setTileMapXY(BG_B, 3, 5, 1);
    CHECK(!sgs_row_contains(BG_B, 1, "HELLO") && sgs.tiles[BG_B][1][5] == 3, "a tile written over a letter replaces it");
    VDP_drawText("X", 63, 1);
    VDP_drawText("XY", 63, 1);
    CHECK(violations() == 8, "text that runs off the row is a violation");
    VDP_clearPlane(BG_B, TRUE);
    CHECK(sgs.tiles[BG_B][2][2] == 0 && !sgs_row_contains(BG_B, 1, "X") && sgs.clears == 1, "clearing a plane clears tiles and text");
    VDP_setTextPalette(4);
    CHECK(violations() == 9, "text palette 4 does not exist");
    VDP_setTextPalette(PAL3);
    VDP_setTextPriority(1);
    CHECK(sgs.text_palette == PAL3 && sgs.text_priority, "text settings kept");

    VDP_setHorizontalScroll(BG_A, -12);
    VDP_setVerticalScroll(BG_B, 7);
    CHECK(sgs.hscroll[BG_A] == -12 && sgs.vscroll[BG_B] == 7, "scroll values kept");
    VDP_setScreenWidth256();
    CHECK(sgs.screen_width == 256, "256 wide");
    VDP_setScreenWidth320();
    CHECK(sgs.screen_width == 320, "320 wide");
}

static void test_tile_sets(void)
{
    TileSet one = {0, 1, tile_a};
    TileSet two = {0, 2, two_tiles};
    memset(two_tiles, 0, sizeof two_tiles);
    two_tiles[0] = 0x22222222;
    two_tiles[8] = 0x33333333;
    begin();
    CHECK(VDP_loadTileSet(&one, TILE_USER_INDEX, DMA) == TRUE && sgs.tile_loaded[TILE_USER_INDEX], "a tile set loads");
    CHECK(sgs_tile_has_ink(TILE_USER_INDEX) && !sgs_tile_has_ink(TILE_USER_INDEX + 1), "ink: the loaded tile has some, the next has none");
    CHECK(sgs_tile_has_ink(TILE_ATTR_FULL(PAL1, 0, 0, 0, TILE_USER_INDEX)), "ink reads the index out of an attribute word");
    VDP_loadTileSet(&one, TILE_USER_INDEX, DMA);
    CHECK(violations() == 0, "reloading is allowed until forbid_tile_overlap is set");
    sgs_opt.forbid_tile_overlap = true;
    VDP_loadTileSet(&one, TILE_USER_INDEX, DMA);
    CHECK(violations() == 1, "loading over a loaded tile is a violation when forbidden");
    VDP_loadTileSet(&two, TILE_USER_INDEX + 1, DMA);
    CHECK(violations() == 1 && sgs.tile_pixels[TILE_USER_INDEX + 2][0] == 0x33333333, "a second tile set loads beside it");
    CHECK(VDP_loadTileSet(&two, SGS_MAX_TILES - 1, DMA) == FALSE && violations() == 2, "a tile set past the last slot is refused");
    {
        TileSet blank = {0, 1, tile_blank};
        VDP_loadTileSet(&blank, 100, DMA);
        CHECK(!sgs_tile_has_ink(100), "a blank tile has no ink");
    }

    sgs_opt.require_loaded_tiles = true;
    VDP_setTileMapXY(BG_A, TILE_USER_INDEX, 0, 0);
    VDP_setTileMapXY(BG_A, 0, 1, 0);
    CHECK(violations() == 2, "tile 0 and a loaded tile are fine");
    VDP_setTileMapXY(BG_A, 500, 2, 0);
    CHECK(violations() == 3 && strstr(sgs_last_violation(), "never loaded"), "an unloaded tile is a violation when required: %s", sgs_last_violation());
    sgs_reset();
    CHECK(!sgs.tile_loaded[TILE_USER_INDEX] && sgs_opt.require_loaded_tiles, "a reset forgets the tiles and keeps the options");
}

static void test_palettes(void)
{
    u16 pal[16];
    int i;
    begin();
    for (i = 0; i < 16; ++i) pal[i] = RGB8_8_8_TO_VDPCOLOR(i * 16, 0, i * 8);
    PAL_setPalette(PAL2, pal, DMA);
    CHECK(sgs.palette[32] == 0 && sgs.palette[47] == pal[15] && sgs.palette[47] != 0 && sgs.palette[0] == 0 && sgs.palette_writes == 16 && violations() == 0,
          "a palette lands in its 16 colours");
    PAL_setColor(63, 0x0eee);
    CHECK(sgs.palette[63] == 0x0eee && violations() == 0, "the last colour slot");
    PAL_setColor(64, 0);
    CHECK(violations() == 1, "colour 64 does not exist");
    PAL_setColor(0, 0x0001);
    CHECK(violations() == 2 && sgs.palette[0] == 0x0000, "colour bits outside 0x0eee are a violation and are not stored");
    PAL_setPalette(4, pal, DMA);
    CHECK(violations() == 3, "palette 4 does not exist");
    VDP_setBackgroundColor(17);
    CHECK(sgs.background_index == 17 && violations() == 3, "background colour index");
    VDP_setBackgroundColor(64);
    CHECK(violations() == 4 && sgs.background_index == 17, "background index 64 refused");
}

static void test_sprites(void)
{
    Palette palette = {16, NULL};
    SpriteDefinition checked = {&palette, 3, 2};
    SpriteDefinition free_form = {&palette, 0, 0};
    Sprite *s, *t;
    int i;
    begin();
    SPR_init();
    s = SPR_addSprite(&checked, 10, 20, TILE_ATTR(PAL2, 0, 0, 0));
    CHECK(s && s == &sgs.sprites[0] && sgs.sprite_count == 1 && s->x == 10 && s->y == 20 && s->definition == &checked, "a sprite is added");
    SPR_setAnim(s, 2);
    SPR_setFrame(s, 1);
    SPR_setVisibility(s, HIDDEN);
    SPR_setPosition(s, 33, 44);
    CHECK(s->anim == 2 && s->frame == 1 && s->visibility == HIDDEN && s->x == 33 && s->y == 44 && violations() == 0, "sprite state is kept");
    SPR_setAnim(s, 3);
    SPR_setFrame(s, 2);
    CHECK(violations() == 2 && s->anim == 2 && s->frame == 1, "an animation or frame past the definition's counts is a violation");
    t = SPR_addSprite(&free_form, 0, 0, 0);
    SPR_setAnim(t, 40);
    SPR_setFrame(t, 40);
    CHECK(violations() == 2 && t->anim == 40, "a definition that gives no counts is not checked");
    sgs_opt.sprite_bound_w = 320;
    sgs_opt.sprite_bound_h = 224;
    SPR_addSprite(&free_form, 319, 223, 0);
    CHECK(violations() == 2, "the last visible pixel is inside the bounds");
    SPR_addSprite(&free_form, 320, 0, 0);
    SPR_setPosition(s, -1, 0);
    CHECK(violations() == 4, "outside the bounds is a violation when bounds are set");
    SPR_update();
    SPR_update();
    CHECK(sgs.sprite_updates == 2, "updates counted");
    SPR_init();
    CHECK(sgs.sprite_count == 0, "SPR_init empties the list");
    for (i = 0; i < SGS_MAX_SPRITES; ++i) SPR_addSprite(&free_form, 1, 1, 0);
    CHECK(violations() == 4, "up to the limit is fine");
    CHECK(SPR_addSprite(&free_form, 1, 1, 0) == NULL && violations() == 5, "one more than the limit is refused");
}

static void test_input(void)
{
    const u16 script[] = {BUTTON_A, 0, BUTTON_START | BUTTON_UP};
    begin();
    CHECK(JOY_readJoypad(JOY_1) == 0 && JOY_readJoypad(JOY_2) == 0, "pads at rest");
    sgs_joy_set(JOY_1, BUTTON_RIGHT);
    CHECK(JOY_readJoypad(JOY_1) == BUTTON_RIGHT && JOY_readJoypad(JOY_2) == 0, "a held state, per port");
    sgs_joy_script(JOY_1, script, 3);
    CHECK(JOY_readJoypad(JOY_1) == BUTTON_A, "the script's first read");
    CHECK(JOY_readJoypad(JOY_2) == 0, "the other port is not scripted");
    CHECK(JOY_readJoypad(JOY_1) == 0 && JOY_readJoypad(JOY_1) == (BUTTON_START | BUTTON_UP), "the script goes on");
    CHECK(JOY_readJoypad(JOY_1) == BUTTON_RIGHT && JOY_readJoypad(JOY_1) == BUTTON_RIGHT, "then the held state takes over");
    JOY_readJoypad(5);
    CHECK(violations() == 1, "a third port does not exist");
    sgs_reset();
    CHECK(JOY_readJoypad(JOY_1) == 0, "a reset puts the pad back at rest");
}

static const u8 sample_a[4] = {1, 2, 3, 4};
static const u8 sample_b[4] = {5, 6, 7, 8};

static void test_audio(void)
{
    begin();
    Z80_loadDriver(Z80_DRIVER_XGM2, TRUE);
    CHECK(sgs.z80_driver == Z80_DRIVER_XGM2, "driver kept");
    CHECK(XGM2_playPCMEx(sample_a, 4, SOUND_PCM_CH1, 5, FALSE, FALSE) == TRUE, "a sample plays");
    CHECK(XGM2_playPCMEx(sample_b, 4, SOUND_PCM_CH3, 9, TRUE, TRUE) == TRUE, "a looping sample plays");
    CHECK(sgs.audio_count == 2 && sgs.audio[1].priority == 9 && sgs.audio[1].half_rate && sgs.audio[1].loop && sgs.audio[1].channel == 2,
          "the call log keeps every argument, channels counted from 0");
    CHECK(sgs.channel_sample[0] == sample_a && sgs.channel_sample[2] == sample_b && sgs.channel_loop[2] && !sgs.channel_loop[0],
          "what each channel plays");
    XGM2_stopPCM(SOUND_PCM_CH3);
    CHECK(sgs.audio_count == 3 && sgs.audio[2].stopped && sgs.audio[2].channel == 2 && sgs.channel_sample[2] == NULL && !sgs.channel_loop[2],
          "a stop is logged and the channel is empty");
    CHECK(violations() == 0, "no violations so far");
    XGM2_playPCMEx(sample_a, 4, SOUND_PCM_CH4, 5, FALSE, FALSE);
    XGM2_playPCMEx(sample_a, 4, SOUND_PCM_CH1, 16, FALSE, FALSE);
    XGM2_playPCMEx(NULL, 4, SOUND_PCM_CH1, 1, FALSE, FALSE);
    XGM2_playPCMEx(sample_a, 0, SOUND_PCM_CH1, 1, FALSE, FALSE);
    XGM2_stopPCM(SOUND_PCM_CH_AUTO);
    CHECK(violations() == 5 && sgs.audio_count == 3, "a fourth channel, priority 16, no sample, no length and a stop of AUTO are violations and are not logged");
    XGM2_playPCMEx(sample_a, 4, SOUND_PCM_CH_AUTO, 1, FALSE, FALSE);
    CHECK(violations() == 5 && sgs.audio[3].channel == -1, "AUTO is a valid channel to play on and is logged as -1");
    sgs_clear_audio_calls();
    CHECK(sgs.audio_count == 0 && sgs.audio[0].sample == NULL, "the call log clears");
    CHECK(sgs.channel_sample[0] == sample_a, "clearing the log leaves what the channels hold");
}

static void test_psg(void)
{
    SgsPsgEvent events[16];
    u16 n;
    begin();
    PSG_setTone(0, 0x123);
    PSG_setEnvelope(0, 4);
    CHECK(sgs.psg_log_length == 3 && sgs.psg_log[0] == 0x83 && sgs.psg_log[1] == 0x12 && sgs.psg_log[2] == 0x94, "raw bytes: latch with low nibble, data with high six bits, volume latch");
    n = sgs_psg_events(events, 16);
    CHECK(n == 3, "three events (latch, data, volume), got %u", (unsigned)n);
    CHECK(events[0].channel == 0 && !events[0].volume && events[0].value == 0x003, "the latch alone has the low four bits");
    CHECK(events[1].value == 0x123, "the data byte completes the period: 0x%x", events[1].value);
    CHECK(events[2].volume && events[2].value == 4 && events[2].channel == 0, "volume event");
    CHECK(sgs_psg_has_tone(0) && !sgs_psg_has_tone(1) && !sgs_psg_has_tone(2), "tone on channel 0 only");
    CHECK(sgs_psg_has_envelope(0, 4) && !sgs_psg_has_envelope(0, 5) && !sgs_psg_has_envelope(1, 4), "envelope matches channel and value");
    CHECK(!sgs_psg_tone_changed(0), "one period is not a change");
    PSG_setTone(2, 0x050);
    CHECK(sgs_psg_has_tone(2) && sgs_psg_has_tone(0), "a tone on channel 2 is found as well (a channel above 0 must match)");
    PSG_setTone(0, 0x200);
    CHECK(sgs_psg_tone_changed(0) && !sgs_psg_tone_changed(2), "a second period on channel 0 is a change");
    PSG_setNoise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_CLOCK8);
    CHECK(sgs.psg_log[sgs.psg_log_length - 1] == 0xE6, "noise control byte");
    n = sgs_psg_events(events, 16);
    CHECK(events[n - 1].channel == 3 && !events[n - 1].volume && events[n - 1].value == 6, "noise is channel 3");
    PSG_reset();
    CHECK(sgs_psg_has_envelope(1, PSG_ENVELOPE_MIN) && sgs_psg_has_envelope(3, PSG_ENVELOPE_MIN), "a reset silences every channel");
    CHECK(violations() == 0, "no violations");
    PSG_setEnvelope(4, 0);
    PSG_setEnvelope(0, 16);
    PSG_setTone(3, 0);
    PSG_setTone(0, 1024);
    PSG_setNoise(2, 0);
    CHECK(violations() == 5, "out-of-range arguments are violations (%u)", (unsigned)violations());
    sgs_reset();
    CHECK(sgs.psg_log_length == 0, "a reset empties the log");
}

static void test_sram(void)
{
    begin();
    SRAM_writeByte(0, 1);
    CHECK(violations() == 1 && sgs.sram[0] == 0, "a write with save RAM off is a violation and is not stored");
    SRAM_readByte(0);
    CHECK(violations() == 2, "a read with save RAM off is a violation");
    SRAM_enable();
    SRAM_writeByte(10, 0xab);
    CHECK(SRAM_readByte(10) == 0xab && violations() == 2, "write then read");
    SRAM_enableRO();
    SRAM_writeByte(10, 0xcd);
    CHECK(violations() == 3 && SRAM_readByte(10) == 0xab, "a write with save RAM read-only is a violation and does not change it");
    SRAM_disable();
    CHECK(sgs.sram_mode == 0, "disabled");
    SRAM_enable();
    SRAM_writeByte(SGS_SRAM_SIZE, 1);
    CHECK(violations() == 4, "an offset past the stub's save RAM is a violation");
}

static void test_system(void)
{
    u16 a, b, c;
    char text[300];
    FILE *tmp;
    begin();
    CHECK(SYS_doVBlankProcess() == TRUE && SYS_doVBlankProcess() == TRUE && sgs.vblanks == 2, "vblanks counted");
    setRandomSeed(1234);
    a = random();
    b = random();
    setRandomSeed(1234);
    c = random();
    CHECK(a == c && a != b, "random() repeats from a seed");
    setRandomSeed(1234);
    CHECK(random() == a, "and gives the same numbers every run");
    CHECK(a != 0 && b != 0, "the generator never sticks at 0");
    KDebug_Alert("first");
    KDebug_Alert("second");
    CHECK(sgs.alert_count == 2 && strcmp(sgs.alerts[0], "first") == 0 && strcmp(sgs.alerts[1], "second") == 0, "alerts kept in order");
    tmp = tmpfile();
    if (tmp) {
        sgs_opt.alert_stream = tmp;
        KDebug_Alert("hello");
        sgs_opt.alert_stream = NULL;
        rewind(tmp);
        text[0] = 0;
        if (fgets(text, sizeof text, tmp) == NULL) text[0] = 0;
        CHECK(strcmp(text, "ROM: hello\n") == 0, "with a stream, alerts print as ROM: lines (got \"%s\")", text);
        fclose(tmp);
    }
}

static const char *seen;
static void handler(const char *what) { seen = what; }

static void test_failure_handling(void)
{
    begin();
    seen = NULL;
    sgs_set_fail_handler(handler);
    VDP_setTileMapXY(BG_A, 1, 100, 0);
    CHECK(seen != NULL && strstr(seen, "outside") && sgs.violations == 1, "a handler is told what broke");
    sgs_record_violations_only();
    seen = NULL;
    VDP_setTileMapXY(BG_A, 1, 100, 0);
    CHECK(seen == NULL && sgs.violations == 2, "recording only does not call the old handler");
    sgs_reset();
    CHECK(sgs.violations == 0 && sgs_last_violation()[0] == 0, "a reset clears the count and the message");
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--fresh") == 0) {
        /* a program that never calls sgs_reset(): the state starts as after a power-on */
        VDP_setTileMapXY(BG_A, 7, 63, 31);
        VDP_drawText("OK", 0, 0);
        PAL_setColor(1, 0x0eee);
        if (sgs.screen_width != 320 || sgs_tile_at(BG_A, 63, 31) != 7 || !sgs_row_contains(BG_A, 0, "OK") || sgs.violations) {
            fputs("the stub did not start in its power-on state\n", stderr);
            return 1;
        }
        puts("sgdkstub-test --fresh: ok");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--violate") == 0) {
        /* the default handler: a broken rule must stop the program (ctest runs this and expects it to fail) */
        signal(SIGABRT, on_abort);
        sgs_reset();
        PAL_setColor(64, 0);
        fputs("the violation did not stop the program\n", stderr);
        return 1;
    }
    test_constants();
    test_planes_and_tiles();
    test_tile_sets();
    test_palettes();
    test_sprites();
    test_input();
    test_audio();
    test_psg();
    test_sram();
    test_system();
    test_failure_handling();
    if (failures) {
        fprintf(stderr, "sgdkstub-test: %d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("sgdkstub-test: %d checks passed\n", checks);
    return 0;
}
