"""Synthetic GBA ROM and ELF builders for the tests. No game ROMs or game binaries are used."""

from __future__ import annotations

import struct

from uppercut_gba_rom_verify import NINTENDO_LOGO, RomConfig, calculate_header_complement

EXAMPLE = RomConfig(title="EXAMPLE GAME", game_code="EXMP", maker_code="01")


def build_rom(config: RomConfig = EXAMPLE, size: int = 0xC0) -> bytearray:
    """A ROM with a valid header for ``config``, padded with zeros to ``size`` bytes."""
    rom = bytearray(size)
    rom[0x04:0xA0] = NINTENDO_LOGO
    rom[0xA0:0xAC] = config.title_field
    rom[0xAC:0xB0] = config.game_code_field
    rom[0xB0:0xB2] = config.maker_code_field
    rom[0xB2] = 0x96
    rom[0xBD] = calculate_header_complement(rom)
    return rom


def build_elf(entry_point: int = 0x08000000) -> bytearray:
    """A minimal ELF32 ARM EABI5 executable with one executable PT_LOAD segment covering the entry."""
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    header = struct.pack(
        "<16sHHIIIIIHHHHHH",
        ident,
        2,  # ET_EXEC
        40,  # EM_ARM
        1,  # EV_CURRENT
        entry_point,
        52,  # program header offset
        0,  # section header offset
        0x05000000,  # EABI5
        52,  # header size
        32,  # program header entry size
        1,  # program header count
        0,
        0,
        0,
    )
    program_header = struct.pack(
        "<IIIIIIII",
        1,  # PT_LOAD
        84,  # file offset
        entry_point,
        entry_point,
        4,  # file size
        4,  # memory size
        5,  # R + X
        4,
    )
    return bytearray(header + program_header + b"\x00\x00\x00\x00")

