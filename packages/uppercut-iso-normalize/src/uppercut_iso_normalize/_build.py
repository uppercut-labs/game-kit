"""Build a PlayStation 2 data disc from one ELF with pycdlib, read it back, and normalize it."""

from __future__ import annotations

import os
import re
import secrets
from io import BytesIO
from pathlib import Path
from typing import Optional, Sequence

import pycdlib

from ._normalize import _ELF_NAME, IsoNormalizeError, normalize_disc, ps2_system_cnf

SYSTEM_IDENTIFIER = "PLAYSTATION"
_VOLUME_ID = re.compile(r"^[A-Z0-9_]{1,32}$")


def system_cnf_bytes(lines: Sequence[bytes]) -> bytes:
    """The SYSTEM.CNF file for these lines: each one ends in CR LF."""
    return b"".join(line + b"\r\n" for line in lines)


def _read_back(image: bytes, expected: dict[str, bytes]) -> None:
    disc = pycdlib.PyCdlib()
    try:
        disc.open_fp(BytesIO(image))
        for path, data in expected.items():
            with disc.open_file_from_iso(iso_path=path) as stream:
                if stream.read() != data:
                    raise IsoNormalizeError(f"disc read-back differs for {path}")
    except pycdlib.pycdlibexception.PyCdlibException as error:
        raise IsoNormalizeError(f"disc read-back failed: {error}") from error
    finally:
        disc.close()


def build_disc(
    elf: bytes,
    *,
    elf_name: str,
    volume_id: Optional[str] = None,
    system_cnf: Optional[Sequence[bytes]] = None,
) -> bytes:
    """The bytes of a PlayStation 2 data disc that boots ``elf``.

    The disc is an ISO 9660 interchange level 1 image with the system identifier ``PLAYSTATION`` and two files in
    its root: ``SYSTEM.CNF;1``, holding ``system_cnf`` (default ``ps2_system_cnf(elf_name)``) with CR LF line ends,
    and ``elf_name;1``. ``volume_id`` defaults to the ELF name without its extension.

    The image is read back with pycdlib, normalized with ``normalize_disc(..., versioned=True)`` (fixed dates, both
    files and SYSTEM.CNF checked), and read back again, so the same inputs always give the same bytes.

    Raises ``IsoNormalizeError`` for a bad name, an ELF that is not a 32-bit little-endian MIPS executable, or a
    disc that does not read back as written.
    """
    if not _ELF_NAME.match(elf_name):
        raise IsoNormalizeError(f"{elf_name!r} is not an ISO 9660 level 1 file name (8.3, capitals, digits and underscores)")
    volume = volume_id if volume_id is not None else elf_name.split(".")[0]
    if not _VOLUME_ID.match(volume):
        raise IsoNormalizeError(f"{volume!r} is not a volume identifier (up to 32 capitals, digits and underscores)")
    lines = list(system_cnf) if system_cnf is not None else ps2_system_cnf(elf_name)
    config = system_cnf_bytes(lines)
    elf = bytes(elf)
    elf_path = f"/{elf_name};1"

    disc = pycdlib.PyCdlib()
    output = BytesIO()
    try:
        disc.new(interchange_level=1, vol_ident=volume, sys_ident=SYSTEM_IDENTIFIER)
        disc.add_fp(BytesIO(config), len(config), iso_path="/SYSTEM.CNF;1")
        disc.add_fp(BytesIO(elf), len(elf), iso_path=elf_path)
        disc.write_fp(output)
    except pycdlib.pycdlibexception.PyCdlibException as error:
        raise IsoNormalizeError(f"pycdlib could not build the disc: {error}") from error
    finally:
        disc.close()

    expected = {elf_path: elf, "/SYSTEM.CNF;1": config}
    image = bytearray(output.getvalue())
    _read_back(bytes(image), expected)
    normalize_disc(image, elf, elf_name=elf_name, system_cnf=lines, versioned=True)
    _read_back(bytes(image), expected)
    return bytes(image)


def build_file(
    elf: "Path | str",
    iso: "Path | str",
    *,
    elf_name: str,
    volume_id: Optional[str] = None,
    system_cnf: Optional[Sequence[bytes]] = None,
) -> None:
    """Build the disc for the ELF file at ``elf`` and write it to ``iso``.

    The image goes to a temporary file next to ``iso`` and is renamed into place only once it is complete, so a
    failure leaves an earlier file at ``iso`` as it was. Raises ``IsoNormalizeError`` (see ``build_disc``) and
    ``OSError`` when a file cannot be read or written.
    """
    iso_path = Path(iso)
    image = build_disc(Path(elf).read_bytes(), elf_name=elf_name, volume_id=volume_id, system_cnf=system_cnf)
    temporary = iso_path.with_name(f".{iso_path.name}.{secrets.token_hex(4)}.part")
    try:
        with open(temporary, "xb") as stream:
            stream.write(image)
        os.replace(temporary, iso_path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
