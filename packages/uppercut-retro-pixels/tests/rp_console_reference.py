"""The texture packers a console port shipped before these layouts moved into the package, kept unchanged
as oracles: the package must produce the same bytes."""


def reference_pack_rgba8_tiles(rgba, width, height):
    """Encode an RGBA byte buffer into GameCube 4x4 GX_TF_RGBA8 tiles."""
    if width % 4 or height % 4:
        raise ValueError(f"Texture dimensions {width}x{height} must be multiples of 4")
    out = bytearray(width * height * 4)
    offset = 0
    for ty in range(0, height, 4):
        for tx in range(0, width, 4):
            ar = offset
            gb = offset + 32
            for y in range(ty, ty + 4):
                row = (y * width + tx) * 4
                for x in range(4):
                    p = row + x * 4
                    out[ar] = rgba[p + 3]
                    out[ar + 1] = rgba[p]
                    out[gb] = rgba[p + 1]
                    out[gb + 1] = rgba[p + 2]
                    ar += 2
                    gb += 2
            offset += 64
    return out


def reference_downsample_box(rgba, width, height):
    """Average 2x2 RGBA blocks with rounding: how the port made each mip level."""
    nw, nh = width // 2, height // 2
    out = bytearray(nw * nh * 4)
    for y in range(nh):
        r0 = (2 * y) * width * 4
        r1 = (2 * y + 1) * width * 4
        dst = y * nw * 4
        for x in range(nw):
            p00 = r0 + x * 8
            p01 = p00 + 4
            p10 = r1 + x * 8
            p11 = p10 + 4
            for c in range(4):
                out[dst + c] = (rgba[p00 + c] + rgba[p01 + c] + rgba[p10 + c] + rgba[p11 + c] + 2) // 4
            dst += 4
    return out, nw, nh


def reference_swizzle_axis(length, other, vertical):
    offsets = []
    for value in range(length):
        offset = shift = 0
        bit = 1
        while bit < max(length, other):
            if vertical and bit < other:
                shift += 1
            if bit < length:
                offset |= bool(value & bit) << shift
                shift += 1
            if not vertical and bit < other:
                shift += 1
            bit <<= 1
        offsets.append(offset * 4)
    return offsets


def reference_nv2a_pack(linear, width, height):
    """The port's loop around ``reference_swizzle_axis`` for 32-bit BGRA pixels."""
    packed = bytearray(len(linear))
    xs = reference_swizzle_axis(width, height, False)
    ys = reference_swizzle_axis(height, width, True)
    for y, row in enumerate(ys):
        for x, column in enumerate(xs):
            start = (y * width + x) * 4
            offset = row | column
            packed[offset:offset + 4] = linear[start:start + 4]
    return packed
