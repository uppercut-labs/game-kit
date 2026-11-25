# uppercut-gba-rom-verify

Checks a Game Boy Advance ROM's cartridge header and the devkitARM ELF it was built from, then reports the ROM's SHA-256. The header checks are the logo, title, game code, maker code, fixed byte 0x96, the complement byte at 0xBD (computed, not hard-coded), the 32 MB size limit and 4-byte alignment. The ELF checks are ELF32, little-endian, ARM EABI5, an executable type, and an entry point inside an executable PT_LOAD segment. Standard library only.

The package takes the cartridge identity (title, game code, maker code) as parameters, so one copy serves every game.

```sh
pip install uppercut-gba-rom-verify
```

## Command line

```sh
uppercut-gba-rom-verify game.gba game.elf --title "EXAMPLE GAME" --game-code EXMP --maker-code 01
```

The identity can come from a JSON file instead, and flags override it:

```json
{ "title": "EXAMPLE GAME", "game_code": "EXMP", "maker_code": "01" }
```

```sh
uppercut-gba-rom-verify game.gba game.elf --config cartridge.json
python -m uppercut_gba_rom_verify game.gba game.elf --config cartridge.json --expect-sha256 <64 hex digits>
```

Each check prints `PASS ...` to stdout or `FAIL ...` to stderr. The exit code is 0 when everything passes, 1 when a check fails and 2 for a usage error, such as a missing identity or a title longer than 12 characters.

## Python

```python
from uppercut_gba_rom_verify import RomConfig, ValidationError, validate_elf_file, validate_rom_file

config = RomConfig(title="EXAMPLE GAME", game_code="EXMP", maker_code="01")

try:
    rom = validate_rom_file("game.gba", config)
    elf = validate_elf_file("game.elf")
except ValidationError as error:
    for detail in error.errors:
        print("FAIL", detail)
else:
    print(rom.sha256, rom.complement, elf.load_segment_count)
```

## Functions

| Function | Returns |
| --- | --- |
| `validate_rom_file(path, config, *, expected_sha256=None)` | `RomInfo` |
| `validate_rom_bytes(rom, config, *, expected_sha256=None)` | `RomInfo` |
| `validate_elf_file(path)` | `ElfInfo` |
| `validate_elf_bytes(elf)` | `ElfInfo` |
| `calculate_header_complement(rom)` | the complement byte for a ROM's header |
| `main(argv=None)` | the exit code, as the command line does |

`RomConfig(title, game_code, maker_code)` checks its own values: the title is 1 to 12 ASCII characters and is NUL-padded to 12 bytes in the header, the game code is exactly 4 ASCII characters and the maker code exactly 2.

Every failed check is collected into `ValidationError.errors`. A missing or unreadable file raises `ValidationError` naming the artifact.

## Notes

- The Nintendo logo is the 156-byte block the GBA BIOS checks. It is the same for every cartridge.
- The ELF checks target devkitARM output (`ET_EXEC`, `EM_ARM`, EABI5 flags).
- Checks the GBA BIOS and gbafix also treat as fixed (main unit code, device type, reserved bytes, software version) are not enforced; the header is validated for the fields listed above only.

## License

MIT
