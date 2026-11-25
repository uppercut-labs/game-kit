import io

import pytest

from images import build_iso, directory, file, make_elf, ps2_disc, system_cnf
from uppercut_iso9660 import (
    IsoError,
    IsoImage,
    boot_elf_name,
    find_ps2_boot,
    parse_system_cnf,
    ps2_summary,
    serial_from_elf_name,
)


def open_image(data) -> IsoImage:
    return IsoImage(io.BytesIO(bytes(data)))


def test_finds_the_boot_elf_and_its_serial():
    elf = make_elf()
    image = open_image(ps2_disc(elf))
    boot = find_ps2_boot(image)
    assert boot.elf_name == "SLUS_218.47"
    assert boot.serial == "SLUS-21847"
    assert boot.entry.size == len(elf)
    assert image.read_file(boot.entry) == elf
    assert boot.system_cnf.boot2 == "cdrom0:\\SLUS_218.47;1"
    assert (boot.system_cnf.ver, boot.system_cnf.vmode) == ("1.00", "NTSC")


def test_names_without_the_version_suffix_are_found_too():
    """A disc as macOS hdiutil writes it has no ;1 on its names, and a normalized one has them."""
    for suffix in (True, False):
        image = open_image(ps2_disc(suffix=suffix))
        assert find_ps2_boot(image).entry.name == "SLUS_218.47"


def test_homebrew_names_have_no_serial():
    image = open_image(ps2_disc(elf_name="GAME.ELF"))
    boot = find_ps2_boot(image)
    assert boot.elf_name == "GAME.ELF" and boot.serial is None


def test_lowercase_cnf_text_and_file_names_are_accepted():
    cnf = b"boot2 = CDROM0:\\slus_218.47;1\nver = 1.00\n"
    data = build_iso([file("system.cnf;1", cnf), file("slus_218.47;1", make_elf())])
    assert find_ps2_boot(open_image(data)).serial == "SLUS-21847"


def test_expected_serial():
    image = open_image(ps2_disc())
    assert find_ps2_boot(image, expected_serial="SLUS-21847").serial == "SLUS-21847"
    with pytest.raises(IsoError, match="serial SLUS-21847 differs from expected SLPM-66965"):
        find_ps2_boot(image, expected_serial="SLPM-66965")
    with pytest.raises(IsoError, match="serial None differs"):
        find_ps2_boot(open_image(ps2_disc(elf_name="GAME.ELF")), expected_serial="SLUS-21847")


def test_the_elf_signature_is_checked_unless_asked_not_to():
    image = open_image(ps2_disc(b"MZ not an elf, just bytes"))
    with pytest.raises(IsoError, match="lacks an ELF signature"):
        find_ps2_boot(image)
    assert find_ps2_boot(image, check_elf=False).entry.size == 25
    with pytest.raises(IsoError, match="lacks an ELF signature"):
        find_ps2_boot(open_image(ps2_disc(b"\x7fEL")))


def test_missing_pieces():
    with pytest.raises(IsoError, match="SYSTEM.CNF was not found"):
        find_ps2_boot(open_image(build_iso([file("A.TXT;1", b"a")])))
    with pytest.raises(IsoError, match="boot executable SLUS_218.47 is absent"):
        find_ps2_boot(open_image(build_iso([file("SYSTEM.CNF;1", system_cnf())])))
    with pytest.raises(IsoError, match="no BOOT2|found 0 BOOT2"):
        find_ps2_boot(open_image(build_iso([file("SYSTEM.CNF;1", b"VER = 1.00\n")])))


def test_the_boot_file_must_be_in_the_root():
    data = build_iso([file("SYSTEM.CNF;1", system_cnf()), directory("SUB", file("SLUS_218.47;1", make_elf()))])
    with pytest.raises(IsoError, match="absent from the ISO root"):
        find_ps2_boot(open_image(data))


def test_a_boot_file_of_another_version_does_not_count():
    data = build_iso([file("SYSTEM.CNF;1", system_cnf()), file("SLUS_218.47;2", make_elf())])
    with pytest.raises(IsoError, match="absent from the ISO root"):
        find_ps2_boot(open_image(data))


def test_duplicate_files_in_the_root_are_refused():
    data = build_iso([file("SYSTEM.CNF;1", system_cnf()), file("SLUS_218.47;1", make_elf()), file("SLUS_218.47;1", make_elf())])
    with pytest.raises(IsoError, match="duplicate ISO root filename"):
        find_ps2_boot(open_image(data))


def test_two_boot_lines_are_refused():
    cnf = system_cnf() + b"BOOT2 = cdrom0:\\OTHER;1\r\n"
    data = build_iso([file("SYSTEM.CNF;1", cnf), file("SLUS_218.47;1", make_elf())])
    with pytest.raises(IsoError, match="exactly one root boot executable \\(found 2 BOOT2 lines\\)"):
        find_ps2_boot(open_image(data))


def test_an_oversized_system_cnf_is_refused():
    cnf = system_cnf(extra="# " + "x" * 70_000 + "\n")
    data = build_iso([file("SYSTEM.CNF;1", cnf), file("SLUS_218.47;1", make_elf())])
    with pytest.raises(IsoError, match="over the limit"):
        find_ps2_boot(open_image(data))


@pytest.mark.parametrize(
    "boot2",
    [
        "cdrom0:\\SLUS_218.47\\OTHER;1",  # a subdirectory
        "cdrom0:\\SLUS_218.47;2",  # another version
        "cdrom0:\\SLUS_218.47;1 extra",  # trailing text
        "cdrom0:SLUS_218.47;1",  # no backslash
        "cdrom:\\SLUS_218.47;1",  # the PS1 key's device
        "cdrom0:\\;1",
        "",
    ],
)
def test_boot_lines_that_are_not_a_root_executable(boot2):
    with pytest.raises(IsoError, match="root boot executable"):
        boot_elf_name(boot2)


def test_boot_line_with_surrounding_blanks():
    assert boot_elf_name("  cdrom0:\\slus_218.47;1\t ") == "SLUS_218.47"


def test_serials():
    assert serial_from_elf_name("SLUS_218.47") == "SLUS-21847"
    assert serial_from_elf_name("slpm_669.65") == "SLPM-66965"
    for name in ("GAME.ELF", "SLUS_21847", "SLUS_218.4", "SLU_218.47", "SLUS_218.47X"):
        assert serial_from_elf_name(name) is None


def test_system_cnf_parsing():
    cnf = parse_system_cnf(b"BOOT2 = cdrom0:\\A.ELF;1\r\n\r\n# comment\r\nVER=1.00\nvmode =   PAL  \nnot a line\n")
    assert cnf.entries == (("BOOT2", "cdrom0:\\A.ELF;1"), ("VER", "1.00"), ("VMODE", "PAL"))
    assert cnf.vmode == "PAL"
    assert cnf.get("missing") is None and cnf.get("missing", "x") == "x"
    assert parse_system_cnf(b"A = 1\rB = 2\r").entries == (("A", "1"), ("B", "2"))  # old Mac line ends


def test_duplicate_keys_are_an_error_on_get_only():
    cnf = parse_system_cnf(b"VER = 1\nVER = 2\n")
    assert cnf.values("ver") == ["1", "2"]
    with pytest.raises(IsoError, match="2 VER lines"):
        cnf.get("VER")


def test_summary_has_a_hash_of_the_boot_elf():
    import hashlib

    elf = make_elf(200)
    summary = ps2_summary(open_image(ps2_disc(elf)))
    assert summary == {
        "serial": "SLUS-21847",
        "boot_executable": "SLUS_218.47",
        "boot_elf_bytes": len(elf),
        "boot_elf_sha256": hashlib.sha256(elf).hexdigest(),
        "volume_id": "SYNTHETIC",
        "layout": "cooked-2048",
    }
