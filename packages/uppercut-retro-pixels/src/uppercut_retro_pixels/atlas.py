"""Glyph atlases: pack glyph bitmaps into one texture, write the metrics as a C header, and cut outlined cells."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable, Sequence


@dataclass(frozen=True)
class Glyph:
    """One glyph bitmap: ``pixels`` is RGBA bytes, row by row, ``width * height * 4`` long (empty for a blank glyph).

    ``y_offset`` is where the bitmap sits against the font's top line; ``advance`` is the pen step in pixels.
    """

    char: int
    width: int
    height: int
    y_offset: int
    advance: int
    pixels: bytes = b""

    def __post_init__(self) -> None:
        if len(self.pixels) != self.width * self.height * 4 and not (self.width == 0 or self.height == 0):
            raise ValueError(f"glyph {self.char}: {len(self.pixels)} bytes for {self.width}x{self.height} RGBA")


@dataclass(frozen=True)
class GlyphRect:
    """Where a glyph went in the atlas, plus its metrics. A blank glyph has ``x = y = width = height = 0``."""

    x: int
    y: int
    width: int
    height: int
    y_offset: int
    advance: int


def pack_glyphs(
    sets: Sequence[Sequence[Glyph]], atlas_width: int, atlas_height: int, *, margin: int = 1
) -> tuple[bytearray, list[list[GlyphRect]]]:
    """Shelf-pack every glyph of every set into one RGBA atlas.

    Glyphs go left to right in the order given, starting ``margin`` pixels in. A glyph that would reach the right edge
    starts a new shelf below the tallest glyph of the last one, ``margin`` pixels lower. The sets share one cursor, so
    the second set continues where the first ended. Blank glyphs take no room. Returns the atlas (``atlas_width *
    atlas_height * 4`` bytes) and, for each set, one :class:`GlyphRect` per glyph. Raises ``ValueError`` when the atlas
    is too small.
    """
    atlas = bytearray(atlas_width * atlas_height * 4)
    cur_x = cur_y = margin
    shelf_h = 0
    result: list[list[GlyphRect]] = []
    for glyphs in sets:
        rects: list[GlyphRect] = []
        for g in glyphs:
            if g.width == 0 or g.height == 0:
                rects.append(GlyphRect(0, 0, 0, 0, g.y_offset, g.advance))
                continue
            if cur_x + g.width + margin >= atlas_width:
                cur_x = margin
                cur_y += shelf_h + margin
                shelf_h = 0
            if cur_y + g.height + margin >= atlas_height:
                raise ValueError(f"glyph atlas overflowed {atlas_width}x{atlas_height}")
            for row in range(g.height):
                dst = ((cur_y + row) * atlas_width + cur_x) * 4
                src = row * g.width * 4
                atlas[dst : dst + g.width * 4] = g.pixels[src : src + g.width * 4]
            rects.append(GlyphRect(cur_x, cur_y, g.width, g.height, g.y_offset, g.advance))
            cur_x += g.width + margin
            shelf_h = max(shelf_h, g.height)
        result.append(rects)
    return atlas, result


def metrics_header(
    tables: Sequence[tuple[str, Sequence[GlyphRect]]],
    *,
    atlas_width: int,
    atlas_height: int,
    first_char: int,
    last_char: int,
    extra_defines: Sequence[tuple[str, int]] = (),
    guard: str = "FONT_METRICS_H",
    define_prefix: str = "FONT_",
    table_prefix: str = "FONT_GLYPHS_",
    struct_name: str = "FontGlyph",
    types: tuple[str, str, str] = ("u16", "u8", "i8"),
    include: str | None = None,
    banner: str = "Generated. Do not edit.",
) -> str:
    """The C header that goes with an atlas: atlas size, character range, and a ``static const`` table of glyph
    rectangles for each ``(name, rects)`` in ``tables``.

    The struct is ``{ x, y, w, h, y_off, advance }`` using the type names in ``types`` (for x and y, for w, h and
    advance, and for y_off). Defines are ``<define_prefix>ATLAS_W``, ``ATLAS_H``, ``FIRST_CHAR``, ``LAST_CHAR`` and
    ``GLYPH_COUNT``, then ``extra_defines``; tables are named ``<table_prefix><name>``. Numbers are written with a
    ``u`` suffix, and the offset without one. Ends with a newline.
    """
    t16, t8, ti8 = types
    count = last_char - first_char + 1
    d = define_prefix
    lines = [f"/* {banner} */", f"#ifndef {guard}", f"#define {guard}", ""]
    if include:
        lines += [f'#include "{include}"', ""]
    lines += [
        f"#define {d}ATLAS_W {atlas_width}u",
        f"#define {d}ATLAS_H {atlas_height}u",
        f"#define {d}FIRST_CHAR {first_char}u",
        f"#define {d}LAST_CHAR {last_char}u",
        f"#define {d}GLYPH_COUNT {count}u",
    ]
    lines += [f"#define {d}{name} {value}u" for name, value in extra_defines]
    lines += [
        "",
        f"typedef struct {struct_name} {{",
        f"    {t16} x;",
        f"    {t16} y;",
        f"    {t8} w;",
        f"    {t8} h;",
        f"    {ti8} y_off;",
        f"    {t8} advance;",
        f"}} {struct_name};",
        "",
    ]
    for name, rects in tables:
        if len(rects) != count:
            raise ValueError(f"table {name} has {len(rects)} glyphs, expected {count}")
        lines.append(f"static const {struct_name} {table_prefix}{name}[{d}GLYPH_COUNT] = {{")
        for r in rects:
            lines.append(f"    {{ {r.x}u, {r.y}u, {r.width}u, {r.height}u, {r.y_offset}, {r.advance}u }},")
        lines += ["};", ""]
    lines += ["#endif", ""]
    return "\n".join(lines)


def outlined_cell_atlas(
    rows_for: Callable[[int], Sequence[str]],
    *,
    first: int = 32,
    last: int = 126,
    columns: int = 16,
    cell_width: int = 8,
    cell_height: int = 12,
    glyph_x: int = 1,
    glyph_y: int = 2,
    atlas_width: int = 128,
    atlas_height: int = 128,
    ink: int = 0xFFFF,
    outline: int = 0x0000,
    blank: int = 0,
) -> list[int]:
    """A texture of equal cells, one per character code, with each glyph outlined by one pixel all round.

    ``rows_for(code)`` gives the glyph as rows of ``"0"`` and ``"1"`` characters (``"1"`` is ink). Cell ``code - first``
    sits at column ``(code - first) % columns`` and row ``(code - first) // columns``; the glyph is placed ``glyph_x``,
    ``glyph_y`` pixels into it, and the outline stays inside the cell. Pixels hold ``ink`` where the glyph is,
    ``outline`` in the 8-neighbourhood of ink and ``blank`` elsewhere: pass values already packed in the target
    format. Returns ``atlas_width * atlas_height`` pixels, row by row.
    """
    if (last - first) // columns >= atlas_height // cell_height or columns * cell_width > atlas_width:
        raise ValueError("the cells do not fit the atlas")
    pixels = [blank] * (atlas_width * atlas_height)
    for code in range(first, last + 1):
        idx = code - first
        gx, gy = (idx % columns) * cell_width, (idx // columns) * cell_height
        rows = rows_for(code)
        fg = [[False] * cell_width for _ in range(cell_height)]
        for r, line in enumerate(rows):
            for c, ch in enumerate(line):
                ty, tx = r + glyph_y, c + glyph_x
                if ch == "1" and 0 <= ty < cell_height and 0 <= tx < cell_width:
                    fg[ty][tx] = True
        for ry in range(cell_height):
            for cx in range(cell_width):
                if fg[ry][cx]:
                    value = ink
                else:
                    near = any(
                        0 <= ry + dy < cell_height and 0 <= cx + dx < cell_width and fg[ry + dy][cx + dx]
                        for dy in (-1, 0, 1)
                        for dx in (-1, 0, 1)
                    )
                    value = outline if near else blank
                pixels[(gy + ry) * atlas_width + gx + cx] = value
    return pixels
