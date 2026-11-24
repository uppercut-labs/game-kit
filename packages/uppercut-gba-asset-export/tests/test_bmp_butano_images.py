import json
import struct

import pytest
from gae_synthetic import FakePillowImage, gradient
from uppercut_gba_asset_export import butano_descriptor, butano_json, from_pillow, indexed_bmp_4bpp

PALETTE = [(i * 16, 255 - i * 16, i) for i in range(16)]


def test_bmp_headers() -> None:
    data = indexed_bmp_4bpp(gradient(16, 8), 16, 8, PALETTE)
    assert data[:2] == b"BM"
    size, _, _, offset = struct.unpack("<IHHI", data[2:14])
    assert size == len(data) == offset + 8 * 8  # 16 pixels is 8 bytes a row, already a multiple of four
    assert offset == 14 + 40 + 64
    header, width, height, planes, bits, compression, image_size, _, _, used, important = struct.unpack("<IiiHHIIiiII", data[14:54])
    assert (header, width, height, planes, bits, compression, image_size, used, important) == (40, 16, 8, 1, 4, 0, 64, 16, 16)


def test_bmp_palette_is_bgr0_and_padded_to_16() -> None:
    data = indexed_bmp_4bpp(bytes(16), 4, 4, [(1, 2, 3), (4, 5, 6)])
    table = data[54 : 54 + 64]
    assert table[:8] == bytes([3, 2, 1, 0, 6, 5, 4, 0])
    assert table[8:] == bytes(56)


def test_bmp_rows_run_bottom_to_top_with_the_left_pixel_in_the_high_nibble() -> None:
    # 4x2 image: top row 1 2 3 4, bottom row 5 6 7 8
    data = indexed_bmp_4bpp(bytes([1, 2, 3, 4, 5, 6, 7, 8]), 4, 2, PALETTE)
    pixels = data[54 + 64 :]
    assert len(pixels) == 8  # two rows of two bytes, each padded to four
    assert pixels[:4] == bytes([0x56, 0x78, 0, 0])  # the bottom row first
    assert pixels[4:] == bytes([0x12, 0x34, 0, 0])


def test_bmp_refusals() -> None:
    with pytest.raises(ValueError):
        indexed_bmp_4bpp(bytes(15), 5, 3, PALETTE)  # odd width
    with pytest.raises(ValueError):
        indexed_bmp_4bpp(bytes(7), 4, 2, PALETTE)  # wrong length
    with pytest.raises(ValueError):
        indexed_bmp_4bpp(bytes([16]) + bytes(7), 4, 2, PALETTE)
    with pytest.raises(ValueError):
        indexed_bmp_4bpp(bytes(8), 4, 2, PALETTE + [(0, 0, 0)])


def test_butano_descriptors() -> None:
    assert butano_descriptor("sprite", (16, 32)) == {
        "type": "sprite",
        "width": 16,
        "height": 32,
        "bpp_mode": "bpp_4",
        "colors_count": 16,
        "compression": "auto_no_huffman",
    }
    bg = butano_descriptor("regular_bg_tiles")
    assert list(bg) == ["type", "bpp_mode", "compression", "generate_palette", "palette_colors_count", "palette_compression"]
    assert bg["generate_palette"] is True
    text = butano_json("sprite", (8, 8))
    assert text.endswith("}\n") and text.startswith('{\n  "type": "sprite",\n') and json.loads(text)["width"] == 8
    with pytest.raises(ValueError):
        butano_descriptor("sprite")
    with pytest.raises(ValueError):
        butano_descriptor("map")  # type: ignore[arg-type]


def test_from_pillow_like_object() -> None:
    palette = [10, 20, 30, 40, 50, 60] + [0] * (768 - 6)
    image = FakePillowImage(bytes([0, 1, 1, 0]), 2, 2, palette)
    got = from_pillow(image)
    assert (got.indices, got.width, got.height) == (bytes([0, 1, 1, 0]), 2, 2)
    assert len(got.palette) == 16 and got.palette[:2] == ((10, 20, 30), (40, 50, 60))
    assert len(from_pillow(image, colors=4).palette) == 4
    short = from_pillow(FakePillowImage(bytes(4), 2, 2, [1, 2, 3]))
    assert short.palette == ((1, 2, 3),)
    with pytest.raises(ValueError):
        from_pillow(FakePillowImage(bytes(4), 2, 2, [], mode="RGB"))
    assert from_pillow(FakePillowImage(bytes(4), 2, 2, None)).palette == ()  # type: ignore[arg-type]


def test_from_real_pillow_when_it_is_installed() -> None:
    pil = pytest.importorskip("PIL.Image")
    image = pil.new("P", (8, 8), 3)
    image.putpalette([c for i in range(16) for c in (i * 16, i, 255 - i * 16)] + [0] * (768 - 48))
    got = from_pillow(image)
    assert got.width == got.height == 8 and set(got.indices) == {3} and got.palette[3] == (48, 3, 207)
