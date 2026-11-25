"""Read and write binary PPM (P6) frames, the picture a recompiled port's frame dump writes, and cut them up."""

from __future__ import annotations

import hashlib
from dataclasses import dataclass
from pathlib import Path
from typing import Union

from .errors import PpmError

PathLike = Union[str, Path]


@dataclass(frozen=True)
class Ppm:
    """An 8-bit RGB picture: `pixels` is `width * height * 3` bytes, row by row."""

    width: int
    height: int
    pixels: bytes

    def __post_init__(self) -> None:
        if self.width < 1 or self.height < 1:
            raise PpmError("a picture needs a width and a height of at least 1")
        if len(self.pixels) != self.width * self.height * 3:
            raise PpmError(f"{self.width}x{self.height} needs {self.width * self.height * 3} bytes, got {len(self.pixels)}")

    def pixel(self, x: int, y: int) -> bytes:
        o = (y * self.width + x) * 3
        return self.pixels[o : o + 3]


def parse_ppm(data: bytes, name: str = "PPM") -> Ppm:
    """Parse the bytes of a P6 file: the magic, width, height and maxval separated by white space (a `#` comment runs to
    the end of its line), one white-space byte, then the pixels. A maxval over 255 (16-bit samples) is refused, and so is a
    file with fewer pixel bytes than the header promises; bytes after the pixels are ignored."""
    if data[:2] != b"P6":
        raise PpmError(f"{name} is not a binary PPM (it does not start with P6)")
    fields: list[bytes] = []
    i = 2
    while len(fields) < 3:
        while i < len(data) and data[i : i + 1].isspace():
            i += 1
        if data[i : i + 1] == b"#":
            while i < len(data) and data[i : i + 1] != b"\n":
                i += 1
            continue
        j = i
        while j < len(data) and not data[j : j + 1].isspace():
            j += 1
        if j == i:
            raise PpmError(f"{name} ends inside its header")
        fields.append(data[i:j])
        i = j
    try:
        width, height, maxval = (int(f) for f in fields)
    except ValueError:
        raise PpmError(f"{name} has a header that is not numbers: {b' '.join(fields)!r}") from None
    if width < 1 or height < 1:
        raise PpmError(f"{name} has no pixels ({width}x{height})")
    if not 0 < maxval < 256:
        raise PpmError(f"{name} has maxval {maxval}: only 8-bit samples are handled")
    i += 1  # the one white-space byte after maxval
    need = width * height * 3
    if len(data) - i < need:
        raise PpmError(f"{name} is cut short: {width}x{height} needs {need} bytes of pixels, found {max(0, len(data) - i)}")
    return Ppm(width, height, bytes(data[i : i + need]))


def read_ppm(path: PathLike) -> Ppm:
    return parse_ppm(Path(path).read_bytes(), Path(path).name)


def encode_ppm(picture: Ppm) -> bytes:
    return b"P6\n%d %d\n255\n" % (picture.width, picture.height) + picture.pixels


def write_ppm(path: PathLike, picture: Ppm) -> None:
    Path(path).write_bytes(encode_ppm(picture))


def crop(picture: Ppm, x0: int, y0: int, x1: int, y1: int) -> Ppm:
    """The pixels from `(x0, y0)` up to but not including `(x1, y1)`."""
    if not (0 <= x0 < x1 <= picture.width and 0 <= y0 < y1 <= picture.height):
        raise PpmError(f"crop {x0},{y0} to {x1},{y1} does not fit a {picture.width}x{picture.height} picture")
    stride = picture.width * 3
    rows = [picture.pixels[y * stride + x0 * 3 : y * stride + x1 * 3] for y in range(y0, y1)]
    return Ppm(x1 - x0, y1 - y0, b"".join(rows))


def enlarge(picture: Ppm, factor: int) -> Ppm:
    """Each pixel repeated `factor` times across and down (nearest neighbour, whole numbers only)."""
    if factor < 1:
        raise PpmError("an enlargement factor is a whole number from 1")
    if factor == 1:
        return picture
    stride = picture.width * 3
    out = bytearray()
    for y in range(picture.height):
        row = picture.pixels[y * stride : (y + 1) * stride]
        wide = b"".join(row[x : x + 3] * factor for x in range(0, stride, 3))
        out += wide * factor
    return Ppm(picture.width * factor, picture.height * factor, bytes(out))


def md5_of(path: PathLike) -> str:
    """The MD5 of a file's bytes. It tells two frame dumps apart; it is not a security check."""
    return hashlib.md5(Path(path).read_bytes()).hexdigest()  # noqa: S324 - a fingerprint for comparison only
