# uppercut-iso-normalize

Builds a PlayStation 2 data disc from an ELF and makes disc images byte-reproducible. A disc made with pycdlib or macOS `hdiutil` carries the time it was made in every directory record and in its volume descriptor (an `hdiutil` disc also in its Rock Ridge timestamps), and `hdiutil` leaves the ISO 9660 version suffix off its file names, which the console's disc reader looks for. This package checks that an image is the disc it should be (a `SYSTEM.CNF` that boots one ELF, and that ELF) and rewrites those fields, so the same inputs always give the same bytes. Building a disc uses [pycdlib](https://pypi.org/project/pycdlib/); normalizing one uses only the standard library.

```sh
pip install uppercut-iso-normalize
```

## Command line

Build a disc from an ELF:

```sh
uppercut-iso-normalize build build/game.elf game.iso --elf-name GAME.ELF
uppercut-iso-normalize build build/demo.elf demo.iso --elf-name DEMO.ELF --volume-id DEMO_DISC --vmode PAL
```

The disc holds `SYSTEM.CNF;1` and `GAME.ELF;1` in the root of an ISO 9660 interchange level 1 volume with the system identifier `PLAYSTATION`. `SYSTEM.CNF` holds `BOOT2 = cdrom0:\GAME.ELF;1`, `VER = 1.00` and `VMODE = NTSC`, each line ending in CR LF. The volume identifier defaults to the ELF name without its extension. The image is read back and checked, normalized, and read back again, then written through a temporary file next to the output, so a failure leaves an earlier image as it was.

Normalize a disc that another tool made, in place:

```sh
uppercut-iso-normalize game.iso build/game.elf --elf-name GAME.ELF                # an hdiutil image
uppercut-iso-normalize game.iso build/game.elf --elf-name GAME.ELF --versioned    # a pycdlib image, names ending in ;1
python -m uppercut_iso_normalize game.iso build/game.elf --elf-name GAME.ELF --vmode PAL
```

Both forms take `--ver` and `--vmode` for the `SYSTEM.CNF` lines. The exit code is 0 on success, 1 when the image is not the disc expected or cannot be built as asked (the message says why, and the file is not touched) and 2 when a file cannot be read or written.

## Python

```python
from uppercut_iso_normalize import build_file

build_file("build/game.elf", "game.iso", elf_name="GAME.ELF")
```

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
| `build_disc(elf, *, elf_name, volume_id=None, system_cnf=None)` | The bytes of a normalized disc that boots `elf`, as the command line builds it |
| `build_file(elf, iso, *, elf_name, volume_id=None, system_cnf=None)` | The same on files: reads the ELF and writes the image through a temporary file next to `iso` |
| `normalize_disc(image, elf, *, elf_name, system_cnf=None, versioned=False)` | Checks and rewrites a `bytearray` in place. `system_cnf` is the list of lines `SYSTEM.CNF` must hold; the default is `ps2_system_cnf(elf_name)`. With `versioned`, the names must already end in `;1` and are kept |
| `normalize_file(iso, elf, *, elf_name, system_cnf=None, versioned=False)` | The same on files: reads both, writes the image back only when it passed |
| `ps2_system_cnf(elf_name, *, ver="1.00", vmode="NTSC")` | The lines `BOOT2 = cdrom0:\NAME;1`, `VER = ...` and `VMODE = ...` |
| `system_cnf_bytes(lines)` | The `SYSTEM.CNF` file for those lines, each ending in CR LF |
| `IsoNormalizeError` | A `ValueError`. Raised, with the image unchanged, when anything does not match |

## What it checks

- The image is a whole number of 2048-byte sectors with a primary volume descriptor at sector 16 and its terminator at sector 17 (what `hdiutil` and pycdlib write).
- The root directory is one sector, and every file record is well formed: its extent lies inside the image and after the directory, it is not a directory or hidden, and its name appears once.
- The disc holds exactly `SYSTEM.CNF` and the named ELF (with `;1` when `versioned`). `SYSTEM.CNF` has exactly the expected lines (compared line by line, so a trailing newline style does not matter).
- The ELF you pass in is a 32-bit little-endian MIPS executable, and the ELF on the disc is byte for byte the same file.
- The Rock Ridge entries of each record are readable (`TF` timestamps have the length their flags give; a continuation entry is refused).
- A built disc is also read back with pycdlib before and after it is normalized, and both files must come back as written.

## What it rewrites

- Each directory record's recording date becomes 2000-01-01 00:00:00, zone 0, and so does each Rock Ridge `TF` timestamp, in its short or its long form.
- The volume descriptor's root record date and its creation and modification dates become the same.
- Each file's name gets the suffix `;1`. The two extra characters keep the padding parity of the record, and the directory is written back padded with zeros to its sector.
- With `versioned` (a pycdlib image), the names already end in `;1` and stay as they are, and the volume's effective date, which pycdlib also sets to the time of the build, becomes the same fixed date when it is set.

Nothing else moves: file data, extents and sizes are not touched. An `hdiutil` image that was already normalized is refused (its names no longer match), so running it twice is safe. A `versioned` run on an image that was already normalized changes nothing.

## Notes

- The fixed date is the same one for every disc this package writes. Pass nothing to change it: the point is that two builds of the same files agree.
- Only this layout is handled: one sector of root directory, no subdirectories, no Joliet or El Torito volumes. pycdlib's interchange level 1 image of two files has it.
- The bytes of a built disc depend on the pycdlib version that lays it out. Pin pycdlib in a build that must match an earlier image byte for byte.
- The tests build synthetic images in Python and with pycdlib (an invented 52-byte "ELF" header and a made-up payload). No game data, BIOS or console material is used.

## Working on it

From the repository root:

```sh
uv sync --all-packages && uv run pytest packages/uppercut-iso-normalize
```

## License

MIT
