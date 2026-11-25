import pytest

from uppercut_rom_headers import RomHeaderError, compute_megadrive_checksum, fix_megadrive_checksum, verify_megadrive_checksum
from rom_images import MEGADRIVE_GOLDEN, stream


def test_adds_the_big_endian_words_from_0x200():
    rom = bytearray(0x400)
    rom[0x200:0x204] = bytes.fromhex("12340002")
    assert fix_megadrive_checksum(rom) == 0x1236
    assert rom[0x18E:0x190] == bytes.fromhex("1236")


@pytest.mark.parametrize("name", sorted(MEGADRIVE_GOLDEN))
def test_matches_the_independent_implementation(name):
    image, expected = MEGADRIVE_GOLDEN[name]
    assert compute_megadrive_checksum(image) == expected


def test_a_trailing_odd_byte_is_not_counted():
    even = bytearray(0x202)
    even[0x200:0x202] = bytes.fromhex("0102")
    odd = bytearray(even) + b"\xff"
    assert compute_megadrive_checksum(even) == compute_megadrive_checksum(odd) == 0x0102


def test_the_sum_wraps_at_16_bits():
    rom = bytearray(b"\xff" * (0x200 + 4))
    assert compute_megadrive_checksum(rom) == 0xFFFE


def test_the_header_before_0x200_is_not_summed_and_the_checksum_word_does_not_feed_itself():
    rom = stream(10, 0x800)
    expected = compute_megadrive_checksum(rom)
    for offset in (0, 0x100, 0x18E, 0x18F, 0x1FF):
        changed = bytearray(rom)
        changed[offset] ^= 0xFF
        assert compute_megadrive_checksum(changed) == expected


def test_fix_changes_only_the_two_checksum_bytes_and_is_idempotent():
    original = stream(11, 0x1000)
    rom = bytearray(original)
    value = fix_megadrive_checksum(rom)
    assert [i for i in range(len(rom)) if rom[i] != original[i]] == [i for i in (0x18E, 0x18F) if original[i] != rom[i]]
    assert fix_megadrive_checksum(rom) == value


def test_verify_reports_expected_and_found():
    rom = stream(12, 0x1000)
    before = verify_megadrive_checksum(rom)
    assert before.ok is False
    fix_megadrive_checksum(rom)
    after = verify_megadrive_checksum(rom)
    assert after.ok is True
    assert after.expected == after.found == before.expected


def test_the_smallest_image_is_0x200_bytes_and_sums_to_zero():
    rom = bytearray(0x200)
    assert fix_megadrive_checksum(rom) == 0


def test_a_shorter_image_or_a_read_only_buffer_is_refused():
    with pytest.raises(RomHeaderError, match="too short"):
        compute_megadrive_checksum(bytes(0x1FF))
    with pytest.raises(RomHeaderError, match="bytearray"):
        fix_megadrive_checksum(bytes(0x400))  # type: ignore[arg-type]
