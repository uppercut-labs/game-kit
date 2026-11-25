import struct
import zlib

import pytest

from port_proof_synthetic import ppm_bytes
from uppercut_port_proof import PpmError, ProofError, Ppm, crop, encode_png, enlarge, md5_of, parse_ppm, ppm_to_png, read_ppm, write_ppm


def test_parse_ppm_reads_size_pixels_and_comments():
    data = ppm_bytes(3, 2, comment=True)
    pic = parse_ppm(data)
    assert (pic.width, pic.height) == (3, 2)
    assert len(pic.pixels) == 18 and pic.pixel(2, 1) == bytes([10, 9, 3])


def test_parse_ppm_ignores_bytes_after_the_pixels_and_accepts_any_white_space():
    data = b"P6 2\t1\r255\n" + bytes(range(6)) + b"trailing"
    assert parse_ppm(data).pixels == bytes(range(6))


@pytest.mark.parametrize(
    "data, text",
    [
        (b"P3\n1 1\n255\n\0\0\0", "not a binary PPM"),
        (b"P6\n2 2\n255\n\0\0\0", "cut short"),
        (b"P6\n1 1\n65535\n\0\0\0\0\0\0", "only 8-bit"),
        (b"P6\n0 4\n255\n", "no pixels"),
        (b"P6\n1", "ends inside its header"),
        (b"P6\nx y z\n", "not numbers"),
    ],
)
def test_parse_ppm_refuses_what_it_cannot_use(data, text):
    with pytest.raises(PpmError, match=text):
        parse_ppm(data)


def test_ppm_round_trip_through_a_file(tmp_path):
    pic = parse_ppm(ppm_bytes(4, 3))
    write_ppm(tmp_path / "a.ppm", pic)
    assert read_ppm(tmp_path / "a.ppm") == pic


def test_crop_and_enlarge():
    pic = parse_ppm(ppm_bytes(6, 4))
    part = crop(pic, 1, 1, 3, 3)
    assert (part.width, part.height) == (2, 2)
    assert part.pixel(0, 0) == pic.pixel(1, 1) and part.pixel(1, 1) == pic.pixel(2, 2)
    big = enlarge(part, 3)
    assert (big.width, big.height) == (6, 6)
    assert big.pixel(0, 0) == big.pixel(2, 2) == part.pixel(0, 0) and big.pixel(3, 3) == part.pixel(1, 1)
    assert enlarge(part, 1) is part
    for bad in [(0, 0, 7, 4), (3, 0, 3, 2), (-1, 0, 2, 2)]:
        with pytest.raises(PpmError):
            crop(pic, *bad)
    with pytest.raises(PpmError):
        enlarge(part, 0)


def test_a_picture_must_match_its_size():
    with pytest.raises(PpmError):
        Ppm(2, 2, b"\0" * 11)


def _chunks(png):
    assert png[:8] == b"\x89PNG\r\n\x1a\n"
    pos, out = 8, []
    while pos < len(png):
        n = struct.unpack(">I", png[pos : pos + 4])[0]
        kind, body = png[pos + 4 : pos + 8], png[pos + 8 : pos + 8 + n]
        assert struct.unpack(">I", png[pos + 8 + n : pos + 12 + n])[0] == zlib.crc32(kind + body) & 0xFFFFFFFF
        out.append((kind, body))
        pos += 12 + n
    return out


@pytest.mark.parametrize("compress", [True, False])
@pytest.mark.parametrize("channels", [3, 4])
def test_encode_png_decodes_to_the_pixels(compress, channels):
    w, h = 37, 11
    pixels = bytes((i * 31 + 7) & 255 for i in range(w * h * channels))
    chunks = _chunks(encode_png(w, h, pixels, channels=channels, compress=compress))
    assert [k for k, _ in chunks] == [b"IHDR", b"IDAT", b"IEND"]
    assert struct.unpack(">IIBBBBB", chunks[0][1]) == (w, h, 8, 2 if channels == 3 else 6, 0, 0, 0)
    raw = zlib.decompress(chunks[1][1])
    stride = w * channels
    assert len(raw) == (stride + 1) * h
    assert b"".join(raw[y * (stride + 1) + 1 : (y + 1) * (stride + 1)] for y in range(h)) == pixels
    assert all(raw[y * (stride + 1)] == 0 for y in range(h))


def test_stored_png_is_byte_for_byte_what_the_c_package_minimal_png_writes():
    # the 2x2 RGBA file pinned in minimal-png's own test (tests/mpng_test.c), made there by a separate writer
    rgba = bytes([255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 1, 2, 3, 4])
    golden = bytes.fromhex(
        "89504e470d0a1a0a0000000d494844520000000200000002080600000072b60d"
        "240000001d494441547801011200edff00ff0000ff00ff0080000000ff000102"
        "030435f504870212de3a0000000049454e44ae426082"
    )
    assert encode_png(2, 2, rgba, channels=4, compress=False) == golden


def test_stored_png_splits_into_blocks_of_65535_bytes():
    w, h = 1, 13108  # 5 bytes a row: 65540 bytes, one block of 65535 and one of 5
    png = encode_png(w, h, bytes(4 * h), channels=4, compress=False)
    z = _chunks(png)[1][1]
    assert z[:2] == b"\x78\x01"
    assert z[2] == 0 and struct.unpack("<HH", z[3:7]) == (65535, 0)
    second = 2 + 5 + 65535
    assert z[second] == 1 and struct.unpack("<HH", z[second + 1 : second + 5]) == (5, 0xFFFA)
    assert len(zlib.decompress(z)) == 65540


def test_encode_png_refuses_a_bad_shape():
    with pytest.raises(ProofError):
        encode_png(0, 4, b"")
    with pytest.raises(ProofError):
        encode_png(2, 2, b"\0" * 11)
    with pytest.raises(ProofError):
        encode_png(2, 2, b"\0" * 8, channels=2)


def test_ppm_to_png_writes_a_file_and_returns_the_picture(tmp_path):
    (tmp_path / "f.ppm").write_bytes(ppm_bytes(5, 4))
    pic = ppm_to_png(tmp_path / "f.ppm", tmp_path / "f.png")
    assert (pic.width, pic.height) == (5, 4)
    chunks = _chunks((tmp_path / "f.png").read_bytes())
    assert struct.unpack(">II", chunks[0][1][:8]) == (5, 4)
    assert md5_of(tmp_path / "f.ppm") == __import__("hashlib").md5(ppm_bytes(5, 4)).hexdigest()
