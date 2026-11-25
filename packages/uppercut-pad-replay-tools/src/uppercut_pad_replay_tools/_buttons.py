"""Names for a port's button bits, for scenario scripts and logs. The table is the caller's: there is no default."""

from __future__ import annotations

from typing import Mapping


class ButtonTable:
    """`ButtonTable({"Start": 0x0008, "Cross": 0x4000})`. A name is one or more bits; matching ignores case."""

    def __init__(self, names: Mapping[str, int]) -> None:
        self._by_name: dict[str, int] = {}
        for name, mask in names.items():
            if not name or "+" in name or name.lower() == "none" or not name.strip() == name:
                raise ValueError(f"not a usable button name: {name!r}")
            if not isinstance(mask, int) or isinstance(mask, bool) or not 0 < mask <= 0xFFFF:
                raise ValueError(f"the mask of {name!r} must be a 16 bit number above 0, got {mask!r}")
            if name.lower() in self._by_name:
                raise ValueError(f"two names that differ only in case: {name!r}")
            self._by_name[name.lower()] = mask
        self._names = dict(names)

    def mask(self, text: str) -> int:
        """`"Left+Cross"` to a mask. `"none"` is 0. A `0x` number may stand in for a name. An unknown name is a KeyError."""
        total = 0
        for part in text.split("+"):
            word = part.strip()
            if word.lower() == "none":
                continue
            if word[:2].lower() == "0x":
                try:
                    value = int(word, 16)
                except ValueError:
                    raise KeyError(word) from None
                if value > 0xFFFF:
                    raise KeyError(word)
                total |= value
            elif word.lower() in self._by_name:
                total |= self._by_name[word.lower()]
            else:
                raise KeyError(word)
        return total

    def names(self, mask: int) -> str:
        """A mask to `"Left+Cross"` in table order, `"none"` for 0, and bits no name covers as `"0x"` and hex."""
        if mask == 0:
            return "none"
        parts: list[str] = []
        covered = 0
        for name, bits in self._names.items():
            if mask & bits == bits and covered & bits != bits:
                parts.append(name)
                covered |= bits
        rest = mask & ~covered
        if rest:
            parts.append(f"0x{rest:x}")
        return "+".join(parts)
