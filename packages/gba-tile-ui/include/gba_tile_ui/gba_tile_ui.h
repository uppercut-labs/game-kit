/* gba-tile-ui: a software tile map and OAM layer for a Game Boy Advance game (README.md).
 *
 * The game draws into shadow copies in ordinary memory: up to GTU_MAP_COUNT tile maps of 32 x 32 entries and a
 * 128-entry OAM. Each change sets a dirty flag when it changes a word. Once a frame the game copies what is dirty to
 * VRAM and OAM. The core knows nothing of the console's registers or of libgba, so it builds and runs on a host;
 * gba_tile_ui_gba.h is the optional few lines that do the copy with libgba. */
#ifndef GBA_TILE_UI_H
#define GBA_TILE_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GTU_VERSION "0.1.0"

#define GTU_MAP_COLUMNS 32
#define GTU_MAP_ROWS 32
#define GTU_MAP_CELLS (GTU_MAP_COLUMNS * GTU_MAP_ROWS)
#define GTU_MAP_BYTES (GTU_MAP_CELLS * 2)
#define GTU_VISIBLE_COLUMNS 30
#define GTU_VISIBLE_ROWS 20
#define GTU_OAM_COUNT 128
#define GTU_OAM_BYTES (GTU_OAM_COUNT * 8)

/* How many maps a GtuUi holds (a GBA has four background layers). Define it before the first include, in every file
 * that includes this header, to hold fewer: a game with one text layer wants 1 and saves 6 KB. */
#ifndef GTU_MAP_COUNT
#define GTU_MAP_COUNT 4
#endif

/* Map entry flags, and the bits of an entry (tile 0-9, flips 10-11, palette bank 12-15). */
#define GTU_HFLIP 0x0001u
#define GTU_VFLIP 0x0002u

/* OAM attribute bits a game may want (the layout of the console's own OAM entry). */
#define GTU_ATTR0_DISABLED 0x0200u

typedef enum { GTU_SPRITE_8 = 0, GTU_SPRITE_16 = 1, GTU_SPRITE_32 = 2, GTU_SPRITE_64 = 3 } GtuSpriteSize;

/* One OAM entry as the console reads it: three attribute words and the fourth (the affine parameter, unused). */
typedef struct {
    uint16_t attr0, attr1, attr2, fill;
} GtuObj;

/* The copy to VRAM and OAM runs best on words, so the two arrays are aligned to four bytes where the compiler can. */
#if defined(__GNUC__)
#define GTU_ALIGN4 __attribute__((aligned(4)))
#else
#define GTU_ALIGN4
#endif

typedef struct {
    uint16_t maps[GTU_MAP_COUNT][GTU_MAP_CELLS] GTU_ALIGN4;
    GtuObj oam[GTU_OAM_COUNT] GTU_ALIGN4;
    bool map_dirty[GTU_MAP_COUNT];
    bool oam_dirty;

    /* The font. glyphs lists the characters in tile order; a character's tile is font_base + its position. A
     * character not in the list draws unknown_tile. When skip_spaces is set, a space writes nothing (so the map
     * behind it shows through); otherwise it draws its glyph. */
    const char *glyphs;
    uint16_t font_base;
    uint16_t unknown_tile;
    bool skip_spaces;

    /* Tile writes outside clip_columns x clip_rows are dropped (all 32 x 32 until changed); text is also held to the
     * visible area, text_columns x text_rows (30 x 20 until changed). */
    uint8_t clip_columns, clip_rows;
    uint8_t text_columns, text_rows;
} GtuUi;

/* Clears everything: maps to tile 0 and palette 0, every sprite hidden, the clip and text areas to their defaults, no
 * font, and every dirty flag set so the first present writes it all out. */
void gtu_init(GtuUi *ui);

/* The font. unknown_tile is what a character missing from glyphs draws: one tile of the font ("the question mark"), the
 * blank tile, whichever the game wants. */
void gtu_set_font(GtuUi *ui, const char *glyphs, uint16_t font_base, uint16_t unknown_tile);
void gtu_set_clip(GtuUi *ui, uint8_t columns, uint8_t rows);
void gtu_set_text_area(GtuUi *ui, uint8_t columns, uint8_t rows);

/* A map entry: tile (10 bits), palette bank (4 bits) and GTU_HFLIP | GTU_VFLIP. */
uint16_t gtu_entry(uint16_t tile, uint8_t palette, unsigned flags);
/* The tile a character draws (the unknown tile for a character the font lacks). */
uint16_t gtu_glyph_tile(const GtuUi *ui, char ch);

/* Maps. bg is 0 to GTU_MAP_COUNT-1; a call with a bad bg does nothing. */
void gtu_clear_map(GtuUi *ui, unsigned bg, uint16_t tile, uint8_t palette);
void gtu_set_tile(GtuUi *ui, unsigned bg, int x, int y, uint16_t tile, uint8_t palette, unsigned flags);
void gtu_set_entry(GtuUi *ui, unsigned bg, int x, int y, uint16_t entry);
uint16_t gtu_get_entry(const GtuUi *ui, unsigned bg, int x, int y); /* 0 outside the map */
void gtu_fill(GtuUi *ui, unsigned bg, int x, int y, int width, int height, uint16_t tile, uint8_t palette);
/* Replaces a whole map with prebuilt entries (a map exported by grit, say); count is at most GTU_MAP_CELLS. */
void gtu_load_map(GtuUi *ui, unsigned bg, const uint16_t *entries, size_t count);

/* Text. A newline goes back to x and down one row. */
void gtu_text(GtuUi *ui, unsigned bg, int x, int y, const char *text, uint8_t palette);
/* Centred in the visible columns: starts at (text_columns - length) / 2. */
void gtu_center(GtuUi *ui, unsigned bg, int y, const char *text, uint8_t palette);
/* value in decimal, right-aligned in width characters like "%*d" (a longer number is not cut). */
void gtu_number(GtuUi *ui, unsigned bg, int x, int y, int value, int width, uint8_t palette);
/* The glyph for a space (the font's blank) over a rectangle, to wipe text. */
void gtu_clear_text_rect(GtuUi *ui, unsigned bg, int x, int y, int width, int height, uint8_t palette);

/* Sprites. */
void gtu_hide_all_sprites(GtuUi *ui);
void gtu_hide_sprite(GtuUi *ui, unsigned index);
/* A square 16-colour sprite at x, y (taken modulo the console's 512 x 256 wrap), with the console's priority 0 to 3.
 * A bad index does nothing. */
void gtu_set_sprite(GtuUi *ui, unsigned index, int x, int y, uint16_t tile, uint8_t palette, GtuSpriteSize size,
                    uint8_t priority, unsigned flags);

/* Dirty flags, for the copy to VRAM. A "take" says whether the layer changed since the last take, and clears it. */
bool gtu_take_map_dirty(GtuUi *ui, unsigned bg);
bool gtu_take_oam_dirty(GtuUi *ui);
void gtu_mark_all_dirty(GtuUi *ui);

#ifdef __cplusplus
}
#endif

#endif
