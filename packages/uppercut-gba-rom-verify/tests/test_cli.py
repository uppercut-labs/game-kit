import json
import subprocess
import sys

import pytest

from uppercut_gba_rom_verify import RomConfig, main, validate_rom_bytes
from uppercut_gba_rom_verify.cli import config_from_args, build_parser
from synthetic import EXAMPLE, build_elf, build_rom


@pytest.fixture
def artifacts(tmp_path):
    rom_path = tmp_path / "example.gba"
    elf_path = tmp_path / "example.elf"
    rom_path.write_bytes(bytes(build_rom()))
    elf_path.write_bytes(bytes(build_elf()))
    return rom_path, elf_path


FLAGS = ["--title", "EXAMPLE GAME", "--game-code", "EXMP", "--maker-code", "01"]


def test_reports_success_for_both_artifacts(artifacts, capsys):
    rom_path, elf_path = artifacts

    assert main([str(rom_path), str(elf_path), *FLAGS]) == 0

    out = capsys.readouterr()
    assert "PASS ROM size: 192 bytes" in out.out
    assert "PASS ROM header: Nintendo logo, EXAMPLE GAME/EXMP/01" in out.out
    assert "PASS ROM SHA-256: " in out.out
    assert "PASS ELF" in out.out
    assert out.err == ""


def test_reports_failure_to_stderr_and_exits_1(artifacts, capsys):
    rom_path, elf_path = artifacts
    rom_path.write_bytes(b"invalid")

    assert main([str(rom_path), str(elf_path), *FLAGS]) == 1

    out = capsys.readouterr()
    assert "FAIL ROM" in out.err
    assert "PASS ELF" in out.out


def test_wrong_cartridge_identity_fails(artifacts, capsys):
    rom_path, elf_path = artifacts

    assert main([str(rom_path), str(elf_path), "--title", "OTHER", "--game-code", "EXMP", "--maker-code", "01"]) == 1
    assert "FAIL ROM: ROM title must be 'OTHER'" in capsys.readouterr().err


def test_expect_sha256_flag_accepts_the_digest_in_either_case(artifacts, capsys):
    rom_path, elf_path = artifacts
    digest = validate_rom_bytes(rom_path.read_bytes(), EXAMPLE).sha256

    assert main([str(rom_path), str(elf_path), *FLAGS, "--expect-sha256", digest.upper()]) == 0


def test_expect_sha256_flag_fails_on_a_different_digest(artifacts, capsys):
    rom_path, elf_path = artifacts

    assert main([str(rom_path), str(elf_path), *FLAGS, "--expect-sha256", "ab" * 32]) == 1
    assert "SHA-256 must be " in capsys.readouterr().err


def test_expect_sha256_must_be_64_hex_characters(artifacts, capsys):
    rom_path, elf_path = artifacts

    with pytest.raises(SystemExit) as raised:
        main([str(rom_path), str(elf_path), *FLAGS, "--expect-sha256", "xyz"])
    assert raised.value.code == 2


def test_config_file_supplies_the_identity(tmp_path, artifacts):
    rom_path, elf_path = artifacts
    config = tmp_path / "cartridge.json"
    config.write_text(json.dumps({"title": "EXAMPLE GAME", "game_code": "EXMP", "maker_code": "01"}))

    assert main([str(rom_path), str(elf_path), "--config", str(config)]) == 0


def test_flags_override_the_config_file(tmp_path, artifacts):
    rom_path, elf_path = artifacts
    config = tmp_path / "cartridge.json"
    config.write_text(json.dumps({"title": "WRONG", "game_code": "EXMP", "maker_code": "01"}))

    assert main([str(rom_path), str(elf_path), "--config", str(config), "--title", "EXAMPLE GAME"]) == 0


def test_missing_identity_is_a_usage_error(artifacts, capsys):
    rom_path, elf_path = artifacts

    with pytest.raises(SystemExit) as raised:
        main([str(rom_path), str(elf_path), "--title", "EXAMPLE GAME"])
    assert raised.value.code == 2
    assert "--game-code" in capsys.readouterr().err


def test_config_file_rejects_unknown_keys_and_non_strings(tmp_path):
    parser = build_parser()
    bad_keys = tmp_path / "bad-keys.json"
    bad_keys.write_text(json.dumps({"title": "A", "game_code": "ABCD", "maker_code": "01", "extra": "x"}))
    bad_type = tmp_path / "bad-type.json"
    bad_type.write_text(json.dumps({"title": 5, "game_code": "ABCD", "maker_code": "01"}))

    with pytest.raises(ValueError, match="unknown keys: extra"):
        config_from_args(parser.parse_args(["r", "e", "--config", str(bad_keys)]))
    with pytest.raises(ValueError, match="must be a string"):
        config_from_args(parser.parse_args(["r", "e", "--config", str(bad_type)]))


def test_invalid_identity_is_a_usage_error(artifacts, capsys):
    rom_path, elf_path = artifacts

    with pytest.raises(SystemExit) as raised:
        main([str(rom_path), str(elf_path), "--title", "A TITLE THAT IS TOO LONG", "--game-code", "EXMP", "--maker-code", "01"])
    assert raised.value.code == 2
    assert "title must be 1 to 12 ASCII characters" in capsys.readouterr().err


def test_config_parses_to_a_rom_config(tmp_path):
    parser = build_parser()
    args = parser.parse_args(["r", "e", "--title", "SHORT!", "--game-code", "SHRT", "--maker-code", "00"])

    assert config_from_args(args) == RomConfig("SHORT!", "SHRT", "00")


def test_module_entry_point_runs(artifacts):
    rom_path, elf_path = artifacts

    result = subprocess.run(
        [sys.executable, "-m", "uppercut_gba_rom_verify", str(rom_path), str(elf_path), *FLAGS],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    assert "PASS ELF" in result.stdout
