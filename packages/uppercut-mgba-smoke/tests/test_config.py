from pathlib import Path

import pytest

from uppercut_mgba_smoke import SmokeConfig, SmokeError, build_command, build_environment, make_sandbox


def config(**overrides):
    values = dict(
        mgba=Path("/opt/mgba/mGBA"),
        rom=Path("/games/example.gba"),
        lua=Path("/games/smoke.lua"),
        output=Path("/out"),
        screenshots=("01-title.png", "02-play.png"),
    )
    values.update(overrides)
    return SmokeConfig(**values)


def test_command_line_isolates_every_path_it_writes():
    with make_sandbox() as box:
        command = build_command(config(), box)
    assert command[:2] == ["/opt/mgba/mGBA", "-2"]
    assert command[-1] == "/games/example.gba"
    options = dict(pair.split("=", 1) for flag, pair in zip(command, command[1:]) if flag == "-C")
    assert options["mute"] == "1"
    assert options["pauseOnFocusLost"] == "0"
    assert options["autoload"] == "0"
    assert options["autosave"] == "0"
    for key in ("screenshotPath", "savegamePath", "savestatePath", "cheatsPath"):
        assert options[key].startswith(str(box.root)), key
    assert "--script" not in command


def test_argument_loader_passes_the_script():
    with make_sandbox() as box:
        command = build_command(config(loader="argument"), box)
    index = command.index("--script")
    assert command[index + 1] == "/games/smoke.lua"
    assert command.index("/games/example.gba") > index  # options come before the ROM


def test_scale_and_extra_options():
    with make_sandbox() as box:
        command = build_command(config(scale=3, mgba_options={"mute": "0", "frameskip": "1"}), box)
    assert command[1] == "-3"
    options = dict(pair.split("=", 1) for flag, pair in zip(command, command[1:]) if flag == "-C")
    assert options["mute"] == "0"  # a caller option overrides an isolating default
    assert options["frameskip"] == "1"


def test_environment_points_mgba_and_the_script_into_the_sandbox():
    base = {"PATH": "/usr/bin", "KEEP": "me"}
    with make_sandbox() as box:
        environment = build_environment(config(env={"GAME_STATE": "02000010"}), box, base)
    assert environment["PATH"] == "/usr/bin" and environment["KEEP"] == "me"
    assert environment["XDG_CONFIG_HOME"] == str(box.config)
    assert environment["MGBA_SMOKE_OUTPUT"] == str(box.screenshots)
    assert environment["MGBA_SMOKE_RESULT"] == str(box.result)
    assert environment["MGBA_SMOKE_HELPER"] == str(box.helper)
    assert environment["GAME_STATE"] == "02000010"
    assert base == {"PATH": "/usr/bin", "KEEP": "me"}  # the caller's mapping is not changed


def test_prefixed_aliases_keep_older_scripts_working():
    with make_sandbox() as box:
        environment = build_environment(config(env_prefix="MY_GAME"), box, {})
    assert environment["MY_GAME_OUTPUT"] == environment["MGBA_SMOKE_OUTPUT"]
    assert environment["MY_GAME_RESULT"] == environment["MGBA_SMOKE_RESULT"]


def test_environment_defaults_to_the_process_environment(monkeypatch):
    monkeypatch.setenv("SOME_VARIABLE", "value")
    with make_sandbox() as box:
        assert build_environment(config(), box)["SOME_VARIABLE"] == "value"


def test_sandbox_has_its_folders_and_is_removed():
    with make_sandbox("example-") as box:
        assert box.root.name.startswith("example-")
        for folder in (box.config, box.screenshots, box.saves, box.states, box.cheats):
            assert folder.is_dir() and folder.parent == box.root
        assert not box.result.exists()
        root = box.root
    assert not root.exists()


@pytest.mark.parametrize(
    "overrides, message",
    [
        ({"timeout": 0}, "timeout"),
        ({"loader": "other"}, "loader"),
        ({"scale": 0}, "scale"),
        ({"scale": 7}, "scale"),
        ({"screenshots": ()}, "at least one"),
        ({"screenshots": ("a.png", "a.png")}, "twice"),
        ({"screenshots": ("a.jpg",)}, "plain file name"),
        ({"screenshots": ("../a.png",)}, "plain file name"),
        ({"screenshots": ("dir\\a.png",)}, "plain file name"),
        ({"screenshots": (".hidden.png",)}, "plain file name"),
        ({"env_prefix": "my game"}, "env_prefix"),
        ({"env_prefix": "lower"}, "env_prefix"),
        ({"env": {"BAD NAME": "x"}}, "valid environment"),
        ({"env": {"XDG_CONFIG_HOME": "x"}}, "set by the runner"),
        ({"env": {"MGBA_SMOKE_OUTPUT": "x"}}, "set by the runner"),
        ({"env": {"MGBA_SMOKE_ANYTHING": "x"}}, "set by the runner"),
        ({"env": {"GAME": 5}}, "must be a string"),
        ({"env": {"GAME": "a\0b"}}, "NUL"),
        ({"reviewed_sha256": {"other.png": "0" * 64}}, "not in screenshots"),
    ],
)
def test_bad_configuration_is_refused_up_front(overrides, message):
    with pytest.raises(SmokeError, match=message):
        config(**overrides)
