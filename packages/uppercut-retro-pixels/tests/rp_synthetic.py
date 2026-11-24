"""Synthetic test images: no game data, just arithmetic."""

from __future__ import annotations


def gradient_rgba(width: int, height: int) -> bytes:
    """RGBA bytes whose channels sweep the 0..255 range in different directions, with alpha stepping 0, 85, 170, 255."""
    out = bytearray()
    for y in range(height):
        for x in range(width):
            out += bytes(
                (
                    (x * 255) // max(width - 1, 1),
                    (y * 255) // max(height - 1, 1),
                    ((x + y) * 255) // max(width + height - 2, 1),
                    (85 * ((x + 2 * y) % 4)),
                )
            )
    return bytes(out)


def all_levels_rgba() -> bytes:
    """Every 8-bit level 0..255 once per channel (r = v, g = 255 - v, b = (v * 7) % 256, a = v)."""
    out = bytearray()
    for v in range(256):
        out += bytes((v, 255 - v, (v * 7) % 256, v))
    return bytes(out)
