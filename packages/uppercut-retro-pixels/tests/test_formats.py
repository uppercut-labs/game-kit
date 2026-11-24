import pytest

from synthetic import all_levels_rgba, gradient_rgba
from uppercut_retro_pixels import (
    ABGR1555,
    ABGR4444,
    ARGB1555,
    ARGB4444,
    BGR555,
    BGR565,
    FORMATS,
    RGB565,
    RGBA5551,
    decode,
    encode,
    encode_unit,
    pack,
    swap_rb,
    unpack,
)

LEVELS = range(256)


def ref_rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def ref_argb4444(a, r, g, b):
    return ((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)


def ref_argb1555(a, r, g, b):
    return ((a >> 7) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)


def ref_rgba5551(r, g, b, a):
    return ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (a >> 7)


def ref_bgr555(r, g, b):
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def test_truncating_encoders_match_plain_shift_arithmetic():
    for i, (r, g, b, a) in enumerate(zip(LEVELS, reversed(LEVELS), (v * 7 % 256 for v in LEVELS), LEVELS)):
        assert encode(RGB565, r, g, b) == ref_rgb565(r, g, b), i
        assert encode(ARGB4444, r, g, b, a) == ref_argb4444(a, r, g, b), i
        assert encode(ARGB1555, r, g, b, a) == ref_argb1555(a, r, g, b), i
        assert encode(RGBA5551, r, g, b, a) == ref_rgba5551(r, g, b, a), i
        assert encode(BGR555, r, g, b, a) == ref_bgr555(r, g, b), i


def test_known_values():
    assert encode(RGB565, 255, 255, 255) == 0xFFFF
    assert encode(RGB565, 255, 0, 0) == 0xF800
    assert encode(RGB565, 0, 255, 0) == 0x07E0
    assert encode(RGB565, 0, 0, 255) == 0x001F
    assert encode(ARGB4444, 255, 255, 255, 255) == 0xFFFF
    assert encode(ARGB4444, 0, 0, 0, 0) == 0
    assert encode(ARGB1555, 255, 255, 255, 128) == 0xFFFF
    assert encode(ARGB1555, 255, 255, 255, 127) == 0x7FFF
    assert encode(RGBA5551, 255, 0, 0, 255) == 0xF801
    assert encode(BGR555, 255, 0, 0) == 0x001F
    assert encode(BGR555, 0, 0, 255) == 0x7C00


def test_nearest_rounding_keeps_full_scale_and_picks_the_closest_level():
    for fmt in (RGB565, ARGB4444, ARGB1555, RGBA5551, BGR555):
        assert decode(fmt, encode(fmt, 255, 255, 255, 255, rounding="nearest")) == (255, 255, 255, 255)
        assert decode(fmt, encode(fmt, 0, 0, 0, 0, rounding="nearest"))[:3] == (0, 0, 0)
    # 5-bit levels are 255 / 31 apart; 4 -> level 0 (0.49 of a step), 5 -> level 1 (0.61 of a step)
    assert encode(RGB565, 4, 0, 0, rounding="nearest") >> 11 == 0
    assert encode(RGB565, 5, 0, 0, rounding="nearest") >> 11 == 1
    # a 1-bit alpha switches on at 128 under both modes
    assert encode(ARGB1555, 0, 0, 0, 127, rounding="nearest") >> 15 == 0
    assert encode(ARGB1555, 0, 0, 0, 128, rounding="nearest") >> 15 == 1


def test_nearest_matches_float_rounding_for_every_level():
    for bits in (4, 5, 6):
        top = (1 << bits) - 1
        for v in LEVELS:
            by_int = (v * top * 2 + 255) // 510
            assert by_int == int(round(v / 255 * top)), (bits, v)
    for v in LEVELS:
        assert encode(RGB565, v, v, v, rounding="nearest") == encode_unit(RGB565, v / 255, v / 255, v / 255)


def test_encode_unit_clamps_rounds_and_masks():
    assert encode_unit(RGB565, 2.0, -1.0, 0.5) == (31 << 11) | 0 | round(0.5 * 31)
    assert encode_unit(ARGB4444, 1.0, 1.0, 1.0, 1.0) == 0xFFFF
    assert encode_unit(ARGB4444, 0.92, 0.04, 0.05, 0.08) == (
        (round(0.08 * 15) << 12) | (round(0.92 * 15) << 8) | (round(0.04 * 15) << 4) | round(0.05 * 15)
    )
    # alpha first in the argument list of the unit form too: (r, g, b, a)
    assert encode_unit(ARGB4444, 1.0, 0.0, 0.0, 0.0) == 0x0F00


def test_decode_replicate_and_scale_agree_at_the_ends_and_differ_between():
    assert decode(RGB565, 0xFFFF) == (255, 255, 255, 255)
    assert decode(RGB565, 0) == (0, 0, 0, 255)
    assert decode(RGB565, 0xFFFF, expand="scale") == (255, 255, 255, 255)
    # 5-bit level 16: replicate 16 << 3 | 16 >> 2 = 132, scale 16 * 255 // 31 = 131
    assert decode(RGB565, 16 << 11)[0] == 132
    assert decode(RGB565, 16 << 11, expand="scale")[0] == 131
    # 4-bit: replicate gives v * 17 exactly
    for v in range(16):
        assert decode(ARGB4444, v << 8)[0] == v * 17
    # 1-bit alpha is 0 or 255
    assert decode(ARGB1555, 0x8000)[3] == 255
    assert decode(ARGB1555, 0x0000)[3] == 0
    assert decode(RGBA5551, 1)[3] == 255


def test_every_value_survives_a_decode_and_re_encode():
    for fmt in FORMATS.values():
        keep = sum(((1 << b) - 1) << s for s, b in fmt.fields.values())
        for value in range(0x10000):
            if value & ~keep:
                continue
            r, g, b, a = decode(fmt, value)
            assert encode(fmt, r, g, b, a) == value, (fmt.name, value)


def test_swap_rb_matches_a_hand_written_swap_for_every_value():
    for value in range(0x10000):
        # 565: red is bits 11-15, blue bits 0-4
        expected_565 = (value & 0x07E0) | ((value & 0xF800) >> 11) | ((value & 0x001F) << 11)
        assert swap_rb(RGB565, value) == expected_565
        # 4444: red is bits 8-11, blue bits 0-3, alpha and green stay
        expected_4444 = (value & 0xF0F0) | ((value & 0x0F00) >> 8) | ((value & 0x000F) << 8)
        assert swap_rb(ARGB4444, value) == expected_4444
        assert swap_rb(RGB565, swap_rb(RGB565, value)) == value


def test_swap_rb_turns_a_format_into_its_mirror():
    for a_fmt, b_fmt in ((RGB565, BGR565), (ARGB4444, ABGR4444), (ARGB1555, ABGR1555)):
        for r, g, b, a in ((255, 0, 0, 255), (10, 200, 90, 255), (0, 0, 255, 0), (64, 128, 192, 255)):
            assert swap_rb(a_fmt, encode(a_fmt, r, g, b, a)) == encode(b_fmt, r, g, b, a)


def test_swap_rb_rejects_unequal_widths():
    from uppercut_retro_pixels import PixelFormat

    odd = PixelFormat("ODD", {"r": (11, 5), "g": (5, 6), "b": (0, 4)})
    with pytest.raises(ValueError):
        swap_rb(odd, 1)


def test_bad_input_is_rejected():
    with pytest.raises(ValueError):
        encode(RGB565, 256, 0, 0)
    with pytest.raises(ValueError):
        encode(RGB565, 0, 0, 0, rounding="up")  # type: ignore[arg-type]
    with pytest.raises(ValueError):
        decode(RGB565, 0x10000)
    with pytest.raises(ValueError):
        decode(RGB565, 0, expand="stretch")  # type: ignore[arg-type]
    with pytest.raises(ValueError):
        pack(RGB565, b"\x00\x00\x00")
    with pytest.raises(ValueError):
        unpack(RGB565, b"\x00")


def test_bulk_pack_matches_per_pixel_encode_in_both_byte_orders():
    rgba = gradient_rgba(16, 8)
    for fmt in (RGB565, ARGB4444, ARGB1555, RGBA5551, BGR555):
        for endian in ("little", "big"):
            packed = pack(fmt, rgba, endian=endian)
            assert len(packed) == 16 * 8 * 2
            for i in range(0, len(rgba), 4):
                value = encode(fmt, *rgba[i : i + 4])
                assert packed[i // 2 : i // 2 + 2] == value.to_bytes(2, endian)


def test_bulk_unpack_round_trips_pack():
    rgba = all_levels_rgba()
    for fmt in (RGB565, ARGB4444, ARGB1555, RGBA5551, BGR555):
        packed = pack(fmt, rgba, rounding="nearest")
        again = pack(fmt, unpack(fmt, packed), rounding="nearest")
        assert again == packed, fmt.name
