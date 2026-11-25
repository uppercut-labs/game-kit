# uppercut-retro-pixels

The pixel arithmetic that console ports keep retyping, as pure functions on bytes, integers and tuples: 16-bit colour formats, red/blue swap, the PSP block swizzle and its inverse, run-length coding, palette ramps and padded palettes, and a glyph-atlas packer that writes the metrics as a C header. Standard library only; no Pillow, no numpy. Turning a PNG into RGBA bytes (and running ImageMagick or a texture converter) stays with the caller.

```sh
pip install uppercut-retro-pixels
```

Requires Python 3.10 or newer.

## Example

```python
from uppercut_retro_pixels import ARGB4444, RGB565, encode, pack, swap_rb, swizzle

# One pixel, or a whole RGBA image, into a console format
value = encode(RGB565, 255, 128, 0)                       # 0xFC00
data = pack(ARGB4444, rgba_bytes, endian="little")        # two bytes a pixel

# PSP wants BGR565, laid out in 16-byte by 8-row blocks
psp = swizzle(pack(RGB565, rgba_bytes), row_bytes=64 * 2, height=64)
bgr = swap_rb(RGB565, value)                              # the same pixel as BGR565
```

## Formats

`RGB565`, `BGR565`, `ARGB4444`, `ABGR4444`, `ARGB1555`, `ABGR1555`, `RGBA5551` and `BGR555` are `PixelFormat` values; `FORMATS` maps names to them. A format lists where each channel sits in the 16-bit value, so other layouts are a one-line `PixelFormat(name, {"r": (shift, bits), ...})`.

| Function | What it does |
| --- | --- |
| `encode(fmt, r, g, b, a=255, *, rounding="truncate")` | Packs 8-bit channels into a 16-bit integer. `"truncate"` is a right shift; `"nearest"` picks the closest level. A 1-bit alpha is on from 128 in both modes. |
| `encode_unit(fmt, r, g, b, a=1.0)` | The same for channels given as floats in 0..1: clamped, scaled, rounded with Python's `round` and masked. |
| `decode(fmt, value, *, expand="replicate")` | Unpacks to `(r, g, b, a)`. `"replicate"` repeats the high bits into the low ones (`v << 3 \| v >> 2` for 5 bits); `"scale"` is `v * 255 // max`. A format with no alpha gives 255. |
| `swap_rb(fmt, value)` | Exchanges the red and blue fields. `swap_rb(RGB565, v)` is `v` read as `BGR565`, and `swap_rb(ARGB4444, v)` is `v` read as `ABGR4444`. |
| `pack(fmt, rgba, *, endian="little", rounding="truncate")` | RGBA bytes to packed pixels, two bytes each. |
| `unpack(fmt, data, *, endian="little", expand="replicate")` | Packed pixels to RGBA bytes. |

## Block layouts

| Function | What it does |
| --- | --- |
| `swizzle(data, row_bytes, height)` | Reorders a linear image into 16-byte by 8-row blocks, the PSP texture layout. `row_bytes` is a multiple of 16 and `height` a multiple of 8. Works for any pixel size, 4-bit included, because it counts bytes. |
| `unswizzle(data, row_bytes, height)` | The inverse. |
| `swizzle_items(items, width, height, *, block_width=8, block_height=8)` | The same reordering for a list of whole pixels. The default block fits 16-bit pixels; use `block_width=16` for 8-bit ones. |
| `pack_nibbles(indices)` / `unpack_nibbles(data)` | 4-bit indices, two to a byte, the first in the low nibble. |
| `morton_index(x, y, bits)` | The interleaved (twiddled) index Dreamcast textures use. |

A byte at `(x, y)` lands at `((y // 8) * (row_bytes // 16) + x // 16) * 128 + (y % 8) * 16 + x % 16`.

## Run-length coding

A record is a one-byte count and a value of one or two bytes.

| Function | What it does |
| --- | --- |
| `rle_runs(values, *, max_run=255)` | `(count, value)` runs. |
| `rle_pack(values, *, value_bytes=1, endian="big", max_run=255)` | Bytes of `count, value` records. |
| `rle_unpack(data, *, value_bytes=1, endian="big", limit=None)` | The values back. A record cut off at the end is dropped; `limit` stops at that many values, so a dump with spare runs past the end of the image is trimmed. |
| `rle_unpack_hex(text, *, value_bytes=2, limit=None)` | The same for records logged as hexadecimal text (two digits of count, then the value), whitespace ignored. |
| `flatten(runs)` | Expands `(count, value)` runs. |

## Palettes

| Function | What it does |
| --- | --- |
| `ramp(stops, steps)` | `steps` colours evenly spaced through a list of `(r, g, b)` stops, ends included, rounded to the nearest integer. |
| `pad_palette(colors, size=256, fill=None)` | Exactly `size` colours; missing ones come from `fill(index)`, or black. |
| `palette_bytes(colors, size=256)` | Flat `r, g, b` bytes, zero-padded to `size` entries. |

## Glyph atlases

| Name | What it does |
| --- | --- |
| `Glyph(char, width, height, y_offset, advance, pixels)` | One glyph bitmap, RGBA bytes. A blank glyph has width and height 0. |
| `pack_glyphs(sets, atlas_width, atlas_height, *, margin=1)` | Shelf-packs the glyphs of one or more sets into an RGBA atlas and returns it with a `GlyphRect` (x, y, width, height, y_offset, advance) for each glyph. The sets share a cursor. Raises `ValueError` when the atlas is full. |
| `metrics_header(tables, *, atlas_width, atlas_height, first_char, last_char, ...)` | The C header: atlas size and character range as `#define`s, a glyph struct and one `static const` rectangle table per `(name, rects)`. Names, guard, integer types and include line are parameters. |
| `outlined_cell_atlas(rows_for, *, first=32, last=126, columns=16, cell_width=8, cell_height=12, ...)` | A texture of equal cells holding 1-bit glyph rows with a one-pixel outline, as pixel values the caller has already packed (`ink`, `outline`, `blank`). |

Fonts render from outside: the caller draws glyphs with ImageMagick, Pillow or a bitmap table and hands the RGBA bytes to `pack_glyphs`; a 5x7 table for `outlined_cell_atlas` comes from `bitmap-font` or `uppercut-pixel-art-sheets`.

## Errors

Every function raises `ValueError` for input it cannot use: a channel outside 0..255, an odd byte count, a size that is not a whole number of blocks, a palette over its size, an atlas that is full.

## Licence

MIT
