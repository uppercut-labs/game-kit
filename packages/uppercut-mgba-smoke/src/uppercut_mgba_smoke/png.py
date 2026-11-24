"""Check the screenshots mGBA writes: complete, un-interlaced, 8-bit RGB PNGs of the right size that are not blank."""

from __future__ import annotations

import hashlib
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Optional, Sequence

from .errors import SmokeError

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
GBA_WIDTH = 240
GBA_HEIGHT = 160


@dataclass(frozen=True)
class PngImage:
    width: int
    height: int
    channels: int
    pixels: bytes
    """The decoded pixels, ``channels`` bytes each, row by row."""
    sha256: str
    """The SHA-256 of the PNG file itself."""


def _paeth(left: int, above: int, upper_left: int) -> int:
    estimate = left + above - upper_left
    left_distance = abs(estimate - left)
    above_distance = abs(estimate - above)
    upper_left_distance = abs(estimate - upper_left)
    if left_distance <= above_distance and left_distance <= upper_left_distance:
        return left
    if above_distance <= upper_left_distance:
        return above
    return upper_left


def _decode_scanlines(compressed: bytes, width: int, height: int, channels: int) -> bytes:
    decompressor = zlib.decompressobj()
    stride = width * channels
    expected_size = height * (stride + 1)
    try:
        raw = decompressor.decompress(compressed, expected_size + 1)
    except zlib.error as error:
        raise SmokeError(f"PNG IDAT data is invalid: {error}") from error
    if len(raw) > expected_size or decompressor.unconsumed_tail:
        raise SmokeError("PNG scanline data exceeds the expected image size")
    try:
        raw += decompressor.flush(expected_size + 1 - len(raw))
    except zlib.error as error:
        raise SmokeError(f"PNG IDAT data is invalid: {error}") from error
    if not decompressor.eof or decompressor.unused_data:
        raise SmokeError("PNG IDAT zlib stream is incomplete or has trailing data")
    if len(raw) != expected_size:
        raise SmokeError(f"PNG scanline data must be {expected_size} bytes, found {len(raw)}")

    pixels = bytearray(height * stride)
    source = 0
    for row in range(height):
        filter_type = raw[source]
        source += 1
        if filter_type > 4:
            raise SmokeError(f"PNG uses invalid row filter {filter_type}")
        row_offset = row * stride
        previous = row_offset - stride
        for column in range(stride):
            encoded = raw[source + column]
            left = pixels[row_offset + column - channels] if column >= channels else 0
            above = pixels[previous + column] if row > 0 else 0
            upper_left = pixels[previous + column - channels] if row > 0 and column >= channels else 0
            if filter_type == 0:
                value = encoded
            elif filter_type == 1:
                value = encoded + left
            elif filter_type == 2:
                value = encoded + above
            elif filter_type == 3:
                value = encoded + ((left + above) // 2)
            else:
                value = encoded + _paeth(left, above, upper_left)
            pixels[row_offset + column] = value & 0xFF
        source += stride
    return bytes(pixels)


def validate_png(path: Path, *, width: int = GBA_WIDTH, height: int = GBA_HEIGHT) -> PngImage:
    """Read and fully decode an un-interlaced 8-bit RGB PNG of ``width`` by ``height`` (240 by 160 by default).

    Every chunk's length and CRC is checked, so a truncated or forged file is refused.
    """
    try:
        data = Path(path).read_bytes()
    except OSError as error:
        raise SmokeError(f"cannot read screenshot {path}: {error}") from error
    name = Path(path).name
    if not data.startswith(PNG_SIGNATURE):
        raise SmokeError(f"{name} is not a PNG file")

    offset = len(PNG_SIGNATURE)
    header: Optional[tuple[int, int, int, int, int, int, int]] = None
    idat: list[bytes] = []
    saw_end = False
    index = 0
    while offset < len(data):
        if len(data) - offset < 12:
            raise SmokeError(f"{name} has a truncated PNG chunk")
        size = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4 : offset + 8]
        end = offset + 12 + size
        if end > len(data):
            raise SmokeError(f"{name} has a truncated PNG chunk payload")
        payload = data[offset + 8 : offset + 8 + size]
        if (zlib.crc32(kind + payload) & 0xFFFFFFFF) != struct.unpack_from(">I", data, offset + 8 + size)[0]:
            raise SmokeError(f"{name} PNG {kind.decode('ascii', 'replace')} CRC mismatch")
        if index == 0 and kind != b"IHDR":
            raise SmokeError(f"{name} PNG first chunk is not IHDR")
        if kind == b"IHDR":
            if header is not None or size != 13:
                raise SmokeError(f"{name} has an invalid PNG IHDR chunk")
            header = struct.unpack(">IIBBBBB", payload)
        elif kind == b"IDAT":
            if header is None or saw_end:
                raise SmokeError(f"{name} has a misplaced PNG IDAT chunk")
            idat.append(payload)
        elif kind == b"IEND":
            if size != 0:
                raise SmokeError(f"{name} has an invalid PNG IEND chunk")
            saw_end = True
            offset = end
            if offset != len(data):
                raise SmokeError(f"{name} has data after PNG IEND")
            break
        offset = end
        index += 1

    if header is None or not idat or not saw_end:
        raise SmokeError(f"{name} is missing required PNG chunks")
    found_width, found_height, depth, color_type, compression, filtering, interlace = header
    if (found_width, found_height) != (width, height):
        raise SmokeError(f"{name} dimensions must be {width}x{height}, found {found_width}x{found_height}")
    if depth != 8 or color_type != 2:
        raise SmokeError(f"{name} must be an 8-bit RGB PNG, found depth {depth}, color type {color_type}")
    if compression != 0 or filtering != 0 or interlace != 0:
        raise SmokeError(f"{name} uses unsupported PNG encoding options")
    return PngImage(
        width=width,
        height=height,
        channels=3,
        pixels=_decode_scanlines(b"".join(idat), width, height, 3),
        sha256=hashlib.sha256(data).hexdigest(),
    )


def validate_screenshot_set(
    directory: Path,
    expected_names: Sequence[str],
    *,
    reviewed_sha256: Optional[Mapping[str, str]] = None,
    width: int = GBA_WIDTH,
    height: int = GBA_HEIGHT,
    min_colors: int = 4,
) -> Mapping[str, PngImage]:
    """Check a folder of screenshots.

    The folder must hold exactly the ``expected_names`` and no other PNG. Each must decode, must hold at least
    ``min_colors`` distinct pixel values (not blank), and must differ in pixels from every other. ``reviewed_sha256``
    maps a name to the SHA-256 of its file; those must match exactly (a screenshot a person reviewed and pinned).
    """
    directory = Path(directory)
    actual = tuple(sorted(path.name for path in directory.glob("*.png")))
    if actual != tuple(sorted(expected_names)):
        missing = sorted(set(expected_names) - set(actual))
        extra = sorted(set(actual) - set(expected_names))
        raise SmokeError(f"screenshot set mismatch; missing={missing}, extra={extra}")

    images: dict[str, PngImage] = {}
    seen: dict[str, str] = {}
    for name in expected_names:
        image = validate_png(directory / name, width=width, height=height)
        step = image.channels
        colors = {image.pixels[i : i + step] for i in range(0, len(image.pixels), step)}
        if len(colors) < min_colors:
            raise SmokeError(f"{name} appears blank or nearly blank")
        digest = hashlib.sha256(image.pixels).hexdigest()
        if digest in seen:
            raise SmokeError(f"duplicate screenshots: {seen[digest]} and {name}")
        seen[digest] = name
        images[name] = image

    for name, expected in (reviewed_sha256 or {}).items():
        if name not in images:
            raise SmokeError(f"reviewed SHA-256 references missing screenshot {name}")
        if images[name].sha256 != expected:
            raise SmokeError(f"{name} does not match reviewed SHA-256: expected {expected}, found {images[name].sha256}")
    return images
