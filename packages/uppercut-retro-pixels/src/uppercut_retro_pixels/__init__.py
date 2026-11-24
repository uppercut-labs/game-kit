"""Retro console pixel formats as pure functions on bytes and tuples."""

from .atlas import Glyph, GlyphRect, metrics_header, outlined_cell_atlas, pack_glyphs
from .blocks import morton_index, pack_nibbles, swizzle, swizzle_items, unpack_nibbles, unswizzle
from .formats import (
    ABGR1555,
    ABGR4444,
    ARGB1555,
    ARGB4444,
    BGR555,
    BGR565,
    FORMATS,
    RGB565,
    RGBA5551,
    PixelFormat,
    decode,
    encode,
    encode_unit,
    pack,
    swap_rb,
    unpack,
)
from .palette import pad_palette, palette_bytes, ramp
from .rle import flatten, rle_pack, rle_runs, rle_unpack, rle_unpack_hex

__all__ = [
    "ABGR1555",
    "ABGR4444",
    "ARGB1555",
    "ARGB4444",
    "BGR555",
    "BGR565",
    "FORMATS",
    "Glyph",
    "GlyphRect",
    "PixelFormat",
    "RGB565",
    "RGBA5551",
    "decode",
    "encode",
    "encode_unit",
    "flatten",
    "metrics_header",
    "morton_index",
    "outlined_cell_atlas",
    "pack",
    "pack_glyphs",
    "pack_nibbles",
    "pad_palette",
    "palette_bytes",
    "ramp",
    "rle_pack",
    "rle_runs",
    "rle_unpack",
    "rle_unpack_hex",
    "swap_rb",
    "swizzle",
    "swizzle_items",
    "unpack",
    "unpack_nibbles",
    "unswizzle",
]

__version__ = "0.1.0"
