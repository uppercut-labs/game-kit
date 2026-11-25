import struct

import pytest

from uppercut_gba_rom_verify import ValidationError, validate_elf_bytes, validate_elf_file
from synthetic import build_elf


def test_accepts_executable_load_segment_covering_entry():
    info = validate_elf_bytes(bytes(build_elf()))

    assert info.entry_point == 0x08000000
    assert info.load_segment_count == 1
    assert info.eabi_version == 5
    assert info.flags == 0x05000000


def test_rejects_header_only_file():
    elf = build_elf()[:52]
    struct.pack_into("<I", elf, 28, 0)
    struct.pack_into("<H", elf, 42, 0)
    struct.pack_into("<H", elf, 44, 0)

    with pytest.raises(ValidationError, match="program header|entry point"):
        validate_elf_bytes(bytes(elf))


def test_rejects_truncated_program_header_table():
    elf = build_elf()[:60]

    with pytest.raises(ValidationError, match="program header table"):
        validate_elf_bytes(bytes(elf))


def test_rejects_entry_outside_executable_load_segment():
    elf = build_elf()
    struct.pack_into("<I", elf, 24, 0x02000000)

    with pytest.raises(ValidationError, match="entry point"):
        validate_elf_bytes(bytes(elf))


def test_rejects_entry_in_uninitialized_tail_of_executable_segment():
    elf = build_elf()
    struct.pack_into("<I", elf, 24, 0x08000003)
    struct.pack_into("<I", elf, 68, 2)

    with pytest.raises(ValidationError, match="entry point"):
        validate_elf_bytes(bytes(elf))


def test_rejects_load_segment_past_end_of_file():
    elf = build_elf()
    struct.pack_into("<I", elf, 56, 0x1000)

    with pytest.raises(ValidationError, match="extends past"):
        validate_elf_bytes(bytes(elf))


@pytest.mark.parametrize(
    ("offset", "replacement", "message"),
    [
        (4, 2, "ELF32"),
        (5, 2, "little-endian"),
        (16, 3, "type must be executable"),
        (18, 3, "machine"),
    ],
)
def test_rejects_wrong_elf_identity_fields(offset, replacement, message):
    elf = build_elf()
    elf[offset] = replacement

    with pytest.raises(ValidationError, match=message):
        validate_elf_bytes(bytes(elf))


def test_rejects_non_eabi5_flags():
    elf = build_elf()
    struct.pack_into("<I", elf, 36, 0x04000000)

    with pytest.raises(ValidationError, match="EABI5"):
        validate_elf_bytes(bytes(elf))


def test_rejects_file_shorter_than_an_elf32_header():
    with pytest.raises(ValidationError, match="too small"):
        validate_elf_bytes(bytes(40))


def test_missing_elf_is_reported_by_name(tmp_path):
    with pytest.raises(ValidationError, match="cannot read ELF"):
        validate_elf_file(tmp_path / "missing.elf")


def test_file_validation_reads_bytes(tmp_path):
    path = tmp_path / "example.elf"
    path.write_bytes(bytes(build_elf()))

    assert validate_elf_file(path).entry_point == 0x08000000
