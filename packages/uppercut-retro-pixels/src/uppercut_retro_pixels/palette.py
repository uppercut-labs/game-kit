"""Palettes: colour ramps and fixed-size, flat-byte palettes."""

from __future__ import annotations

from typing import Callable, Iterable, Sequence

Color = tuple[int, int, int]


def ramp(stops: Sequence[Color], steps: int) -> list[Color]:
    """``steps`` colours evenly spaced along a path through ``stops``, ends included.

    Each channel is interpolated linearly and rounded to the nearest integer (halves go up). One stop gives that colour
    repeated; ``steps`` of 1 gives the first stop.
    """
    if not stops:
        raise ValueError("a ramp needs at least one stop")
    if steps < 1:
        raise ValueError("steps must be at least 1")
    for stop in stops:
        if len(stop) != 3 or any(not 0 <= c <= 255 for c in stop):
            raise ValueError(f"stop {stop!r} is not an (r, g, b) triple of 0..255")
    if steps == 1 or len(stops) == 1:
        return [tuple(stops[0])] * steps  # type: ignore[list-item]
    segments = len(stops) - 1
    out: list[Color] = []
    for i in range(steps):
        pos = i * segments / (steps - 1)
        seg = min(int(pos), segments - 1)
        # work in integers: the fraction is (i * segments - seg * (steps - 1)) / (steps - 1)
        num = i * segments - seg * (steps - 1)
        den = steps - 1
        a, b = stops[seg], stops[seg + 1]
        out.append(tuple((a[c] * (den - num) + b[c] * num + den // 2) // den for c in range(3)))  # type: ignore[arg-type]
    return out


def pad_palette(
    colors: Iterable[Color], size: int = 256, fill: Callable[[int], Color] | None = None
) -> list[Color]:
    """Return exactly ``size`` colours: ``colors`` first, then ``fill(index)`` for each missing index (black by default).

    More colours than ``size`` raise ``ValueError``.
    """
    out = [tuple(c) for c in colors]
    if len(out) > size:
        raise ValueError(f"{len(out)} colours do not fit a palette of {size}")
    for index in range(len(out), size):
        out.append(fill(index) if fill else (0, 0, 0))
    return out  # type: ignore[return-value]


def palette_bytes(colors: Iterable[Color], size: int = 256) -> bytes:
    """Flat ``r, g, b`` bytes for ``size`` entries, padded with zeros. More colours than ``size`` raise ``ValueError``."""
    flat = bytearray()
    count = 0
    for r, g, b in colors:
        flat += bytes((r, g, b))
        count += 1
    if count > size:
        raise ValueError(f"{count} colours do not fit a palette of {size}")
    flat += bytes(size * 3 - len(flat))
    return bytes(flat)
