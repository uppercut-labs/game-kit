"""Run mpng-write for a few shapes and decode each file with zlib (the real inflater), as a second opinion on the C test's
own stored-block reader.  Usage: check_with_zlib.py <mpng-write> <scratch dir>"""
import struct
import subprocess
import sys
import zlib
from pathlib import Path


def chunks(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "signature"
    pos, out = 8, []
    while pos < len(data):
        n = struct.unpack(">I", data[pos:pos + 4])[0]
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        crc = struct.unpack(">I", data[pos + 8 + n:pos + 12 + n])[0]
        assert crc == zlib.crc32(kind + body) & 0xFFFFFFFF, f"{kind!r} crc"
        out.append((kind, body))
        pos += 12 + n
    assert out[0][0] == b"IHDR" and out[-1] == (b"IEND", b"")
    return out


def pixel(x, y, channels):
    p = [(x * 7 + y * 3) & 255, (x ^ y) & 255, (x * y) & 255]
    if channels == 4:
        p.append((255 - x - y) & 255)
    return p


def main():
    exe, scratch = sys.argv[1], Path(sys.argv[2])
    for out, inp, w, h in [("rgb", "rgba", 33, 17), ("rgba", "rgba", 33, 17), ("rgb", "rgb", 70, 40), ("rgba", "rgb", 70, 40),
                           ("rgba", "rgba", 400, 300), ("rgb", "rgb", 1, 1)]:
        path = scratch / f"zlib-{out}-{inp}-{w}x{h}.png"
        subprocess.run([exe, out, inp, str(w), str(h), str(path)], check=True)
        parts = chunks(path.read_bytes())
        width, height, depth, ctype, comp, filt, inter = struct.unpack(">IIBBBBB", parts[0][1])
        oc = 3 if out == "rgb" else 4
        ic = 3 if inp == "rgb" else 4
        assert (width, height, depth, ctype, comp, filt, inter) == (w, h, 8, 2 if oc == 3 else 6, 0, 0, 0)
        raw = zlib.decompress(b"".join(body for kind, body in parts if kind == b"IDAT"))
        assert len(raw) == (w * oc + 1) * h
        for y in range(h):
            row = raw[y * (w * oc + 1):(y + 1) * (w * oc + 1)]
            assert row[0] == 0
            for x in range(w):
                want = pixel(x, y, ic)[:3] + ([pixel(x, y, ic)[3] if ic == 4 else 255] if oc == 4 else [])
                assert list(row[1 + x * oc:1 + (x + 1) * oc]) == want, (path.name, x, y)
    print("zlib reads every file")


main()
