# hud-path-font

A single-stroke HUD font for console games. Every glyph is a short path on a six-by-eight drafting grid. The library rasterizes the paths once, at start-up or in a bake step, into a 256 x 128 atlas of 4-bit coverage, calling a function of yours for every covered texel so the atlas can be any texel format the console wants. A renderer that draws lines instead of textured quads strokes the same paths directly, so a game can use the atlas on one console and strokes on another and get the same letters.

Nothing here allocates or depends on a graphics library. Version 1.0.0. Written in C99 with CMake, using `<stdint.h>` types in the public header. MIT licence.

## Install

This is a C library, so it is not on a package index. A project takes it at a release tag, with CMake `FetchContent` (CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(hud_path_font
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        hud-path-font@1.0.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/hud-path-font)
FetchContent_MakeAvailable(hud_path_font)

target_link_libraries(my-game PRIVATE hud_path_font)
```

`examples/consumer/CMakeLists.txt` is that, complete, with a program that bakes the atlas, prints a glyph and strokes another. For an offline build set `-DFETCHCONTENT_SOURCE_DIR_HUD_PATH_FONT=<a checkout of game-kit>`. A project that vendors the folder uses `add_subdirectory(<path>/hud-path-font)`. The including project gets the CMake variable `HUD_PATH_FONT_VERSION`.

A console build that uses its SDK's Makefile instead of CMake adds `src/hud_path_font.c` to its sources and `include/` to its include path. The rasterizer calls `sqrtf`, so link the maths library (`-lm`) where it is separate.

## Use

Bake the atlas into a texture, here a 4-bit paired one (two texels per byte, low nibble first):

```c
#include "hud_path_font/hud_path_font.h"

static int8_t slot[128];
static uint8_t atlas[HUD_PATH_FONT_ATLAS_W * HUD_PATH_FONT_ATLAS_H / 2];

static void put_ink(uint16_t offset, uint8_t alpha)
{
    uint8_t *pair = &atlas[offset / 2];
    unsigned shift = (offset & 1u) * 4u;
    if (alpha > ((*pair >> shift) & 15u))
        *pair = (uint8_t)((*pair & ~(15u << shift)) | ((unsigned)alpha << shift));
}

hud_path_font_build(slot, put_ink, HUD_PATH_FONT_ALL_GLYPHS);
```

Then draw a string as one textured quad per glyph:

```c
for (; *text; ++text, x += HUD_PATH_FONT_ADVANCE * size) {
    uint8_t ch = (uint8_t)*text;
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    if (ch >= 128 || slot[ch] < 0) continue;
    uint16_t u = hud_path_font_cell_x((uint8_t)slot[ch]), v = hud_path_font_cell_y((uint8_t)slot[ch]);
    draw_quad(x, y, HUD_PATH_FONT_QUAD_W * size, HUD_PATH_FONT_QUAD_H * size,
              u, v, HUD_PATH_FONT_CELL_W, HUD_PATH_FONT_CELL_H);
}
```

Or stroke the paths, with no texture at all:

```c
static void stroke(void *user, uint8_t ax, uint8_t ay, uint8_t bx, uint8_t by)
{
    const Pen *pen = user;
    draw_line(pen->x + (ax + HUD_PATH_FONT_STROKE_ORIGIN) * pen->size, pen->y + (ay + HUD_PATH_FONT_STROKE_ORIGIN) * pen->size,
              pen->x + (bx + HUD_PATH_FONT_STROKE_ORIGIN) * pen->size, pen->y + (by + HUD_PATH_FONT_STROKE_ORIGIN) * pen->size,
              HUD_PATH_FONT_STROKE_WIDTH * pen->size);
}

hud_path_font_segments(hud_path_font_path('K', HUD_PATH_FONT_ALL_GLYPHS), stroke, &pen);
```

| Name | What it does |
| --- | --- |
| `hud_path_font_build(slot, put_ink, glyphs)` | Rasterizes the first `glyphs` glyphs, glyph `i` into cell `i`, calling `put_ink(offset, alpha)` for each covered texel: `offset` is `y * 256 + x` and `alpha` is 0 to 15. Strokes overlap, so a texel can be called more than once; keep the largest alpha. Fills `slot[code]` with the glyph's cell for every code in the set and -1 for every other code. Clear the atlas to zero coverage first. |
| `hud_path_font_path(code, glyphs)` | The glyph's path, or NULL when the set has no glyph for that code. Codes are exact, so fold lowercase to capitals first. |
| `hud_path_font_segments(path, segment, user)` | Calls `segment(user, ax, ay, bx, by)` for each stroke in grid units, in order, and returns the stroke count. `segment` may be NULL to count only. |
| `hud_path_font_code(index)` | The character code of the glyph in cell `index`, or 0 past the last glyph. |
| `hud_path_font_cell_x(slot)`, `hud_path_font_cell_y(slot)` | The atlas texel of a cell's top-left corner. |
| `HudPathInk`, `HudPathSegment` | The two callback types. `HudPathInk` has no user pointer, so the atlas it writes is usually a static array. |

The build is deterministic: the same glyph set always makes the same calls in the same order, and the tests pin them by digest.

## Glyph sets

| Constant | Glyphs | Cells |
| --- | --- | --- |
| `HUD_PATH_FONT_CORE_GLYPHS` (41) | `A` to `Z`, `0` to `9`, `/ - % + :` | 0 to 40 |
| `HUD_PATH_FONT_ALL_GLYPHS` (44) | the core set and `! . ~` | 0 to 43 |

The extra glyphs come after the core set, so the first 41 cells of both atlases are identical and a game that switches from the core set to all glyphs keeps every cell where it was. A game that already ships the core set and does not want `!`, `.` or `~` to start drawing where its text has them keeps `HUD_PATH_FONT_CORE_GLYPHS`. Other counts build that many glyphs in the same order, and counts above 44 are clamped to 44. A new glyph would only ever be added at the end, in a minor release.

## Metrics

| Constant | Value | Meaning |
| --- | --- | --- |
| `HUD_PATH_FONT_ATLAS_W`, `HUD_PATH_FONT_ATLAS_H` | 256, 128 | The atlas in texels. |
| `HUD_PATH_FONT_CELL_W`, `HUD_PATH_FONT_CELL_H` | 20, 24 | One glyph's cell in texels. |
| `HUD_PATH_FONT_COLS`, `HUD_PATH_FONT_ROWS` | 12, 5 | Cells across and down. |
| `HUD_PATH_FONT_GRID_W`, `HUD_PATH_FONT_GRID_H` | 6, 8 | Path coordinates run from 0 to these. The rasterizer puts two texels in a grid unit and insets the grid 1.5 units into the cell. |
| `HUD_PATH_FONT_ALPHA_MAX` | 15 | The largest coverage `put_ink` receives. |
| `HUD_PATH_FONT_ADVANCE` | 8.7 | Screen units from one glyph's left edge to the next at size 1, for every byte, drawn or not. |
| `HUD_PATH_FONT_QUAD_W`, `HUD_PATH_FONT_QUAD_H` | 10, 12 | The on-screen quad for a cell at size 1: half its texels. |
| `HUD_PATH_FONT_STROKE_ORIGIN` | 1.45 | Where grid point 0 sits, in screen units at size 1, for a stroking renderer. |
| `HUD_PATH_FONT_STROKE_WIDTH` | 1.5 | The stroke width at size 1 that matches the atlas's weight. |

## Paths

A path is pairs of digits, x then y, and each point joins the one before it with a straight stroke. A `/` lifts the pen, so the next point starts a new stroke. `A` is `080220406268/0464`: from (0, 8) up to (0, 2), across the top through (2, 0) and (4, 0), down through (6, 2) to (6, 8), then a separate crossbar from (0, 4) to (6, 4).

## Checking an earlier copy

Before this package, the font was carried as a `hud_font.c` and `hud_font.h` in each project, with `hud_font_build(slot, put_ink)` and `hud_font_path(code)`. To prove a project's copy and the package build the same atlas, point CMake at a folder holding that copy's `hud_font.c`, `hud_font.h` and any header they include:

```sh
cmake -S packages/hud-path-font -B build-parity -G Ninja "-DHUD_PATH_FONT_PARITY_DIRS=/path/to/copy-a;/path/to/copy-b"
cmake --build build-parity
ctest --test-dir build-parity --output-on-failure
```

Each folder gets a test that builds the copy and the package into one program with the same compiler flags, works out the glyph set from the copy's slot table, and passes only when every ink call matches in order, the atlases match texel for texel, the slot tables match, and both give the same path for every character code. On GCC and Clang it runs three times: with the compiler's default floating-point contraction, with contraction off and with contraction forced on, so a fused multiply-add on one console cannot hide a difference. The digests in `tests/hud_path_font_test.c` were recorded from the two copies this package replaced, and both match under all three settings.

## Build and test

```sh
./build-and-test.sh            # macOS and Linux
.\build-and-test.ps1           # Windows
```

Both need CMake, Ninja and a C compiler.
