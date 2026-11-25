import math
import wave
import io

import pytest

import uppercut_chiptune_synth as u


def tone(n=1000, amp=0.5):
    return [amp * math.sin(i * 0.05) for i in range(n)]


def test_pcm16_little_and_big_endian_are_swaps():
    x = tone()
    little, big = u.pcm_bytes(x, 16, "little"), u.pcm_bytes(x, 16, "big")
    assert len(little) == 2 * len(x)
    assert bytes(b for i in range(0, len(little), 2) for b in (little[i + 1], little[i])) == big


def test_pcm8_is_unsigned_with_128_as_zero():
    data = u.pcm_bytes([0.0] * 200, 8, fade_edges=False)
    assert set(data) == {128}
    loud = u.pcm_bytes([1.0] * 200, 8, limit=1.0, fade_edges=False)
    assert set(loud) == {255}


def test_peak_limit_and_fade():
    x = [2.0] * 300
    out = u.prepare(x)
    assert max(out) == pytest.approx(0.78)
    assert out[0] == 0.0 and out[-1] == 0.0
    assert u.prepare([2.0] * 100)[0] == pytest.approx(0.78)  # 100 samples: no fade
    assert x == [2.0] * 300  # input untouched


def test_bits_and_byteorder_are_checked():
    with pytest.raises(ValueError):
        u.pcm_bytes([0.0], 24)
    with pytest.raises(ValueError):
        u.pcm_bytes([0.0], 16, "middle")


def test_wav_round_trip_and_header_check():
    data = u.wav_bytes(tone(), rate=11025, bits=16)
    info = u.inspect_wav(data)
    assert (info.channels, info.rate, info.bits, info.frames) == (1, 11025, 16, 1000)
    with wave.open(io.BytesIO(data)) as wf:
        assert wf.getframerate() == 11025 and wf.getnframes() == 1000
    eight = u.inspect_wav(u.wav_bytes(tone(), rate=8000, bits=8))
    assert eight.bits == 8 and eight.rate == 8000


@pytest.mark.parametrize(
    "mutate",
    [
        lambda b: b[:40],
        lambda b: b"RIFX" + b[4:],
        lambda b: b[:20] + b"\x03\x00" + b[22:],
        lambda b: b + b"\x00\x00",
        lambda b: b[:32] + b"\x04\x00" + b[34:],
    ],
)
def test_inspect_wav_rejects_bad_headers(mutate):
    with pytest.raises(ValueError):
        u.inspect_wav(mutate(u.wav_bytes(tone())))


def test_write_wav_returns_sha256_and_makes_folders(tmp_path):
    import hashlib

    path = tmp_path / "a" / "b" / "x.wav"
    digest = u.write_wav(str(path), tone())
    assert digest == hashlib.sha256(path.read_bytes()).hexdigest()


def test_log8_table_and_codes():
    m = u.LOG8_MAGNITUDES
    assert len(m) == 128 and m[0] == 0 and m[127] == 32767
    assert all(a < b for a, b in zip(m, m[1:]))
    codes = u.log8_encode([0, 32767, -32768, -1, 1, 100, -100])
    assert codes[0] == 0 and codes[1] == 127 and codes[2] == 255
    assert codes[5] & 127 == codes[6] & 127 and codes[6] & 128
    table = u.log8_decode_table()
    assert len(table) == 256 and table[128 + 5] == -table[5]


def test_log8_is_nearest_step():
    for sample in (0, 3, 50, 1000, 12345, 32000, 32767):
        code = u.log8_encode([sample])[0]
        best = min(range(128), key=lambda i: abs(u.LOG8_MAGNITUDES[i] - sample))
        assert abs(u.LOG8_MAGNITUDES[code] - sample) == abs(u.LOG8_MAGNITUDES[best] - sample)
