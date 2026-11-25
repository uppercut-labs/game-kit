"""Reading an indexed image. Pillow is not a dependency: an object that looks like a Pillow ``P`` image works."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class IndexedImage:
    indices: bytes  # one palette index per pixel, row by row
    width: int
    height: int
    palette: tuple[tuple[int, int, int], ...]  # the image's own palette, as many colours as it holds


def from_pillow(image: Any, *, colors: int = 16) -> IndexedImage:
    """An indexed (mode ``P``) Pillow image as an ``IndexedImage``. ``colors`` is how many palette entries to keep.

    Only ``mode``, ``size``, ``tobytes()`` and ``getpalette()`` are used, so Pillow itself is not imported."""
    if image.mode != "P":
        raise ValueError(f"expected an indexed (P) image, got mode {image.mode!r}")
    width, height = image.size
    flat = image.getpalette() or []
    palette = tuple((flat[i], flat[i + 1], flat[i + 2]) for i in range(0, min(len(flat), colors * 3) - 2, 3))
    return IndexedImage(bytes(image.tobytes()), width, height, palette)
