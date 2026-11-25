"""Reading the addresses a MIPS function builds, from its instruction words.

A function that uses a global loads its address with `lui` and a 16-bit offset, or reaches it from `$gp`, or calls it with
`jal`. Compare the same function in two builds and each such instruction gives a pair of addresses: where the compiled
object points, and where the original does. The matching tools use those pairs to find where a piece of data sits.
"""

from __future__ import annotations

import struct
from typing import Dict, Iterator, List, Optional, Sequence, Tuple


def words(b: bytes) -> List[int]:
    """Little-endian 32-bit words of `b` (a trailing partial word is ignored)."""
    return list(struct.unpack("<%dI" % (len(b) // 4), b[: len(b) // 4 * 4]))


def simm(w: int) -> int:
    v = w & 0xffff
    return v - 0x10000 if v & 0x8000 else v


GPR_LOADS = {0x1e, 0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x37}
MEM = GPR_LOADS | {0x1f, 0x28, 0x29, 0x2b, 0x31, 0x39, 0x3f}
CLOBBERED = (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 31)   # at v0-v1 a0-a3 t0-t9 ra


def is_branch(w: int) -> bool:
    op = w >> 26
    return op in (0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17) or op == 0x01 or \
        (op in (0x10, 0x11) and (w >> 21) & 31 == 8)


def refs(words: Sequence[Optional[int]], base: int, gp: int) -> Iterator[Tuple[int, int, str]]:
    """Yield (index, address, kind) for each address the code builds.

    A linear scan, plus one control-flow case: a lui in a branch's delay slot
    also reaches the branch target (gcc puts it there with branch-likely)."""
    hi: Dict[int, int] = {}
    carry: Dict[int, Dict[int, int]] = {}
    clobber_after = -1
    for i, w in enumerate(words):
        if w is None:
            hi.clear()
            continue
        if i in carry:
            hi.update(carry.pop(i))
        op = w >> 26
        rs = (w >> 21) & 31
        rt = (w >> 16) & 31
        before = words[i - 1] if i > 0 else None
        if op == 0x0f and before is not None and is_branch(before):
            target = i + simm(before)          # (i-1) + 1 + offset
            if target > i:
                carry.setdefault(target, {})[rt] = (w & 0xffff) << 16
        if op == 3:
            yield i, ((base + 4 * i) & 0xf0000000) | ((w & 0x3ffffff) << 2), 'call'
            clobber_after = i + 1            # the call kills caller-saved registers,
                                             # after its delay slot has used them
        elif op == 2:                        # j: a tail call to another function (gcc
            yield i, ((base + 4 * i) & 0xf0000000) | ((w & 0x3ffffff) << 2), 'call'
                                             # sibcalls), so its target is solvable too
        elif op == 0x0f:
            hi[rt] = (w & 0xffff) << 16
        elif op in (0x09, 0x19) or op in MEM:
            if rs == 28:
                yield i, (gp + simm(w)) & 0xffffffff, 'gp'
            elif rs in hi:
                yield i, (hi[rs] + simm(w)) & 0xffffffff, 'lo'
            if op in (0x09, 0x19) or op in GPR_LOADS:
                hi.pop(rt, None)
        elif op == 0:
            hi.pop((w >> 11) & 31, None)
            if w & 0x3f == 0x09:             # jalr
                clobber_after = i + 1
        elif op in (0x08, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x18):
            hi.pop(rt, None)
        if i == clobber_after:
            for r in CLOBBERED:
                hi.pop(r, None)
