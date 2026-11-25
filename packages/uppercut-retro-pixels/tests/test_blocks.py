import pytest

from uppercut_retro_pixels import (
    morton_index,
    pack_nibbles,
    swizzle,
    swizzle_items,
    unpack_nibbles,
    unswizzle,
)


def ref_address(x, y, row_bytes):
    """The layout as an address formula: 16-byte by 8-row blocks, row-major, 128 bytes each."""
    return ((y // 8) * (row_bytes // 16) + x // 16) * 128 + (y % 8) * 16 + (x % 16)


def ramp_bytes(count):
    return bytes((i * 7 + i // 256) % 256 for i in range(count))


@pytest.mark.parametrize("row_bytes,height", [(16, 8), (64, 8), (128, 64), (32, 24), (48, 16)])
def test_swizzle_follows_the_block_address_formula(row_bytes, height):
    data = ramp_bytes(row_bytes * height)
    out = swizzle(data, row_bytes, height)
    for y in range(height):
        for x in range(row_bytes):
            assert out[ref_address(x, y, row_bytes)] == data[y * row_bytes + x]


@pytest.mark.parametrize("row_bytes,height", [(16, 8), (128, 64), (48, 16)])
def test_unswizzle_inverts_swizzle(row_bytes, height):
    data = ramp_bytes(row_bytes * height)
    assert unswizzle(swizzle(data, row_bytes, height), row_bytes, height) == data
    assert swizzle(unswizzle(data, row_bytes, height), row_bytes, height) == data


def test_swizzle_is_a_permutation_that_moves_bytes():
    data = bytes(range(256)) * 2
    out = swizzle(data, 64, 8)
    assert sorted(out) == sorted(data)
    assert out != data


def test_single_block_is_unchanged():
    data = ramp_bytes(128)
    assert swizzle(data, 16, 8) == data


def test_swizzle_items_equals_byte_swizzle_for_16_bit_pixels():
    width, height = 32, 16
    pixels = [(x * 3 + y * 5) & 0xFFFF for y in range(height) for x in range(width)]
    as_bytes = b"".join(p.to_bytes(2, "little") for p in pixels)
    swizzled = swizzle(as_bytes, width * 2, height)
    assert b"".join(p.to_bytes(2, "little") for p in swizzle_items(pixels, width, height)) == swizzled


def test_swizzle_items_block_width_for_8_bit_pixels():
    width, height = 32, 16
    pixels = [(x + y * 11) & 0xFF for y in range(height) for x in range(width)]
    assert bytes(swizzle_items(pixels, width, height, block_width=16)) == swizzle(bytes(pixels), width, height)


def test_swizzle_rejects_bad_sizes():
    with pytest.raises(ValueError):
        swizzle(bytes(24 * 8), 24, 8)
    with pytest.raises(ValueError):
        swizzle(bytes(16 * 7), 16, 7)
    with pytest.raises(ValueError):
        swizzle(bytes(10), 16, 8)
    with pytest.raises(ValueError):
        swizzle_items([0] * 64, 8, 8, block_width=3)
    with pytest.raises(ValueError):
        swizzle_items([0] * 63, 8, 8)


def test_four_bit_images_swizzle_by_their_byte_rows():
    # 64 x 16 pixels at 4 bits is 32 bytes a row: two blocks wide
    indices = [(x ^ y) & 15 for y in range(16) for x in range(64)]
    packed = pack_nibbles(indices)
    assert len(packed) == 32 * 16
    out = swizzle(packed, 32, 16)
    for y in range(16):
        for bx in range(32):
            assert out[ref_address(bx, y, 32)] == packed[y * 32 + bx]


def test_nibbles_low_first_and_round_trip():
    assert pack_nibbles([1, 2, 3, 15]) == bytes([0x21, 0xF3])
    assert unpack_nibbles(bytes([0x21, 0xF3])) == [1, 2, 3, 15]
    data = [(i * 5) % 16 for i in range(64)]
    assert unpack_nibbles(pack_nibbles(data)) == data
    with pytest.raises(ValueError):
        pack_nibbles([1, 2, 3])
    with pytest.raises(ValueError):
        pack_nibbles([16, 0])


def test_morton_index_known_values_and_is_a_bijection():
    assert [morton_index(x, y, 1) for y in range(2) for x in range(2)] == [0, 2, 1, 3]
    assert morton_index(3, 0, 2) == 10
    assert morton_index(0, 3, 2) == 5
    assert sorted(morton_index(x, y, 4) for y in range(16) for x in range(16)) == list(range(256))
    with pytest.raises(ValueError):
        morton_index(4, 0, 2)
