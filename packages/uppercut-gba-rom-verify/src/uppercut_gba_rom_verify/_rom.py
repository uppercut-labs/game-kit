"""GBA cartridge header checks: logo, title, game code, maker code, fixed byte, complement, size and alignment."""

from __future__ import annotations

import hashlib
from dataclasses import dataclass
from typing import Optional

from ._errors import PathInput, ValidationError, read_artifact

ROM_HEADER_SIZE = 0xC0
MAX_ROM_SIZE = 32 * 1024 * 1024
TITLE_FIELD_SIZE = 12
GAME_CODE_SIZE = 4
MAKER_CODE_SIZE = 2
FIXED_BYTE = 0x96

NINTENDO_LOGO = bytes.fromhex(
    "24ffae51699aa2213d84820a84e409ad11248b98c0817f21a352be199309ce201"
    "0464a4af82731ec58c7e83382e3cebf85f4df94ce4b09c194568ac01372a7fc"
    "9f844d73a3ca9a615897a327fc039876231dc7610304ae56bf38840040a70efd"
    "ff52fe036f9530f197fbc08560d68025a963be03014e38e2f9a234ffbb3e034"
    "4780090cb88113a9465c07c6387f03cafd625e48b380aac7221d4f807"
)

def _ascii_field(value: str, name: str, size: int, exact: bool) -> bytes:
    try:
        raw = value.encode("ascii")
    except UnicodeEncodeError as error:
        raise ValueError(f"{name} must be ASCII, found {value!r}") from error
    if b"\x00" in raw:
        raise ValueError(f"{name} must not contain NUL bytes")
    if exact and len(raw) != size:
        raise ValueError(f"{name} must be exactly {size} ASCII characters, found {len(raw)}")
    if not exact and not 1 <= len(raw) <= size:
        raise ValueError(f"{name} must be 1 to {size} ASCII characters, found {len(raw)}")
    return raw


@dataclass(frozen=True)
class RomConfig:
    """The cartridge identity a ROM must carry.

    ``title`` is at most 12 ASCII characters. The header field is NUL-padded to 12 bytes.
    ``game_code`` is exactly 4 ASCII characters and ``maker_code`` exactly 2.
    """

    title: str
    game_code: str
    maker_code: str

    def __post_init__(self) -> None:
        _ascii_field(self.title, "title", TITLE_FIELD_SIZE, exact=False)
        _ascii_field(self.game_code, "game code", GAME_CODE_SIZE, exact=True)
        _ascii_field(self.maker_code, "maker code", MAKER_CODE_SIZE, exact=True)

    @property
    def title_field(self) -> bytes:
        """The 12-byte title field as it sits at offset 0xA0."""
        return _ascii_field(self.title, "title", TITLE_FIELD_SIZE, exact=False).ljust(
            TITLE_FIELD_SIZE, b"\x00"
        )

    @property
    def game_code_field(self) -> bytes:
        return _ascii_field(self.game_code, "game code", GAME_CODE_SIZE, exact=True)

    @property
    def maker_code_field(self) -> bytes:
        return _ascii_field(self.maker_code, "maker code", MAKER_CODE_SIZE, exact=True)


@dataclass(frozen=True)
class RomInfo:
    size: int
    title: str
    game_code: str
    maker_code: str
    complement: int
    sha256: str


def calculate_header_complement(rom: bytes) -> int:
    """Return the header complement byte at 0xBD for a ROM whose header is complete.

    The GBA BIOS checks that the sum of bytes 0xA0 to 0xBC, plus this value and 0x19,
    is zero modulo 256. Equivalent to ``(-sum(rom[0xA0:0xBD]) - 0x19) & 0xFF``.
    """
    return (-sum(rom[0xA0:0xBD]) - 0x19) & 0xFF


def validate_rom_bytes(
    rom: bytes,
    config: RomConfig,
    *,
    expected_sha256: Optional[str] = None,
) -> RomInfo:
    """Validate ROM bytes against a cartridge identity.

    Raises ``ValidationError`` listing every failed check. ``expected_sha256``, when given,
    must match the SHA-256 of the whole file (case-insensitive).
    """
    errors: list[str] = []
    rom_size = len(rom)

    if not ROM_HEADER_SIZE <= rom_size <= MAX_ROM_SIZE:
        errors.append(
            f"ROM size must be between {ROM_HEADER_SIZE} and {MAX_ROM_SIZE} bytes, "
            f"found {rom_size}"
        )
    if rom_size % 4 != 0:
        errors.append(f"ROM size must be 4-byte aligned, found {rom_size} bytes")

    expected_complement = 0
    if rom_size >= ROM_HEADER_SIZE:
        if rom[0x04:0xA0] != NINTENDO_LOGO:
            errors.append("ROM Nintendo logo bytes do not match the GBA header logo")
        if rom[0xA0:0xAC] != config.title_field:
            errors.append(
                f"ROM title must be {config.title!r} in a {TITLE_FIELD_SIZE}-byte field "
                "padded with NUL bytes"
            )
        if rom[0xAC:0xB0] != config.game_code_field:
            errors.append(f"ROM game code must be {config.game_code}")
        if rom[0xB0:0xB2] != config.maker_code_field:
            errors.append(f"ROM maker code must be {config.maker_code}")
        if rom[0xB2] != FIXED_BYTE:
            errors.append(
                f"ROM fixed byte must be 0x{FIXED_BYTE:02X}, found 0x{rom[0xB2]:02X}"
            )

        expected_complement = calculate_header_complement(rom)
        if rom[0xBD] != expected_complement:
            errors.append(
                "ROM complement checksum mismatch: "
                f"expected 0x{expected_complement:02X}, found 0x{rom[0xBD]:02X}"
            )

    digest = hashlib.sha256(rom).hexdigest()
    if expected_sha256 is not None:
        wanted = expected_sha256.strip().lower()
        if digest != wanted:
            errors.append(f"ROM SHA-256 must be {wanted}, found {digest}")

    if errors:
        raise ValidationError(errors)

    return RomInfo(
        size=rom_size,
        title=config.title,
        game_code=config.game_code,
        maker_code=config.maker_code,
        complement=expected_complement,
        sha256=digest,
    )


def validate_rom_file(
    path: PathInput,
    config: RomConfig,
    *,
    expected_sha256: Optional[str] = None,
) -> RomInfo:
    """Read and validate a GBA ROM file. See ``validate_rom_bytes``."""
    return validate_rom_bytes(
        read_artifact(path, "ROM"), config, expected_sha256=expected_sha256
    )

