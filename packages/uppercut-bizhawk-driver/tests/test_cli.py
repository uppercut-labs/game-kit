import hashlib
import json
import zipfile

from bizhawk_support import write_config
from uppercut_bizhawk_driver import cli


def test_edit_config_sets_values_and_prints_them(tmp_path, capsys, monkeypatch):
    path = write_config(tmp_path / "config.ini")
    monkeypatch.setattr("uppercut_bizhawk_driver.config.emuhawk_running", lambda *a, **k: False)
    assert cli.main(["edit-config", str(path), "--set", "PreferredCores/PSX=Octoshock", "--set", "RunInBackground=true", "--copy-to", str(tmp_path / "keep.ini")]) == 0
    out = capsys.readouterr().out.splitlines()
    assert out == ['PreferredCores/PSX = "Octoshock"', "RunInBackground = true"]
    assert json.loads(path.read_text())["PreferredCores"]["PSX"] == "Octoshock"
    assert json.loads((tmp_path / "keep.ini").read_text())["RunInBackground"] is True


def test_edit_config_exits_2_with_a_message_when_it_cannot(tmp_path, capsys, monkeypatch):
    path = write_config(tmp_path / "config.ini")
    monkeypatch.setattr("uppercut_bizhawk_driver.config.emuhawk_running", lambda *a, **k: True)
    assert cli.main(["edit-config", str(path), "--set", "A/B=1"]) == 2
    assert "EmuHawk is running" in capsys.readouterr().err
    monkeypatch.setattr("uppercut_bizhawk_driver.config.emuhawk_running", lambda *a, **k: False)
    assert cli.main(["edit-config", str(path), "--set", "No/Such=1"]) == 2


def test_lua_prints_a_json_file_as_a_lua_chunk(tmp_path, capsys):
    (tmp_path / "c.json").write_text(json.dumps({"frames": 3, "buttons": ["A"]}))
    assert cli.main(["lua", str(tmp_path / "c.json")]) == 0
    assert capsys.readouterr().out == 'return {["frames"] = 3, ["buttons"] = {"A"}}\n'


def test_install_through_the_command_line(tmp_path, capsys, monkeypatch):
    archive = tmp_path / "bh.zip"
    with zipfile.ZipFile(archive, "w") as z:
        z.writestr("EmuHawk.exe", b"x")
        z.writestr("Lua/a.lua", b"x")
    pin = hashlib.sha256(archive.read_bytes()).hexdigest()
    monkeypatch.setattr("uppercut_bizhawk_driver.install._default_fetch", lambda url, dest: dest.write_bytes(archive.read_bytes()))
    assert cli.main(["install", str(tmp_path / "bh"), "--url", "https://example.test/bh.zip", "--sha256", pin]) == 0
    assert "Installed in" in capsys.readouterr().out
    assert cli.main(["install", str(tmp_path / "bh"), "--url", "https://example.test/bh.zip", "--sha256", pin]) == 2
    assert "already exists" in capsys.readouterr().err
