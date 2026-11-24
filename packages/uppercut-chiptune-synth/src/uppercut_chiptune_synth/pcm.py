"""Quantising, raw PCM, WAV files and the PS2-style logarithmic 8-bit bank format."""

from __future__ import annotations

import array
import hashlib
import io
import os
import sys
import wave
from bisect import bisect_left
from dataclasses import dataclass
from typing import Iterable, Sequence

DEFAULT_LIMIT = 0.78
FADE_SAMPLES = 64


def prepare(samples: Sequence[float], limit: float = DEFAULT_LIMIT, fade_edges: bool = True) -> list[float]:
    """Edge fade then peak limit, the finishing step both copies share.

    With `fade_edges` and more than 128 samples, the first and last 64 samples are
    ramped linearly from 0 to 1. If the peak then exceeds `limit` the whole signal is
    scaled so the peak equals `limit`. The input is not changed.
    """
    x = list(samples)
    n = len(x)
    if fade_edges and n > 2 * FADE_SAMPLES:
        last = FADE_SAMPLES - 1.0
        for i in range(FADE_SAMPLES):
            w = i / last
            x[i] *= w
            x[n - 1 - i] *= w
    peak = max((abs(v) for v in x), default=0.0)
    if peak > limit:
        scale = limit / peak
        x = [v * scale for v in x]
    return x


def to_int16(samples: Sequence[float], limit: float = DEFAULT_LIMIT, fade_edges: bool = True) -> list[int]:
    out = []
    for v in prepare(samples, limit, fade_edges):
        iv = int(round(v * 32767.0))
        out.append(32767 if iv > 32767 else -32768 if iv < -32768 else iv)
    return out


def pcm_bytes(
    samples: Sequence[float],
    bits: int = 16,
    byteorder: str = "little",
    limit: float = DEFAULT_LIMIT,
    fade_edges: bool = True,
) -> bytes:
    """Raw mono PCM. 16-bit is signed; 8-bit is unsigned with 128 as zero, as WAV stores it."""
    if bits == 16:
        if byteorder not in ("little", "big"):
            raise ValueError("byteorder must be 'little' or 'big'")
        pcm = array.array("h", to_int16(samples, limit, fade_edges))
        if sys.byteorder != byteorder:
            pcm.byteswap()
        return pcm.tobytes()
    if bits == 8:
        out = bytearray()
        for v in prepare(samples, limit, fade_edges):
            iv = int(round(v * 127.0)) + 128
            out.append(255 if iv > 255 else 0 if iv < 0 else iv)
        return bytes(out)
    raise ValueError("bits must be 8 or 16")


def wav_bytes(
    samples: Sequence[float],
    rate: int = 22050,
    bits: int = 16,
    limit: float = DEFAULT_LIMIT,
    fade_edges: bool = True,
) -> bytes:
    """A mono PCM WAV file with the canonical 44-byte header."""
    data = pcm_bytes(samples, bits, "little", limit, fade_edges)
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(bits // 8)
        wf.setframerate(rate)
        wf.writeframes(data)
    return buf.getvalue()


def write_wav(
    path: str,
    samples: Sequence[float],
    rate: int = 22050,
    bits: int = 16,
    limit: float = DEFAULT_LIMIT,
    fade_edges: bool = True,
) -> str:
    """Writes the WAV (creating folders) and returns its SHA-256 as hex."""
    data = wav_bytes(samples, rate, bits, limit, fade_edges)
    folder = os.path.dirname(path)
    if folder:
        os.makedirs(folder, exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(data)
    return hashlib.sha256(data).hexdigest()


@dataclass(frozen=True)
class WavInfo:
    channels: int
    rate: int
    bits: int
    frames: int


def inspect_wav(data: bytes) -> WavInfo:
    """Check a canonical 44-byte PCM header (RIFF, WAVE, 16-byte fmt, data) and return its fields.

    Raises ValueError when the header is not exactly that layout or the sizes do not add up.
    """
    if len(data) < 44:
        raise ValueError("shorter than the 44-byte header")
    if data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE file")
    le = lambda a, b: int.from_bytes(data[a:b], "little")  # noqa: E731
    if data[12:16] != b"fmt " or le(16, 20) != 16:
        raise ValueError("fmt chunk is not the 16-byte PCM form at offset 12")
    if le(20, 22) != 1:
        raise ValueError("not integer PCM")
    channels, rate, byte_rate, align, bits = le(22, 24), le(24, 28), le(28, 32), le(32, 34), le(34, 36)
    if data[36:40] != b"data":
        raise ValueError("data chunk does not follow fmt at offset 36")
    size = le(40, 44)
    if channels < 1 or bits not in (8, 16, 24, 32):
        raise ValueError("unsupported channel count or bit depth")
    if align != channels * bits // 8 or byte_rate != rate * align:
        raise ValueError("block align or byte rate disagrees with the format")
    if le(4, 8) != 36 + size or len(data) != 44 + size or size % align:
        raise ValueError("chunk sizes disagree with the file length")
    return WavInfo(channels, rate, bits, size // align)


LOG8_MAGNITUDES = tuple(round(32767 * (256 ** (i / 127) - 1) / 255) for i in range(128))
"""The 128 magnitudes of the logarithmic 8-bit format: step i is 32767 * (256**(i/127) - 1) / 255."""

_log8_table: bytes | None = None


def _log8_quantize(sample: int) -> int:
    magnitude = min(abs(sample), 32767)
    target = min(bisect_left(LOG8_MAGNITUDES, magnitude), 127)
    if target > 0 and magnitude - LOG8_MAGNITUDES[target - 1] < LOG8_MAGNITUDES[target] - magnitude:
        target -= 1
    return target | (128 if sample < 0 else 0)


def log8_encode(pcm16: Iterable[int]) -> bytes:
    """16-bit samples to sign-magnitude logarithmic bytes (bit 7 is the sign, bits 0-6 the
    nearest `LOG8_MAGNITUDES` step). The 65,536-entry lookup table is built on first use."""
    global _log8_table
    if _log8_table is None:
        _log8_table = bytes(_log8_quantize(s) for s in range(-32768, 32768))
    table = _log8_table
    return bytes(table[s + 32768] for s in pcm16)


def log8_decode_table() -> list[int]:
    """The 256 signed 16-bit values a decoder needs: positive steps, then negated steps."""
    return [*LOG8_MAGNITUDES, *(-m for m in LOG8_MAGNITUDES)]
