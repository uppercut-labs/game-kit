# uppercut-gba-asset-export

Writes the files a Game Boy Advance project needs from indexed pixel art: BGR555 palette words, 4bpp tile data, the C files grit would write (`name.h` and `name.c`), a 4bpp BMP plus the JSON descriptor Butano reads, and GIMP `.gpl` palettes. It works on plain bytes (one palette index per pixel), so Pillow is optional: `from_pillow` reads a Pillow `P` image without importing Pillow.

The colour arithmetic is `uppercut-retro-pixels` (its `BGR555` format and `pack_nibbles`); this package adds the GBA file formats around it and does not repeat the pixel-format code.

```sh
pip install uppercut-gba-asset-export
```

Requires Python 3.10 or newer and `uppercut-retro-pixels`.

## Example

```python
from pathlib import Path

from PIL import Image
from uppercut_gba_asset_export import (
    butano_json, from_pillow, grit_files, gpl_text, indexed_bmp_4bpp, palette_words, tile_words,
)

image = from_pillow(Image.open("city.png"))                       # indices, size and the image's own palette
words = tile_words(image.indices, image.width, image.height)      # 4bpp tile words, 8 to a tile
palette = palette_words(image.palette, 16, transparent_first=True)

files = grit_files("city", words, palette, width=image.width, height=image.height)
Path("city.h").write_text(files.header)
Path("city.c").write_text(files.source)

Path("city.bmp").write_bytes(indexed_bmp_4bpp(image.indices, image.width, image.height, image.palette))
Path("city.json").write_text(butano_json("regular_bg_tiles"))
Path("city.gpl").write_text(gpl_text("City", image.palette, comment="# exact GBA BGR555 colors"))
```

## Palettes

| Function | What it does |
| --- | --- |
| `bgr555(color, *, rounding="nearest")` | One `(r, g, b)` as the 15-bit word the console reads. `"nearest"` picks the closest of 32 levels (255 stays full scale, the default); `"truncate"` is `value >> 3`. |
| `palette_words(colors, count=16, *, rounding="nearest", transparent_first=False)` | `count` words, black-padded. `transparent_first` writes word 0 as 0. |
| `gpl_text(name, colors, *, columns=16, comment="#", labels=None)` | A GIMP palette file: header, one `r g b<TAB>label` line a colour. |
| `palette_entries(colors, *, rgb="list", rounding="nearest")` | `{"index", "rgb", "bgr555"}` for each colour, for a metadata JSON file; `rgb="hex"` gives `"#RRGGBB"`. |

**Rounding matters.** The two ways of reducing 8 bits to 5 give different colours for many values (200 is 24 rounded and 25 truncated). A project that mixes them in different tools gets different hardware colours from the same palette. Pick one per game; `"nearest"` is the default because it keeps full scale exact and spreads the error evenly.

## Tiles

| Function | What it does |
| --- | --- |
| `tile_bytes(indices, width, height)` | The 4bpp tile data: tiles left to right then top to bottom, 8 rows of 4 bytes each, the left pixel in the low nibble. The image must be a whole number of 8x8 tiles and use indices 0 to 15. |
| `tile_words(indices, width, height)` | The same as little-endian 32-bit words, for a C `unsigned int` array. |
| `tile_reuse(indices, width, height)` | A `TileReuse(total, unique, unique_with_flips)`: how many tiles there are, how many differ, and how many differ when a horizontal or vertical flip counts as the same tile. |

## Files

| Function | What it does |
| --- | --- |
| `grit_files(name, words, palette, *, width, height, ...)` | `GritFiles(header, source)`: the text of `name.h` and `name.c` with `NameTiles`, `NamePal`, `NameTilesLen` and `NamePalLen`. The default templates follow grit's output (a `BLOCK` banner, arrays aligned to four bytes). A project with its own file header or include guard passes `header_template` and `source_template` (`str.format` strings, fields listed in the docstring) and sets `indent`, `separator`, `palette_per_line` and `trailing_comma` (and `palette_indent` and `palette_trailing_comma` when the palette's differ) to match the layout of the files it already has. |
| `c_name(text)` | A C identifier from a file name. |
| `indexed_bmp_4bpp(indices, width, height, palette)` | The bytes of an uncompressed 4bpp BMP that keeps its indices: a 16-entry palette, rows bottom to top, rows padded to four bytes. Butano and grit read it. `width` must be even. |
| `butano_descriptor(kind, frame_size=None)`, `butano_json(...)` | The Butano `.json` next to the BMP: `"sprite"` (with the frame width and height) or `"regular_bg_tiles"`; the dict, or the file text (two-space indent, final newline). |
| `from_pillow(image, *, colors=16)` | An `IndexedImage(indices, width, height, palette)` from a Pillow `P` image. |

## Not in this version

- Palette tables as C arrays (several palettes in one `[n][16]` array), flip-aware tile-map deduplication and map files, 8bpp output, compression. Projects keep their own for now.
- Rendering or quantising art: the input is already indexed.
- Running grit or Butano: this writes the same kind of file without them.

## Errors

Every function raises `ValueError` for input it cannot use: an image that is not a whole number of tiles, an index above 15, a palette of more than 16 colours, a channel outside 0..255.

## Licence

MIT
