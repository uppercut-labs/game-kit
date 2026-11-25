"""Palettes: BGR555 words, GIMP .gpl text and the JSON entries the games keep next to their art."""

from __future__ import annotations

from collections.abc import Sequence
from typing import Literal

from uppercut_retro_pixels import BGR555, encode

Rgb = tuple[int, int, int]
Rounding = Literal["truncate", "nearest"]


def bgr555(color: Rgb, *, rounding: Rounding = "nearest") -> int:
    """One colour as the GBA stores it: blue in bits 10-14, green in 5-9, red in 0-4.

    ``"nearest"`` picks the closest of the 32 levels (255 stays full scale) and is the default. ``"truncate"`` is a right
    shift by three. The two differ for many values (200 gives 25 truncated and 24 rounded), so a game that mixes them shows different
    hardware colours for the same palette.
    """
    r, g, b = color
    return encode(BGR555, r, g, b, rounding=rounding)


def palette_words(
    colors: Sequence[Rgb],
    count: int = 16,
    *,
    rounding: Rounding = "nearest",
    transparent_first: bool = False,
) -> list[int]:
    """``count`` BGR555 words, black-padded when ``colors`` is shorter. ``transparent_first`` writes word 0 as 0 whatever
    colour 0 is (a sprite or background's transparent entry)."""
    if len(colors) > count:
        raise ValueError(f"{len(colors)} colours do not fit in {count} palette entries")
    words = [bgr555(c, rounding=rounding) for c in colors] + [0] * (count - len(colors))
    if transparent_first and words:
        words[0] = 0
    return words


def gpl_text(
    name: str,
    colors: Sequence[Rgb],
    *,
    columns: int = 16,
    comment: str = "#",
    labels: Sequence[str] | None = None,
) -> str:
    """A GIMP palette file. ``comment`` is the whole line after the header, such as ``# exact GBA BGR555 colors``;
    ``labels`` names each colour (``colour_00``... when left out)."""
    if labels is not None and len(labels) != len(colors):
        raise ValueError("labels and colours differ in length")
    lines = ["GIMP Palette", f"Name: {name}", f"Columns: {columns}", comment]
    for index, (r, g, b) in enumerate(colors):
        label = labels[index] if labels is not None else f"colour_{index:02d}"
        lines.append(f"{r:3d} {g:3d} {b:3d}\t{label}")
    return "\n".join(lines) + "\n"


def palette_entries(
    colors: Sequence[Rgb],
    *,
    rgb: Literal["list", "hex"] = "list",
    rounding: Rounding = "nearest",
) -> list[dict[str, object]]:
    """``{"index", "rgb", "bgr555"}`` for each colour, ready for ``json.dumps``. ``rgb`` is ``[r, g, b]`` or
    ``"#RRGGBB"``; ``bgr555`` is the word as ``"0x%04X"``."""
    if rgb not in ("list", "hex"):
        raise ValueError(f"unknown rgb style {rgb!r}")
    out: list[dict[str, object]] = []
    for index, color in enumerate(colors):
        shown: object = list(color) if rgb == "list" else "#%02X%02X%02X" % tuple(color)
        out.append({"index": index, "rgb": shown, "bgr555": f"0x{bgr555(color, rounding=rounding):04X}"})
    return out
