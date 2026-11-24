import hashlib
import zipfile
from pathlib import Path

import pytest

from uppercut_bizhawk_driver import InstallError, find_emuhawk, install_bizhawk, safe_extract, sha256_file, validate_install


def make_zip(path: Path, members: dict) -> str:
    with zipfile.ZipFile(path, "w") as z:
        for name, data in members.items():
            z.writestr(name, data)
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fetcher(zip_path: Path, calls: list):
    def fetch(url: str, destination: Path) -> None:
        calls.append(url)
        destination.write_bytes(zip_path.read_bytes())

    return fetch


def test_install_checks_the_pin_and_unpacks(tmp_path):
    pin = make_zip(tmp_path / "BizHawk-0.0.1-win-x64.zip", {"EmuHawk.exe": b"fake", "Lua/readme.txt": b"x"})
    calls: list = []
    where = install_bizhawk(tmp_path / "tools" / "bizhawk", "https://example.test/dl/BizHawk-0.0.1-win-x64.zip", pin, fetch=fetcher(tmp_path / "BizHawk-0.0.1-win-x64.zip", calls))
    assert calls == ["https://example.test/dl/BizHawk-0.0.1-win-x64.zip"]
    assert (where / "EmuHawk.exe").read_bytes() == b"fake" and (where / "Lua" / "readme.txt").is_file()
    assert sorted(p.name for p in (tmp_path / "tools").iterdir()) == ["bizhawk"]  # the scratch folder is gone
    assert validate_install(where) == (where / "EmuHawk.exe", where / "Lua")


def test_a_wrong_digest_leaves_nothing_behind(tmp_path):
    make_zip(tmp_path / "a.zip", {"EmuHawk.exe": b"fake"})
    with pytest.raises(InstallError, match="checksum mismatch for a.zip"):
        install_bizhawk(tmp_path / "out" / "bh", "https://example.test/a.zip", "0" * 64, fetch=fetcher(tmp_path / "a.zip", []))
    assert not (tmp_path / "out" / "bh").exists()
    assert list((tmp_path / "out").iterdir()) == []


def test_the_pin_must_be_a_sha256():
    for bad in ("", "abc", "g" * 64):
        with pytest.raises(InstallError, match="SHA-256"):
            install_bizhawk("x", "https://example.test/a.zip", bad, fetch=lambda u, d: None)


def test_an_existing_folder_is_not_replaced_unless_asked(tmp_path):
    pin = make_zip(tmp_path / "a.zip", {"EmuHawk.exe": b"new"})
    target = tmp_path / "bh"
    target.mkdir()
    (target / "keep.txt").write_text("mine")
    with pytest.raises(InstallError, match="already exists"):
        install_bizhawk(target, "https://example.test/a.zip", pin, fetch=fetcher(tmp_path / "a.zip", []))
    assert (target / "keep.txt").read_text() == "mine"
    install_bizhawk(target, "https://example.test/a.zip", pin, replace=True, fetch=fetcher(tmp_path / "a.zip", []))
    assert not (target / "keep.txt").exists() and (target / "EmuHawk.exe").read_bytes() == b"new"


def test_a_failed_replacement_keeps_the_old_copy(tmp_path):
    make_zip(tmp_path / "a.zip", {"EmuHawk.exe": b"new"})
    target = tmp_path / "bh"
    target.mkdir()
    (target / "keep.txt").write_text("mine")
    with pytest.raises(InstallError):
        install_bizhawk(target, "https://example.test/a.zip", "0" * 64, replace=True, fetch=fetcher(tmp_path / "a.zip", []))
    assert (target / "keep.txt").read_text() == "mine"


def test_a_member_outside_the_folder_is_refused(tmp_path):
    pin = make_zip(tmp_path / "evil.zip", {"../escape.txt": b"x", "EmuHawk.exe": b"fake"})
    with pytest.raises(InstallError, match="outside its folder"):
        install_bizhawk(tmp_path / "bh", "https://example.test/evil.zip", pin, fetch=fetcher(tmp_path / "evil.zip", []))
    assert not (tmp_path / "escape.txt").exists() and not (tmp_path / "bh").exists()
    with pytest.raises(InstallError):
        safe_extract(tmp_path / "evil.zip", tmp_path / "dest")


def test_a_download_that_is_not_a_zip_is_an_install_error(tmp_path):
    (tmp_path / "junk.zip").write_bytes(b"not a zip")
    pin = hashlib.sha256(b"not a zip").hexdigest()
    with pytest.raises(InstallError, match="not a zip"):
        install_bizhawk(tmp_path / "bh", "https://example.test/junk.zip", pin, fetch=fetcher(tmp_path / "junk.zip", []))


def test_find_emuhawk_looks_in_the_root_first_then_below_it_and_prefers_the_platform_launcher(tmp_path):
    (tmp_path / "a" / "b").mkdir(parents=True)
    (tmp_path / "a" / "b" / "EmuHawkMono.sh").write_text("#!/bin/sh\n")
    assert find_emuhawk(tmp_path, windows=False) == tmp_path / "a" / "b" / "EmuHawkMono.sh"
    (tmp_path / "EmuHawk.exe").write_bytes(b"x")
    assert find_emuhawk(tmp_path, windows=True) == tmp_path / "EmuHawk.exe"
    assert find_emuhawk(tmp_path, windows=False) == tmp_path / "EmuHawk.exe"  # a launcher in the root wins over one below it
    with pytest.raises(FileNotFoundError, match="No supported EmuHawk launcher"):
        find_emuhawk(tmp_path / "a" / "b" / "nowhere")


def test_validate_install_needs_the_lua_folder(tmp_path):
    (tmp_path / "EmuHawk.exe").write_bytes(b"x")
    with pytest.raises(FileNotFoundError, match="No bundled Lua resources"):
        validate_install(tmp_path)


def test_sha256_file(tmp_path):
    (tmp_path / "f").write_bytes(b"abc")
    assert sha256_file(tmp_path / "f") == hashlib.sha256(b"abc").hexdigest()
