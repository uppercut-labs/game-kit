"""uppercut-iso-normalize rewrites the dates and names of a one-ELF PS2 disc. This package must read both the disc
before and the disc after, and must agree with it about what the boot ELF and SYSTEM.CNF are. The test runs where
both packages are installed and is skipped otherwise."""

import io

import pytest

from images import build_iso, file, system_cnf
from uppercut_iso9660 import IsoImage, find_ps2_boot

normalize = pytest.importorskip("uppercut_iso_normalize")


def mips_elf() -> bytes:
    header = bytearray(52)
    header[0:7] = b"\x7fELF\x01\x01\x01"
    header[16:20] = b"\x02\x00\x08\x00"
    return bytes(header) + bytes(range(64))


def test_the_same_boot_before_and_after_normalizing():
    elf = mips_elf()
    disc = build_iso([file("SYSTEM.CNF", system_cnf("GAME.ELF")), file("GAME.ELF", elf)], rock_ridge_names=True)

    before = find_ps2_boot(IsoImage(io.BytesIO(bytes(disc))))
    normalize.normalize_disc(disc, elf, elf_name="GAME.ELF")
    after_image = IsoImage(io.BytesIO(bytes(disc)))
    after = find_ps2_boot(after_image)

    assert before.elf_name == after.elf_name == "GAME.ELF"
    assert before.entry.version is None and after.entry.version == 1  # normalizing adds the ;1 suffix
    assert after.entry.identifier == "GAME.ELF;1"
    assert after_image.read_file(after.entry) == elf
    assert after.system_cnf.entries == before.system_cnf.entries


def test_the_system_cnf_it_writes_is_the_one_this_package_parses():
    cnf = normalize.ps2_system_cnf("SLUS_218.47", ver="1.00", vmode="PAL")
    from uppercut_iso9660 import boot_elf_name, parse_system_cnf

    values = parse_system_cnf(b"\r\n".join(cnf))
    assert boot_elf_name(values.boot2) == "SLUS_218.47"
    assert (values.ver, values.vmode) == ("1.00", "PAL")
