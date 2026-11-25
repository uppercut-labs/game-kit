/* gtu-test: gba-tile-ui with made-up tiles, strings and sprites. No console, no libgba, no display. */
#include <stdio.h>
#include <string.h>

#include "gba_tile_ui/gba_tile_ui.h"
#include "gba_tile_ui/gba_tile_ui_gba.h"

uint8_t fake_vram[0x10000];
uint8_t fake_oam[1024];
unsigned fake_dma_calls;

static int checks, failures;

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

/* a font of 10 glyphs at tile 100: "AB0 1.-!?" with a space; the unknown character draws tile 109 (the "?" glyph) */
static const char GLYPHS[] = "AB01.-!? Z";
#define FONT_BASE 100
#define UNKNOWN_TILE (FONT_BASE + 7)

static GtuUi ui;

static void fresh(void)
{
    gtu_init(&ui);
    gtu_set_font(&ui, GLYPHS, FONT_BASE, UNKNOWN_TILE);
    /* clear the dirty flags the init set, so a test sees only its own changes */
    gtu_take_oam_dirty(&ui);
    {
        unsigned bg;
        for (bg = 0; bg < GTU_MAP_COUNT; ++bg) gtu_take_map_dirty(&ui, bg);
    }
}

static void test_entries(void)
{
    CHECK(gtu_entry(5, 0, 0) == 5, "plain entry");
    CHECK(gtu_entry(0x3ff, 0, 0) == 0x3ff && gtu_entry(0x7ff, 0, 0) == 0x3ff, "tile is ten bits");
    CHECK(gtu_entry(1, 0, GTU_HFLIP) == (0x0400 | 1), "horizontal flip is bit 10");
    CHECK(gtu_entry(1, 0, GTU_VFLIP) == (0x0800 | 1), "vertical flip is bit 11");
    CHECK(gtu_entry(1, 3, 0) == (3u << 12 | 1), "palette bank in bits 12-15");
    CHECK(gtu_entry(1, 19, 0) == (3u << 12 | 1), "palette bank is four bits");
    CHECK(GTU_MAP_CELLS == 1024 && GTU_MAP_BYTES == 2048 && GTU_OAM_BYTES == 1024 && sizeof(GtuObj) == 8, "sizes");
}

static void test_init(void)
{
    unsigned i, bg;
    gtu_init(&ui);
    CHECK(ui.clip_columns == 32 && ui.clip_rows == 32 && ui.text_columns == 30 && ui.text_rows == 20, "default areas");
    for (i = 0; i < GTU_OAM_COUNT; ++i)
        CHECK(ui.oam[i].attr0 == 0x0200 && ui.oam[i].attr1 == 0 && ui.oam[i].attr2 == 0 && ui.oam[i].fill == 0, "sprite %u starts hidden", i);
    CHECK(ui.oam_dirty, "OAM starts dirty");
    for (bg = 0; bg < GTU_MAP_COUNT; ++bg) CHECK(ui.map_dirty[bg] && ui.maps[bg][0] == 0 && ui.maps[bg][1023] == 0, "map %u starts empty and dirty", bg);
    CHECK(((uintptr_t)ui.maps & 3) == 0 && ((uintptr_t)ui.oam & 3) == 0, "arrays are word aligned");
}

static void test_tiles(void)
{
    unsigned bg;
    fresh();
    gtu_set_tile(&ui, 0, 3, 4, 17, 2, 0);
    CHECK(gtu_get_entry(&ui, 0, 3, 4) == (2u << 12 | 17) && ui.maps[0][4 * 32 + 3] == (2u << 12 | 17), "a tile lands at row * 32 + column");
    CHECK(gtu_get_entry(&ui, 1, 3, 4) == 0, "and only on its own map");
    CHECK(ui.map_dirty[0] && !ui.map_dirty[1], "only its map is dirty");
    gtu_set_tile(&ui, 1, 31, 31, 9, 0, GTU_HFLIP | GTU_VFLIP);
    CHECK(gtu_get_entry(&ui, 1, 31, 31) == (0x0c00 | 9), "the last cell, both flips");
    gtu_set_tile(&ui, 0, -1, 0, 1, 0, 0);
    gtu_set_tile(&ui, 0, 0, -1, 1, 0, 0);
    gtu_set_tile(&ui, 0, 32, 0, 1, 0, 0);
    gtu_set_tile(&ui, 0, 0, 32, 1, 0, 0);
    gtu_set_tile(&ui, GTU_MAP_COUNT, 0, 0, 1, 0, 0);
    for (bg = 0; bg < GTU_MAP_COUNT; ++bg) {
        unsigned i, others = 0;
        for (i = 0; i < GTU_MAP_CELLS; ++i)
            if (ui.maps[bg][i] && !((bg == 0 && i == 4 * 32 + 3) || (bg == 1 && i == 1023))) ++others;
        CHECK(others == 0, "writes outside the map or to a bad map change nothing (map %u)", bg);
    }
    gtu_set_clip(&ui, 30, 20);
    gtu_set_tile(&ui, 0, 29, 19, 6, 0, 0);
    gtu_set_tile(&ui, 0, 30, 0, 7, 0, 0);
    gtu_set_tile(&ui, 0, 0, 20, 7, 0, 0);
    CHECK(gtu_get_entry(&ui, 0, 29, 19) == 6 && gtu_get_entry(&ui, 0, 30, 0) == 0 && gtu_get_entry(&ui, 0, 0, 20) == 0, "a clip of 30 x 20 holds writes to the screen");
    gtu_set_clip(&ui, 99, 99);
    CHECK(ui.clip_columns == 32 && ui.clip_rows == 32, "a clip is held to the map");
    gtu_set_entry(&ui, 0, 30, 0, 0x1234);
    CHECK(gtu_get_entry(&ui, 0, 30, 0) == 0x1234, "a raw entry goes in as it is");
    gtu_clear_map(&ui, 0, 40, 5);
    CHECK(gtu_get_entry(&ui, 0, 0, 0) == (5u << 12 | 40) && gtu_get_entry(&ui, 0, 31, 31) == (5u << 12 | 40) && gtu_get_entry(&ui, 0, 3, 4) == (5u << 12 | 40), "clear_map fills the whole map, even outside a clip");
}

static void test_fill_and_load(void)
{
    uint16_t entries[GTU_MAP_CELLS + 5];
    size_t i;
    fresh();
    gtu_fill(&ui, 0, 2, 3, 4, 2, 9, 1);
    CHECK(gtu_get_entry(&ui, 0, 2, 3) == (1u << 12 | 9) && gtu_get_entry(&ui, 0, 5, 4) == (1u << 12 | 9), "fill covers its corners");
    CHECK(gtu_get_entry(&ui, 0, 6, 3) == 0 && gtu_get_entry(&ui, 0, 2, 5) == 0 && gtu_get_entry(&ui, 0, 1, 3) == 0, "and nothing around it");
    gtu_fill(&ui, 0, 30, 30, 5, 5, 3, 0);
    CHECK(gtu_get_entry(&ui, 0, 31, 31) == 3, "a fill that runs off the map is clipped, not wrapped");
    CHECK(gtu_get_entry(&ui, 0, 0, 0) == 0, "no wrap to the other side");
    gtu_fill(&ui, 0, 0, 0, -3, 4, 8, 0);
    gtu_fill(&ui, 0, 0, 0, 3, 0, 8, 0);
    CHECK(gtu_get_entry(&ui, 0, 0, 0) == 0, "an empty or negative size fills nothing");
    for (i = 0; i < GTU_MAP_CELLS + 5; ++i) entries[i] = (uint16_t)(i + 1);
    fresh();
    gtu_load_map(&ui, 2, entries, GTU_MAP_CELLS + 5);
    CHECK(ui.maps[2][0] == 1 && ui.maps[2][1023] == 1024 && ui.map_dirty[2], "a prebuilt map loads, held to 1024 entries");
    fresh();
    gtu_load_map(&ui, 2, entries, 3);
    CHECK(ui.maps[2][2] == 3 && ui.maps[2][3] == 0, "a short map loads only its entries");
    gtu_load_map(&ui, 2, NULL, 3);
}

static void test_dirty(void)
{
    unsigned bg;
    fresh();
    CHECK(!gtu_take_map_dirty(&ui, 0) && !gtu_take_oam_dirty(&ui), "nothing changed, nothing dirty");
    gtu_set_tile(&ui, 0, 1, 1, 5, 0, 0);
    CHECK(gtu_take_map_dirty(&ui, 0) && !gtu_take_map_dirty(&ui, 0), "a change is dirty once, until the next change");
    gtu_set_tile(&ui, 0, 1, 1, 5, 0, 0);
    CHECK(!gtu_take_map_dirty(&ui, 0), "writing the same entry again is not a change");
    gtu_set_tile(&ui, 0, 1, 1, 6, 0, 0);
    CHECK(gtu_take_map_dirty(&ui, 0), "a different entry is");
    gtu_clear_map(&ui, 1, 0, 0);
    CHECK(!gtu_take_map_dirty(&ui, 1), "clearing a map that is already clear is not a change");
    gtu_clear_map(&ui, 1, 3, 0);
    CHECK(gtu_take_map_dirty(&ui, 1), "clearing it to another tile is");
    gtu_set_sprite(&ui, 3, 10, 20, 7, 1, GTU_SPRITE_16, 2, 0);
    CHECK(gtu_take_oam_dirty(&ui), "a new sprite dirties the OAM");
    gtu_set_sprite(&ui, 3, 10, 20, 7, 1, GTU_SPRITE_16, 2, 0);
    CHECK(!gtu_take_oam_dirty(&ui), "the same sprite again does not");
    gtu_hide_all_sprites(&ui);
    CHECK(gtu_take_oam_dirty(&ui), "hiding a shown sprite does");
    gtu_hide_all_sprites(&ui);
    CHECK(!gtu_take_oam_dirty(&ui), "hiding hidden sprites does not");
    CHECK(!gtu_take_map_dirty(&ui, GTU_MAP_COUNT), "a bad map is never dirty");
    gtu_mark_all_dirty(&ui);
    CHECK(gtu_take_oam_dirty(&ui), "mark_all_dirty: OAM");
    for (bg = 0; bg < GTU_MAP_COUNT; ++bg) CHECK(gtu_take_map_dirty(&ui, bg), "mark_all_dirty: map %u", bg);
}

static void test_text(void)
{
    fresh();
    CHECK(gtu_glyph_tile(&ui, 'A') == FONT_BASE && gtu_glyph_tile(&ui, 'B') == FONT_BASE + 1 && gtu_glyph_tile(&ui, 'Z') == FONT_BASE + 9, "a glyph's tile is the font base plus its place in the list");
    CHECK(gtu_glyph_tile(&ui, ' ') == FONT_BASE + 8, "the space has a glyph");
    CHECK(gtu_glyph_tile(&ui, 'q') == UNKNOWN_TILE && gtu_glyph_tile(&ui, '\0') == UNKNOWN_TILE, "a character the font lacks draws the unknown tile");
    gtu_text(&ui, 0, 2, 3, "AB", 4);
    CHECK(gtu_get_entry(&ui, 0, 2, 3) == (4u << 12 | FONT_BASE) && gtu_get_entry(&ui, 0, 3, 3) == (4u << 12 | (FONT_BASE + 1)), "text writes one tile a character");
    gtu_text(&ui, 0, 2, 4, "A B", 0);
    CHECK(gtu_get_entry(&ui, 0, 3, 4) == FONT_BASE + 8, "a space draws its glyph by default");
    ui.skip_spaces = true;
    gtu_clear_map(&ui, 0, 50, 0);
    gtu_text(&ui, 0, 2, 4, "A B", 0);
    CHECK(gtu_get_entry(&ui, 0, 3, 4) == 50 && gtu_get_entry(&ui, 0, 2, 4) == FONT_BASE && gtu_get_entry(&ui, 0, 4, 4) == FONT_BASE + 1, "with skip_spaces a space leaves what was there");
    ui.skip_spaces = false;
    gtu_clear_map(&ui, 0, 0, 0);

    gtu_text(&ui, 0, 28, 0, "ABAB", 0);
    CHECK(gtu_get_entry(&ui, 0, 29, 0) == FONT_BASE + 1 && gtu_get_entry(&ui, 0, 30, 0) == 0 && gtu_get_entry(&ui, 0, 31, 0) == 0, "text stops at column 30, not wrapping and not reaching the map's last columns");
    gtu_text(&ui, 0, -2, 1, "ABA", 0);
    CHECK(gtu_get_entry(&ui, 0, 0, 1) == FONT_BASE && gtu_get_entry(&ui, 0, 1, 1) == 0, "text that starts left of the screen is cut at the edge (here only the third character shows)");
    gtu_text(&ui, 0, 0, 20, "A", 0);
    gtu_text(&ui, 0, 0, -1, "A", 0);
    CHECK(gtu_get_entry(&ui, 0, 0, 20) == 0 && gtu_get_entry(&ui, 0, 0, -1) == 0, "rows outside the text area are dropped");
    gtu_text(&ui, 0, 5, 6, "A\nB\nAB", 0);
    CHECK(gtu_get_entry(&ui, 0, 5, 6) == FONT_BASE && gtu_get_entry(&ui, 0, 5, 7) == FONT_BASE + 1 && gtu_get_entry(&ui, 0, 5, 8) == FONT_BASE && gtu_get_entry(&ui, 0, 6, 8) == FONT_BASE + 1, "a newline goes back to the start column and down");
    gtu_set_text_area(&ui, 10, 3);
    gtu_text(&ui, 0, 8, 1, "ABAB", 0);
    gtu_text(&ui, 0, 0, 3, "A", 0);
    CHECK(gtu_get_entry(&ui, 0, 9, 1) == FONT_BASE + 1 && gtu_get_entry(&ui, 0, 10, 1) == 0 && gtu_get_entry(&ui, 0, 0, 3) == 0, "a smaller text area holds the text");
    gtu_text(&ui, 0, 0, 0, NULL, 0);
    gtu_text(&ui, GTU_MAP_COUNT, 0, 0, "A", 0);
}

static void test_center_and_number(void)
{
    fresh();
    gtu_center(&ui, 0, 2, "AB", 0); /* (30 - 2) / 2 = 14 */
    CHECK(gtu_get_entry(&ui, 0, 14, 2) == FONT_BASE && gtu_get_entry(&ui, 0, 15, 2) == FONT_BASE + 1 && gtu_get_entry(&ui, 0, 13, 2) == 0 && gtu_get_entry(&ui, 0, 16, 2) == 0, "centred in 30 columns");
    gtu_center(&ui, 0, 3, "ABA", 0); /* (30 - 3) / 2 = 13 */
    CHECK(gtu_get_entry(&ui, 0, 13, 3) == FONT_BASE, "an odd length rounds down");
    gtu_set_text_area(&ui, 20, 20);
    gtu_center(&ui, 0, 4, "AB", 0); /* (20 - 2) / 2 = 9 */
    CHECK(gtu_get_entry(&ui, 0, 9, 4) == FONT_BASE, "centred in the text area's width");
    gtu_center(&ui, 0, 5, NULL, 0);

    fresh();
    ui.skip_spaces = true;
    gtu_number(&ui, 0, 2, 0, 5, 1, 0);
    gtu_number(&ui, 0, 2, 1, 5, 3, 0);
    gtu_number(&ui, 0, 2, 2, 123, 2, 0);
    gtu_number(&ui, 0, 2, 3, 0, 1, 0);
    gtu_number(&ui, 0, 2, 4, -42, 5, 0);
    gtu_number(&ui, 0, 2, 5, -2147483647 - 1, 1, 0);
    /* the font only has the digits 0 and 1 and "-": a digit it lacks draws the unknown tile */
    CHECK(gtu_get_entry(&ui, 0, 2, 0) == UNKNOWN_TILE, "a digit the font lacks draws the unknown tile");
    CHECK(gtu_get_entry(&ui, 0, 2, 1) == 0 && gtu_get_entry(&ui, 0, 3, 1) == 0 && gtu_get_entry(&ui, 0, 4, 1) == UNKNOWN_TILE, "width 3 right-aligns, and the padding spaces are skipped");
    CHECK(gtu_get_entry(&ui, 0, 2, 2) == FONT_BASE + 3 && gtu_get_entry(&ui, 0, 4, 2) == UNKNOWN_TILE && gtu_get_entry(&ui, 0, 5, 2) == 0, "a number wider than its width is not cut");
    CHECK(gtu_get_entry(&ui, 0, 2, 3) == FONT_BASE + 2, "zero is one digit");
    CHECK(gtu_get_entry(&ui, 0, 2, 4) == 0 && gtu_get_entry(&ui, 0, 5, 4) == UNKNOWN_TILE && gtu_get_entry(&ui, 0, 4, 4) == FONT_BASE + 5, "a negative number puts its minus sign next to the digits (width 5: 2 spaces, '-', two digits)");
    CHECK(gtu_get_entry(&ui, 0, 2, 5) == FONT_BASE + 5 && gtu_get_entry(&ui, 0, 12, 5) == UNKNOWN_TILE && gtu_get_entry(&ui, 0, 13, 5) == 0, "the smallest int prints all its digits (a minus sign and ten digits)");
    fresh();
    gtu_number(&ui, 0, 0, 0, 1, 4, 0);
    CHECK(gtu_get_entry(&ui, 0, 0, 0) == FONT_BASE + 8 && gtu_get_entry(&ui, 0, 3, 0) == FONT_BASE + 3, "with spaces drawn, the padding is the space glyph");
}

static void test_clear_text_rect(void)
{
    fresh();
    gtu_fill(&ui, 0, 0, 0, 10, 4, 77, 0);
    gtu_clear_text_rect(&ui, 0, 1, 1, 3, 2, 6);
    CHECK(gtu_get_entry(&ui, 0, 1, 1) == (6u << 12 | (FONT_BASE + 8)) && gtu_get_entry(&ui, 0, 3, 2) == (6u << 12 | (FONT_BASE + 8)), "a cleared rectangle is the space glyph in the given palette");
    CHECK(gtu_get_entry(&ui, 0, 4, 1) == 77 && gtu_get_entry(&ui, 0, 1, 3) == 77 && gtu_get_entry(&ui, 0, 0, 1) == 77, "and nothing else");
}

static void test_sprites(void)
{
    fresh();
    gtu_set_sprite(&ui, 5, 100, 50, 0x12, 3, GTU_SPRITE_32, 2, 0);
    CHECK(ui.oam[5].attr0 == 50, "attr0: y in bits 0-7, normal square 16-colour");
    CHECK(ui.oam[5].attr1 == (2u << 14 | 100), "attr1: x in bits 0-8, size in bits 14-15");
    CHECK(ui.oam[5].attr2 == (3u << 12 | 2u << 10 | 0x12) && ui.oam[5].fill == 0, "attr2: tile, priority, palette");
    gtu_set_sprite(&ui, 6, 3, 4, 1, 0, GTU_SPRITE_8, 0, GTU_HFLIP);
    CHECK(ui.oam[6].attr1 == (0x1000 | 3), "horizontal flip is attr1 bit 12");
    gtu_set_sprite(&ui, 6, 3, 4, 1, 0, GTU_SPRITE_64, 0, GTU_VFLIP);
    CHECK(ui.oam[6].attr1 == (3u << 14 | 0x2000 | 3), "vertical flip is attr1 bit 13, size 64 is 3");
    gtu_set_sprite(&ui, 7, -8, -4, 1, 0, GTU_SPRITE_16, 0, 0);
    CHECK(ui.oam[7].attr0 == 252 && (ui.oam[7].attr1 & 0x1ff) == 504, "a negative position wraps as the console's 8-bit y and 9-bit x do");
    gtu_set_sprite(&ui, 8, 600, 300, 0x7ff, 20, GTU_SPRITE_8, 7, 0);
    CHECK(ui.oam[8].attr0 == (300 & 255) && (ui.oam[8].attr1 & 0x1ff) == (600 & 511) && (ui.oam[8].attr2 & 0x3ff) == 0x3ff && ((ui.oam[8].attr2 >> 10) & 3) == 3 && (ui.oam[8].attr2 >> 12) == 4, "tile, priority and palette are held to their bits");
    gtu_hide_sprite(&ui, 5);
    CHECK(ui.oam[5].attr0 == 0x0200 && ui.oam[5].attr1 == 0 && ui.oam[5].attr2 == 0, "a hidden sprite has the disable bit and nothing else");
    CHECK(ui.oam[6].attr1 != 0, "hiding one leaves the others");
    gtu_hide_all_sprites(&ui);
    CHECK(ui.oam[6].attr0 == 0x0200 && ui.oam[8].attr2 == 0 && ui.oam[127].attr0 == 0x0200, "hide all");
    gtu_set_sprite(&ui, 128, 1, 1, 1, 1, GTU_SPRITE_8, 0, 0);
    gtu_hide_sprite(&ui, 128);
    CHECK(ui.oam[127].attr0 == 0x0200 && ui.oam[127].attr1 == 0, "a bad index changes nothing");
}

static void test_glue(void)
{
    fresh();
    memset(fake_vram, 0, sizeof fake_vram);
    memset(fake_oam, 0, sizeof fake_oam);
    fake_dma_calls = 0;
    gtu_set_tile(&ui, 1, 2, 0, 0x55, 1, 0);
    gtu_set_sprite(&ui, 0, 10, 20, 3, 0, GTU_SPRITE_8, 0, 0);
    gtu_present_gba(&ui, 28);
    {
        uint16_t word;
        memcpy(&word, fake_vram + 29u * 0x800u + 2u * 2u, 2);
        CHECK(word == (1u << 12 | 0x55), "map 1 lands in screen block 29 (first block 28 + 1)");
        memcpy(&word, fake_vram + 28u * 0x800u + 2u * 2u, 2);
        CHECK(word == 0, "and map 0 was not touched");
        memcpy(&word, fake_oam, 2);
        CHECK(word == 20, "the OAM was copied");
    }
    CHECK(fake_dma_calls == 2, "two copies: the one map that changed and the OAM (%u)", fake_dma_calls);
    gtu_present_gba(&ui, 28);
    CHECK(fake_dma_calls == 2, "a present with no change copies nothing");
    gtu_set_tile(&ui, 0, 0, 0, 1, 0, 0);
    gtu_present_gba(&ui, 28);
    CHECK(fake_dma_calls == 3, "a change to one map copies one map");
}

int main(void)
{
    test_entries();
    test_init();
    test_tiles();
    test_fill_and_load();
    test_dirty();
    test_text();
    test_center_and_number();
    test_clear_text_rect();
    test_sprites();
    test_glue();
    if (failures) {
        fprintf(stderr, "gtu-test: %d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("gtu-test: %d checks passed\n", checks);
    return 0;
}
