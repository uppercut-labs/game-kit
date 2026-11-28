/* hud-path-font: single-stroke HUD glyphs as paths on a drafting grid, rasterized once into a 4-bit coverage atlas.
 *
 * Each glyph is a string of grid points, two digits each ("x then y", x from 0 to 6 and y from 0 to 8), joined by
 * straight strokes; a '/' lifts the pen. hud_path_font_build draws every glyph into its own cell of a 256 x 128 atlas
 * by calling your function once per covered texel, so the atlas can be any texel format (4-bit pairs, ARGB4444, a 1-bit
 * mask). A renderer that draws lines instead of textured quads strokes the same paths directly.
 *
 *   static int8_t slot[128];
 *   hud_path_font_build(slot, put_ink, HUD_PATH_FONT_ALL_GLYPHS);
 *
 * Nothing here allocates. The rasterizer uses float math and sqrtf, so link the C maths library where it is separate.
 */
#ifndef HUD_PATH_FONT_H
#define HUD_PATH_FONT_H

#include <stdint.h>

/* The package version, also read by CMakeLists.txt. */
#define HUD_PATH_FONT_VERSION "1.0.0"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    /* The atlas: COLS x ROWS cells of CELL_W x CELL_H texels, glyph i in cell i, row by row. */
    HUD_PATH_FONT_ATLAS_W = 256,
    HUD_PATH_FONT_ATLAS_H = 128,
    HUD_PATH_FONT_CELL_W = 20,
    HUD_PATH_FONT_CELL_H = 24,
    HUD_PATH_FONT_COLS = 12,
    HUD_PATH_FONT_ROWS = 5,
    /* The largest coverage the callback receives. */
    HUD_PATH_FONT_ALPHA_MAX = 15,
    /* The drafting grid: path x runs from 0 to GRID_W, y from 0 to GRID_H. */
    HUD_PATH_FONT_GRID_W = 6,
    HUD_PATH_FONT_GRID_H = 8,
    /* The glyph sets. CORE is A to Z, 0 to 9 and / - % + : (41 glyphs). ALL adds ! . ~ in cells 41 to 43, so the
       first 41 cells of both atlases are the same. Any other count builds that many glyphs of the ALL order. */
    HUD_PATH_FONT_CORE_GLYPHS = 41,
    HUD_PATH_FONT_ALL_GLYPHS = 44
};

/* Drawing metrics at size 1, in screen units. A textured renderer draws a glyph's cell as a QUAD_W x QUAD_H quad (half
   the cell's texels) and moves the pen ADVANCE to the right for every byte, drawn or not. A stroking renderer places
   grid point (gx, gy) at (x + (gx + STROKE_ORIGIN) * size, y + (gy + STROKE_ORIGIN) * size) and draws each segment
   STROKE_WIDTH * size wide, which lines it up with the quad. */
#define HUD_PATH_FONT_ADVANCE 8.7f
#define HUD_PATH_FONT_QUAD_W 10.f
#define HUD_PATH_FONT_QUAD_H 12.f
#define HUD_PATH_FONT_STROKE_ORIGIN 1.45f
#define HUD_PATH_FONT_STROKE_WIDTH 1.5f

/* Receives one texel's coverage: offset is y * HUD_PATH_FONT_ATLAS_W + x and alpha runs from 0 to 15. Strokes overlap,
   so one texel can be called more than once; keep the largest alpha. Texels that are never called have coverage 0. */
typedef void HudPathInk(uint16_t offset, uint8_t alpha);

/* Receives one stroke of a path, from grid point (ax, ay) to (bx, by). */
typedef void HudPathSegment(void *user, uint8_t ax, uint8_t ay, uint8_t bx, uint8_t by);

/* Rasterizes the first `glyphs` glyphs (HUD_PATH_FONT_CORE_GLYPHS or HUD_PATH_FONT_ALL_GLYPHS; larger counts are
   clamped to ALL) into the atlas through put_ink, glyph by glyph and stroke by stroke in a fixed order, and fills slot:
   slot[code] is the glyph's cell index for every character code the set has and -1 for every other code. The atlas
   memory is yours; clear it to zero coverage first. */
void hud_path_font_build(int8_t slot[128], HudPathInk *put_ink, uint8_t glyphs);

/* The glyph's path for a character code, or NULL when the first `glyphs` glyphs do not include it. Codes are exact:
   lowercase letters have no glyph, so fold them to capitals first. */
const char *hud_path_font_path(uint8_t code, uint8_t glyphs);

/* The character code of the glyph in cell `index`, or 0 past the last glyph. */
uint8_t hud_path_font_code(uint8_t index);

/* Calls segment(user, ...) for each stroke of a path in order and returns the number of strokes. A path of one point,
   and a NULL path, have none. segment may be NULL to count only. */
uint8_t hud_path_font_segments(const char *path, HudPathSegment *segment, void *user);

/* The atlas texel of a cell's top-left corner. */
uint16_t hud_path_font_cell_x(uint8_t slot);
uint16_t hud_path_font_cell_y(uint8_t slot);

#ifdef __cplusplus
}
#endif

#endif
