import pytest
from gae_synthetic import blank, gradient, two_tiles_mirrored
from uppercut_gba_asset_export import tile_bytes, tile_reuse, tile_words


def test_one_tile_layout_low_nibble_is_the_left_pixel() -> None:
    image = bytes([1, 2, 3, 4, 5, 6, 7, 8]) + bytes(56)
    data = tile_bytes(image, 8, 8)
    assert len(data) == 32
    assert data[:4] == bytes([0x21, 0x43, 0x65, 0x87])
    assert data[4:] == bytes(28)


def test_tiles_come_left_to_right_then_top_to_bottom() -> None:
    # a 16x16 image of four flat tiles with values 1 (top-left), 2 (top-right), 3 (bottom-left), 4 (bottom-right)
    rows = []
    for y in range(16):
        left, right = (1, 2) if y < 8 else (3, 4)
        rows.append(bytes([left] * 8 + [right] * 8))
    data = tile_bytes(b"".join(rows), 16, 16)
    assert [data[i * 32] for i in range(4)] == [0x11, 0x22, 0x33, 0x44]


def test_words_are_little_endian_and_eight_to_a_tile() -> None:
    image = bytes([1, 2, 3, 4, 5, 6, 7, 8]) + bytes(56)
    words = tile_words(image, 8, 8)
    assert len(words) == 8
    assert words[0] == 0x87654321
    assert words[1:] == [0] * 7


def test_word_count_scales_with_size() -> None:
    assert len(tile_words(gradient(32, 16), 32, 16)) == 8 * 4 * 2


def test_bad_images_are_refused() -> None:
    with pytest.raises(ValueError):
        tile_words(bytes(60), 10, 6)  # not tile-aligned
    with pytest.raises(ValueError):
        tile_words(bytes(63), 8, 8)  # wrong length
    with pytest.raises(ValueError):
        tile_words(bytes([16]) + bytes(63), 8, 8)  # index above 15
    with pytest.raises(ValueError):
        tile_words(b"", 0, 0)


def test_tile_reuse_counts_exact_and_flipped_duplicates() -> None:
    image, w, h = two_tiles_mirrored()
    reuse = tile_reuse(image, w, h)
    assert (reuse.total, reuse.unique, reuse.unique_with_flips) == (2, 2, 1)
    flat = blank(16, 16)
    reuse = tile_reuse(flat, 16, 16)
    assert (reuse.total, reuse.unique, reuse.unique_with_flips) == (4, 1, 1)


def test_vertical_and_both_flips_count() -> None:
    glyph = [[1, 0, 0, 0, 0, 0, 0, 0]] + [[0] * 8 for _ in range(6)] + [[2, 2, 0, 0, 0, 0, 0, 0]]
    vertical = glyph[::-1]
    both = [row[::-1] for row in vertical]
    # three tiles side by side: a row of the image is a row from each tile
    flat = bytearray()
    for y in range(8):
        for tile in (glyph, vertical, both):
            flat += bytes(tile[y])
    reuse = tile_reuse(bytes(flat), 24, 8)
    assert (reuse.total, reuse.unique, reuse.unique_with_flips) == (3, 3, 1)
