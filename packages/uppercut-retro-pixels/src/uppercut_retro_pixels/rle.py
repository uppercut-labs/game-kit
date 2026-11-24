"""Run-length coding for framebuffer dumps: a run is a one-byte count and a value of one or two bytes."""

from __future__ import annotations

from typing import Iterable, Sequence

from .formats import Endian


def rle_runs(values: Sequence[int], *, max_run: int = 255) -> list[tuple[int, int]]:
    """Split ``values`` into ``(count, value)`` runs, none longer than ``max_run``."""
    if not 1 <= max_run <= 255:
        raise ValueError("max_run must be 1..255")
    runs: list[tuple[int, int]] = []
    for value in values:
        if runs and runs[-1][1] == value and runs[-1][0] < max_run:
            runs[-1] = (runs[-1][0] + 1, value)
        else:
            runs.append((1, value))
    return runs


def rle_pack(values: Sequence[int], *, value_bytes: int = 1, endian: Endian = "big", max_run: int = 255) -> bytes:
    """Encode ``values`` as repeated ``count, value`` records. ``value_bytes`` is 1 or 2."""
    _check_width(value_bytes)
    out = bytearray()
    for count, value in rle_runs(values, max_run=max_run):
        out.append(count)
        out += value.to_bytes(value_bytes, endian)
    return bytes(out)


def rle_unpack(
    data: bytes, *, value_bytes: int = 1, endian: Endian = "big", limit: int | None = None
) -> list[int]:
    """Decode ``count, value`` records into a list of values.

    A record cut off at the end of ``data`` is ignored. With ``limit`` the output stops at that many values, so a
    dump that carries extra runs past the end of the image is trimmed.
    """
    _check_width(value_bytes)
    step = 1 + value_bytes
    out: list[int] = []
    for i in range(0, len(data) - step + 1, step):
        if limit is not None and len(out) >= limit:
            break
        count = data[i]
        value = int.from_bytes(data[i + 1 : i + step], endian)
        if limit is not None:
            count = min(count, limit - len(out))
        out.extend([value] * count)
    return out


def rle_unpack_hex(text: str, *, value_bytes: int = 2, limit: int | None = None) -> list[int]:
    """Decode records written as hexadecimal text, two digits of count then ``2 * value_bytes`` digits of value,
    the form a console logs over a debug channel. Whitespace is ignored."""
    digits = "".join(text.split())
    if len(digits) % 2:
        digits = digits[:-1]
    return rle_unpack(bytes.fromhex(digits), value_bytes=value_bytes, endian="big", limit=limit)


def _check_width(value_bytes: int) -> None:
    if value_bytes not in (1, 2):
        raise ValueError("value_bytes must be 1 or 2")


def flatten(runs: Iterable[tuple[int, int]]) -> list[int]:
    """Expand ``(count, value)`` runs into the values they stand for."""
    out: list[int] = []
    for count, value in runs:
        out.extend([value] * count)
    return out
