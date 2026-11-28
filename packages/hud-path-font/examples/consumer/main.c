/* A made-up project baking the HUD font into a 4-bit paired atlas (two texels per byte, low nibble first, the layout
 * of a PS2 T4 texture), then printing one glyph's cell as text and stroking another glyph's path into a line list.
 *
 *   my-hud-path-font
 */
#include <stdio.h>

#include "hud_path_font/hud_path_font.h"

static uint8_t atlas[HUD_PATH_FONT_ATLAS_W * HUD_PATH_FONT_ATLAS_H / 2];
static int8_t slot[128];

/* keep the largest coverage in each nibble */
static void put_ink(uint16_t offset, uint8_t alpha)
{
    uint8_t *pair = &atlas[offset / 2];
    unsigned shift = (offset & 1u) * 4u;
    if (alpha > ((*pair >> shift) & 15u))
        *pair = (uint8_t)((*pair & ~(15u << shift)) | ((unsigned)alpha << shift));
}

static uint8_t texel(uint16_t x, uint16_t y)
{
    uint16_t offset = (uint16_t)(y * HUD_PATH_FONT_ATLAS_W + x);
    return (uint8_t)((atlas[offset / 2] >> ((offset & 1u) * 4u)) & 15u);
}

typedef struct {
    float x, y, size;
    int lines;
} Pen;

/* a renderer that draws lines would call its own stroke function here */
static void stroke(void *user, uint8_t ax, uint8_t ay, uint8_t bx, uint8_t by)
{
    Pen *pen = (Pen *)user;
    float x0 = pen->x + (ax + HUD_PATH_FONT_STROKE_ORIGIN) * pen->size;
    float y0 = pen->y + (ay + HUD_PATH_FONT_STROKE_ORIGIN) * pen->size;
    float x1 = pen->x + (bx + HUD_PATH_FONT_STROKE_ORIGIN) * pen->size;
    float y1 = pen->y + (by + HUD_PATH_FONT_STROKE_ORIGIN) * pen->size;
    printf("  line (%.2f, %.2f) to (%.2f, %.2f), width %.2f\n", x0, y0, x1, y1, HUD_PATH_FONT_STROKE_WIDTH * pen->size);
    pen->lines++;
}

int main(void)
{
    static const char shade[] = " .:-=+*#%@@@@@@@";
    hud_path_font_build(slot, put_ink, HUD_PATH_FONT_ALL_GLYPHS);

    uint8_t cell = (uint8_t)slot['K'];
    uint16_t u = hud_path_font_cell_x(cell), v = hud_path_font_cell_y(cell);
    unsigned inked = 0;
    for (uint16_t y = 0; y < HUD_PATH_FONT_CELL_H; ++y) {
        for (uint16_t x = 0; x < HUD_PATH_FONT_CELL_W; ++x) {
            uint8_t a = texel((uint16_t)(u + x), (uint16_t)(v + y));
            inked += a > 0;
            putchar(shade[a]);
        }
        putchar('\n');
    }

    Pen pen = {100.f, 40.f, 1.25f, 0};
    printf("'!' at (%.0f, %.0f), size %.2f; the next glyph starts at x = %.2f\n", pen.x, pen.y, pen.size,
           pen.x + HUD_PATH_FONT_ADVANCE * pen.size);
    hud_path_font_segments(hud_path_font_path('!', HUD_PATH_FONT_ALL_GLYPHS), stroke, &pen);

    /* K is in cell 10, inked; '!' is two strokes; the CORE set has no '!' */
    return cell == 10 && inked > 40 && pen.lines == 2 && !hud_path_font_path('!', HUD_PATH_FONT_CORE_GLYPHS) ? 0 : 1;
}
