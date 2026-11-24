import os
import subprocess
from pathlib import Path
from types import SimpleNamespace

import pytest

from uppercut_godot_render import (
    GodotRun,
    RenderError,
    build_command,
    find_godot,
    movie_arguments,
    needs_virtual_display,
    run_godot,
    viewport_override,
)


def test_find_godot_order(monkeypatch):
    monkeypatch.setattr("shutil.which", lambda name: f"/bin/{name}" if name == "godot4" else None)
    assert find_godot("/given/godot") == "/given/godot"
    assert find_godot(None, {"GODOT": "/env/godot"}) == "/env/godot"
    assert find_godot(None, {}) == "/bin/godot4"
    monkeypatch.setattr("shutil.which", lambda name: None)
    assert find_godot(None, {}) is None
    monkeypatch.setattr("shutil.which", lambda name: "/bin/godot" if name == "godot" else "/bin/godot4")
    assert find_godot(None, {}) == "/bin/godot"


@pytest.mark.parametrize(
    "platform, environ, expected",
    [
        ("linux", {}, True),
        ("linux", {"DISPLAY": ":0"}, False),
        ("linux", {"WAYLAND_DISPLAY": "wayland-0"}, False),
        ("darwin", {}, False),
        ("win32", {}, False),
    ],
)
def test_virtual_display_is_for_displayless_linux(platform, environ, expected):
    assert needs_virtual_display(platform, environ) is expected


def test_movie_arguments():
    arguments = movie_arguments(Path("work/raw"), 60)
    assert arguments == ["--write-movie", str(Path("work/raw/frame.png")), "--fixed-fps", "60"]


def test_movie_arguments_optional_flags_keep_their_order():
    arguments = movie_arguments(Path("raw"), 30, quit_after=93, resolution=(1080, 1920), always_on_top=True, extra=["--disable-vsync"])
    assert arguments[0] == "--always-on-top"
    assert arguments[1:5] == ["--write-movie", str(Path("raw/frame.png")), "--fixed-fps", "30"]
    assert arguments[5:] == ["--quit-after", "93", "--resolution", "1080x1920", "--disable-vsync"]


def test_movie_arguments_reject_a_bad_rate():
    with pytest.raises(RenderError, match="fps must be positive"):
        movie_arguments(Path("raw"), 0)


def test_build_command_puts_the_games_flags_after_the_separator():
    assert build_command("godot", Path("game"), ["--fixed-fps", "60"], ["--look=hd", "--out=o"]) == [
        "godot", "--path", "game", "--fixed-fps", "60", "--", "--look=hd", "--out=o",
    ]  # fmt: skip
    assert build_command("godot", Path("game"), ["--fixed-fps", "60"]) == ["godot", "--path", "game", "--fixed-fps", "60"]


def make_run(stdout="out\n", stderr="err\n", code=0, raises=None):
    calls = []

    def run(command, **kwargs):
        calls.append((command, kwargs))
        if raises:
            raise raises
        return SimpleNamespace(returncode=code, stdout=stdout, stderr=stderr)

    run.calls = calls
    return run


def test_run_godot_returns_the_exit_code_and_both_streams():
    run = make_run(code=3)
    result = run_godot(["godot", "--x"], display=False, run=run)
    assert result == GodotRun(3, "out\nerr\n")
    assert run.calls[0][0] == ["godot", "--x"]
    assert run.calls[0][1]["timeout"] == 1800 and run.calls[0][1]["check"] is False


def test_run_godot_wraps_a_displayless_linux_box_in_xvfb():
    run = make_run()
    run_godot(["godot"], run=run, platform="linux", environment={"DISPLAY": ""})
    command = run.calls[0][0]
    assert command == ["xvfb-run", "-a", "-s", "-screen 0 1920x1920x24", "godot"]


def test_run_godot_does_not_wrap_with_a_display_or_when_not_asked():
    run = make_run()
    run_godot(["godot"], run=run, platform="linux", environment={"DISPLAY": ":1"})
    run_godot(["godot"], run=run, platform="linux", environment={"DISPLAY": ""}, display=False)
    run_godot(["godot"], run=run, platform="darwin")
    assert [call[0] for call in run.calls] == [["godot"]] * 3


def test_run_godot_merges_the_environment(monkeypatch):
    monkeypatch.setenv("KEEP", "yes")
    run = make_run()
    run_godot(["godot"], display=False, run=run, environment={"VK_ICD_FILENAMES": "/nowhere"})
    env = run.calls[0][1]["env"]
    assert env["KEEP"] == "yes" and env["VK_ICD_FILENAMES"] == "/nowhere"
    assert "VK_ICD_FILENAMES" not in os.environ


def test_run_godot_failures_are_render_errors():
    with pytest.raises(RenderError, match="did not finish within 5 s"):
        run_godot(["godot"], display=False, timeout=5, run=make_run(raises=subprocess.TimeoutExpired("godot", 5)))
    with pytest.raises(RenderError, match="cannot run Godot"):
        run_godot(["godot"], display=False, run=make_run(raises=FileNotFoundError("godot")))


def test_run_godot_tolerates_missing_streams():
    assert run_godot(["godot"], display=False, run=make_run(stdout=None, stderr=None)).output == ""


def test_a_landscape_override_writes_nothing_but_still_guards(tmp_path):
    path = tmp_path / "override.cfg"
    with viewport_override(path):
        assert not path.exists()
    path.write_text("[display]\n")
    with pytest.raises(RenderError, match="another render is running"), viewport_override(path):
        pytest.fail("started with a stale override")
    assert path.exists()  # the stale file is left for a person to see


def test_a_sized_override_exists_only_while_the_block_runs(tmp_path):
    path = tmp_path / "override.cfg"
    with viewport_override(path, (1080, 1920)):
        text = path.read_text(encoding="utf-8")
        assert "window/size/viewport_width=1080" in text
        assert "window/size/viewport_height=1920" in text
        assert 'window/stretch/mode="viewport"' in text
        assert text.startswith("; Written for one Movie Maker render")
        with pytest.raises(RenderError), viewport_override(path, (1080, 1920)):
            pass
        assert path.exists()  # the failed second render did not delete the first one's file
    assert not path.exists()


def test_the_stretch_line_is_optional(tmp_path):
    path = tmp_path / "override.cfg"
    with viewport_override(path, (1920, 1080), stretch_viewport=False):
        assert "stretch" not in path.read_text(encoding="utf-8")


def test_an_interrupted_render_removes_its_override(tmp_path):
    path = tmp_path / "override.cfg"
    with pytest.raises(KeyboardInterrupt), viewport_override(path, (1080, 1920)):
        raise KeyboardInterrupt
    assert not path.exists()
