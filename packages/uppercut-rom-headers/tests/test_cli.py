import subprocess
import sys

import pytest

from uppercut_rom_headers import compute_gba_complement, compute_megadrive_checksum, compute_n64_checksums, main
from rom_images import N64_SIZE, stream


def test_fix_then_verify_for_every_format(tmp_path, capsys):
    images = {"n64": stream(1, N64_SIZE), "megadrive": stream(2, 0x1000), "gba": stream(3, 0x400)}
    for name, image in images.items():
        path = tmp_path / f"{name}.rom"
        path.write_bytes(bytes(image))
        assert main(["verify", name, str(path)]) == 1
        assert main(["fix", name, str(path)]) == 0
        assert main(["verify", name, str(path)]) == 0
    out = capsys.readouterr()
    assert out.out.count("PASS") == 3
    assert out.err.count("FAIL") == 3


def test_fix_writes_what_the_library_computes(tmp_path):
    n64 = stream(4, N64_SIZE)
    path = tmp_path / "game.n64"
    path.write_bytes(bytes(n64))
    main(["fix", "n64", str(path)])
    expected = compute_n64_checksums(n64)
    assert path.read_bytes()[0x10:0x18] == expected.crc1.to_bytes(4, "big") + expected.crc2.to_bytes(4, "big")

    md = stream(5, 0x800)
    path = tmp_path / "game.md"
    path.write_bytes(bytes(md))
    main(["fix", "megadrive", str(path)])
    assert path.read_bytes()[0x18E:0x190] == compute_megadrive_checksum(md).to_bytes(2, "big")

    gba = stream(6, 0x200)
    path = tmp_path / "game.gba"
    path.write_bytes(bytes(gba))
    main(["fix", "gba", str(path)])
    assert path.read_bytes()[0xBD] == compute_gba_complement(gba)


def test_verify_prints_expected_and_found(tmp_path, capsys):
    path = tmp_path / "game.gba"
    image = stream(7, 0xC0)
    image[0xBD] = 0x00
    path.write_bytes(bytes(image))
    assert main(["verify", "gba", str(path)]) in (0, 1)
    out = capsys.readouterr()
    assert "gba header: expected 0x" in out.out + out.err
    assert "found 0x00" in out.out + out.err


def test_preserve_zero_header_leaves_the_file_untouched(tmp_path, capsys):
    image = stream(8, N64_SIZE)
    image[0x10:0x18] = bytes(8)
    path = tmp_path / "zero.n64"
    path.write_bytes(bytes(image))
    assert main(["fix", "n64", str(path), "--preserve-zero-header"]) == 0
    assert path.read_bytes() == bytes(image)
    assert "left" in capsys.readouterr().out


def test_a_missing_or_short_file_exits_2_and_changes_nothing(tmp_path, capsys):
    assert main(["verify", "gba", str(tmp_path / "missing.gba")]) == 2
    short = tmp_path / "short.n64"
    short.write_bytes(b"abc")
    assert main(["fix", "n64", str(short)]) == 2
    assert short.read_bytes() == b"abc"
    err = capsys.readouterr().err
    assert "cannot read" in err
    assert "too short" in err


def test_python_dash_m_runs_the_command(tmp_path):
    path = tmp_path / "game.gba"
    path.write_bytes(bytes(stream(9, 0xC0)))
    done = subprocess.run([sys.executable, "-m", "uppercut_rom_headers", "fix", "gba", str(path)], capture_output=True, text=True)
    assert done.returncode == 0
    assert "fixed gba header" in done.stdout


def test_an_unknown_format_is_a_usage_error(tmp_path):
    with pytest.raises(SystemExit) as raised:
        main(["verify", "snes", str(tmp_path / "x")])
    assert raised.value.code == 2
