# uppercut-iso-normalize

Makes a PlayStation 2 data disc image byte-reproducible. A disc made with macOS `hdiutil` carries the time it was made in every directory record, in its Rock Ridge timestamps and in its volume descriptor, and its file names have no ISO 9660 version suffix, which the console's disc reader looks for. This package checks that the image is the disc it should be (a `SYSTEM.CNF` that boots one ELF, and that ELF) and rewrites those fields, so the same inputs always give the same bytes. Standard library only.

```sh
pip install uppercut-iso-normalize
```

## Command line

```sh
uppercut-iso-normalize game.iso build/game.elf --elf-name GAME.ELF
python -m uppercut_iso_normalize game.iso build/game.elf --elf-name GAME.ELF --vmode PAL
```

The image is rewritten in place. The exit code is 0 on success, 1 when the image is not the disc expected (the message says why, and the file is not touched) and 2 when a file cannot be read or written.

## Python

```python
from uppercut_iso_normalize import IsoNormalizeError, normalize_disc, ps2_system_cnf

image = bytearray(open("game.iso", "rb").read())
elf = open("build/game.elf", "rb").read()
try:
    normalize_disc(image, elf, elf_name="GAME.ELF")
except IsoNormalizeError as error:
    raise SystemExit(f"game.iso: {error}")
open("game.iso", "wb").write(image)
```

| Function | What it does |
| --- | --- |
| `normalize_disc(image, elf, *, elf_name, system_cnf=None)` | Checks and rewrites a `bytearray` in place. `system_cnf` is the list of lines `SYSTEM.CNF` must hold; the default is `ps2_system_cnf(elf_name)` |
| `normalize_file(iso, elf, *, elf_name, system_cnf=None)` | The same on files: reads both, writes the image back only when it passed |
| `ps2_system_cnf(elf_name, *, ver="1.00", vmode="NTSC")` | The lines `BOOT2 = cdrom0:\NAME;1`, `VER = ...` and `VMODE = ...` |
| `IsoNormalizeError` | A `ValueError`. Raised, with the image unchanged, when anything does not match |

## What it checks

- The image is a whole number of 2048-byte sectors with a primary volume descriptor at sector 16 and its terminator at sector 17 (what `hdiutil` writes).
- The root directory is one sector, and every file record is well formed: its extent lies inside the image and after the directory, it is not a directory or hidden, and its name appears once.
- The disc holds exactly `SYSTEM.CNF` and the named ELF. `SYSTEM.CNF` has exactly the expected lines (compared line by line, so a trailing newline style does not matter).
- The ELF you pass in is a 32-bit little-endian MIPS executable, and the ELF on the disc is byte for byte the same file.
- The Rock Ridge entries of each record are readable (`TF` timestamps have the length their flags give; a continuation entry is refused).

## What it rewrites

- Each directory record's recording date becomes 2000-01-01 00:00:00, zone 0, and so does each Rock Ridge `TF` timestamp, in its short or its long form.
- The volume descriptor's root record date and its creation and modification dates become the same.
- Each file's name gets the suffix `;1`. The two extra characters keep the padding parity of the record, and the directory is written back padded with zeros to its sector.

Nothing else moves: file data, extents and sizes are not touched. An image that was already normalized is refused (its names no longer match), so running it twice is safe.

## Notes

- The fixed date is the same one for every disc this package writes. Pass nothing to change it: the point is that two builds of the same files agree.
- Only this layout is handled: one sector of root directory, no subdirectories, no Joliet or El Torito volumes.
- The tests build synthetic images in Python (an invented 52-byte "ELF" header and a made-up payload). No game data, BIOS or console material is used.

## Working on it

From the repository root:

```sh
uv sync --all-packages && uv run pytest packages/uppercut-iso-normalize
```

## License

MIT
