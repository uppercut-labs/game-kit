"""16-bit pixel formats: encode, decode, R/B swap and bulk conversion."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

Rounding = Literal["truncate", "nearest"]
Expand = Literal["replicate", "scale"]
Endian = Literal["little", "big"]


@dataclass(frozen=True)
class PixelFormat:
    """A 16-bit packed format. ``fields`` maps a channel letter (r, g, b, a) to ``(shift, bits)``."""

    name: str
    fields: dict[str, tuple[int, int]]

    def has_alpha(self) -> bool:
        return "a" in self.fields

    def mask(self, channel: str) -> int:
        return (1 << self.fields[channel][1]) - 1


RGB565 = PixelFormat("RGB565", {"r": (11, 5), "g": (5, 6), "b": (0, 5)})
BGR565 = PixelFormat("BGR565", {"r": (0, 5), "g": (5, 6), "b": (11, 5)})
ARGB4444 = PixelFormat("ARGB4444", {"a": (12, 4), "r": (8, 4), "g": (4, 4), "b": (0, 4)})
ABGR4444 = PixelFormat("ABGR4444", {"a": (12, 4), "r": (0, 4), "g": (4, 4), "b": (8, 4)})
ARGB1555 = PixelFormat("ARGB1555", {"a": (15, 1), "r": (10, 5), "g": (5, 5), "b": (0, 5)})
ABGR1555 = PixelFormat("ABGR1555", {"a": (15, 1), "r": (0, 5), "g": (5, 5), "b": (10, 5)})
RGBA5551 = PixelFormat("RGBA5551", {"r": (11, 5), "g": (6, 5), "b": (1, 5), "a": (0, 1)})
BGR555 = PixelFormat("BGR555", {"r": (0, 5), "g": (5, 5), "b": (10, 5)})

FORMATS: dict[str, PixelFormat] = {
    f.name: f for f in (RGB565, BGR565, ARGB4444, ABGR4444, ARGB1555, ABGR1555, RGBA5551, BGR555)
}


def _reduce(value: int, bits: int, rounding: Rounding) -> int:
    if not 0 <= value <= 255:
        raise ValueError(f"channel value {value} is outside 0..255")
    top = (1 << bits) - 1
    if rounding == "truncate":
        return value >> (8 - bits)
    if rounding == "nearest":
        return (value * top * 2 + 255) // 510
    raise ValueError(f"unknown rounding {rounding!r}")


def encode(fmt: PixelFormat, r: int, g: int, b: int, a: int = 255, *, rounding: Rounding = "truncate") -> int:
    """Pack 8-bit channels into one 16-bit value.

    ``rounding="truncate"`` drops the low bits (a shift). ``"nearest"`` picks the closest level, so 255 stays full
    scale and 128 is the alpha threshold of a 1-bit channel. Alpha is ignored by formats without it.
    """
    values = {"r": r, "g": g, "b": b, "a": a}
    out = 0
    for channel, (shift, bits) in fmt.fields.items():
        out |= _reduce(values[channel], bits, rounding) << shift
    return out


def encode_unit(fmt: PixelFormat, r: float, g: float, b: float, a: float = 1.0) -> int:
    """Pack channels given as floats in 0..1. Each is clamped, scaled to the channel's range and rounded
    (Python's round, so exact halves go to the even level), then masked."""
    values = {"r": r, "g": g, "b": b, "a": a}
    out = 0
    for channel, (shift, bits) in fmt.fields.items():
        v = values[channel]
        v = 0.0 if v < 0.0 else (1.0 if v > 1.0 else v)
        top = (1 << bits) - 1
        out |= (int(round(v * top)) & top) << shift
    return out


def _expand(value: int, bits: int, mode: Expand) -> int:
    if bits == 8:
        return value
    if mode == "scale":
        return value * 255 // ((1 << bits) - 1)
    if mode == "replicate":
        out = value << (8 - bits)
        filled = bits
        while filled < 8:
            out |= out >> filled
            filled *= 2
        return out & 0xFF
    raise ValueError(f"unknown expand mode {mode!r}")


def decode(fmt: PixelFormat, value: int, *, expand: Expand = "replicate") -> tuple[int, int, int, int]:
    """Unpack a 16-bit value to ``(r, g, b, a)``, each 0..255. A format without alpha gives 255.

    ``"replicate"`` repeats the high bits into the low ones (5 bits: ``v << 3 | v >> 2``), which maps full scale to 255
    and 0 to 0. ``"scale"`` multiplies by 255 and divides by the channel maximum.
    """
    if not 0 <= value <= 0xFFFF:
        raise ValueError(f"pixel value {value} does not fit in 16 bits")
    out = {"a": 255}
    for channel, (shift, bits) in fmt.fields.items():
        out[channel] = _expand((value >> shift) & ((1 << bits) - 1), bits, expand)
    return out["r"], out["g"], out["b"], out["a"]


def swap_rb(fmt: PixelFormat, value: int) -> int:
    """Exchange the red and blue fields of a packed value, leaving the other bits alone.

    ``swap_rb(RGB565, v)`` is the same value read as BGR565, and ``swap_rb(ARGB4444, v)`` the same value as ABGR4444.
    Applying it twice returns ``v``. Red and blue must be the same width.
    """
    (rs, rb), (bs, bb) = fmt.fields["r"], fmt.fields["b"]
    if rb != bb:
        raise ValueError("red and blue differ in width")
    mask = (1 << rb) - 1
    r, b = (value >> rs) & mask, (value >> bs) & mask
    return (value & ~((mask << rs) | (mask << bs))) | (b << rs) | (r << bs)


def pack(fmt: PixelFormat, rgba: bytes, *, endian: Endian = "little", rounding: Rounding = "truncate") -> bytes:
    """Convert RGBA bytes (4 per pixel) to packed 16-bit pixels, two bytes each."""
    if len(rgba) % 4:
        raise ValueError("RGBA data length is not a multiple of 4")
    out = bytearray(len(rgba) // 2)
    for i in range(0, len(rgba), 4):
        v = encode(fmt, rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3], rounding=rounding)
        out[i // 2 : i // 2 + 2] = v.to_bytes(2, endian)
    return bytes(out)


def unpack(fmt: PixelFormat, data: bytes, *, endian: Endian = "little", expand: Expand = "replicate") -> bytes:
    """Convert packed 16-bit pixels to RGBA bytes."""
    if len(data) % 2:
        raise ValueError("pixel data length is odd")
    out = bytearray(len(data) * 2)
    for i in range(0, len(data), 2):
        r, g, b, a = decode(fmt, int.from_bytes(data[i : i + 2], endian), expand=expand)
        out[i * 2 : i * 2 + 4] = bytes((r, g, b, a))
    return bytes(out)
