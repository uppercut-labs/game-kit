/* bitmap-font: small bitmap fonts as tables, with a callback to draw them.
 *
 * Three fonts come from one source file (data/bitmap-fonts.txt): 5x7, 8x8 and a 3x5 set of title letters. Each is a table
 * of row bytes: a glyph is `height` bytes, one per row, and pixel x of a row is set when bit (width - 1 - x) is. The
 * tables are generated into bfont_tables.h; nothing here allocates, and nothing depends on a graphics library.
 *
 *   bfont_draw_text(&bfont_5x7, "SCORE 10", 8, 8, 2, fill_rect, &canvas);
 *
 * A lowercase letter draws as its capital when the font has no lowercase glyph of its own, which none of these does.
 */
#ifndef BFONT_H
#define BFONT_H

#define BFONT_VERSION "0.1.0"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bfont {
    int width;                      /* columns in a glyph */
    int height;                     /* rows in a glyph, and bytes per glyph */
    int advance;                    /* pixels from one glyph's left edge to the next, at scale 1 */
    int first;                      /* the character code of table entry 0 */
    int count;                      /* entries in the table */
    const unsigned char *rows;      /* count * height bytes */
    const unsigned char *present;   /* count bytes: 1 where the font has a glyph for that code */
} bfont;

extern const bfont bfont_5x7;
extern const bfont bfont_8x8;
extern const bfont bfont_3x5;

/* The glyph's `height` row bytes, or NULL when the font has no glyph for the code (outside its range, not drawn in it,
   or a lowercase letter whose capital is missing too). A lowercase letter gives its capital's rows. */
const unsigned char *bfont_glyph(const bfont *font, int code);

/* 1 when the pixel at column x, row y is set, 0 when it is clear, the glyph is missing or x, y are outside it. */
int bfont_pixel(const bfont *font, int code, int x, int y);

/* Fills the rectangle at (x, y), w pixels wide and h tall. */
typedef void (*bfont_fill_fn)(void *user, int x, int y, int w, int h);

/* Draws one glyph with its top-left corner at (x, y), each font pixel as a scale-by-scale square, by calling fill once
   per horizontal run of set pixels (a run is w = run * scale wide, h = scale tall). Returns 1 when the glyph exists and
   was drawn, 0 when it is missing or scale is below 1 (nothing is called). */
int bfont_draw_glyph(const bfont *font, int code, int x, int y, int scale, bfont_fill_fn fill, void *user);

/* Draws a NUL-terminated string left to right, advancing font->advance * scale for every byte (a missing glyph leaves a
   gap). Returns the pixels advanced. Bytes are character codes, so UTF-8 is not decoded. */
int bfont_draw_text(const bfont *font, const char *text, int x, int y, int scale, bfont_fill_fn fill, void *user);

/* The width in pixels of a string, without the gap after its last glyph: (n - 1) * advance + width, times scale. 0 for an
   empty string. */
int bfont_text_width(const bfont *font, const char *text, int scale);

#ifdef __cplusplus
}
#endif

#endif
