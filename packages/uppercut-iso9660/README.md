# uppercut-iso9660

Reads ISO 9660 disc images, never writes to them. It handles plain `.iso` files (2048-byte sectors) and raw CD dumps (`.bin` with 2352-byte sectors in Mode 1 or Mode 2, or 2336-byte sectors), finds out which by looking for the volume descriptor, then walks the directory tree, reads and extracts files with a SHA-256, and finds a PlayStation 2 disc's `SYSTEM.CNF` and boot ELF. Standard library only.

It reads; it does not change a disc. To make a macOS-built PS2 disc byte-reproducible, see `uppercut-iso-normalize`, which rewrites one. The two agree on what a PS2 disc is: `SYSTEM.CNF` with `BOOT2 = cdrom0:\NAME;1`, and `NAME` in the root.

```sh
pip install uppercut-iso9660
```

## Python

```python
from uppercut_iso9660 import IsoImage, IsoError, find_ps2_boot

with IsoImage("game.iso") as disc:                  # or a .bin dump, or a binary file object
    print(disc.volume_id, disc.layout.name)         # "GAME", "cooked-2048"
    for entry in disc.walk():                       # every file and directory, depth first
        print(entry.path, entry.size, entry.lba)
    data = disc.read_file("DATA/INTRO.BIN")         # any case, / or \, ;1 optional
    result = disc.extract("DATA/INTRO.BIN", "intro.bin")   # result.size, result.sha256
    boot = find_ps2_boot(disc, expected_serial="SLUS-21847")
    print(boot.serial, boot.elf_name, disc.sha256(boot.entry))
```

| `IsoImage` | |
| --- | --- |
| `IsoImage(path_or_file, strict=True)` | open; `layout`, `volume_id`, `system_id`, `volume_blocks`, `root` |
| `listdir(dir=None)`, `walk(dir=None)`, `files()` | directory entries (`DirectoryEntry`: `path`, `identifier`, `name`, `version`, `lba`, `size`, `is_dir`, `flags`) |
| `find(path)` | an entry by path; raises `IsoNotFound` (also a `FileNotFoundError`) |
| `read_file(path, limit=None)`, `read_extent(entry, offset, length)`, `iter_file(path)` | file contents, whole, a slice, or in pieces |
| `sha256(path)`, `extract(path, destination)` | hash a file, or copy it out and get `ExtractResult(size, sha256)` |
| `read_sectors(lba, count)` | the 2048-byte user data of raw sectors, whatever the layout |

`extract` writes to a temporary file next to the destination and renames it into place only after the whole file was read and hashed, so a failure leaves no partial file and leaves an earlier file at the destination as it was.

### What it checks

- The primary volume descriptor, its version, a 2048-byte logical block size, and that the file holds all the sectors the volume claims. A descriptor search runs up to 32 sectors from sector 16.
- Every directory record: its length, its identifier fitting inside it, and (with `strict=True`) that the little-endian and big-endian copies of the extent and the size agree. `strict=False` takes the little-endian copy, for images from tools that get the second one wrong.
- Every sector read lies inside the image. A directory that points back at one already visited is skipped, so a damaged tree cannot loop; a tree of more than 400,000 entries or a directory over 16 MiB is refused.
- A file with several extents (flag 0x80) is listed but refused when read.

### PlayStation 2

`find_ps2_boot(disc, check_elf=True, expected_serial=None)` reads `SYSTEM.CNF` from the root (64 KiB at most), requires exactly one `BOOT2` line of the form `cdrom0:\NAME;1` (no subdirectory, no other version, no trailing text), finds `NAME` in the root and checks it starts with the ELF signature. It returns `Ps2Boot(system_cnf, elf_name, serial, entry)`; `serial` is `SLUS-21847` for a boot file named `SLUS_218.47` and `None` for a homebrew name such as `GAME.ELF`. A disc whose names carry no `;1` (as macOS `hdiutil` writes them) is read the same as one that does.

`parse_system_cnf(bytes)` gives every `KEY = value` line with its key in capitals, `boot_elf_name("cdrom0:\\NAME;1")` and `serial_from_elf_name("SLUS_218.47")` are the two parsing steps on their own, and `ps2_summary(disc)` is a dictionary with the serial, boot file, its size and SHA-256.

## Command line

```sh
uppercut-iso9660 ls game.iso [dir]               # F or D, size, LBA and path of every entry
uppercut-iso9660 get game.iso DATA/INTRO.BIN out.bin    # prints size and SHA-256
uppercut-iso9660 ps2 game.iso                    # the boot serial, ELF and its SHA-256, as JSON
```

Add `--lenient` to any of them for `strict=False`. An error is one line on stderr and exit code 1.

## Notes

- Names are the plain ISO 9660 identifiers (`SYSTEM.CNF;1`). Joliet, Rock Ridge and UDF names are not read; a disc that has them is read through its ISO 9660 tree, which every such disc also carries.
- Raw dumps are read with one fixed data offset per image (16 bytes into a Mode 1 sector, 24 into a Mode 2 Form 1 sector, 8 into a 2336-byte one). Audio tracks and Form 2 sectors are not data and are not read.
- The tests build their discs in Python (an 8-byte "ELF" header and made-up payloads). No game data, BIOS or console material is used or needed.

## License

MIT
