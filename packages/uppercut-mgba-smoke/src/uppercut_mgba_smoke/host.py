"""Checks and helpers that touch the machine: is another mGBA running, may this process drive the menus, and the
AppleScript that loads a script through mGBA's Tools > Scripting window (macOS)."""

from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Callable, Sequence

from .errors import SmokeError

Run = Callable[..., "subprocess.CompletedProcess[str]"]


def _run(command: Sequence[str], label: str, run: Run) -> "subprocess.CompletedProcess[str]":
    try:
        return run(list(command), check=False, capture_output=True, text=True)
    except OSError as error:
        raise SmokeError(f"cannot run {label}: {error}") from error


def ensure_no_running_mgba(process_name: str = "mGBA", *, run: Run = subprocess.run) -> None:
    """Refuse to start while another mGBA is running, so the run cannot touch its windows or settings.

    ``pgrep -x`` exits 0 when it finds a process, 1 when it finds none and anything else when it could not look. The
    result is checked: a missing ``pgrep`` or an exit status of 2 is an error, not "none running".
    """
    result = _run(("pgrep", "-x", process_name), "pgrep", run)
    if result.returncode == 0:
        pids = ", ".join(result.stdout.split()) or "unknown"
        raise SmokeError(f"{process_name} is already running (PID {pids}); close it before the smoke test")
    if result.returncode != 1:
        detail = (result.stderr or result.stdout).strip()
        raise SmokeError(f"cannot check for running {process_name} processes: {detail}")


def ensure_accessibility(*, run: Run = subprocess.run) -> None:
    """Require macOS Accessibility access, which the AppleScript loader needs to click mGBA's menus."""
    result = _run(
        ("osascript", "-e", 'tell application "System Events" to return UI elements enabled'),
        "AppleScript accessibility check",
        run,
    )
    if result.returncode != 0 or result.stdout.strip().lower() != "true":
        detail = (result.stderr or result.stdout).strip()
        raise SmokeError("macOS Accessibility access is required for mGBA scripting automation" + (f": {detail}" if detail else ""))


SCRIPT_LOADER = r'''
on run argv
    set scriptPath to item 1 of argv
    set targetPid to (item 2 of argv) as integer
    tell application "System Events"
        set targetProcess to missing value
        repeat 200 times
            set matches to every application process whose unix id is targetPid
            if (count of matches) is 1 then
                set targetProcess to item 1 of matches
                exit repeat
            end if
            delay 0.1
        end repeat
        if targetProcess is missing value then error "mGBA process did not register with System Events"

        tell targetProcess
            set frontmost to true
            set toolsReady to false
            repeat 200 times
                if exists menu bar item "Tools" of menu bar 1 then
                    if exists menu item "Scripting..." of menu 1 of menu bar item "Tools" of menu bar 1 then
                        set toolsReady to true
                        exit repeat
                    end if
                end if
                delay 0.1
            end repeat
            if not toolsReady then error "mGBA Tools > Scripting menu did not become ready"
            click menu item "Scripting..." of menu 1 of menu bar item "Tools" of menu bar 1

            set scriptingReady to false
            repeat 200 times
                if exists window "Scripting" then
                    set scriptingReady to true
                    exit repeat
                end if
                delay 0.1
            end repeat
            if not scriptingReady then error "mGBA Scripting window did not open"
            perform action "AXRaise" of window "Scripting"
            set frontmost to true

            if not (exists menu item "Load script..." of menu 1 of menu bar item "File" of menu bar 1) then
                error "mGBA File > Load script... menu is unavailable"
            end if
            click menu item "Load script..." of menu 1 of menu bar item "File" of menu bar 1

            set filePanelReady to false
            repeat 100 times
                if exists sheet 1 of window "Scripting" then
                    set filePanelReady to true
                    exit repeat
                end if
                if exists window "Select script to load" then
                    set filePanelReady to true
                    exit repeat
                end if
                delay 0.1
            end repeat
            if not filePanelReady then error "mGBA script file panel did not open"
        end tell

        keystroke "g" using {command down, shift down}
        delay 0.3
        keystroke scriptPath
        key code 36
        delay 0.5
        key code 36
    end tell
end run
'''


def load_lua_script(lua: Path, pid: int, *, run: Run = subprocess.run) -> None:
    """Load ``lua`` into the running mGBA ``pid`` through its Scripting window (macOS, needs Accessibility)."""
    try:
        result = run(
            ["osascript", "-", str(lua), str(pid)],
            input=SCRIPT_LOADER,
            check=False,
            capture_output=True,
            text=True,
            timeout=45,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise SmokeError(f"mGBA script loader failed: {error}") from error
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise SmokeError(f"mGBA script loader failed: {detail}")
