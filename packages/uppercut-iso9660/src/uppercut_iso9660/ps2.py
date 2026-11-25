"""PlayStation 2 discs: ``SYSTEM.CNF``, the ``BOOT2`` line and the boot ELF it names."""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Mapping, Optional

from .errors import IsoError
from .image import DirectoryEntry, IsoImage

SYSTEM_CNF = "SYSTEM.CNF"
SYSTEM_CNF_LIMIT = 64 * 1024
ELF_MAGIC = b"\x7fELF"

_LINE = re.compile(r"^[ \t]*([A-Za-z0-9_]+)[ \t]*=[ \t]*(.*?)[ \t]*$")
_BOOT2 = re.compile(r"(?i)cdrom0:\\([a-z0-9_.]+);1")
_SERIAL = re.compile(r"([A-Z]{4})_(\d{3})\.(\d{2})")


@dataclass(frozen=True)
class SystemCnf:
    """The ``KEY = value`` lines of a ``SYSTEM.CNF``, keys in capitals, in file order."""

    entries: tuple[tuple[str, str], ...]

    def values(self, key: str) -> list[str]:
        return [value for name, value in self.entries if name == key.upper()]

    def get(self, key: str, default: Optional[str] = None) -> Optional[str]:
        """The value of ``key``, ``default`` when it is absent. Raises ``IsoError`` when the key appears twice."""
        found = self.values(key)
        if len(found) > 1:
            raise IsoError(f"SYSTEM.CNF has {len(found)} {key.upper()} lines, expected one")
        return found[0] if found else default

    @property
    def boot2(self) -> Optional[str]:
        return self.get("BOOT2")

    @property
    def ver(self) -> Optional[str]:
        return self.get("VER")

    @property
    def vmode(self) -> Optional[str]:
        return self.get("VMODE")


def parse_system_cnf(data: bytes) -> SystemCnf:
    """Parse ``SYSTEM.CNF`` text (CRLF or LF). Lines that are not ``KEY = value`` are ignored."""
    entries = []
    for line in data.decode("latin-1").replace("\r\n", "\n").replace("\r", "\n").split("\n"):
        match = _LINE.match(line)
        if match:
            entries.append((match.group(1).upper(), match.group(2)))
    return SystemCnf(tuple(entries))


def boot_elf_name(boot2: str) -> str:
    """The file name in ``BOOT2 = cdrom0:\\NAME;1``, in capitals. The file must be in the root and version 1: a
    subdirectory, another version or trailing text is refused."""
    match = _BOOT2.fullmatch(boot2.strip(" \t"))
    if not match:
        raise IsoError("SYSTEM.CNF does not identify a root boot executable (expected cdrom0:\\NAME;1)")
    return match.group(1).upper()


def serial_from_elf_name(name: str) -> Optional[str]:
    """``SLUS_218.47`` becomes ``SLUS-21847``. ``None`` when the name is not in the retail serial form."""
    match = _SERIAL.fullmatch(name.upper())
    return "{}-{}{}".format(*match.groups()) if match else None


@dataclass(frozen=True)
class Ps2Boot:
    system_cnf: SystemCnf
    elf_name: str
    """The boot file's name as ``SYSTEM.CNF`` gives it, in capitals (``SLUS_218.47``)."""
    serial: Optional[str]
    """``SLUS-21847`` when the name has the retail serial form, else ``None`` (homebrew names its ELF anything)."""
    entry: DirectoryEntry


def find_ps2_boot(image: IsoImage, *, check_elf: bool = True, expected_serial: Optional[str] = None) -> Ps2Boot:
    """Find ``SYSTEM.CNF`` in the root, read its single ``BOOT2`` line and locate the boot file in the root.

    With ``check_elf`` (the default) the file must start with the ELF signature. With ``expected_serial`` (for
    example ``"SLUS-21847"``) the boot file's name must give that serial. Raises ``IsoError`` for each failure.
    """
    cnf_entry = _root_file(image, SYSTEM_CNF)
    if cnf_entry is None:
        raise IsoError("PS2 SYSTEM.CNF was not found in the ISO root")
    cnf = parse_system_cnf(image.read_file(cnf_entry, limit=SYSTEM_CNF_LIMIT))
    boot2_lines = cnf.values("BOOT2")
    if len(boot2_lines) != 1:
        raise IsoError(f"SYSTEM.CNF must identify exactly one root boot executable (found {len(boot2_lines)} BOOT2 lines)")
    name = boot_elf_name(boot2_lines[0])
    serial = serial_from_elf_name(name)
    if expected_serial is not None and serial != expected_serial:
        raise IsoError(f"disc serial {serial} differs from expected {expected_serial}")
    entry = _root_file(image, name, version=1)
    if entry is None:
        raise IsoError(f"SYSTEM.CNF boot executable {name} is absent from the ISO root")
    if check_elf and (entry.size < 4 or image.read_extent(entry, 0, 4) != ELF_MAGIC):
        raise IsoError("boot executable lacks an ELF signature")
    return Ps2Boot(cnf, name, serial, entry)


def _root_file(image: IsoImage, name: str, *, version: Optional[int] = None) -> Optional[DirectoryEntry]:
    """A file in the root by name (any case). A name with no version on disc matches version 1 and any other
    request for it; a ``version`` that is on disc must equal the one asked for."""
    found = None
    for entry in image.listdir():
        if entry.is_dir or entry.name.upper() != name.upper():
            continue
        if version is not None and entry.version is not None and entry.version != version:
            continue
        if found is not None:
            raise IsoError(f"duplicate ISO root filename {name}")
        found = entry
    return found


def ps2_summary(image: IsoImage) -> Mapping[str, object]:
    """A plain dictionary describing the boot: serial, boot file, its size and SHA-256 (read in pieces)."""
    boot = find_ps2_boot(image)
    return {
        "serial": boot.serial,
        "boot_executable": boot.elf_name,
        "boot_elf_bytes": boot.entry.size,
        "boot_elf_sha256": image.sha256(boot.entry),
        "volume_id": image.volume_id,
        "layout": image.layout.name,
    }
