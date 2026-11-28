/* The hud-path-font tests: the atlas for each glyph set against its recorded digests, the slot table, the paths, the
 * stroke walker and the cell metrics. */
#include "hud_path_font/hud_path_font.h"

#include "check.h"

/* Recorded from the atlas builds this package replaced (see README.md, "Checking an earlier copy"). The call digest
   covers every ink call in order; the atlas digest covers the 256 x 128 texels at one byte each, largest alpha. */
static const struct {
    uint8_t glyphs;
    uint32_t calls;
    uint64_t call_digest;
    uint64_t atlas_digest;
} expected[] = {
    {HUD_PATH_FONT_CORE_GLYPHS, 9750u, 0xce475161ecb89e89ULL, 0xe5e1d7556a5f7190ULL},
    {HUD_PATH_FONT_ALL_GLYPHS, 9938u, 0xa64012663330b481ULL, 0xca6b9c39109da67cULL},
};

static Recording core, all;

static void build(Recording *r, int8_t slot[128], uint8_t glyphs)
{
    recording_reset(r);
    recording_target = r;
    hud_path_font_build(slot, record_ink, glyphs);
}

static void test_atlas_digests(void)
{
    int8_t slot[128];
    for (size_t i = 0; i < sizeof expected / sizeof *expected; ++i) {
        Recording *r = expected[i].glyphs == HUD_PATH_FONT_CORE_GLYPHS ? &core : &all;
        build(r, slot, expected[i].glyphs);
        uint64_t atlas_digest = fnv1a(FNV_START, r->atlas, sizeof r->atlas);
        printf("%u glyphs: %u ink calls, call digest 0x%016llx, atlas digest 0x%016llx\n", (unsigned)expected[i].glyphs,
               (unsigned)r->count, (unsigned long long)r->digest, (unsigned long long)atlas_digest);
        CHECK(!r->overflow);
        CHECK(r->count == expected[i].calls);
        CHECK(r->digest == expected[i].call_digest);
        CHECK(atlas_digest == expected[i].atlas_digest);
    }
}

/* The CORE atlas is the ALL atlas without its last three cells: the same calls, then only calls inside cells 41-43. */
static void test_core_is_a_prefix_of_all(void)
{
    CHECK(core.count > 0 && core.count < all.count);
    CHECK(memcmp(core.calls, all.calls, core.count * sizeof *core.calls) == 0);
    for (uint32_t i = core.count; i < all.count && i < RECORD_LIMIT; ++i) {
        uint16_t x = (uint16_t)(all.calls[i].offset % HUD_PATH_FONT_ATLAS_W);
        uint16_t y = (uint16_t)(all.calls[i].offset / HUD_PATH_FONT_ATLAS_W);
        uint8_t cell = (uint8_t)(y / HUD_PATH_FONT_CELL_H * HUD_PATH_FONT_COLS + x / HUD_PATH_FONT_CELL_W);
        CHECK(cell >= HUD_PATH_FONT_CORE_GLYPHS && cell < HUD_PATH_FONT_ALL_GLYPHS);
    }
}

/* Every call lands inside the atlas, inside the cell of the glyph being drawn, with alpha 0 to 15; and the build is
   the same on a second run. */
static void test_calls_stay_in_their_cells(void)
{
    uint8_t cell_seen = 0;
    for (uint32_t i = 0; i < all.count && i < RECORD_LIMIT; ++i) {
        uint16_t x = (uint16_t)(all.calls[i].offset % HUD_PATH_FONT_ATLAS_W);
        uint16_t y = (uint16_t)(all.calls[i].offset / HUD_PATH_FONT_ATLAS_W);
        CHECK(y < HUD_PATH_FONT_ATLAS_H);
        CHECK(x < HUD_PATH_FONT_COLS * HUD_PATH_FONT_CELL_W);
        CHECK(all.calls[i].alpha <= HUD_PATH_FONT_ALPHA_MAX);
        uint8_t cell = (uint8_t)(y / HUD_PATH_FONT_CELL_H * HUD_PATH_FONT_COLS + x / HUD_PATH_FONT_CELL_W);
        CHECK(cell >= cell_seen);   /* glyphs are drawn in cell order */
        cell_seen = cell;
    }
    static Recording again;
    int8_t slot[128];
    build(&again, slot, HUD_PATH_FONT_ALL_GLYPHS);
    CHECK(again.count == all.count && again.digest == all.digest);
}

static void test_slots(void)
{
    const uint8_t sets[] = {HUD_PATH_FONT_CORE_GLYPHS, HUD_PATH_FONT_ALL_GLYPHS};
    static Recording scratch;
    for (size_t s = 0; s < sizeof sets; ++s) {
        int8_t slot[128];
        build(&scratch, slot, sets[s]);
        uint8_t present = 0;
        for (uint8_t code = 0; code < 128; ++code) {
            if (slot[code] < 0) {
                CHECK(slot[code] == -1);
                CHECK(hud_path_font_path(code, sets[s]) == NULL);
                continue;
            }
            ++present;
            CHECK(hud_path_font_code((uint8_t)slot[code]) == code);
            CHECK(hud_path_font_path(code, sets[s]) != NULL);
        }
        CHECK(present == sets[s]);
    }
    int8_t slot[128];
    build(&scratch, slot, HUD_PATH_FONT_CORE_GLYPHS);
    CHECK(slot['A'] == 0 && slot['Z'] == 25 && slot['0'] == 26 && slot[':'] == 40);
    CHECK(slot['!'] == -1 && slot['.'] == -1 && slot['~'] == -1 && slot['a'] == -1 && slot[' '] == -1);
    build(&scratch, slot, HUD_PATH_FONT_ALL_GLYPHS);
    CHECK(slot['!'] == 41 && slot['.'] == 42 && slot['~'] == 43);
    /* a count above ALL is clamped to it */
    static Recording clamped;
    build(&clamped, slot, 200);
    CHECK(clamped.count == all.count && clamped.digest == all.digest);
}

static void test_paths(void)
{
    CHECK(strcmp(hud_path_font_path('A', HUD_PATH_FONT_CORE_GLYPHS), "080220406268/0464") == 0);
    CHECK(strcmp(hud_path_font_path('~', HUD_PATH_FONT_ALL_GLYPHS), "05234563") == 0);
    CHECK(hud_path_font_path('!', HUD_PATH_FONT_CORE_GLYPHS) == NULL);
    CHECK(hud_path_font_path('!', HUD_PATH_FONT_ALL_GLYPHS) != NULL);
    CHECK(hud_path_font_path('a', HUD_PATH_FONT_ALL_GLYPHS) == NULL);
    CHECK(hud_path_font_path(200, HUD_PATH_FONT_ALL_GLYPHS) == NULL);
    CHECK(hud_path_font_path('A', 0) == NULL);
    CHECK(hud_path_font_code(HUD_PATH_FONT_ALL_GLYPHS) == 0);
    CHECK(hud_path_font_code(255) == 0);

    /* every path: pairs of digits on the grid, runs split by single slashes, each run at least one stroke */
    uint64_t digest = FNV_START;
    for (uint8_t i = 0; i < HUD_PATH_FONT_ALL_GLYPHS; ++i) {
        const char *path = hud_path_font_path(hud_path_font_code(i), HUD_PATH_FONT_ALL_GLYPHS);
        CHECK(path != NULL);
        if (!path)
            continue;
        digest = fnv1a(digest, path, strlen(path) + 1);
        size_t run = 0;
        for (const char *p = path;; ++p) {
            if (*p == '/' || !*p) {
                CHECK(run >= 4 && run % 2 == 0);
                run = 0;
                if (!*p)
                    break;
                continue;
            }
            CHECK(*p >= '0' && *p <= '0' + (run % 2 ? HUD_PATH_FONT_GRID_H : HUD_PATH_FONT_GRID_W));
            ++run;
        }
    }
    printf("paths digest 0x%016llx\n", (unsigned long long)digest);
    CHECK(digest == 0xe2344914742514a3ULL);
}

typedef struct {
    uint8_t count;
    uint8_t points[16][4];
} Strokes;

static void collect(void *user, uint8_t ax, uint8_t ay, uint8_t bx, uint8_t by)
{
    Strokes *s = (Strokes *)user;
    if (s->count < 16) {
        s->points[s->count][0] = ax;
        s->points[s->count][1] = ay;
        s->points[s->count][2] = bx;
        s->points[s->count][3] = by;
    }
    ++s->count;
}

static void test_segments(void)
{
    Strokes s = {0};
    /* A: 08 02 20 40 62 68, then a lifted pen and 04 64 */
    CHECK(hud_path_font_segments("080220406268/0464", collect, &s) == 6);
    CHECK(s.count == 6);
    CHECK(s.points[0][0] == 0 && s.points[0][1] == 8 && s.points[0][2] == 0 && s.points[0][3] == 2);
    CHECK(s.points[4][0] == 6 && s.points[4][1] == 2 && s.points[4][2] == 6 && s.points[4][3] == 8);
    CHECK(s.points[5][0] == 0 && s.points[5][1] == 4 && s.points[5][2] == 6 && s.points[5][3] == 4);
    CHECK(hud_path_font_segments("3738", NULL, NULL) == 1);
    CHECK(hud_path_font_segments("37", NULL, NULL) == 0);
    CHECK(hud_path_font_segments("", NULL, NULL) == 0);
    CHECK(hud_path_font_segments(NULL, collect, &s) == 0);
    CHECK(hud_path_font_segments(hud_path_font_path('%', HUD_PATH_FONT_CORE_GLYPHS), NULL, NULL) == 9);
}

static void test_metrics(void)
{
    CHECK(strlen(HUD_PATH_FONT_VERSION) > 0);
    CHECK(HUD_PATH_FONT_COLS * HUD_PATH_FONT_CELL_W <= HUD_PATH_FONT_ATLAS_W);
    CHECK(HUD_PATH_FONT_ROWS * HUD_PATH_FONT_CELL_H <= HUD_PATH_FONT_ATLAS_H);
    CHECK(HUD_PATH_FONT_COLS * HUD_PATH_FONT_ROWS >= HUD_PATH_FONT_ALL_GLYPHS);
    CHECK(HUD_PATH_FONT_QUAD_W * 2 == HUD_PATH_FONT_CELL_W && HUD_PATH_FONT_QUAD_H * 2 == HUD_PATH_FONT_CELL_H);
    CHECK(hud_path_font_cell_x(0) == 0 && hud_path_font_cell_y(0) == 0);
    CHECK(hud_path_font_cell_x(11) == 220 && hud_path_font_cell_y(11) == 0);
    CHECK(hud_path_font_cell_x(12) == 0 && hud_path_font_cell_y(12) == 24);
    CHECK(hud_path_font_cell_x(43) == 140 && hud_path_font_cell_y(43) == 72);
}

int main(void)
{
    test_atlas_digests();
    test_core_is_a_prefix_of_all();
    test_calls_stay_in_their_cells();
    test_slots();
    test_paths();
    test_segments();
    test_metrics();
    return TEST_RESULT("hud-path-font");
}
