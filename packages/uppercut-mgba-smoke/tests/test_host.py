import subprocess
from types import SimpleNamespace

import pytest

from uppercut_mgba_smoke import SCRIPT_LOADER, SmokeError, ensure_accessibility, ensure_no_running_mgba, load_lua_script


def runner(returncode=0, stdout="", stderr="", raises=None):
    calls = []

    def run(command, **kwargs):
        calls.append((command, kwargs))
        if raises:
            raise raises
        return SimpleNamespace(returncode=returncode, stdout=stdout, stderr=stderr)

    run.calls = calls
    return run


def test_a_running_mgba_stops_the_run_and_names_its_pids():
    run = runner(returncode=0, stdout="123\n456\n")
    with pytest.raises(SmokeError, match=r"mGBA is already running \(PID 123, 456\)"):
        ensure_no_running_mgba(run=run)
    assert run.calls[0][0] == ["pgrep", "-x", "mGBA"]


def test_the_pgrep_result_is_not_ignored():
    """One of the older runners called pgrep and threw its answer away, so its "isolated" claim was never enforced."""
    with pytest.raises(SmokeError):
        ensure_no_running_mgba(run=runner(returncode=0, stdout="999\n"))


def test_no_process_is_the_all_clear():
    ensure_no_running_mgba(run=runner(returncode=1))


@pytest.mark.parametrize("code", [2, 3, 127])
def test_a_pgrep_that_could_not_look_is_an_error(code):
    with pytest.raises(SmokeError, match="cannot check for running mGBA processes: bad option"):
        ensure_no_running_mgba(run=runner(returncode=code, stderr="bad option"))


def test_a_missing_pgrep_is_an_error_not_a_pass():
    with pytest.raises(SmokeError, match="cannot run pgrep"):
        ensure_no_running_mgba(run=runner(raises=FileNotFoundError("pgrep")))


def test_the_process_name_is_a_parameter():
    run = runner(returncode=1)
    ensure_no_running_mgba("mgba-qt", run=run)
    assert run.calls[0][0] == ["pgrep", "-x", "mgba-qt"]


def test_accessibility():
    ensure_accessibility(run=runner(stdout="true\n"))
    with pytest.raises(SmokeError, match="Accessibility"):
        ensure_accessibility(run=runner(stdout="false\n"))
    with pytest.raises(SmokeError, match="Accessibility access is required.*denied"):
        ensure_accessibility(run=runner(returncode=1, stderr="denied"))


def test_load_lua_script_hands_the_path_and_pid_to_osascript(tmp_path):
    run = runner()
    load_lua_script(tmp_path / "smoke.lua", 4242, run=run)
    command, kwargs = run.calls[0]
    assert command == ["osascript", "-", str(tmp_path / "smoke.lua"), "4242"]
    assert kwargs["input"] == SCRIPT_LOADER
    assert kwargs["timeout"] == 45


def test_load_lua_script_failures(tmp_path):
    with pytest.raises(SmokeError, match="script loader failed: no menu"):
        load_lua_script(tmp_path / "s.lua", 1, run=runner(returncode=1, stderr="no menu"))
    with pytest.raises(SmokeError, match="script loader failed"):
        load_lua_script(tmp_path / "s.lua", 1, run=runner(raises=subprocess.TimeoutExpired("osascript", 45)))
    with pytest.raises(SmokeError, match="script loader failed"):
        load_lua_script(tmp_path / "s.lua", 1, run=runner(raises=FileNotFoundError("osascript")))


def test_loader_script_walks_the_scripting_menus():
    for line in ('"Scripting..."', '"Load script..."', 'window "Scripting"', "keystroke scriptPath"):
        assert line in SCRIPT_LOADER
