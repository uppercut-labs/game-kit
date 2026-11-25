import pytest

from uppercut_rom_headers import RomHeaderError, compute_gba_complement, fix_gba_complement, verify_gba_complement
from rom_images import stream


def test_the_complement_of_a_small_header():
    rom = bytearray(0xC0)
    rom[0xA0] = 0x10
    rom[0xBC] = 0x05
    assert fix_gba_complement(rom) == (-0x10 - 0x05 - 0x19) & 0xFF
    assert rom[0xBD] == (-0x10 - 0x05 - 0x19) & 0xFF


def test_the_bios_rule_holds_after_a_fix():
    for seed in range(1, 40):
        rom = stream(seed, 0x200)
        fix_gba_complement(rom)
        assert (sum(rom[0xA0:0xBE]) + 0x19) & 0xFF == 0


def test_only_bytes_0xa0_to_0xbc_matter():
    rom = stream(3, 0x400)
    expected = compute_gba_complement(rom)
    for offset in (0, 0x9F, 0xBD, 0xBE, 0xBF, 0xC0, 0x3FF):
        changed = bytearray(rom)
        changed[offset] ^= 0xFF
        assert compute_gba_complement(changed) == expected
    for offset in (0xA0, 0xAC, 0xBC):
        changed = bytearray(rom)
        changed[offset] ^= 0x01
        assert compute_gba_complement(changed) != expected


def test_fix_changes_only_the_complement_byte_and_is_idempotent():
    original = stream(4, 0x300)
    rom = bytearray(original)
    value = fix_gba_complement(rom)
    assert [i for i in range(len(rom)) if rom[i] != original[i]] in ([], [0xBD])
    assert fix_gba_complement(rom) == value


def test_verify_reports_expected_and_found():
    rom = stream(5, 0xC0)
    rom[0xBD] = (compute_gba_complement(rom) + 1) & 0xFF
    bad = verify_gba_complement(rom)
    assert bad.ok is False
    assert bad.found == (rom[0xBD],)
    fix_gba_complement(rom)
    assert verify_gba_complement(rom).ok is True


def test_agrees_with_uppercut_gba_rom_verify_when_it_is_installed():
    verify = pytest.importorskip("uppercut_gba_rom_verify")
    for seed in range(1, 60):
        rom = stream(seed, 0xC0 + seed)
        assert compute_gba_complement(rom) == verify.calculate_header_complement(bytes(rom))


def test_a_shorter_header_or_a_read_only_buffer_is_refused():
    with pytest.raises(RomHeaderError, match="too short"):
        compute_gba_complement(bytes(0xBF))
    with pytest.raises(RomHeaderError, match="bytearray"):
        fix_gba_complement(bytes(0xC0))  # type: ignore[arg-type]
