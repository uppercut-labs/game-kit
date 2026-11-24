"""Bitmap font, pattern stamping and offset drawing helpers."""

from __future__ import annotations

from typing import Mapping, Sequence

from PIL import ImageDraw

from ._font5x7 import FONT_5X7 as _FONT_5X7

Color = int | tuple[int, ...]
Pattern = Sequence[str]

# 5 wide, 7 tall glyphs. "1" is an inked pixel. Lowercase draws as uppercase. The table is generated from the single
# font source in game-kit's bitmap-font package (see _font5x7.py), so the C and Python sides cannot drift apart.
FONT_5X7: Mapping[str, Pattern] = _FONT_5X7


def stamp_pattern(
    draw: ImageDraw.ImageDraw,
    pattern: Pattern,
    x: int,
    y: int,
    color: Color,
    *,
    scale: int = 1,
    ink: str = "1",
) -> None:
    """Draw every ``ink`` character of ``pattern`` as a ``scale`` by ``scale`` block at (x, y).

    Works on indexed and RGB images; ``color`` is a palette index or a tuple.
    """
    if scale < 1:
        raise ValueError("scale must be at least 1")
    for row, line in enumerate(pattern):
        for col, ch in enumerate(line):
            if ch == ink:
                px, py = x + col * scale, y + row * scale
                draw.rectangle((px, py, px + scale - 1, py + scale - 1), fill=color)


def text_width(text: str, *, font: Mapping[str, Pattern] = FONT_5X7, spacing: int = 6, scale: int = 1) -> int:
    """Pixels used by ``text``, not counting the gap after the last glyph."""
    if not text:
        return 0
    glyph_w = max(len(p[0]) for p in font.values())
    return ((len(text) - 1) * spacing + glyph_w) * scale


def draw_text(
    draw: ImageDraw.ImageDraw,
    text: str,
    x: int,
    y: int,
    color: Color,
    *,
    font: Mapping[str, Pattern] = FONT_5X7,
    spacing: int = 6,
    scale: int = 1,
) -> int:
    """Draw ``text`` with a bitmap font and return the advance in pixels.

    ``spacing`` is the distance between glyph origins before scaling. Text is
    upper-cased, and characters missing from the font draw as ``?``.
    """
    fallback = font.get("?")
    cursor = x
    for ch in text.upper():
        pattern = font.get(ch, fallback)
        if pattern is not None:
            stamp_pattern(draw, pattern, cursor, y, color, scale=scale)
        cursor += spacing * scale
    return cursor - x


def cut_corner_box(
    draw: ImageDraw.ImageDraw,
    box: tuple[int, int, int, int],
    fill: Color,
    outline: Color | None = None,
) -> None:
    """Draw a rectangle with its four corner pixels cut off. ``box`` is inclusive (x0, y0, x1, y1)."""
    x0, y0, x1, y1 = box
    if x1 - x0 < 2 or y1 - y0 < 2:
        raise ValueError("box must be at least 3 pixels in each direction")
    poly = [(x0 + 1, y0), (x1 - 1, y0), (x1, y0 + 1), (x1, y1 - 1),
            (x1 - 1, y1), (x0 + 1, y1), (x0, y1 - 1), (x0, y0 + 1)]
    draw.polygon(poly, fill=fill)
    if outline is not None:
        draw.line([*poly, poly[0]], fill=outline, width=1)


class Origin:
    """Drawing calls that add an offset ``(ox, oy)``, so a tile can be drawn in its own coordinates."""

    def __init__(self, draw: ImageDraw.ImageDraw, ox: int = 0, oy: int = 0) -> None:
        self.draw, self.ox, self.oy = draw, ox, oy

    def _pt(self, p: tuple[int, int]) -> tuple[int, int]:
        return (p[0] + self.ox, p[1] + self.oy)

    def _box(self, b: Sequence[int]) -> tuple[int, int, int, int]:
        return (b[0] + self.ox, b[1] + self.oy, b[2] + self.ox, b[3] + self.oy)

    def point(self, p: tuple[int, int], fill: Color) -> None:
        self.draw.point(self._pt(p), fill=fill)

    def line(self, points: Sequence[tuple[int, int]], fill: Color, width: int = 1) -> None:
        self.draw.line([self._pt(p) for p in points], fill=fill, width=width)

    def rect(self, box: Sequence[int], fill: Color | None = None, outline: Color | None = None) -> None:
        self.draw.rectangle(self._box(box), fill=fill, outline=outline)

    def ellipse(self, box: Sequence[int], fill: Color | None = None, outline: Color | None = None) -> None:
        self.draw.ellipse(self._box(box), fill=fill, outline=outline)

    def polygon(self, points: Sequence[tuple[int, int]], fill: Color | None = None, outline: Color | None = None) -> None:
        self.draw.polygon([self._pt(p) for p in points], fill=fill, outline=outline)

    def pattern(self, pattern: Pattern, x: int, y: int, color: Color, *, scale: int = 1) -> None:
        stamp_pattern(self.draw, pattern, x + self.ox, y + self.oy, color, scale=scale)

    def text(self, text: str, x: int, y: int, color: Color, *, spacing: int = 6, scale: int = 1) -> int:
        return draw_text(self.draw, text, x + self.ox, y + self.oy, color, spacing=spacing, scale=scale)
