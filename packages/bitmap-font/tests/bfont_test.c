/* bfont_test: the font tables and the drawing callback, with no display. */
#include <stdio.h>
#include <string.h>

#include "bfont/bfont.h"

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

typedef struct {
    int calls;
    int rects[256][4];
} Rects;

static void record(void *user, int x, int y, int w, int h)
{
    Rects *r = (Rects *)user;

    if (r->calls < 256) {
        r->rects[r->calls][0] = x;
        r->rects[r->calls][1] = y;
        r->rects[r->calls][2] = w;
        r->rects[r->calls][3] = h;
    }
    r->calls++;
}

typedef struct {
    char cells[12][40];
} Canvas;

static void canvas_clear(Canvas *c)
{
    int y;

    for (y = 0; y < 12; y++) {
        memset(c->cells[y], '.', 39);
        c->cells[y][39] = 0;
    }
}

static void canvas_fill(void *user, int x, int y, int w, int h)
{
    Canvas *c = (Canvas *)user;
    int i, j;

    for (j = y; j < y + h; j++) {
        for (i = x; i < x + w; i++) {
            if (i >= 0 && i < 39 && j >= 0 && j < 12) {
                c->cells[j][i] = '#';
            }
        }
    }
}

/* A canvas row is the given prefix followed by clear cells. */
static int row_is(const Canvas *c, int y, const char *prefix)
{
    size_t n = strlen(prefix);
    size_t i;

    if (strncmp(c->cells[y], prefix, n) != 0) {
        return 0;
    }
    for (i = n; i < 39; i++) {
        if (c->cells[y][i] != '.') {
            return 0;
        }
    }
    return 1;
}

static int rows_equal(const unsigned char *rows, const unsigned char *want, int n)
{
    return rows && memcmp(rows, want, (size_t)n) == 0;
}

static void test_known_glyphs(void)
{
    static const unsigned char A[7] = { 14, 17, 17, 31, 17, 17, 17 };
    static const unsigned char I[7] = { 14, 4, 4, 4, 4, 4, 14 };
    static const unsigned char DOT[7] = { 0, 0, 0, 0, 0, 12, 12 };
    static const unsigned char A8[8] = { 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00 };
    static const unsigned char P3[5] = { 6, 5, 6, 4, 4 };

    CHECK(rows_equal(bfont_glyph(&bfont_5x7, 'A'), A, 7), "5x7 A");
    CHECK(rows_equal(bfont_glyph(&bfont_5x7, 'I'), I, 7), "5x7 I is centred with serifs");
    CHECK(rows_equal(bfont_glyph(&bfont_5x7, '.'), DOT, 7), "5x7 full stop is two wide");
    CHECK(rows_equal(bfont_glyph(&bfont_8x8, 'A'), A8, 8), "8x8 A");
    CHECK(rows_equal(bfont_glyph(&bfont_3x5, 'P'), P3, 5), "3x5 P");
    CHECK(bfont_5x7.width == 5 && bfont_5x7.height == 7 && bfont_5x7.advance == 6, "5x7 metrics");
    CHECK(bfont_8x8.width == 8 && bfont_8x8.height == 8 && bfont_8x8.advance == 8, "8x8 metrics");
    CHECK(bfont_3x5.width == 3 && bfont_3x5.height == 5 && bfont_3x5.advance == 4, "3x5 metrics");
}

static void test_tables_are_well_formed(void)
{
    const bfont *fonts[3];
    int f, i, r, present;

    fonts[0] = &bfont_5x7;
    fonts[1] = &bfont_8x8;
    fonts[2] = &bfont_3x5;
    for (f = 0; f < 3; f++) {
        const bfont *font = fonts[f];
        unsigned mask = (1u << font->width) - 1u;

        present = 0;
        for (i = 0; i < font->count; i++) {
            if (!font->present[i]) {
                for (r = 0; r < font->height; r++) {
                    CHECK(font->rows[i * font->height + r] == 0, "font %d: an absent entry %d is not blank", f, i);
                }
                continue;
            }
            present++;
            for (r = 0; r < font->height; r++) {
                CHECK((font->rows[i * font->height + r] & ~mask) == 0, "font %d entry %d row %d has bits beyond its width", f, i, r);
            }
        }
        CHECK(font->present[0] == 1 || f == 2, "font %d starts with a glyph", f);
        CHECK(present > 0, "font %d has glyphs", f);
    }
}

static void test_coverage(void)
{
    int c, n5 = 0, n8 = 0, n3 = 0;
    static const char *const TITLE = "!ABDEHILPTUX";

    for (c = 0; c < 256; c++) {
        n5 += bfont_glyph(&bfont_5x7, c) != NULL;
        n8 += bfont_glyph(&bfont_8x8, c) != NULL;
        n3 += bfont_glyph(&bfont_3x5, c) != NULL;
    }
    /* 67 drawn in the 5x7 table, plus the 26 lowercase letters that fold onto capitals */
    CHECK(n5 == 67 + 26, "5x7 reaches %d codes", n5);
    CHECK(n8 == 64 + 26, "8x8 reaches %d codes", n8);
    CHECK(n3 == 12 + 11, "3x5 reaches %d codes", n3);
    for (c = 0x20; c <= 0x5F; c++) {
        CHECK(bfont_glyph(&bfont_8x8, c) != NULL, "8x8 has %d", c);
    }
    for (c = 0; TITLE[c]; c++) {
        CHECK(bfont_glyph(&bfont_3x5, TITLE[c]) != NULL, "3x5 has %c", TITLE[c]);
    }
    CHECK(bfont_glyph(&bfont_3x5, 'Z') == NULL, "3x5 has no Z");
}

static void test_lookup_edges(void)
{
    CHECK(bfont_glyph(&bfont_5x7, 'a') == bfont_glyph(&bfont_5x7, 'A'), "lowercase folds to the capital");
    CHECK(bfont_glyph(&bfont_5x7, 'z') == bfont_glyph(&bfont_5x7, 'Z'), "z folds to Z");
    CHECK(bfont_glyph(&bfont_5x7, 1) == NULL, "control code");
    CHECK(bfont_glyph(&bfont_5x7, 127) == NULL, "127");
    CHECK(bfont_glyph(&bfont_5x7, 200) == NULL, "200");
    CHECK(bfont_glyph(&bfont_5x7, -1) == NULL, "negative");
    CHECK(bfont_glyph(&bfont_5x7, '{') == NULL, "brace is not drawn");
    CHECK(bfont_glyph(&bfont_8x8, 'a') == bfont_glyph(&bfont_8x8, 'A'), "8x8 lowercase folds");
    CHECK(bfont_glyph(&bfont_8x8, '`') == NULL, "8x8 stops at 0x5F");
    CHECK(bfont_glyph(&bfont_3x5, 'z') == NULL, "no capital Z to fold to");
    CHECK(bfont_glyph(NULL, 'A') == NULL, "null font");
    CHECK(bfont_glyph(&bfont_5x7, ' ') != NULL, "space is a glyph");
}

static void test_pixels(void)
{
    /* A: .###. / #...# / #...# / ##### / #...# ... */
    CHECK(bfont_pixel(&bfont_5x7, 'A', 1, 0) == 1 && bfont_pixel(&bfont_5x7, 'A', 0, 0) == 0, "row 0 of A");
    CHECK(bfont_pixel(&bfont_5x7, 'A', 0, 3) == 1 && bfont_pixel(&bfont_5x7, 'A', 4, 3) == 1, "the bar of A");
    CHECK(bfont_pixel(&bfont_5x7, 'A', 2, 1) == 0, "inside A");
    CHECK(bfont_pixel(&bfont_5x7, 'A', 5, 0) == 0 && bfont_pixel(&bfont_5x7, 'A', 0, 7) == 0, "outside the cell");
    CHECK(bfont_pixel(&bfont_5x7, 'A', -1, 0) == 0 && bfont_pixel(&bfont_5x7, 'A', 0, -1) == 0, "negative");
    CHECK(bfont_pixel(&bfont_5x7, 1, 0, 0) == 0, "a missing glyph is clear");
    CHECK(bfont_pixel(&bfont_8x8, 'A', 2, 0) == 1 && bfont_pixel(&bfont_8x8, 'A', 1, 0) == 0, "8x8 A row 0 is 0x3C");
}

static void test_draw_glyph_runs(void)
{
    Rects r;

    memset(&r, 0, sizeof(r));
    CHECK(bfont_draw_glyph(&bfont_5x7, 'A', 0, 0, 1, record, &r) == 1, "A drawn");
    /* .###. | #...# | #...# | ##### | #...# | #...# | #...#  -> 1 + 2 + 2 + 1 + 2 + 2 + 2 runs */
    CHECK(r.calls == 12, "A is 12 runs, got %d", r.calls);
    CHECK(r.rects[0][0] == 1 && r.rects[0][1] == 0 && r.rects[0][2] == 3 && r.rects[0][3] == 1, "first run");
    CHECK(r.rects[1][0] == 0 && r.rects[1][1] == 1 && r.rects[1][2] == 1, "left stem on row 1");
    CHECK(r.rects[2][0] == 4 && r.rects[2][1] == 1 && r.rects[2][2] == 1, "right stem on row 1");
    CHECK(r.rects[5][0] == 0 && r.rects[5][1] == 3 && r.rects[5][2] == 5, "the bar is one run of five");

    memset(&r, 0, sizeof(r));
    CHECK(bfont_draw_glyph(&bfont_5x7, 'A', 10, 20, 3, record, &r) == 1, "A at scale 3");
    CHECK(r.calls == 12, "same runs at any scale");
    CHECK(r.rects[0][0] == 13 && r.rects[0][1] == 20 && r.rects[0][2] == 9 && r.rects[0][3] == 3, "scaled first run");
    CHECK(r.rects[5][0] == 10 && r.rects[5][1] == 29 && r.rects[5][2] == 15, "scaled bar");

    memset(&r, 0, sizeof(r));
    CHECK(bfont_draw_glyph(&bfont_5x7, ' ', 0, 0, 1, record, &r) == 1 && r.calls == 0, "space draws nothing and succeeds");
    CHECK(bfont_draw_glyph(&bfont_5x7, 1, 0, 0, 1, record, &r) == 0, "missing glyph");
    CHECK(bfont_draw_glyph(&bfont_5x7, 'A', 0, 0, 0, record, &r) == 0, "scale 0");
    CHECK(bfont_draw_glyph(&bfont_5x7, 'A', 0, 0, 1, NULL, &r) == 0, "no callback");
    CHECK(r.calls == 0, "nothing called on refusals");
}

static void test_text(void)
{
    Canvas c;
    int adv;

    canvas_clear(&c);
    adv = bfont_draw_text(&bfont_5x7, "HI", 0, 0, 1, canvas_fill, &c);
    CHECK(adv == 12, "HI advances 12, got %d", adv);
    CHECK(row_is(&c, 0, "#...#..###."), "row 0 of HI: %s", c.cells[0]);
    CHECK(row_is(&c, 3, "#####...#.."), "row 3 of HI: %s", c.cells[3]);
    CHECK(row_is(&c, 6, "#...#..###."), "row 6 of HI: %s", c.cells[6]);
    CHECK(row_is(&c, 7, ""), "row 7 is clear");

    canvas_clear(&c);
    adv = bfont_draw_text(&bfont_5x7, "a\x01" "b", 0, 0, 1, canvas_fill, &c);
    CHECK(adv == 18, "a missing glyph still advances, got %d", adv);
    CHECK(c.cells[0][6] == '.' && c.cells[3][6] == '.', "the gap is empty");
    CHECK(c.cells[3][12] == '#', "b follows the gap");

    CHECK(bfont_text_width(&bfont_5x7, "", 1) == 0, "empty");
    CHECK(bfont_text_width(&bfont_5x7, "A", 1) == 5, "one glyph is its width");
    CHECK(bfont_text_width(&bfont_5x7, "HI", 1) == 11, "two glyphs");
    CHECK(bfont_text_width(&bfont_5x7, "HI", 2) == 22, "scaled");
    CHECK(bfont_text_width(&bfont_8x8, "AB", 1) == 16, "8x8 text");
    CHECK(bfont_text_width(NULL, "A", 1) == 0 && bfont_text_width(&bfont_5x7, NULL, 1) == 0, "nulls");
    CHECK(bfont_draw_text(&bfont_5x7, NULL, 0, 0, 1, canvas_fill, &c) == 0, "null text");
    CHECK(bfont_draw_text(&bfont_5x7, "A", 0, 0, 0, canvas_fill, &c) == 0, "scale 0");
}

int main(void)
{
    test_known_glyphs();
    test_tables_are_well_formed();
    test_coverage();
    test_lookup_edges();
    test_pixels();
    test_draw_glyph_runs();
    test_text();
    printf("bfont-test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
