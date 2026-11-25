import subprocess
import sys

from uppercut_iso_normalize import main, normalize_file, IsoNormalizeError
from iso_images import DATE_B, LONG_B, build_disc, build_iso, make_elf, read_root, system_cnf

import pytest

ELF = make_elf()


@pytest.fixture
def files(tmp_path):
    iso = tmp_path / "game.iso"
    elf = tmp_path / "game.elf"
    iso.write_bytes(bytes(build_disc(ELF)))
    elf.write_bytes(ELF)
    return iso, elf


def test_normalizes_the_file_in_place_and_exits_0(files):
    iso, elf = files
    assert main([str(iso), str(elf), "--elf-name", "GAME.ELF"]) == 0
    assert [record["name"] for record in read_root(iso.read_bytes())][2:] == [b"SYSTEM.CNF;1", b"GAME.ELF;1"]


def test_two_images_that_differ_only_in_time_become_the_same_file(tmp_path, files):
    first, elf = files
    second = tmp_path / "other.iso"
    second.write_bytes(bytes(build_disc(ELF, dates=(DATE_B, LONG_B))))
    assert first.read_bytes() != second.read_bytes()
    for path in (first, second):
        assert main([str(path), str(elf), "--elf-name", "GAME.ELF"]) == 0
    assert first.read_bytes() == second.read_bytes()


def test_a_mismatch_exits_1_names_the_problem_and_leaves_the_file_alone(files, capsys):
    iso, elf = files
    other = make_elf(8, fill=0x01)
    elf.write_bytes(other)
    before = iso.read_bytes()
    assert main([str(iso), str(elf), "--elf-name", "GAME.ELF"]) == 1
    assert iso.read_bytes() == before
    assert "disc ELF differs from the input ELF" in capsys.readouterr().err


def test_a_missing_file_exits_2(files, tmp_path, capsys):
    iso, elf = files
    assert main([str(tmp_path / "missing.iso"), str(elf), "--elf-name", "GAME.ELF"]) == 2
    assert main([str(iso), str(tmp_path / "missing.elf"), "--elf-name", "GAME.ELF"]) == 2
    assert "missing" in capsys.readouterr().err


def test_vmode_and_ver_select_the_system_cnf_lines(tmp_path):
    iso = tmp_path / "pal.iso"
    elf = tmp_path / "pal.elf"
    iso.write_bytes(bytes(build_iso([(b"SYSTEM.CNF", system_cnf(vmode="PAL", ver="1.10")), (b"GAME.ELF", ELF)])))
    elf.write_bytes(ELF)
    assert main([str(iso), str(elf), "--elf-name", "GAME.ELF"]) == 1
    assert main([str(iso), str(elf), "--elf-name", "GAME.ELF", "--vmode", "PAL", "--ver", "1.10"]) == 0


def test_normalize_file_raises_for_a_mismatch_and_writes_only_after_a_pass(files):
    iso, elf = files
    before = iso.read_bytes()
    with pytest.raises(IsoNormalizeError):
        normalize_file(iso, elf, elf_name="OTHER.ELF")
    assert iso.read_bytes() == before
    normalize_file(iso, elf, elf_name="GAME.ELF")
    assert iso.read_bytes() != before


def test_python_dash_m_runs_the_command(files):
    iso, elf = files
    done = subprocess.run(
        [sys.executable, "-m", "uppercut_iso_normalize", str(iso), str(elf), "--elf-name", "GAME.ELF"], capture_output=True, text=True
    )
    assert done.returncode == 0
    assert done.stdout == ""


def test_the_elf_name_is_required():
    with pytest.raises(SystemExit) as raised:
        main(["a.iso", "a.elf"])
    assert raised.value.code == 2
