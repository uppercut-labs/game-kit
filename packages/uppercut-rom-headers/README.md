# uppercut-rom-headers

Computes, writes and checks the header checksums that homebrew ROM builds have to get right: the two CRCs of a Nintendo 64 image (CIC-6102), the word checksum of a Mega Drive or 32X image, and the header complement byte of a Game Boy Advance image. Each format has a `compute_`, a `fix_` and a `verify_` function, and there is a small command line. Standard library only.

```sh
pip install uppercut-rom-headers
```

## Python

```python
from uppercut_rom_headers import fix_n64_checksums, verify_n64_checksums

image = bytearray(open("game.z64", "rb").read())
crcs = fix_n64_checksums(image)          # writes CRC1 and CRC2 at 0x10 and 0x14
print(hex(crcs.crc1), hex(crcs.crc2))
assert verify_n64_checksums(image).ok
open("game.z64", "wb").write(image)
```

| Format | Compute | Fix (writes into a `bytearray`, returns the value) | Verify (returns `Verification`) |
| --- | --- | --- | --- |
| Nintendo 64, CIC-6102 | `compute_n64_checksums(rom)` returns `N64Checksums(crc1, crc2)` | `fix_n64_checksums(rom, preserve_zero_header=False)` | `verify_n64_checksums(rom)` |
| Mega Drive and 32X | `compute_megadrive_checksum(rom)` | `fix_megadrive_checksum(rom)` | `verify_megadrive_checksum(rom)` |
| Game Boy Advance | `compute_gba_complement(rom)` | `fix_gba_complement(rom)` | `verify_gba_complement(rom)` |

`Verification` has `ok`, `expected` (what the contents give) and `found` (what the header holds), each a tuple of integers. The `compute_` and `verify_` functions take `bytes`, `bytearray` or `memoryview`; the `fix_` functions need a `bytearray`. An image that is too short for its checksum raises `RomHeaderError`, a `ValueError`.

| Format | What is summed | Where it goes | Smallest image |
| --- | --- | --- | --- |
| Nintendo 64 | the 1 MiB at 0x1000 to 0x101000, as big-endian 32-bit words, with the CIC-6102 seed | CRC1 at 0x10 and CRC2 at 0x14, big-endian | 0x101000 bytes |
| Mega Drive and 32X | every big-endian 16-bit word from 0x200 to the end of the image, modulo 65536; a trailing odd byte is not counted | 0x18E, big-endian | 0x200 bytes |
| Game Boy Advance | bytes 0xA0 to 0xBC: the complement is `-(sum + 0x19)` modulo 256 | 0xBD | 0xC0 bytes |

`preserve_zero_header=True` leaves a Nintendo 64 image alone when both header words are already zero, for toolchains that leave them zero on purpose; the result then has `skipped=True`.

## Command line

```sh
uppercut-rom-headers fix n64 game.z64
uppercut-rom-headers verify megadrive game.32x
python -m uppercut_rom_headers verify gba game.gba
```

`fix` rewrites the file in place. `verify` prints `PASS ...` to stdout or `FAIL ...` to stderr with the expected and found values. The exit code is 0 for success, 1 when a verification fails and 2 for a missing, unreadable or too short file.

## Notes

- The Nintendo 64 CRC is the CIC-6102 algorithm, which CIC-6101 shares. The final values are `t6 ^ t4 ^ t3` and `t5 ^ t2 ^ t1`. Some code combines them with `+` as CIC-6103 does, while keeping the 6102 seed; that gives CRCs that a 6102 boot check does not accept. CIC-6103, 6105 and 6106 are not implemented.
- The header complement of a Game Boy Advance image is the same one that `uppercut-gba-rom-verify` checks. This package fixes and verifies the byte alone; that package checks the whole header, the ELF and the SHA-256. The tests compare the two when both are installed.
- Only the checksums are here. Logos, security blocks, licence data and boot code are not, and a checksum alone does not make an image bootable.
- The tests use synthetic images from a fixed pseudo-random stream. The expected checksums were written by an independent implementation of the same algorithms.

## Working on it

From the repository root:

```sh
uv sync --all-packages && uv run pytest packages/uppercut-rom-headers
```

## License

MIT
