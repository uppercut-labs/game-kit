import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

import pytest

from bizhawk_support import fake_bizhawk, made_up_config, read_lua, write_config
from uppercut_bizhawk_driver import (
    RunError, RunTimeout, build_command, run_capture, start_run, wait_for_done, write_evidence,
)

posix_only = pytest.mark.skipif(os.name == "nt", reason="Windows stops a process with TerminateProcess, which cannot be ignored")


@pytest.fixture
def setup(tmp_path):
    launcher = fake_bizhawk(tmp_path / "bizhawk")
    rom = tmp_path / "game.gb"
    rom.write_bytes(b"synthetic rom")
    lua = tmp_path / "capture.lua"
    lua.write_text("-- a script\n")
    config = write_config(tmp_path / "bizhawk" / "config.ini")
    return launcher, rom, lua, config, tmp_path / "caps" / "pass1"


def test_build_command_orders_the_arguments():
    assert build_command("/x/EmuHawk.exe", "g.gb", lua="s.lua") == ["/x/EmuHawk.exe", "--lua=s.lua", "g.gb"]
    assert build_command("/x/EmuHawkMono.sh", "g.gb", lua="s.lua") == ["/x/EmuHawkMono.sh", "--gdi", "--lua=s.lua", "g.gb"]
    assert build_command(Path("/x/EmuHawk.exe"), "g.gb", lua="s.lua", config="c.ini", chromeless=True, extra=("--fullscreen",)) == [
        str(Path("/x/EmuHawk.exe")), "--config=c.ini", "--lua=s.lua", "--chromeless", "--fullscreen", "g.gb"]
    assert build_command(["py", "-m", "fake"], "g.gb") == ["py", "-m", "fake", "g.gb"]
    assert build_command(["wine", "/x/EmuHawk.exe"], "g.gb", config="c.ini") == ["wine", "/x/EmuHawk.exe", "--config=c.ini", "g.gb"]
    with pytest.raises(ValueError):
        build_command([], "g.gb")


def test_a_run_reaches_done_and_leaves_evidence(setup):
    launcher, rom, lua, config, out = setup
    result = run_capture(
        launcher, rom, out, lua=lua, base_config=config, cores={"SNES": "BSNESv115+"}, keep_config=True,
        settings={"frames": 600, "layers": {"obj": False}, "fake_noise": 3}, exit_grace=10)
    assert result.finished and not result.stopped and result.returncode == 0
    assert result.argv[-3:] == [f"--config={out}.bizhawk.ini", f"--lua={lua.resolve()}", str(rom.resolve())]
    assert ("--gdi" in result.argv) == (os.name != "nt"), "--gdi follows EmuHawkMono.sh only"
    assert result.muted and result.background
    assert len(result.warnings) == (1 if os.name == "nt" else 0)  # Windows: the note that a window may still take focus
    assert all("shown without activation" in w for w in result.warnings)
    seen = json.loads(Path(f"{out}.argv.json").read_text())
    assert seen["config"]["SoundEnabled"] is False and seen["config"]["RunInBackground"] is True
    if os.name == "nt":  # the start-up the driver asked for: the window is shown without activation (SW_SHOWNOACTIVATE)
        assert seen["startup"]["dwFlags"] & 0x1 and seen["startup"]["wShowWindow"] == 4
    assert seen["config"]["PreferredCores"]["SNES"] == "BSNESv115+" and seen["config"]["Unrelated"] == {"Keep": [1, 2, 3]}
    table = read_lua(Path(f"{out}.cfg.lua").read_text())
    assert table["frames"] == 600 and table["layers"] == {"obj": False} and table["out"] == str(out).replace("\\", "/")
    assert json.loads(config.read_text()) == made_up_config(), "the shared config is untouched"
    log = Path(f"{out}.emuhawk.log").read_text()
    assert log.splitlines()[0] == "fake EmuHawk starting" and "noise line 2" in log
    evidence = json.loads(Path(f"{out}.run.json").read_text())
    assert evidence["rom_sha256"] == result.rom_sha256 and evidence["pid"] == result.pid and evidence["finished"] is True


def test_the_config_copy_is_removed_unless_kept(setup):
    launcher, rom, lua, config, out = setup
    run_capture(launcher, rom, out, lua=lua, base_config=config, exit_grace=10)
    assert not Path(f"{out}.bizhawk.ini").exists()


def test_without_a_base_config_the_run_says_what_it_could_not_set(setup):
    launcher, rom, lua, config, out = setup
    result = run_capture(launcher, rom, out, lua=lua, exit_grace=10)
    assert not result.muted and not result.background
    assert any("no base config" in w for w in result.warnings)
    assert "--config" not in " ".join(result.argv)


def test_the_script_failing_is_a_run_error_with_its_text(setup):
    launcher, rom, lua, config, out = setup
    with pytest.raises(RunError, match="boom") as caught:
        run_capture(launcher, rom, out, lua=lua, settings={"fake_mode": "err"}, exit_grace=5)
    assert "script error: boom" in caught.value.error_text


def test_an_exit_before_done_is_a_run_error_with_the_code_and_the_output(setup):
    launcher, rom, lua, config, out = setup
    with pytest.raises(RunError, match=r"exited with code 7 before the script wrote pass1.done") as caught:
        run_capture(launcher, rom, out, lua=lua, settings={"fake_mode": "exit", "fake_code": 7})
    assert caught.value.returncode == 7 and "fake EmuHawk starting" in str(caught.value)


def test_a_hung_run_times_out_and_is_stopped(setup):
    launcher, rom, lua, config, out = setup
    run = start_run(launcher, rom, out, lua=lua, settings={"fake_mode": "hang"})
    pid = run.pid
    start = time.monotonic()
    with pytest.raises(RunTimeout, match=f"stopped pid {pid}"):
        run.wait(timeout=0.5, poll=0.05)
    assert time.monotonic() - start < 8
    assert run.process.poll() is not None
    if os.name != "nt":  # on Windows os.kill ends the process it is given, so the exit status above is the check
        with pytest.raises(ProcessLookupError):
            os.kill(pid, 0)


@posix_only
def test_a_process_that_ignores_sigterm_is_killed_after_the_grace(setup):
    launcher, rom, lua, config, out = setup
    run = start_run(launcher, rom, out, lua=lua, settings={"fake_mode": "stubborn", "fake_delay": 0})
    time.sleep(0.5)
    started = time.monotonic()
    code = run.stop(grace=0.3)
    assert code == -signal.SIGKILL and time.monotonic() - started < 5


def test_only_the_launched_process_is_stopped(setup):
    launcher, rom, lua, config, out = setup
    bystander = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
    try:
        with pytest.raises(RunTimeout):
            run_capture(launcher, rom, out, lua=lua, settings={"fake_mode": "hang"}, timeout=0.4)
        assert bystander.poll() is None, "another process of the same kind must survive"
    finally:
        bystander.kill()
        bystander.wait()


def test_after_done_a_process_that_will_not_exit_is_stopped(setup):
    launcher, rom, lua, config, out = setup
    result = run_capture(launcher, rom, out, lua=lua, settings={"fake_mode": "done_hang"}, exit_grace=0.3)
    assert result.finished and result.stopped and result.returncode != 0


def test_a_rom_that_changes_during_the_run_is_an_error(setup):
    launcher, rom, lua, config, out = setup
    with pytest.raises(RunError, match="changed while the emulator ran"):
        run_capture(launcher, rom, out, lua=lua, settings={"fake_mode": "edit_rom"}, exit_grace=5)


def test_files_from_an_earlier_run_are_not_silently_reused(setup):
    launcher, rom, lua, config, out = setup
    run_capture(launcher, rom, out, lua=lua, exit_grace=10)
    with pytest.raises(FileExistsError, match="files from an earlier run"):
        start_run(launcher, rom, out, lua=lua)
    assert Path(f"{out}.done").exists(), "the earlier run's files are still there"
    again = run_capture(launcher, rom, out, lua=lua, stale="remove", exit_grace=10)
    assert again.finished
    Path(f"{out}_9.bin").write_bytes(b"old dump")
    run = start_run(launcher, rom, out, lua=lua, stale="keep", settings={"fake_mode": "hang"})
    run.stop()
    assert Path(f"{out}_9.bin").exists()
    with pytest.raises(ValueError):
        start_run(launcher, rom, out, lua=lua, stale="maybe")


def test_start_run_refuses_a_missing_rom_and_makes_nothing(tmp_path):
    with pytest.raises(FileNotFoundError, match="ROM does not exist"):
        start_run(["true"], tmp_path / "nope.gb", tmp_path / "caps" / "x", lua=tmp_path / "s.lua")
    assert not (tmp_path / "caps").exists()


def test_the_environment_names_the_config_and_can_be_changed(setup, monkeypatch):
    launcher, rom, lua, config, out = setup
    monkeypatch.setenv("MY_CFG_ENV", "x")
    monkeypatch.setenv("FAKE_EMUHAWK_ENV", "MY_CFG_ENV")
    result = run_capture(launcher, rom, out, lua=lua, env_var="MY_CFG_ENV", env={"UNRELATED": "1"}, exit_grace=10)
    seen = json.loads(Path(f"{out}.argv.json").read_text())
    assert seen["env_name"] == "MY_CFG_ENV" and result.finished


def test_write_evidence_is_json(tmp_path):
    from uppercut_bizhawk_driver import RunResult

    result = RunResult(["a"], 1, "o", 0, 1.5, True, False, "ab" * 32, None, "o.cfg.lua", None, True, True)
    path = write_evidence(result, tmp_path / "e.json")
    assert json.loads(path.read_text())["rom_sha256"] == "ab" * 32


def test_wait_for_done_for_a_caller_that_starts_bizhawk_itself(tmp_path):
    out = tmp_path / "cap"
    Path(f"{out}.done").write_text("")
    assert wait_for_done(out, timeout=1) == out
    err_out = tmp_path / "bad"
    Path(f"{err_out}.err").write_text("traceback here")
    with pytest.raises(RunError, match="traceback here"):
        wait_for_done(err_out, timeout=1)
    with pytest.raises(RunTimeout, match="no none.done or none.err"):
        wait_for_done(tmp_path / "none", timeout=0.2, poll=0.05)
    dead = subprocess.Popen([sys.executable, "-c", "raise SystemExit(5)"])
    dead.wait()
    with pytest.raises(RunError, match="exited with code 5") as caught:
        wait_for_done(tmp_path / "gone", timeout=30, poll=0.05, process=dead)
    assert caught.value.returncode == 5
    assert not isinstance(caught.value, TimeoutError)
    assert issubclass(RunTimeout, TimeoutError) and issubclass(RunError, RuntimeError)  # scripts that caught RuntimeError still do
