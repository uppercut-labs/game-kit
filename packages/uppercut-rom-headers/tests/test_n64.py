import struct

import pytest

from uppercut_rom_headers import RomHeaderError, compute_n64_checksums, fix_n64_checksums, verify_n64_checksums
from rom_images import N64_GOLDEN, N64_SIZE, stream


@pytest.mark.parametrize("name", sorted(N64_GOLDEN))
def test_matches_the_independent_implementation(name):
    image, crc1, crc2 = N64_GOLDEN[name]
    result = compute_n64_checksums(image)
    assert (result.crc1, result.crc2) == (crc1, crc2)
    assert result.skipped is False


def test_compute_leaves_the_image_alone_and_accepts_any_byte_buffer():
    image = stream(5, N64_SIZE)
    before = bytes(image)
    from_bytearray = compute_n64_checksums(image)
    assert bytes(image) == before
    assert compute_n64_checksums(bytes(image)) == from_bytearray
    assert compute_n64_checksums(memoryview(image)) == from_bytearray


def test_fix_writes_both_crcs_big_endian_at_0x10_and_0x14():
    image = stream(1, N64_SIZE)
    result = fix_n64_checksums(image)
    assert image[0x10:0x18] == bytes.fromhex("4126d6c91bb5d2fe")
    assert (result.crc1, result.crc2) == (0x4126D6C9, 0x1BB5D2FE)


def test_fix_changes_only_the_eight_header_bytes_and_is_idempotent():
    original = stream(2, N64_SIZE)
    image = bytearray(original)
    first = fix_n64_checksums(image)
    changed = [offset for offset in range(len(image)) if image[offset] != original[offset]]
    assert all(0x10 <= offset < 0x18 for offset in changed)
    assert fix_n64_checksums(image) == first


def test_the_checksum_ignores_its_own_header_words_and_everything_outside_the_range():
    image = stream(3, N64_SIZE + 64)
    expected = compute_n64_checksums(image)
    for offset in (0x10, 0x17, 0x20, 0xFFF, N64_SIZE, N64_SIZE + 63):
        changed = bytearray(image)
        changed[offset] ^= 0xFF
        assert compute_n64_checksums(changed) == expected, hex(offset)


@pytest.mark.parametrize("offset", [0x1000, 0x1003, 0x80000, N64_SIZE - 1])
def test_a_change_inside_the_range_changes_the_checksum(offset):
    image = stream(4, N64_SIZE)
    expected = compute_n64_checksums(image)
    changed = bytearray(image)
    changed[offset] ^= 0x01
    assert compute_n64_checksums(changed) != expected


def test_verify_passes_after_fix_and_reports_both_values_when_it_does_not():
    image = stream(6, N64_SIZE)
    before = verify_n64_checksums(image)
    assert before.ok is False
    assert before.found == struct.unpack_from(">II", image, 0x10)
    fix_n64_checksums(image)
    after = verify_n64_checksums(image)
    assert after.ok is True
    assert after.expected == after.found == before.expected


def test_verify_detects_one_wrong_word():
    image = stream(6, N64_SIZE)
    fix_n64_checksums(image)
    image[0x17] ^= 0x01
    result = verify_n64_checksums(image)
    assert result.ok is False
    assert result.expected[0] == result.found[0]
    assert result.expected[1] != result.found[1]


def test_preserve_zero_header_leaves_an_all_zero_header_alone():
    image = stream(8, N64_SIZE)
    image[0x10:0x18] = bytes(8)
    before = bytes(image)
    result = fix_n64_checksums(image, preserve_zero_header=True)
    assert (result.crc1, result.crc2, result.skipped) == (0, 0, True)
    assert bytes(image) == before


def test_preserve_zero_header_still_fixes_anything_else():
    half = stream(8, N64_SIZE)
    half[0x10:0x18] = bytes([0, 0, 0, 0, 0, 0, 0, 1])
    assert fix_n64_checksums(half, preserve_zero_header=True).skipped is False
    assert verify_n64_checksums(half).ok

    nonzero = stream(9, N64_SIZE)
    assert fix_n64_checksums(nonzero, preserve_zero_header=True).skipped is False
    assert verify_n64_checksums(nonzero).ok


def test_without_the_option_a_zero_header_is_fixed():
    image = stream(8, N64_SIZE)
    image[0x10:0x18] = bytes(8)
    assert fix_n64_checksums(image).skipped is False
    assert verify_n64_checksums(image).ok


def test_an_image_shorter_than_the_range_is_refused():
    for size in (0, 0x1000, N64_SIZE - 1):
        with pytest.raises(RomHeaderError, match="too short"):
            compute_n64_checksums(bytes(size))
        with pytest.raises(RomHeaderError, match="too short"):
            fix_n64_checksums(bytearray(size))


def test_fix_needs_a_writable_bytearray():
    with pytest.raises(RomHeaderError, match="bytearray"):
        fix_n64_checksums(bytes(N64_SIZE))  # type: ignore[arg-type]
    with pytest.raises(RomHeaderError, match="bytes, bytearray or memoryview"):
        compute_n64_checksums("not bytes")  # type: ignore[arg-type]
