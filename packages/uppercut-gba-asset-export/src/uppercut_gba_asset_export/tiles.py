"""4bpp tile data from indexed pixels (a flat ``bytes`` of palette indices, row by row)."""

from __future__ import annotations

from dataclasses import dataclass

from uppercut_retro_pixels import pack_nibbles


def _check(indices: bytes, width: int, height: int) -> None:
    if width <= 0 or height <= 0 or width % 8 or height % 8:
        raise ValueError(f"a {width}x{height} image is not a whole number of 8x8 tiles")
    if len(indices) != width * height:
        raise ValueError(f"{len(indices)} indices for a {width}x{height} image")
    if max(indices, default=0) > 15:
        raise ValueError("palette index above 15 in a 4bpp image")


def tile_bytes(indices: bytes, width: int, height: int) -> bytes:
    """The 4bpp tile data: tiles left to right, then top to bottom; inside a tile, 8 rows of 4 bytes with the left pixel in
    the low nibble. That is the layout the console's tile memory (and grit) uses."""
    _check(indices, width, height)
    out = bytearray()
    for tile_y in range(0, height, 8):
        for tile_x in range(0, width, 8):
            for row in range(8):
                start = (tile_y + row) * width + tile_x
                out += pack_nibbles(indices[start : start + 8])
    return bytes(out)


def tile_words(indices: bytes, width: int, height: int) -> list[int]:
    """``tile_bytes`` as little-endian 32-bit words, eight to a tile, as a C ``unsigned int`` array holds them."""
    data = tile_bytes(indices, width, height)
    return [int.from_bytes(data[i : i + 4], "little") for i in range(0, len(data), 4)]


@dataclass(frozen=True)
class TileReuse:
    """Tile counts of an image: all of them, the distinct ones, and the distinct ones when a horizontal or vertical flip
    (or both) of a tile counts as the same tile, which is what a flip-aware tile map can save."""

    total: int
    unique: int
    unique_with_flips: int


def tile_reuse(indices: bytes, width: int, height: int) -> TileReuse:
    _check(indices, width, height)
    exact: set[bytes] = set()
    canonical: set[bytes] = set()
    total = 0
    for tile_y in range(0, height, 8):
        for tile_x in range(0, width, 8):
            rows = [indices[(tile_y + r) * width + tile_x : (tile_y + r) * width + tile_x + 8] for r in range(8)]
            plain = b"".join(rows)
            h = b"".join(row[::-1] for row in rows)
            v = b"".join(reversed(rows))
            hv = b"".join(row[::-1] for row in reversed(rows))
            exact.add(plain)
            canonical.add(min(plain, h, v, hv))
            total += 1
    return TileReuse(total, len(exact), len(canonical))
