import json

import pytest

from bizhawk_support import made_up_config, split_path, write_config
from uppercut_bizhawk_driver import (
    ConfigError, edit_config_in_place, emuhawk_running, load_config, make_run_config, parse_assignment, set_path,
)


def test_load_config_reads_json_with_or_without_a_byte_order_mark(tmp_path):
    assert load_config(write_config(tmp_path / "a.ini")) == made_up_config()
    assert load_config(write_config(tmp_path / "b.ini", bom=True)) == made_up_config()


def test_load_config_refuses_what_is_not_a_json_object(tmp_path):
    (tmp_path / "x.ini").write_text("[ini]\nkey=value\n")
    with pytest.raises(ConfigError, match="not BizHawk's JSON"):
        load_config(tmp_path / "x.ini")
    (tmp_path / "y.ini").write_text("[1, 2]")
    with pytest.raises(ConfigError, match="not an object"):
        load_config(tmp_path / "y.ini")


def test_set_path_walks_names_that_hold_dots():
    cfg = made_up_config()
    key = ("CoreSettings", "BizHawk.Emulation.Cores.Nintendo.Gameboy.Gameboy", "DisplayBG")
    set_path(cfg, key, False)
    assert cfg["CoreSettings"]["BizHawk.Emulation.Cores.Nintendo.Gameboy.Gameboy"]["DisplayBG"] is False
    with pytest.raises(ConfigError, match="no 'Missing'"):
        set_path(cfg, ("Missing", "Key"), 1)
    set_path(cfg, ("Missing", "Key"), 1, create=True)
    assert cfg["Missing"] == {"Key": 1}
    with pytest.raises(ConfigError, match="not an object"):
        set_path(cfg, ("SoundEnabled", "x"), 1)
    with pytest.raises(ConfigError):
        set_path(cfg, (), 1)


def test_make_run_config_copies_edits_and_leaves_the_base_alone(tmp_path):
    base = write_config(tmp_path / "config.ini")
    before = base.read_bytes()
    gb = "BizHawk.Emulation.Cores.Nintendo.Gameboy.Gameboy"
    target = make_run_config(
        base, tmp_path / "run" / "run.ini", cores={"SNES": "BSNESv115+"},
        edits=[(("CoreSettings", gb, "Muted"), True), (("CoreSettings", gb, "DisplayOBJ"), False)], create=True)
    assert base.read_bytes() == before
    cfg = json.loads(target.read_text())
    assert cfg["PreferredCores"] == {"SNES": "BSNESv115+", "PSX": "Nymashock"}
    assert (cfg["SoundEnabled"], cfg["SoundEnabledNormal"], cfg["SoundEnabledRWFF"]) == (False, False, False)
    assert cfg["RunInBackground"] is True
    assert cfg["CoreSettings"][gb] == {"Muted": True, "DisplayBG": True, "DisplayOBJ": False}
    assert cfg["Unrelated"] == {"Keep": [1, 2, 3]}


def test_make_run_config_options(tmp_path):
    base = write_config(tmp_path / "config.ini")
    cfg = json.loads(make_run_config(base, tmp_path / "r.ini", sound=True, background=False).read_text())
    assert cfg["SoundEnabled"] is True and cfg["RunInBackground"] is False
    with pytest.raises(ConfigError):  # an edit under a core that was never saved
        make_run_config(base, tmp_path / "r2.ini", edits=[(("CoreSettings", "No.Such.Core", "Muted"), True)])


def test_emuhawk_running_reads_the_process_list():
    unix = "launchd\nbash\n/Applications/BizHawk/EmuHawk\nzsh\n"
    assert emuhawk_running(ps=lambda argv: unix, windows=False)
    assert not emuhawk_running(ps=lambda argv: "bash\nzsh\nEmuHawkViewer\n", windows=False)
    tasklist = '"System","4","Services","0","100 K"\n"EmuHawk.exe","812","Console","1","90,112 K"\n'
    assert emuhawk_running(ps=lambda argv: tasklist, windows=True)
    assert not emuhawk_running(ps=lambda argv: '"explorer.exe","1","Console","1","1 K"\n', windows=True)
    seen = []
    emuhawk_running(ps=lambda argv: seen.append(argv) or "", windows=True)
    emuhawk_running(ps=lambda argv: seen.append(argv) or "", windows=False)
    assert seen[0][0] == "tasklist" and seen[1][:2] == ["ps", "-A"]


def test_editing_a_shared_config_is_refused_while_the_emulator_runs(tmp_path):
    path = write_config(tmp_path / "config.ini")
    before = path.read_bytes()
    with pytest.raises(ConfigError, match="EmuHawk is running"):
        edit_config_in_place(path, [(("PreferredCores", "PSX"), "Octoshock")], ps=lambda argv: "EmuHawk\n")
    assert path.read_bytes() == before


def test_editing_a_shared_config_and_keeping_a_copy(tmp_path):
    path = write_config(tmp_path / "config.ini")
    cfg = edit_config_in_place(path, [(("PreferredCores", "PSX"), "Octoshock")], ps=lambda argv: "", keep_copy=tmp_path / "evidence" / "psx.config.ini")
    assert cfg["PreferredCores"]["PSX"] == "Octoshock"
    assert json.loads(path.read_text())["PreferredCores"]["PSX"] == "Octoshock"
    assert json.loads((tmp_path / "evidence" / "psx.config.ini").read_text())["PreferredCores"]["PSX"] == "Octoshock"


@pytest.mark.parametrize(
    "text, expected",
    [
        ("PreferredCores/PSX=Octoshock", (("PreferredCores", "PSX"), "Octoshock")),
        ("RunInBackground=true", (("RunInBackground",), True)),
        ("A/B/C=3", (("A", "B", "C"), 3)),
        ('A/B="quoted"', (("A", "B"), "quoted")),
        ("A/B=x=y", (("A", "B"), "x=y")),
    ],
)
def test_parse_assignment(text, expected):
    assert parse_assignment(text) == expected


def test_parse_assignment_refuses_what_is_not_an_assignment():
    for bad in ("nothing", "=3", "//=3"):
        with pytest.raises(ConfigError):
            parse_assignment(bad)
    assert split_path("a/b") == ("a", "b")
