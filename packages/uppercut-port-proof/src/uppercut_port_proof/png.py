"""Write PNG files with the standard library: zlib-compressed, or stored blocks that match the C package minimal-png."""

from __future__ import annotations

import struct
import zlib
from pathlib import Path
from typing import Union

from .errors import ProofError
from .ppm import Ppm, PathLike, read_ppm

STORED_MAX = 65535
SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _chunk(kind: bytes, body: bytes) -> bytes:
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def _stored(raw: bytes) -> bytes:
    out = bytearray(b"\x78\x01")
    pos = 0
    while True:
        n = min(STORED_MAX, len(raw) - pos)
        final = pos + n >= len(raw)
        out += bytes([1 if final else 0]) + struct.pack("<HH", n, n ^ 0xFFFF) + raw[pos : pos + n]
        pos += n
        if final:
            break
    return bytes(out) + struct.pack(">I", zlib.adler32(raw) & 0xFFFFFFFF)


def encode_png(width: int, height: int, pixels: bytes, *, channels: int = 3, compress: bool = True, level: int = 6) -> bytes:
    """A PNG of 8-bit RGB (`channels=3`) or RGBA (`channels=4`) pixels, rows packed.

    `compress=True` uses zlib at `level`. `compress=False` writes stored (uncompressed) deflate blocks, byte for byte
    what `mpng_encode` in the C package minimal-png writes for the same image, so a Python tool and a C port can be
    compared with `cmp`."""
    if channels not in (3, 4):
        raise ProofError("channels is 3 (RGB) or 4 (RGBA)")
    if not 1 <= width <= 0x7FFFFFFF or not 1 <= height <= 0x7FFFFFFF:
        raise ProofError(f"a PNG is {width}x{height}: both sides must be from 1 to 2147483647")
    stride = width * channels
    if len(pixels) != stride * height:
        raise ProofError(f"{width}x{height} with {channels} channels needs {stride * height} bytes, got {len(pixels)}")
    raw = b"".join(b"\x00" + pixels[y * stride : (y + 1) * stride] for y in range(height))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2 if channels == 3 else 6, 0, 0, 0)
    z = zlib.compress(raw, level) if compress else _stored(raw)
    return SIGNATURE + _chunk(b"IHDR", ihdr) + _chunk(b"IDAT", z) + _chunk(b"IEND", b"")


def write_png(path: PathLike, width: int, height: int, pixels: bytes, *, channels: int = 3, compress: bool = True, level: int = 6) -> None:
    Path(path).write_bytes(encode_png(width, height, pixels, channels=channels, compress=compress, level=level))


def picture_to_png(picture: Ppm, *, compress: bool = True, level: int = 6) -> bytes:
    return encode_png(picture.width, picture.height, picture.pixels, compress=compress, level=level)


def ppm_to_png(source: Union[PathLike, Ppm], target: PathLike, *, compress: bool = True, level: int = 6) -> Ppm:
    """Read a PPM file (or take a picture), write it as a PNG, and return the picture."""
    picture = source if isinstance(source, Ppm) else read_ppm(source)
    Path(target).write_bytes(picture_to_png(picture, compress=compress, level=level))
    return picture
