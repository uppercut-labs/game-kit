"""Check a Game Boy Advance ROM header and its devkitARM ELF, and report the ROM's SHA-256."""

from ._elf import ElfInfo, validate_elf_bytes, validate_elf_file
from ._errors import ValidationError
from ._rom import (
    NINTENDO_LOGO,
    RomConfig,
    RomInfo,
    calculate_header_complement,
    validate_rom_bytes,
    validate_rom_file,
)
from .cli import main

__all__ = [
    "ElfInfo",
    "NINTENDO_LOGO",
    "RomConfig",
    "RomInfo",
    "ValidationError",
    "calculate_header_complement",
    "main",
    "validate_elf_bytes",
    "validate_elf_file",
    "validate_rom_bytes",
    "validate_rom_file",
]
__version__ = "1.0.0"
