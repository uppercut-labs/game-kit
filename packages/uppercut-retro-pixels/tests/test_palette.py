import pytest

from uppercut_retro_pixels import pad_palette, palette_bytes, ramp


def test_ramp_endpoints_and_length():
    out = ramp([(0, 0, 0), (255, 255, 255)], 5)
    assert out[0] == (0, 0, 0)
    assert out[-1] == (255, 255, 255)
    assert len(out) == 5
    assert out == [(0, 0, 0), (64, 64, 64), (128, 128, 128), (191, 191, 191), (255, 255, 255)]


def test_ramp_passes_through_every_stop():
    stops = [(10, 20, 30), (200, 100, 0), (0, 255, 128)]
    out = ramp(stops, 5)
    assert out[0] == stops[0] and out[2] == stops[1] and out[4] == stops[2]


def test_ramp_is_monotone_between_two_stops():
    out = ramp([(0, 40, 255), (255, 40, 0)], 17)
    reds = [c[0] for c in out]
    blues = [c[2] for c in out]
    assert reds == sorted(reds)
    assert blues == sorted(blues, reverse=True)
    assert {c[1] for c in out} == {40}


def test_ramp_degenerate_cases():
    assert ramp([(1, 2, 3)], 3) == [(1, 2, 3)] * 3
    assert ramp([(1, 2, 3), (9, 9, 9)], 1) == [(1, 2, 3)]
    with pytest.raises(ValueError):
        ramp([], 2)
    with pytest.raises(ValueError):
        ramp([(0, 0, 0)], 0)
    with pytest.raises(ValueError):
        ramp([(0, 0, 256)], 2)


def test_pad_palette_fills_with_black_or_a_function():
    base = [(1, 2, 3), (4, 5, 6)]
    padded = pad_palette(base, 4)
    assert padded == [(1, 2, 3), (4, 5, 6), (0, 0, 0), (0, 0, 0)]
    custom = pad_palette(base, 4, fill=lambda i: (i, i * 2, i * 3))
    assert custom[2:] == [(2, 4, 6), (3, 6, 9)]
    assert len(pad_palette([], 256)) == 256
    with pytest.raises(ValueError):
        pad_palette(base, 1)


def test_palette_bytes_flattens_and_pads_with_zeros():
    out = palette_bytes([(1, 2, 3), (4, 5, 6)], size=4)
    assert out == bytes([1, 2, 3, 4, 5, 6] + [0] * 6)
    assert len(palette_bytes([(9, 9, 9)])) == 768
    with pytest.raises(ValueError):
        palette_bytes([(0, 0, 0)] * 3, size=2)
