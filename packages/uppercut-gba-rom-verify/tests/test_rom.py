import pytest

from uppercut_gba_rom_verify import (
    NINTENDO_LOGO,
    RomConfig,
    ValidationError,
    calculate_header_complement,
    validate_rom_bytes,
    validate_rom_file,
)
from synthetic import EXAMPLE, build_rom


def complement_by_loop(rom: bytes) -> int:
    """An independent reading of the GBA header complement, written as a plain loop."""
    total = 0
    for index in range(0xA0, 0xBD):
        total = (total - rom[index]) & 0xFF
    return (total - 0x19) & 0xFF


def test_logo_constant_is_the_156_byte_header_logo():
    assert len(NINTENDO_LOGO) == 156
    assert NINTENDO_LOGO[:4] == bytes.fromhex("24ffae51")


def test_accepts_valid_header_and_reports_fields():
    info = validate_rom_bytes(bytes(build_rom()), EXAMPLE)

    assert info.size == 0xC0
    assert info.title == "EXAMPLE GAME"
    assert info.game_code == "EXMP"
    assert info.maker_code == "01"
    assert info.complement == complement_by_loop(build_rom())
    assert len(info.sha256) == 64


def test_complement_is_computed_for_each_title_and_code():
    for config in (
        RomConfig("TWELVE CHARS", "TWLV", "00"),
        RomConfig("EXAMPLE!", "EXMP", "01"),
        RomConfig("A", "ABCD", "ZZ"),
    ):
        with_title = build_rom(config)
        info = validate_rom_bytes(bytes(with_title), config)
        assert info.complement == complement_by_loop(with_title)


def test_complement_helper_matches_loop_on_arbitrary_header_bytes():
    rom = bytes((index * 37 + 11) & 0xFF for index in range(0xC0))
    assert calculate_header_complement(rom) == complement_by_loop(rom)


def test_title_is_nul_padded_to_twelve_bytes():
    config = RomConfig("SHORT!", "SHRT", "00")
    rom = build_rom(config)

    assert rom[0xA0:0xAC] == b"SHORT!" + bytes(6)
    validate_rom_bytes(bytes(rom), config)


def test_twelve_character_title_needs_no_padding():
    config = RomConfig("TWELVE CHARS", "TWLV", "00")
    assert config.title_field == b"TWELVE CHARS"
    validate_rom_bytes(bytes(build_rom(config)), config)


@pytest.mark.parametrize(
    ("offset", "replacement", "message"),
    [
        (0x04, b"\x00", "Nintendo logo"),
        (0xA0, b"?", "title"),
        (0xAC, b"?", "game code"),
        (0xB0, b"?", "maker code"),
        (0xB2, b"\x00", "fixed byte"),
    ],
)
def test_rejects_each_required_header_field(offset, replacement, message):
    rom = build_rom()
    rom[offset : offset + len(replacement)] = replacement
    with pytest.raises(ValidationError, match=message):
        validate_rom_bytes(bytes(rom), EXAMPLE)


def test_wrong_title_is_reported_even_when_other_fields_are_valid():
    rom = build_rom()
    rom[0xA0:0xAC] = b"EXAMPLE BAD\x00"
    rom[0xBD] = calculate_header_complement(rom)

    with pytest.raises(ValidationError, match="title") as raised:
        validate_rom_bytes(bytes(rom), EXAMPLE)
    assert raised.value.errors == (
        "ROM title must be 'EXAMPLE GAME' in a 12-byte field padded with NUL bytes",
    )


def test_wrong_complement_is_rejected():
    rom = build_rom()
    rom[0xBD] = (rom[0xBD] + 1) & 0xFF

    with pytest.raises(ValidationError, match="complement checksum"):
        validate_rom_bytes(bytes(rom), EXAMPLE)


def test_complement_check_uses_the_current_header_bytes():
    rom = build_rom()
    rom[0xA0] ^= 0x01  # change a title byte but keep the stored complement

    with pytest.raises(ValidationError) as raised:
        validate_rom_bytes(bytes(rom), EXAMPLE)
    assert any("complement checksum" in detail for detail in raised.value.errors)


def test_rejects_short_and_misaligned_roms():
    with pytest.raises(ValidationError, match="ROM size"):
        validate_rom_bytes(bytes(0xBF), EXAMPLE)

    with pytest.raises(ValidationError, match="4-byte aligned"):
        validate_rom_bytes(bytes(build_rom()) + b"\x00", EXAMPLE)


def test_rejects_roms_over_32_mebibytes():
    rom = bytes(32 * 1024 * 1024 + 4)
    with pytest.raises(ValidationError, match="ROM size"):
        validate_rom_bytes(rom, EXAMPLE)


def test_short_rom_reports_only_the_size_error():
    with pytest.raises(ValidationError) as raised:
        validate_rom_bytes(bytes(0x20), EXAMPLE)
    assert raised.value.errors == ("ROM size must be between 192 and 33554432 bytes, found 32",)


def test_expected_sha256_matches_case_insensitively():
    rom = bytes(build_rom())
    digest = validate_rom_bytes(rom, EXAMPLE).sha256

    info = validate_rom_bytes(rom, EXAMPLE, expected_sha256=digest.upper())
    assert info.sha256 == digest


def test_expected_sha256_mismatch_is_reported_with_other_failures():
    rom = bytes(build_rom())
    wrong = "0" * 64

    with pytest.raises(ValidationError) as raised:
        validate_rom_bytes(rom + b"\x00", EXAMPLE, expected_sha256=wrong)
    assert any(detail.startswith("ROM SHA-256 must be " + wrong) for detail in raised.value.errors)


def test_missing_file_is_reported_by_name(tmp_path):
    missing = tmp_path / "missing.gba"

    with pytest.raises(ValidationError, match="cannot read ROM"):
        validate_rom_file(missing, EXAMPLE)


def test_file_validation_reads_bytes(tmp_path):
    path = tmp_path / "example.gba"
    path.write_bytes(bytes(build_rom()))

    info = validate_rom_file(path, EXAMPLE)
    assert info.size == 0xC0
