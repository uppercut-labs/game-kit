"""An uncompressed 4bpp BMP that keeps its palette indices, the form Butano and grit read."""

from __future__ import annotations

import struct
from collections.abc import Sequence


def indexed_bmp_4bpp(indices: bytes, width: int, height: int, palette: Sequence[tuple[int, int, int]]) -> bytes:
    """The bytes of a 4bpp BMP: a 16-entry palette (colours past ``palette`` are black), rows bottom to top, two pixels a
    byte with the left one in the high nibble, each row padded to four bytes. ``width`` must be even."""
    if width <= 0 or height <= 0 or width % 2:
        raise ValueError("a 4bpp BMP needs an even, positive width and a positive height")
    if len(indices) != width * height:
        raise ValueError(f"{len(indices)} indices for a {width}x{height} image")
    if max(indices, default=0) > 15:
        raise ValueError("palette index above 15 in a 4bpp image")
    if len(palette) > 16:
        raise ValueError(f"a 4bpp BMP palette has at most 16 colours, not {len(palette)}")
    row_bytes = width // 2
    stride = (row_bytes + 3) & ~3
    pixel_size = stride * height
    table = b"".join(struct.pack("<BBBB", b, g, r, 0) for r, g, b in palette) + bytes(4 * (16 - len(palette)))
    offset = 14 + 40 + len(table)
    file_header = b"BM" + struct.pack("<IHHI", offset + pixel_size, 0, 0, offset)
    info_header = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 4, 0, pixel_size, 2835, 2835, 16, 16)
    padding = bytes(stride - row_bytes)
    rows = []
    for y in range(height - 1, -1, -1):
        line = indices[y * width : (y + 1) * width]
        rows.append(bytes((line[x] << 4) | line[x + 1] for x in range(0, width, 2)) + padding)
    return file_header + info_header + table + b"".join(rows)
