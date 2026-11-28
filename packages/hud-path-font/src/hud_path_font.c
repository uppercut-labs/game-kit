/* hud-path-font: the glyph paths and the rasterizer (include/hud_path_font/hud_path_font.h). */
#include "hud_path_font/hud_path_font.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    char code;
    const char *path;
} Glyph;

/* Single-stroke instrument alphabet on a six-by-eight drafting grid. The order is the cell order, and the CORE set is
   the first HUD_PATH_FONT_CORE_GLYPHS entries, so new glyphs only ever go at the end. */
static const Glyph glyph_table[HUD_PATH_FONT_ALL_GLYPHS] = {
    {'A', "080220406268/0464"}, {'B', "0800506163535404/5465675808"},
    {'C', "6150100107185867"}, {'D', "08004062664808"},
    {'E', "60000858/0454"}, {'F', "080060/0454"},
    {'G', "61501001071858676444"}, {'H', "0008/6068/0464"},
    {'I', "1050/3038/1858"}, {'J', "106066480806"},
    {'K', "0008/6005/2468"}, {'L', "000858"},
    {'M', "0800346068"}, {'N', "08006860"},
    {'O', "105061675818070110"}, {'P', "08005061635404"},
    {'Q', "105061665718070110/4468"}, {'R', "08005061635404/3468"},
    {'S', "615010010314546567581807"}, {'T', "0060/3038"},
    {'U', "000718586760"}, {'V', "00283860"}, {'W', "0018334860"},
    {'X', "0068/6008"}, {'Y', "003460/3438"}, {'Z', "00600868"},
    {'0', "105061675818070110/1652"}, {'1', "123038/1858"},
    {'2', "0110506163541405070868"}, {'3', "00506163545465675808/2454"},
    {'4', "4048/400565"}, {'5', "600004546567581807"},
    {'6', "605010010718586765541404"}, {'7', "006018"},
    {'8', "105061635414030110/145465675818070514"},
    {'9', "071858676150100103145463"}, {'/', "0860"}, {'-', "1464"},
    {'%', "0860/1101021211/5556666555"}, {'+', "1464/3137"}, {':', "3233/3637"},
    {'!', "3035/3738"}, {'.', "3738"}, {'~', "05234563"}
};

static uint8_t glyph_count(uint8_t glyphs)
{
    return glyphs > HUD_PATH_FONT_ALL_GLYPHS ? (uint8_t)HUD_PATH_FONT_ALL_GLYPHS : glyphs;
}

const char *hud_path_font_path(uint8_t code, uint8_t glyphs)
{
    uint8_t count = glyph_count(glyphs);
    for (uint8_t i = 0; i < count; ++i)
        if ((uint8_t)glyph_table[i].code == code)
            return glyph_table[i].path;
    return NULL;
}

uint8_t hud_path_font_code(uint8_t index)
{
    return index < HUD_PATH_FONT_ALL_GLYPHS ? (uint8_t)glyph_table[index].code : 0;
}

uint8_t hud_path_font_segments(const char *path, HudPathSegment *segment, void *user)
{
    uint8_t strokes = 0;
    int8_t ax = -1, ay = 0;
    if (!path)
        return 0;
    for (const char *p = path; *p;) {
        if (*p == '/') {
            ax = -1;
            ++p;
            continue;
        }
        uint8_t bx = (uint8_t)(*p++ - '0'), by = (uint8_t)(*p++ - '0');
        if (ax >= 0) {
            if (segment)
                segment(user, (uint8_t)ax, (uint8_t)ay, bx, by);
            ++strokes;
        }
        ax = (int8_t)bx;
        ay = (int8_t)by;
    }
    return strokes;
}

uint16_t hud_path_font_cell_x(uint8_t slot)
{
    return (uint16_t)(slot % HUD_PATH_FONT_COLS * HUD_PATH_FONT_CELL_W);
}

uint16_t hud_path_font_cell_y(uint8_t slot)
{
    return (uint16_t)(slot / HUD_PATH_FONT_COLS * HUD_PATH_FONT_CELL_H);
}

typedef struct {
    uint8_t cell;
    HudPathInk *put_ink;
} InkCell;

/* The distance from each texel centre to the segment, two texels per grid unit and the grid inset by 1.5 units,
   turned into coverage of a stroke 0.81 units wide on each side with a one-texel ramp. The float expressions are kept
   exactly as the projects that used this font wrote them, so the atlas stays the same bit for bit. */
static void ink_segment(void *user, uint8_t ax, uint8_t ay, uint8_t bx, uint8_t by)
{
    const InkCell *ink = (const InkCell *)user;
    uint8_t cell = ink->cell;
    float vx = bx - ax, vy = by - ay, length = vx * vx + vy * vy;
    for (uint8_t y = 0; y < HUD_PATH_FONT_CELL_H; ++y)
        for (uint8_t x = 0; x < HUD_PATH_FONT_CELL_W; ++x) {
            float dx = (x + .5f) * .5f - ax - 1.5f, dy = (y + .5f) * .5f - ay - 1.5f;
            float t = length ? (dx * vx + dy * vy) / length : 0;
            if (t < 0)
                t = 0;
            if (t > 1)
                t = 1;
            dx -= t * vx;
            dy -= t * vy;
            float coverage = .5f + 2.f * (.81f - sqrtf(dx * dx + dy * dy));
            if (coverage <= 0)
                continue;
            uint8_t a = coverage >= 1 ? HUD_PATH_FONT_ALPHA_MAX : (uint8_t)(coverage * 15 + .5f);
            uint16_t offset = (uint16_t)((cell / HUD_PATH_FONT_COLS * HUD_PATH_FONT_CELL_H + y) * HUD_PATH_FONT_ATLAS_W +
                                         cell % HUD_PATH_FONT_COLS * HUD_PATH_FONT_CELL_W + x);
            ink->put_ink(offset, a);
        }
}

void hud_path_font_build(int8_t slot[128], HudPathInk *put_ink, uint8_t glyphs)
{
    uint8_t count = glyph_count(glyphs);
    memset(slot, -1, 128);
    for (uint8_t i = 0; i < count; ++i) {
        InkCell ink = {i, put_ink};
        slot[(uint8_t)glyph_table[i].code] = (int8_t)i;
        hud_path_font_segments(glyph_table[i].path, ink_segment, &ink);
    }
}
