import random

import pytest
from rp_console_reference import (
    reference_downsample_box,
    reference_nv2a_pack,
    reference_pack_rgba8_tiles,
    reference_swizzle_axis,
)
from rp_synthetic import gradient_rgba

from uppercut_retro_pixels import (
    morton_index,
    nv2a_index,
    nv2a_swizzle,
    nv2a_unswizzle,
    pack_gx_rgba8,
    unpack_gx_rgba8,
)


def noise_rgba(width, height, seed):
    return random.Random(seed).randbytes(width * height * 4)


def images(width, height):
    yield gradient_rgba(width, height)
    yield noise_rgba(width, height, width * 1000 + height)


# GameCube GX_TF_RGBA8


@pytest.mark.parametrize("width,height,levels", [(128, 128, 3), (512, 256, 1), (64, 32, 4), (4, 4, 1)])
def test_gx_rgba8_matches_the_reference_on_every_mip_level(width, height, levels):
    for rgba in images(width, height):
        w, h = width, height
        for level in range(levels):
            packed = pack_gx_rgba8(rgba, w, h)
            assert packed == reference_pack_rgba8_tiles(rgba, w, h), (w, h, level)
            assert unpack_gx_rgba8(packed, w, h) == bytes(rgba)
            if level + 1 < levels:
                rgba, w, h = reference_downsample_box(rgba, w, h)


def test_gx_rgba8_tile_layout():
    # Pixel i of a 4x4 image is (r, g, b, a) = (i, 16 + i, 32 + i, 48 + i).
    rgba = bytes(c for i in range(16) for c in (i, 16 + i, 32 + i, 48 + i))
    packed = pack_gx_rgba8(rgba, 4, 4)
    assert packed[:32] == bytes(c for i in range(16) for c in (48 + i, i))
    assert packed[32:] == bytes(c for i in range(16) for c in (16 + i, 32 + i))


def test_gx_rgba8_tiles_run_left_to_right_then_down():
    width, height = 8, 8
    rgba = bytes(c for y in range(height) for x in range(width) for c in (x, y, 0, 255))
    packed = pack_gx_rgba8(rgba, width, height)
    # Tile t starts at t * 64; its first alpha-red pair is the tile's top-left pixel.
    for tile, (x, y) in enumerate([(0, 0), (4, 0), (0, 4), (4, 4)]):
        assert packed[tile * 64 : tile * 64 + 2] == bytes((255, x))
        assert packed[tile * 64 + 32 : tile * 64 + 34] == bytes((y, 0))


@pytest.mark.parametrize("width,height,length", [(6, 4, 96), (4, 6, 96), (4, 4, 63), (0, 4, 0)])
def test_gx_rgba8_rejects_bad_sizes(width, height, length):
    with pytest.raises(ValueError):
        pack_gx_rgba8(bytes(length), width, height)
    with pytest.raises(ValueError):
        unpack_gx_rgba8(bytes(length), width, height)


# Original Xbox NV2A swizzle


@pytest.mark.parametrize("width,height", [(128, 128), (512, 256), (256, 512), (64, 16), (1, 8), (8, 1), (1, 1)])
def test_nv2a_matches_the_reference(width, height):
    for linear in images(width, height):
        packed = nv2a_swizzle(linear, width, height)
        assert packed == reference_nv2a_pack(linear, width, height)
        assert nv2a_unswizzle(packed, width, height) == linear


@pytest.mark.parametrize("width,height", [(128, 128), (512, 256), (16, 64)])
def test_nv2a_index_agrees_with_the_reference_axes(width, height):
    xs = reference_swizzle_axis(width, height, False)
    ys = reference_swizzle_axis(height, width, True)
    seen = set()
    for y in range(height):
        for x in range(width):
            index = nv2a_index(x, y, width, height)
            assert index * 4 == xs[x] | ys[y]
            seen.add(index)
    assert seen == set(range(width * height))


def test_nv2a_square_is_morton_order_with_x_lowest():
    for y in range(32):
        for x in range(32):
            # morton_index puts its first argument in the odd bits; NV2A puts y there.
            assert nv2a_index(x, y, 32, 32) == morton_index(y, x, 5)


def test_nv2a_rectangle_puts_the_long_side_bits_on_top():
    # 8 wide, 2 high: x0 y0 x1 x2.
    assert [nv2a_index(x, 0, 8, 2) for x in range(8)] == [0, 1, 4, 5, 8, 9, 12, 13]
    assert nv2a_index(0, 1, 8, 2) == 2


@pytest.mark.parametrize("size", [1, 2, 3])
def test_nv2a_other_pixel_sizes(size):
    data = noise_rgba(16, 8, size)[: 16 * 8 * size]
    packed = nv2a_swizzle(data, 16, 8, bytes_per_pixel=size)
    for y in range(8):
        for x in range(16):
            start = (y * 16 + x) * size
            index = nv2a_index(x, y, 16, 8) * size
            assert packed[index : index + size] == data[start : start + size]
    assert nv2a_unswizzle(packed, 16, 8, bytes_per_pixel=size) == data


@pytest.mark.parametrize("width,height,length", [(12, 8, 384), (8, 8, 255), (0, 8, 0)])
def test_nv2a_rejects_bad_sizes(width, height, length):
    with pytest.raises(ValueError):
        nv2a_swizzle(bytes(length), width, height)
    with pytest.raises(ValueError):
        nv2a_unswizzle(bytes(length), width, height)


@pytest.mark.parametrize("width,height", [(12, 8), (8, 0), (8, 6)])
def test_nv2a_index_rejects_sizes_that_are_not_powers_of_two(width, height):
    with pytest.raises(ValueError):
        nv2a_index(0, 0, width, height)


def test_nv2a_index_rejects_outside_coordinates():
    with pytest.raises(ValueError):
        nv2a_index(8, 0, 8, 8)
    with pytest.raises(ValueError):
        nv2a_index(0, -1, 8, 8)
