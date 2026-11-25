import hashlib

import pytest

from images import build_iso, directory, file, make_elf, ps2_disc, wrap
from uppercut_iso9660.cli import main


@pytest.fixture
def disc(tmp_path):
    path = tmp_path / "disc.iso"
    path.write_bytes(bytes(build_iso([file("A.TXT;1", b"alpha"), directory("D", file("B.TXT;1", b"beta" * 700))])))
    return path


def test_ls_lists_every_entry(disc, capsys):
    assert main(["ls", str(disc)]) == 0
    lines = capsys.readouterr().out.splitlines()
    assert [line.split()[-1] for line in lines] == ["A.TXT", "D", "D/B.TXT"]
    assert lines[0].startswith("F ") and lines[1].startswith("D ")
    assert " 2800 " in lines[2]


def test_ls_of_a_directory(disc, capsys):
    assert main(["ls", str(disc), "d"]) == 0
    assert [line.split()[-1] for line in capsys.readouterr().out.splitlines()] == ["D/B.TXT"]


def test_get_extracts_and_prints_size_and_hash(disc, tmp_path, capsys):
    out = tmp_path / "b.txt"
    assert main(["get", str(disc), "D/B.TXT", str(out)]) == 0
    assert capsys.readouterr().out.split() == ["2800", hashlib.sha256(b"beta" * 700).hexdigest()]
    assert out.read_bytes() == b"beta" * 700


def test_ps2_prints_json(tmp_path, capsys):
    path = tmp_path / "ps2.bin"
    path.write_bytes(wrap(bytes(ps2_disc(make_elf())), "raw-2352-mode1"))
    assert main(["ps2", str(path)]) == 0
    out = capsys.readouterr().out
    assert '"serial": "SLUS-21847"' in out and '"layout": "raw-2352-mode1"' in out


def test_errors_are_one_line_and_exit_1(tmp_path, capsys):
    junk = tmp_path / "junk.iso"
    junk.write_bytes(bytes(40 * 2048))
    assert main(["ls", str(junk)]) == 1
    assert "junk.iso: no ISO 9660 volume descriptor" in capsys.readouterr().err
    assert main(["ls", str(tmp_path / "missing.iso")]) == 1
    assert "missing.iso" in capsys.readouterr().err


def test_missing_file_in_the_image(disc, tmp_path, capsys):
    assert main(["get", str(disc), "NOPE.TXT", str(tmp_path / "x")]) == 1
    assert "NOPE.TXT" in capsys.readouterr().err
    assert not (tmp_path / "x").exists()
