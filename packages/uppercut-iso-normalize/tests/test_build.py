import time
from io import BytesIO

import pycdlib
import pytest

from uppercut_iso_normalize import (
    IsoNormalizeError,
    build_disc,
    build_file,
    main,
    normalize_disc,
    normalize_file,
    ps2_system_cnf,
    system_cnf_bytes,
)
from uppercut_iso_normalize._normalize import LONG_DATE, PRIMARY, SECTOR_SIZE, SHORT_DATE
from iso_images import build_disc as build_hdiutil_disc
from iso_images import make_elf, read_root

ELF = make_elf(5000)
BOOT = b"BOOT2 = cdrom0:\\GAME.ELF;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n"


def pycdlib_disc(elf=ELF, name="GAME.ELF", volume="GAME", boot=BOOT):
    """The image the games' old packaging scripts made: pycdlib, interchange level 1, dates of the moment."""
    disc = pycdlib.PyCdlib()
    disc.new(interchange_level=1, vol_ident=volume, sys_ident="PLAYSTATION")
    disc.add_fp(BytesIO(boot), len(boot), iso_path="/SYSTEM.CNF;1")
    disc.add_fp(BytesIO(elf), len(elf), iso_path=f"/{name};1")
    output = BytesIO()
    disc.write_fp(output)
    disc.close()
    return output.getvalue()


def read_files(image):
    disc = pycdlib.PyCdlib()
    disc.open_fp(BytesIO(image))
    files = {}
    for child in disc.list_children(iso_path="/"):
        if child.is_file():
            name = child.file_identifier().decode()
            with disc.open_file_from_iso(iso_path="/" + name) as stream:
                files[name] = stream.read()
    volume = disc.pvd.volume_identifier.decode().strip()
    system = disc.pvd.system_identifier.decode().strip()
    disc.close()
    return files, volume, system


def root_dates(image):
    disc = pycdlib.PyCdlib()
    disc.open_fp(BytesIO(image))
    start = disc.pvd.root_directory_record().extent_location() * SECTOR_SIZE
    disc.close()
    dates, offset = [], start
    while image[offset]:
        dates.append(bytes(image[offset + 18 : offset + 25]))
        offset += image[offset]
    return dates


@pytest.fixture
def clock(monkeypatch):
    """Lets a test set the time pycdlib reads."""

    def at(seconds):
        monkeypatch.setattr(time, "time", lambda: seconds)

    return at


def test_the_disc_holds_system_cnf_and_the_elf():
    files, volume, system = read_files(build_disc(ELF, elf_name="GAME.ELF"))
    assert files == {"SYSTEM.CNF;1": BOOT, "GAME.ELF;1": ELF}
    assert (volume, system) == ("GAME", "PLAYSTATION")


def test_system_cnf_matches_the_old_packaging_scripts():
    expected = b"BOOT2 = cdrom0:\\GAME.ELF;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n"
    assert system_cnf_bytes(ps2_system_cnf("GAME.ELF")) == expected


def test_builds_at_different_times_are_the_same_bytes(clock):
    clock(1_700_000_000.0)
    raw_first, first = pycdlib_disc(), build_disc(ELF, elf_name="GAME.ELF")
    clock(1_800_000_000.0)
    raw_second, second = pycdlib_disc(), build_disc(ELF, elf_name="GAME.ELF")
    assert raw_first != raw_second
    assert first == second


def test_every_date_is_fixed():
    image = build_disc(ELF, elf_name="GAME.ELF")
    assert image[PRIMARY + 813 : PRIMARY + 881] == LONG_DATE * 2 + b"0" * 16 + b"\0" + LONG_DATE
    assert image[PRIMARY + 174 : PRIMARY + 181] == SHORT_DATE
    assert set(root_dates(image)) == {SHORT_DATE}


def test_only_dates_differ_from_the_pycdlib_image():
    raw = pycdlib_disc()
    built = build_disc(ELF, elf_name="GAME.ELF")
    assert len(raw) == len(built)
    image = bytearray(raw)
    normalize_disc(image, ELF, elf_name="GAME.ELF", versioned=True)
    assert bytes(image) == built


def test_volume_id_and_system_cnf_options():
    lines = ps2_system_cnf("DEMO.ELF", vmode="PAL")
    files, volume, _ = read_files(build_disc(ELF, elf_name="DEMO.ELF", volume_id="DEMO_DISC", system_cnf=lines))
    assert volume == "DEMO_DISC"
    assert files["SYSTEM.CNF;1"] == b"BOOT2 = cdrom0:\\DEMO.ELF;1\r\nVER = 1.00\r\nVMODE = PAL\r\n"


@pytest.mark.parametrize(
    "options, message",
    [
        ({"elf_name": "game.elf"}, "is not an ISO 9660 level 1 file name"),
        ({"elf_name": "GAME.ELF", "volume_id": "my game"}, "is not a volume identifier"),
        ({"elf_name": "GAME.ELF", "volume_id": "X" * 33}, "is not a volume identifier"),
    ],
)
def test_bad_names_are_refused(options, message):
    with pytest.raises(IsoNormalizeError, match=message):
        build_disc(ELF, **options)


def test_an_elf_that_is_not_mips_is_refused():
    with pytest.raises(IsoNormalizeError, match="32-bit little-endian MIPS"):
        build_disc(b"\x7fELF" + bytes(100), elf_name="GAME.ELF")


def test_the_disc_reads_back_with_uppercut_iso9660():
    iso9660 = pytest.importorskip("uppercut_iso9660")
    with iso9660.IsoImage(BytesIO(build_disc(ELF, elf_name="GAME.ELF"))) as disc:
        boot = iso9660.find_ps2_boot(disc)
        assert boot.elf_name == "GAME.ELF"
        assert disc.read_file(boot.entry.path) == ELF
        assert disc.read_file("SYSTEM.CNF") == BOOT


def test_build_file_replaces_the_image_only_when_complete(tmp_path):
    elf, iso = tmp_path / "game.elf", tmp_path / "game.iso"
    elf.write_bytes(ELF)
    build_file(elf, iso, elf_name="GAME.ELF")
    assert iso.read_bytes() == build_disc(ELF, elf_name="GAME.ELF")
    iso.write_bytes(b"earlier")
    elf.write_bytes(b"not an elf at all" * 10)
    with pytest.raises(IsoNormalizeError):
        build_file(elf, iso, elf_name="GAME.ELF")
    assert iso.read_bytes() == b"earlier"
    assert sorted(path.name for path in tmp_path.iterdir()) == ["game.elf", "game.iso"]


def test_versioned_normalize_keeps_names_and_refuses_unversioned_ones(tmp_path):
    image = bytearray(pycdlib_disc())
    normalize_disc(image, ELF, elf_name="GAME.ELF", versioned=True)
    assert read_files(bytes(image))[0].keys() == {"SYSTEM.CNF;1", "GAME.ELF;1"}
    hdiutil = build_hdiutil_disc(ELF)
    with pytest.raises(IsoNormalizeError, match="only SYSTEM.CNF;1 and GAME.ELF;1"):
        normalize_disc(hdiutil, ELF, elf_name="GAME.ELF", versioned=True)
    iso, elf = tmp_path / "raw.iso", tmp_path / "game.elf"
    iso.write_bytes(pycdlib_disc())
    elf.write_bytes(ELF)
    normalize_file(iso, elf, elf_name="GAME.ELF", versioned=True)
    assert iso.read_bytes() == build_disc(ELF, elf_name="GAME.ELF")


def test_hdiutil_images_keep_their_effective_date():
    image = build_hdiutil_disc(ELF)
    image[PRIMARY + 864 : PRIMARY + 881] = b"2026112412345600\x00"
    normalize_disc(image, ELF, elf_name="GAME.ELF")
    assert image[PRIMARY + 864 : PRIMARY + 881] == b"2026112412345600\x00"


def test_command_line_build(tmp_path, capsys):
    elf, iso = tmp_path / "demo.elf", tmp_path / "out.iso"
    elf.write_bytes(ELF)
    assert main(["build", str(elf), str(iso), "--elf-name", "DEMO.ELF", "--volume-id", "DEMO_DISC"]) == 0
    files, volume, _ = read_files(iso.read_bytes())
    assert volume == "DEMO_DISC" and files["DEMO.ELF;1"] == ELF
    assert main(["build", str(elf), str(iso), "--elf-name", "demo.elf"]) == 1
    assert "is not an ISO 9660 level 1 file name" in capsys.readouterr().err
    assert main(["build", str(tmp_path / "missing.elf"), str(iso), "--elf-name", "DEMO.ELF"]) == 2
    assert "missing.elf" in capsys.readouterr().err


def test_command_line_versioned_normalize(tmp_path):
    iso, elf = tmp_path / "raw.iso", tmp_path / "game.elf"
    iso.write_bytes(pycdlib_disc())
    elf.write_bytes(ELF)
    assert main([str(iso), str(elf), "--elf-name", "GAME.ELF", "--versioned"]) == 0
    assert iso.read_bytes() == build_disc(ELF, elf_name="GAME.ELF")
    # hdiutil-style images still take the original form and get their names suffixed.
    hdiutil = tmp_path / "mac.iso"
    hdiutil.write_bytes(bytes(build_hdiutil_disc(ELF)))
    assert main([str(hdiutil), str(elf), "--elf-name", "GAME.ELF"]) == 0
    assert [record["name"] for record in read_root(hdiutil.read_bytes())][2:] == [b"SYSTEM.CNF;1", b"GAME.ELF;1"]
