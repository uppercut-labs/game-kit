#include "bfont/bfont.h"

#include <stddef.h>

#include "bfont/bfont_tables.h"

#define DEFINE_FONT(name, ID)                                                                               \
    const bfont name = { ID##_WIDTH, ID##_HEIGHT, ID##_ADVANCE, ID##_FIRST, ID##_COUNT, ID##_ROWS, ID##_PRESENT }

DEFINE_FONT(bfont_5x7, BFONT_5X7);
DEFINE_FONT(bfont_8x8, BFONT_8X8);
DEFINE_FONT(bfont_3x5, BFONT_3X5);

static const unsigned char *entry(const bfont *font, int code)
{
    int index = code - font->first;

    if (index < 0 || index >= font->count || !font->present[index]) {
        return NULL;
    }
    return font->rows + (size_t)index * (size_t)font->height;
}

const unsigned char *bfont_glyph(const bfont *font, int code)
{
    const unsigned char *rows;

    if (!font) {
        return NULL;
    }
    rows = entry(font, code);
    if (!rows && code >= 'a' && code <= 'z') {
        rows = entry(font, code - 'a' + 'A');
    }
    return rows;
}

int bfont_pixel(const bfont *font, int code, int x, int y)
{
    const unsigned char *rows = bfont_glyph(font, code);

    if (!rows || x < 0 || y < 0 || x >= font->width || y >= font->height) {
        return 0;
    }
    return (rows[y] >> (font->width - 1 - x)) & 1;
}

int bfont_draw_glyph(const bfont *font, int code, int x, int y, int scale, bfont_fill_fn fill, void *user)
{
    const unsigned char *rows = bfont_glyph(font, code);
    int row;

    if (!rows || scale < 1 || !fill) {
        return 0;
    }
    for (row = 0; row < font->height; row++) {
        int column = 0;

        while (column < font->width) {
            int start;

            if (!((rows[row] >> (font->width - 1 - column)) & 1)) {
                column++;
                continue;
            }
            start = column++;
            while (column < font->width && ((rows[row] >> (font->width - 1 - column)) & 1)) {
                column++;
            }
            fill(user, x + start * scale, y + row * scale, (column - start) * scale, scale);
        }
    }
    return 1;
}

int bfont_draw_text(const bfont *font, const char *text, int x, int y, int scale, bfont_fill_fn fill, void *user)
{
    int pen = 0;

    if (!font || !text || scale < 1) {
        return 0;
    }
    for (; *text; text++) {
        bfont_draw_glyph(font, (unsigned char)*text, x + pen, y, scale, fill, user);
        pen += font->advance * scale;
    }
    return pen;
}

int bfont_text_width(const bfont *font, const char *text, int scale)
{
    size_t n = 0;

    if (!font || !text || scale < 1) {
        return 0;
    }
    while (text[n]) {
        n++;
    }
    return n == 0 ? 0 : ((int)(n - 1) * font->advance + font->width) * scale;
}
