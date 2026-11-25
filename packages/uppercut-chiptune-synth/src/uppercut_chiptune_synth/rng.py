"""Random sources for the two voicings.

The accumulated voicing draws from `random.Random`. The closed-form voicing was
written against NumPy's `default_rng`, so `NumpyCompatRng` reproduces that
generator's stream (SeedSequence seeding and PCG64 output, `random()` and
`uniform()`) in plain Python and the package needs no NumPy.
"""

from __future__ import annotations

import random
from typing import Protocol

_M32 = 0xFFFFFFFF
_M64 = (1 << 64) - 1
_M128 = (1 << 128) - 1
_PCG_MULT = 0x2360ED051FC65DA44385DF649FCCF645


class Rng(Protocol):
    def random(self) -> float: ...


def stdlib_rng(seed: int) -> random.Random:
    """The generator the stdlib copy used: `random.Random(seed)`."""
    return random.Random(seed)


def _hashmix(value: int, const: list[int], mult: int) -> int:
    value = (value ^ const[0]) & _M32
    const[0] = (const[0] * mult) & _M32
    value = (value * const[0]) & _M32
    return value ^ (value >> 16)


def _mix(x: int, y: int) -> int:
    result = (0xCA01F9DD * x - 0x4973F715 * y) & _M32
    return result ^ (result >> 16)


def _seed_words(seed: int) -> list[int]:
    if seed < 0:
        raise ValueError("seed must be a non-negative integer")
    words = []
    while seed:
        words.append(seed & _M32)
        seed >>= 32
    return words or [0]


def seed_sequence_state(seed: int) -> list[int]:
    """Four 64-bit words, as `SeedSequence(seed).generate_state(4, uint64)`."""
    entropy = _seed_words(seed)
    pool = [0, 0, 0, 0]
    const = [0x43B0D7E5]
    for i in range(4):
        pool[i] = _hashmix(entropy[i] if i < len(entropy) else 0, const, 0x931E8875)
    for src in range(4):
        for dst in range(4):
            if src != dst:
                pool[dst] = _mix(pool[dst], _hashmix(pool[src], const, 0x931E8875))
    for extra in entropy[4:]:
        for dst in range(4):
            pool[dst] = _mix(pool[dst], _hashmix(extra, const, 0x931E8875))
    const = [0x8B51F9DD]
    words = []
    for i in range(8):
        value = pool[i % 4] ^ const[0]
        const[0] = (const[0] * 0x58F38DED) & _M32
        value = (value * const[0]) & _M32
        words.append(value ^ (value >> 16))
    return [words[2 * i] | (words[2 * i + 1] << 32) for i in range(4)]


class NumpyCompatRng:
    """`numpy.random.default_rng(seed)` for integer seeds, without NumPy."""

    def __init__(self, seed: int) -> None:
        s = seed_sequence_state(seed)
        initstate = (s[0] << 64) | s[1]
        initseq = (s[2] << 64) | s[3]
        self._inc = ((initseq << 1) | 1) & _M128
        self._state = 0
        self._step()
        self._state = (self._state + initstate) & _M128
        self._step()

    def _step(self) -> None:
        self._state = (self._state * _PCG_MULT + self._inc) & _M128

    def next_u64(self) -> int:
        self._step()
        state = self._state
        value = ((state >> 64) ^ state) & _M64
        rot = state >> 122
        return ((value >> rot) | (value << ((-rot) & 63))) & _M64

    def random(self) -> float:
        return (self.next_u64() >> 11) * (1.0 / 9007199254740992.0)

    def uniform(self, low: float = 0.0, high: float = 1.0) -> float:
        return low + (high - low) * self.random()
