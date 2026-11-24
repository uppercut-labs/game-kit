import hashlib
import struct

import pytest

from pngs import chunk, make_png
from uppercut_mgba_smoke import SmokeError, validate_png, validate_screenshot_set


def test_decodes_a_complete_rgb_png(tmp_path):
    data = make_png()
    path = tmp_path / "screen.png"
    path.write_bytes(data)

    image = validate_png(path)

    assert (image.width, image.height, image.channels) == (240, 160, 3)
    assert image.sha256 == hashlib.sha256(data).hexdigest()
    assert len(image.pixels) == 240 * 160 * 3
    assert image.pixels[:3] == bytes((0, 0, 0))
    assert image.pixels[3:6] == bytes((1, 0, 1))


@pytest.mark.parametrize("filter_type", [1, 2, 3, 4])
def test_every_row_filter_decodes_to_the_same_pixels(tmp_path, filter_type):
    plain = tmp_path / "plain.png"
    filtered = tmp_path / "filtered.png"
    plain.write_bytes(make_png(seed=9))
    filtered.write_bytes(make_png(seed=9, filter_type=filter_type))
    assert validate_png(plain).pixels == validate_png(filtered).pixels


def test_dimensions_are_a_parameter(tmp_path):
    path = tmp_path / "small.png"
    path.write_bytes(make_png(width=16, height=8))
    assert validate_png(path, width=16, height=8).width == 16
    with pytest.raises(SmokeError, match="must be 240x160, found 16x8"):
        validate_png(path)


def test_rejects_a_short_file_with_forged_dimensions(tmp_path):
    path = tmp_path / "forged.png"
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + b"NOTIHDR!" + struct.pack(">II", 240, 160))
    with pytest.raises(SmokeError, match="PNG|chunk|truncated"):
        validate_png(path)


def test_rejects_a_bad_crc(tmp_path):
    data = bytearray(make_png())
    data[29] ^= 1
    path = tmp_path / "crc.png"
    path.write_bytes(data)
    with pytest.raises(SmokeError, match="CRC"):
        validate_png(path)


@pytest.mark.parametrize(
    "options, message",
    [
        ({"color_type": 6}, "8-bit RGB"),
        ({"depth": 16}, "8-bit RGB"),
        ({"drop_end": True}, "truncated|missing"),
        ({"trailing": b"x"}, "data after|truncated"),
    ],
)
def test_rejects_other_encodings_and_damage(tmp_path, options, message):
    path = tmp_path / "bad.png"
    path.write_bytes(make_png(**options))
    with pytest.raises(SmokeError, match=message):
        validate_png(path)


def test_rejects_something_that_is_not_a_png(tmp_path):
    path = tmp_path / "text.png"
    path.write_text("hello")
    with pytest.raises(SmokeError, match="not a PNG"):
        validate_png(path)
    with pytest.raises(SmokeError, match="cannot read"):
        validate_png(tmp_path / "missing.png")


def test_rejects_a_first_chunk_that_is_not_ihdr(tmp_path):
    path = tmp_path / "order.png"
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IDAT", b"") + chunk(b"IEND", b""))
    with pytest.raises(SmokeError, match="first chunk is not IHDR"):
        validate_png(path)


def test_set_must_match_the_expected_names_exactly(tmp_path):
    (tmp_path / "a.png").write_bytes(make_png(seed=1))
    (tmp_path / "extra.png").write_bytes(make_png(seed=2))
    with pytest.raises(SmokeError, match=r"missing=\['b.png'\], extra=\['extra.png'\]"):
        validate_screenshot_set(tmp_path, ["a.png", "b.png"])


def test_blank_screenshots_are_refused(tmp_path):
    (tmp_path / "a.png").write_bytes(make_png(solid=True))
    with pytest.raises(SmokeError, match="blank"):
        validate_screenshot_set(tmp_path, ["a.png"])


def test_min_colors_is_a_parameter(tmp_path):
    (tmp_path / "a.png").write_bytes(make_png(solid=True))
    validate_screenshot_set(tmp_path, ["a.png"], min_colors=1)


def test_duplicate_screenshots_are_refused(tmp_path):
    data = make_png(seed=3)
    (tmp_path / "a.png").write_bytes(data)
    (tmp_path / "b.png").write_bytes(data)
    with pytest.raises(SmokeError, match="duplicate screenshots: a.png and b.png"):
        validate_screenshot_set(tmp_path, ["a.png", "b.png"])


def test_reviewed_hashes_must_match_the_file(tmp_path):
    data = make_png(seed=4)
    (tmp_path / "a.png").write_bytes(data)
    good = hashlib.sha256(data).hexdigest()
    images = validate_screenshot_set(tmp_path, ["a.png"], reviewed_sha256={"a.png": good})
    assert images["a.png"].sha256 == good
    with pytest.raises(SmokeError, match="reviewed SHA-256"):
        validate_screenshot_set(tmp_path, ["a.png"], reviewed_sha256={"a.png": "0" * 64})
    with pytest.raises(SmokeError, match="missing screenshot"):
        validate_screenshot_set(tmp_path, ["a.png"], reviewed_sha256={"z.png": good})
