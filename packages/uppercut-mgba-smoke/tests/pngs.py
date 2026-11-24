"""Synthetic 240x160 PNGs for the tests."""

from __future__ import annotations

import struct
import zlib


def chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def make_png(
    *,
    seed: int = 0,
    solid: bool = False,
    width: int = 240,
    height: int = 160,
    color_type: int = 2,
    depth: int = 8,
    interlace: int = 0,
    filter_type: int = 0,
    trailing: bytes = b"",
    drop_end: bool = False,
) -> bytes:
    """An un-interlaced RGB PNG whose pixels depend on ``seed`` (so two seeds differ). ``filter_type`` 1 to 4 encode
    each row with that PNG filter, to exercise the decoder."""
    channels = {2: 3, 6: 4}.get(color_type, 3)
    stride = width * channels
    pixels = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            if solid:
                row += bytes((7, 11, 13, 255)[:channels])
            else:
                row += bytes(((x + seed) & 0xFF, (y * 3 + seed) & 0xFF, (x + y + seed * 5) & 0xFF, 255)[:channels])
        pixels.append(bytes(row))
    encoded = bytearray()
    for y, row in enumerate(pixels):
        encoded.append(filter_type)
        previous = pixels[y - 1] if y else bytes(stride)
        for i in range(stride):
            left = row[i - channels] if i >= channels else 0
            above = previous[i]
            upper_left = previous[i - channels] if i >= channels else 0
            if filter_type == 0:
                predictor = 0
            elif filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            else:
                estimate = left + above - upper_left
                distances = [(abs(estimate - left), 0, left), (abs(estimate - above), 1, above), (abs(estimate - upper_left), 2, upper_left)]
                predictor = min(distances)[2]
            encoded.append((row[i] - predictor) & 0xFF)
    header = struct.pack(">IIBBBBB", width, height, depth, color_type, 0, 0, interlace)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(encoded)))
    if not drop_end:
        data += chunk(b"IEND", b"")
    return data + trailing
