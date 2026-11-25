"""Small vector helpers and the vertex-set builders shared by the solids."""

from __future__ import annotations

import itertools
import math

Vec = tuple

PHI = (1.0 + 5.0**0.5) / 2.0


def norm(v):
    m = math.sqrt(sum(c * c for c in v))
    if m < 1e-12:
        return (0.0, 1.0, 0.0)
    return tuple(c / m for c in v)


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def scale(v, s):
    return tuple(c * s for c in v)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def even_perms(v):
    a, b, c = v
    return [(a, b, c), (b, c, a), (c, a, b)]


def all_perms(v):
    return list(set(itertools.permutations(v)))


def signs(v, negative_zero=False):
    """All eight sign flips of `v`, without duplicates.

    A zero coordinate comes out as +0.0 unless `negative_zero` is true, in which case it
    keeps the sign the flip gave it (-0.0), which prints as `-0.00000`. Both behaviours
    existed in the source copies.
    """
    out = set()
    for s in itertools.product((-1, 1), repeat=3):
        if negative_zero:
            out.add(tuple(x * float(y) for x, y in zip(v, s)))
        else:
            out.add(tuple((x * y) if x * y != 0 else 0.0 for x, y in zip(v, s)))
    return list(out)


def dedupe(points, tol=1e-6):
    out = []
    for p in points:
        if not any(max(abs(p[i] - q[i]) for i in range(3)) < tol for q in out):
            out.append(p)
    return out
