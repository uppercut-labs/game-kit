"""Synthetic ROM images for the tests. The bytes come from a fixed xorshift stream, so they are the same on every run."""

from __future__ import annotations

import struct


def stream(seed: int, size: int) -> bytearray:
    """``size`` pseudo-random bytes from a xorshift32 stream seeded with ``seed``."""
    state = seed & 0xFFFFFFFF or 1
    words = []
    for _ in range((size + 3) // 4):
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= state >> 17
        state ^= (state << 5) & 0xFFFFFFFF
        words.append(state)
    return bytearray(struct.pack(f">{len(words)}I", *words)[:size])


N64_SIZE = 0x101000

# CRC1 and CRC2 of four images, as written by an independent implementation of the same algorithm: the JavaScript
# fixer this package was ported from, with its final combine corrected to XOR (see the README).
N64_GOLDEN = {
    "stream-1": (stream(1, N64_SIZE), 0x4126D6C9, 0x1BB5D2FE),
    "stream-7-longer": (stream(7, N64_SIZE + 0x2000), 0x77B0A014, 0x1DA127E4),
    "zeros": (bytearray(N64_SIZE), 0xF8CA4DDC, 0x303A4DDC),
    "ones": (bytearray(b"\xff" * N64_SIZE), 0xF8C24DDC, 0xC1544DDC),
}

MEGADRIVE_GOLDEN = {
    "stream-3-odd-length": (stream(3, 0x1203), 0xF936),
    "stream-4": (stream(4, 0x4000), 0x7BC4),
}
