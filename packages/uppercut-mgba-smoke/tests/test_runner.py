import hashlib
import os
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

import fake_mgba
from pngs import make_png
from uppercut_mgba_smoke import RESULT_NAME, SmokeConfig, SmokeError, publish_results, run_smoke

NAMES = ["01-title.png", "02-play.png", "03-end.png"]


@pytest.fixture
def fake_env(monkeypatch):
    def set_env(values):
        for key, value in values.items():
            monkeypatch.setenv(key, value)

    return set_env


def make_config(tmp_path, executable, **overrides):
    rom = tmp_path / "game.gba"
    lua = tmp_path / "smoke.lua"
    rom.write_bytes(b"synthetic rom")
    lua.write_text("-- script")
    values = dict(
        mgba=executable,
        rom=rom,
        lua=lua,
        output=tmp_path / "published",
        screenshots=NAMES,
        loader="argument",
        timeout=20,
        check_running=False,
    )
    values.update(overrides)
    return SmokeConfig(**values)


def quiet(**overrides):
    return dict(sleep=lambda _s: None, **overrides)


def test_a_passing_run_publishes_the_checked_screenshots(tmp_path, fake_env):
    shots = fake_mgba.distinct_shots(NAMES)
    executable, env = fake_mgba.install(tmp_path, shots=shots)
    fake_env(env)

    result = run_smoke(make_config(tmp_path, executable))

    for name, data in shots.items():
        assert (tmp_path / "published" / name).read_bytes() == data
        assert result.screenshots[name] == hashlib.sha256(data).hexdigest()
    assert (tmp_path / "published" / RESULT_NAME).read_text() == "PASS\n"
    assert result.output == tmp_path / "published"
    assert sorted(p.name for p in (tmp_path / "published").iterdir()) == sorted(NAMES + [RESULT_NAME])  # no temp files left


def test_mgba_runs_only_inside_the_sandbox(tmp_path, fake_env):
    executable, env = fake_mgba.install(tmp_path, shots=fake_mgba.distinct_shots(NAMES))
    fake_env(env)
    config = make_config(tmp_path, executable, env={"TEST_GAME_STATE": "02000010"}, env_prefix="TEST_GAME", loader="argument")

    run_smoke(config)

    record = fake_mgba.recorded(tmp_path)
    options = record["options"]
    sandbox = Path(options["screenshotPath"]).parent
    assert sandbox != tmp_path and not sandbox.exists()  # a temporary folder, removed afterwards
    for key in ("savegamePath", "savestatePath", "cheatsPath"):
        assert Path(options[key]).parent == sandbox
    env = record["env"]
    assert Path(env["XDG_CONFIG_HOME"]).parent == sandbox
    assert env["MGBA_SMOKE_OUTPUT"] == options["screenshotPath"]
    assert env["TEST_GAME_OUTPUT"] == env["MGBA_SMOKE_OUTPUT"]
    assert env["TEST_GAME_RESULT"] == env["MGBA_SMOKE_RESULT"]
    assert env["TEST_GAME_STATE"] == "02000010"
    assert Path(env["MGBA_SMOKE_HELPER"]).parent == sandbox
    assert record["args"][-1] == str(tmp_path / "game.gba")
    assert "--script" in record["args"]


def test_the_helper_is_in_the_sandbox_when_mgba_starts(tmp_path, fake_env):
    executable, env = fake_mgba.install(tmp_path, shots=fake_mgba.distinct_shots(NAMES))
    fake_env(env)
    seen = {}

    def popen(command, **kwargs):
        helper = Path(kwargs["env"]["MGBA_SMOKE_HELPER"])
        seen["text"] = helper.read_text(encoding="utf-8")
        return subprocess.Popen(command, **kwargs)

    run_smoke(make_config(tmp_path, executable), popen=popen)
    assert seen["text"].startswith("-- uppercut-mgba-smoke helper")


def test_a_failing_lua_script_fails_the_run_and_publishes_nothing(tmp_path, fake_env):
    executable, env = fake_mgba.install(tmp_path, shots=fake_mgba.distinct_shots(NAMES), result="FAIL\nreset: expected 0, got 3\n", log="mgba started")
    fake_env(env)
    with pytest.raises(SmokeError, match="Lua smoke test failed:\nreset: expected 0, got 3") as raised:
        run_smoke(make_config(tmp_path, executable))
    assert "Last mGBA log lines:\nmgba started" in str(raised.value)
    assert not (tmp_path / "published").exists()


def test_mgba_exiting_without_a_result_is_an_error_with_its_status(tmp_path, fake_env):
    executable, env = fake_mgba.install(tmp_path, shots={}, result=None, exit=3, log="segfault-ish")
    fake_env(env)
    with pytest.raises(SmokeError, match="exited with status 3 before producing a result") as raised:
        run_smoke(make_config(tmp_path, executable))
    assert "segfault-ish" in str(raised.value)


def test_a_hung_mgba_times_out_and_is_stopped(tmp_path, fake_env):
    executable, env = fake_mgba.install(tmp_path, shots={}, result=None, hang=True)
    fake_env(env)
    children = []

    def popen(command, **kwargs):
        process = subprocess.Popen(command, **kwargs)
        children.append(process)
        return process

    with pytest.raises(SmokeError, match="timed out after 1s"):
        run_smoke(make_config(tmp_path, executable, timeout=1), popen=popen)
    assert children[0].poll() is not None  # terminated, not left running


def test_a_garbled_result_is_an_error(tmp_path, fake_env):
    executable, env = fake_mgba.install(tmp_path, shots=fake_mgba.distinct_shots(NAMES), result="maybe\n")
    fake_env(env)
    with pytest.raises(SmokeError, match="neither PASS nor FAIL"):
        run_smoke(make_config(tmp_path, executable))


def test_missing_blank_or_pinned_screenshots_fail_after_a_script_pass(tmp_path, fake_env):
    shots = fake_mgba.distinct_shots(NAMES)
    cases = {
        "missing": ({k: v for k, v in shots.items() if k != NAMES[2]}, {}, "missing=\\['03-end.png'\\]"),
        "blank": ({**shots, NAMES[1]: make_png(solid=True)}, {}, "blank"),
        "duplicate": ({**shots, NAMES[1]: shots[NAMES[0]]}, {}, "duplicate"),
        "pinned": (shots, {"reviewed_sha256": {NAMES[0]: "0" * 64}}, "reviewed SHA-256"),
    }
    for label, (given, extra, message) in cases.items():
        folder = tmp_path / label
        folder.mkdir()
        executable, env = fake_mgba.install(folder, shots=given)
        fake_env(env)
        config = make_config(folder, executable, **extra)
        with pytest.raises(SmokeError, match=message):
            run_smoke(config)
        assert not config.output.exists(), label


def test_another_running_mgba_stops_the_run_before_it_starts(tmp_path, fake_env):
    executable, env = fake_mgba.install(tmp_path, shots=fake_mgba.distinct_shots(NAMES))
    fake_env(env)
    started = []
    run = lambda *a, **k: SimpleNamespace(returncode=0, stdout="77\n", stderr="")  # noqa: E731
    with pytest.raises(SmokeError, match="already running"):
        run_smoke(make_config(tmp_path, executable, check_running=True), run=run, popen=lambda *a, **k: started.append(1))
    assert started == []
    assert not (tmp_path / "record.json").exists()


def test_the_applescript_loader_needs_macos(tmp_path, fake_env):
    executable, env = fake_mgba.install(tmp_path, shots={})
    fake_env(env)
    with pytest.raises(SmokeError, match="needs macOS"):
        run_smoke(make_config(tmp_path, executable, loader="applescript"), platform="linux")


def test_the_applescript_loader_gets_the_new_process_id(tmp_path, fake_env):
    executable, env = fake_mgba.install(tmp_path, shots=fake_mgba.distinct_shots(NAMES))
    fake_env(env)
    calls = []

    def run(command, **kwargs):
        calls.append(command)
        if command[0] == "osascript" and command[1] == "-e":
            return SimpleNamespace(returncode=0, stdout="true\n", stderr="")
        return SimpleNamespace(returncode=0, stdout="", stderr="")

    pids = []

    def popen(command, **kwargs):
        process = subprocess.Popen(command, **kwargs)
        pids.append(process.pid)
        return process

    config = make_config(tmp_path, executable, loader="applescript")
    run_smoke(config, platform="darwin", run=run, popen=popen)

    loader = [c for c in calls if c[:2] == ["osascript", "-"]]
    assert loader == [["osascript", "-", str(config.lua), str(pids[0])]]
    assert "--script" not in fake_mgba.recorded(tmp_path)["args"]


@pytest.mark.parametrize("what", ["mgba", "rom", "lua"])
def test_missing_inputs_are_named(tmp_path, what):
    executable = tmp_path / "mgba"
    executable.write_text("x")
    executable.chmod(0o755)
    config = make_config(tmp_path, executable)
    if what == "mgba":
        executable.chmod(0o644)
    else:
        getattr(config, what).unlink()
    with pytest.raises(SmokeError, match={"mgba": "mGBA is not executable", "rom": "ROM does not exist", "lua": "Lua smoke script does not exist"}[what]):
        run_smoke(config)


def test_publish_results_writes_the_result_last_and_leaves_no_temporaries(tmp_path):
    staging = tmp_path / "staging"
    staging.mkdir()
    (staging / "a.png").write_bytes(b"A")
    (staging / "b.png").write_bytes(b"B")
    output = tmp_path / "deep" / "out"

    publish_results(staging, output, ["a.png", "b.png"])

    assert (output / "a.png").read_bytes() == b"A"
    assert (output / RESULT_NAME).read_text() == "PASS\n"
    assert sorted(p.name for p in output.iterdir()) == ["a.png", "b.png", RESULT_NAME]
    if os.name == "posix":
        assert (output / "a.png").stat().st_mode & 0o777 == 0o644


def test_a_failed_copy_leaves_the_previous_published_file(tmp_path):
    staging = tmp_path / "staging"
    staging.mkdir()
    (staging / "a.png").write_bytes(b"NEW")
    output = tmp_path / "out"
    output.mkdir()
    (output / "a.png").write_bytes(b"OLD")
    (output / RESULT_NAME).write_text("PASS\n")
    with pytest.raises(OSError):
        publish_results(staging, output, ["a.png", "missing.png"])
    assert (output / "a.png").read_bytes() == b"NEW"  # a.png was complete before the next one failed
    assert sorted(p.name for p in output.iterdir()) == ["a.png", RESULT_NAME]


def test_python_is_what_runs_the_fake():
    assert Path(sys.executable).exists()
