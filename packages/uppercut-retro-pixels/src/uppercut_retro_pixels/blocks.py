"""Block swizzle, 4-bit packing and Morton order for texture memory layouts."""

from __future__ import annotations

from typing import Sequence, TypeVar

T = TypeVar("T")
BLOCK_BYTES_WIDE = 16
BLOCK_ROWS = 8


def _check(row_bytes: int, height: int, length: int) -> None:
    if row_bytes <= 0 or row_bytes % BLOCK_BYTES_WIDE:
        raise ValueError(f"row size {row_bytes} bytes is not a multiple of {BLOCK_BYTES_WIDE}")
    if height <= 0 or height % BLOCK_ROWS:
        raise ValueError(f"height {height} is not a multiple of {BLOCK_ROWS}")
    if length != row_bytes * height:
        raise ValueError(f"data is {length} bytes, expected {row_bytes * height}")


def swizzle(data: bytes, row_bytes: int, height: int) -> bytes:
    """Reorder a linear image into 16-byte by 8-row blocks, the PSP graphics engine's texture layout.

    The blocks are laid out left to right, then top to bottom; each block's rows are stored one after another.
    ``row_bytes`` is the width in bytes (``width * bits_per_pixel // 8``), a multiple of 16, and ``height`` a multiple
    of 8. The byte at ``(x, y)`` lands at
    ``((y // 8) * (row_bytes // 16) + x // 16) * 128 + (y % 8) * 16 + x % 16``.
    """
    _check(row_bytes, height, len(data))
    blocks_per_row = row_bytes // BLOCK_BYTES_WIDE
    out = bytearray(len(data))
    for y in range(height):
        base = ((y // BLOCK_ROWS) * blocks_per_row) * 128 + (y % BLOCK_ROWS) * BLOCK_BYTES_WIDE
        row = y * row_bytes
        for block in range(blocks_per_row):
            src = row + block * BLOCK_BYTES_WIDE
            dst = base + block * 128
            out[dst : dst + BLOCK_BYTES_WIDE] = data[src : src + BLOCK_BYTES_WIDE]
    return bytes(out)


def unswizzle(data: bytes, row_bytes: int, height: int) -> bytes:
    """Undo :func:`swizzle`."""
    _check(row_bytes, height, len(data))
    blocks_per_row = row_bytes // BLOCK_BYTES_WIDE
    out = bytearray(len(data))
    for y in range(height):
        base = ((y // BLOCK_ROWS) * blocks_per_row) * 128 + (y % BLOCK_ROWS) * BLOCK_BYTES_WIDE
        row = y * row_bytes
        for block in range(blocks_per_row):
            src = base + block * 128
            dst = row + block * BLOCK_BYTES_WIDE
            out[dst : dst + BLOCK_BYTES_WIDE] = data[src : src + BLOCK_BYTES_WIDE]
    return bytes(out)


def swizzle_items(items: Sequence[T], width: int, height: int, *, block_width: int = 8, block_height: int = 8) -> list[T]:
    """The same reordering for a list of whole pixels (any value), with the block measured in pixels.

    The default 8 by 8 block is the layout for 16-bit pixels (8 pixels of 2 bytes make a 16-byte row), so for
    those ``swizzle_items(pixels, w, h)`` equals :func:`swizzle` on their bytes. Use ``block_width=16`` for 8-bit
    pixels and ``4`` for 32-bit ones.
    """
    if width % block_width or height % block_height:
        raise ValueError("width and height must be whole numbers of blocks")
    if len(items) != width * height:
        raise ValueError(f"expected {width * height} pixels, got {len(items)}")
    out: list[T] = []
    for tile_y in range(0, height, block_height):
        for tile_x in range(0, width, block_width):
            for row in range(tile_y, tile_y + block_height):
                start = row * width + tile_x
                out.extend(items[start : start + block_width])
    return out


def pack_nibbles(indices: Sequence[int]) -> bytes:
    """Pack 4-bit indices two to a byte, the first index in the low nibble. The count must be even."""
    if len(indices) % 2:
        raise ValueError("an odd number of 4-bit indices")
    out = bytearray(len(indices) // 2)
    for i in range(0, len(indices), 2):
        lo, hi = indices[i], indices[i + 1]
        if not (0 <= lo < 16 and 0 <= hi < 16):
            raise ValueError("a 4-bit index is outside 0..15")
        out[i // 2] = lo | (hi << 4)
    return bytes(out)


def unpack_nibbles(data: bytes) -> list[int]:
    """Undo :func:`pack_nibbles`."""
    out: list[int] = []
    for byte in data:
        out.append(byte & 0xF)
        out.append(byte >> 4)
    return out


def morton_index(x: int, y: int, bits: int) -> int:
    """Interleave the low ``bits`` bits of ``x`` and ``y`` (y in the even positions, x in the odd ones): the
    twiddled order Dreamcast PowerVR textures are stored in."""
    if x < 0 or y < 0 or x >> bits or y >> bits:
        raise ValueError("coordinate does not fit in the bit count")
    return sum((((x >> b) & 1) << (2 * b + 1)) | (((y >> b) & 1) << (2 * b)) for b in range(bits))

