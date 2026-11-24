"""Made-up indexed images for the tests: no game art, no console files."""

from __future__ import annotations


def gradient(width: int, height: int) -> bytes:
    """Index = (x + 2 * y) % 16, so every tile differs and every nibble value appears."""
    return bytes((x + 2 * y) % 16 for y in range(height) for x in range(width))


def blank(width: int, height: int, value: int = 0) -> bytes:
    return bytes([value]) * (width * height)


def two_tiles_mirrored() -> tuple[bytes, int, int]:
    """A 16x8 image: tile 0 is an asymmetric glyph, tile 1 is its left-right mirror."""
    tile = [
        [1, 0, 0, 0, 0, 0, 0, 0],
        [1, 1, 0, 0, 0, 0, 0, 0],
        [1, 1, 1, 0, 0, 0, 0, 0],
        [1, 1, 1, 1, 0, 0, 0, 0],
        [2, 0, 0, 0, 0, 0, 0, 0],
        [2, 2, 0, 0, 0, 0, 0, 0],
        [2, 2, 2, 0, 0, 0, 0, 0],
        [3, 3, 3, 3, 3, 3, 3, 3],
    ]
    rows = [row + row[::-1] for row in tile]
    return bytes(v for row in rows for v in row), 16, 8


class FakePillowImage:
    """The few attributes of a Pillow ``P`` image that from_pillow reads."""

    def __init__(self, indices: bytes, width: int, height: int, palette: list[int], mode: str = "P") -> None:
        self.mode = mode
        self.size = (width, height)
        self._indices = indices
        self._palette = palette

    def tobytes(self) -> bytes:
        return self._indices

    def getpalette(self) -> list[int] | None:
        return self._palette
