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



GX_TILE = 4
GX_RGBA8_TILE_BYTES = 64


def _check_rgba(width: int, height: int, length: int, what: str) -> None:
    if width <= 0 or height <= 0:
        raise ValueError(f"size {width}x{height} is not positive")
    if length != width * height * 4:
        raise ValueError(f"{what} is {length} bytes, expected {width * height * 4}")


def _check_gx(width: int, height: int, length: int, what: str) -> None:
    _check_rgba(width, height, length, what)
    if width % GX_TILE or height % GX_TILE:
        raise ValueError(f"size {width}x{height} is not a multiple of {GX_TILE}")


def pack_gx_rgba8(rgba: bytes, width: int, height: int) -> bytes:
    """Lay out RGBA bytes as GameCube and Wii ``GX_TF_RGBA8`` texture data.

    The image is cut into 4x4 tiles, left to right, then top to bottom. Each tile is 64 bytes: 32 bytes of
    alpha-red pairs for its 16 pixels in row order, then 32 bytes of green-blue pairs. Width and height are
    multiples of 4. Mipmap levels are packed one after another, each with its own call.
    """
    _check_gx(width, height, len(rgba), "data")
    out = bytearray(len(rgba))
    offset = 0
    for tile_y in range(0, height, GX_TILE):
        for tile_x in range(0, width, GX_TILE):
            ar = offset
            gb = offset + 32
            for y in range(tile_y, tile_y + GX_TILE):
                row = (y * width + tile_x) * 4
                for x in range(GX_TILE):
                    p = row + x * 4
                    out[ar] = rgba[p + 3]
                    out[ar + 1] = rgba[p]
                    out[gb] = rgba[p + 1]
                    out[gb + 1] = rgba[p + 2]
                    ar += 2
                    gb += 2
            offset += GX_RGBA8_TILE_BYTES
    return bytes(out)


def unpack_gx_rgba8(data: bytes, width: int, height: int) -> bytes:
    """Undo :func:`pack_gx_rgba8`: ``GX_TF_RGBA8`` tiles back to linear RGBA bytes."""
    _check_gx(width, height, len(data), "data")
    out = bytearray(len(data))
    offset = 0
    for tile_y in range(0, height, GX_TILE):
        for tile_x in range(0, width, GX_TILE):
            ar = offset
            gb = offset + 32
            for y in range(tile_y, tile_y + GX_TILE):
                row = (y * width + tile_x) * 4
                for x in range(GX_TILE):
                    p = row + x * 4
                    out[p + 3] = data[ar]
                    out[p] = data[ar + 1]
                    out[p + 1] = data[gb]
                    out[p + 2] = data[gb + 1]
                    ar += 2
                    gb += 2
            offset += GX_RGBA8_TILE_BYTES
    return bytes(out)


def _power_of_two(value: int) -> bool:
    return value > 0 and not value & (value - 1)


def _nv2a_offset(value: int, length: int, other: int, vertical: bool) -> int:
    """The pixel offset one coordinate contributes along its axis.

    Bits of x and y alternate, x in the lower position, while both axes have bits left; once the shorter
    axis runs out, the longer one takes the remaining bits in order.
    """
    offset = shift = 0
    bit = 1
    while bit < max(length, other):
        if vertical and bit < other:
            shift += 1
        if bit < length:
            offset |= bool(value & bit) << shift
            shift += 1
        if not vertical and bit < other:
            shift += 1
        bit <<= 1
    return offset


def _nv2a_axis(length: int, other: int, vertical: bool) -> list[int]:
    return [_nv2a_offset(value, length, other, vertical) for value in range(length)]


def _check_nv2a(width: int, height: int, bytes_per_pixel: int, length: int) -> None:
    if not (_power_of_two(width) and _power_of_two(height)):
        raise ValueError(f"size {width}x{height} is not a power of two on both sides")
    if bytes_per_pixel <= 0:
        raise ValueError(f"bytes_per_pixel {bytes_per_pixel} is not positive")
    if length != width * height * bytes_per_pixel:
        raise ValueError(f"data is {length} bytes, expected {width * height * bytes_per_pixel}")


def nv2a_index(x: int, y: int, width: int, height: int) -> int:
    """The pixel position of ``(x, y)`` in an original Xbox (NV2A) swizzled texture of ``width`` by ``height``."""
    if not (_power_of_two(width) and _power_of_two(height)):
        raise ValueError(f"size {width}x{height} is not a power of two on both sides")
    if not (0 <= x < width and 0 <= y < height):
        raise ValueError(f"({x}, {y}) is outside {width}x{height}")
    return _nv2a_offset(x, width, height, False) | _nv2a_offset(y, height, width, True)


def nv2a_swizzle(data: bytes, width: int, height: int, *, bytes_per_pixel: int = 4) -> bytes:
    """Reorder a linear image into the original Xbox (NV2A) swizzled texture layout.

    The pixel at ``(x, y)`` goes to :func:`nv2a_index`: the bits of x and y interleaved, x lowest, with the
    longer side's extra bits on top. Width and height are powers of two; they need not be equal. The bytes
    of each pixel are copied as they are, so convert to the texture's channel order (``BGRA`` for
    ``A8R8G8B8``) first.
    """
    _check_nv2a(width, height, bytes_per_pixel, len(data))
    xs = _nv2a_axis(width, height, False)
    ys = _nv2a_axis(height, width, True)
    out = bytearray(len(data))
    size = bytes_per_pixel
    start = 0
    for row in ys:
        for column in xs:
            offset = (row | column) * size
            out[offset : offset + size] = data[start : start + size]
            start += size
    return bytes(out)


def nv2a_unswizzle(data: bytes, width: int, height: int, *, bytes_per_pixel: int = 4) -> bytes:
    """Undo :func:`nv2a_swizzle`."""
    _check_nv2a(width, height, bytes_per_pixel, len(data))
    xs = _nv2a_axis(width, height, False)
    ys = _nv2a_axis(height, width, True)
    out = bytearray(len(data))
    size = bytes_per_pixel
    start = 0
    for row in ys:
        for column in xs:
            offset = (row | column) * size
            out[start : start + size] = data[offset : offset + size]
            start += size
    return bytes(out)
