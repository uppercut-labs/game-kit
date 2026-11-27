# uppercut-bizhawk-driver

The Python side of a BizHawk automation: install a pinned release and check it, make a private config copy for each run, hand
a Lua script its settings as a table, start the emulator, wait for the script to write `.done` or `.err`, and stop only the
process that was started. Standard library only. BizHawk is not bundled.

```sh
pip install uppercut-bizhawk-driver
```

Requires Python 3.10 or newer. Everything that belongs to one machine (the install folder, the ROM, the Lua script, the output
folder) is an argument; nothing here names a path, a release URL or a hash.

## A run

```python
from uppercut_bizhawk_driver import find_emuhawk, run_capture

result = run_capture(
    find_emuhawk("tools/bizhawk"),            # or a command prefix: ["wine", "/opt/bh/EmuHawk.exe"]
    "game.gb",
    "captures/title/run1",                    # the prefix of everything this run writes
    lua="capture.lua",                        # your script
    settings={"frames": 600, "shots": {"every": 30}, "inputs": [{"from": 300, "to": 302, "buttons": ["Start"]}]},
    base_config="tools/bizhawk/config.ini",   # copied, never edited
    cores={"SNES": "BSNESv115+"},
    timeout=900,
)
print(result.finished, result.rom_sha256, result.returncode)
```

What happens, in order:

1. The ROM must exist; its SHA-256 is taken. Files already starting with the prefix (an earlier run's `.done`, dumps that are
   written only when memory changes and would be read as this run's) raise `FileExistsError`; `stale="remove"` deletes them and
   `stale="keep"` leaves them.
2. With `base_config`, a copy goes to `<prefix>.bizhawk.ini`: sound off, running in the background, the cores you prefer, your
   edits. BizHawk is started with `--config=` that copy, so a BizHawk folder that sibling sessions share is never edited.
3. `settings` is written to `<prefix>.cfg.lua` as `return { out = "<prefix>", ... }`, and the file's path goes in the environment
   variable `BIZHAWK_RUN_CONFIG` (`env_var=` renames it). Your script reads it with `dofile(os.getenv("BIZHAWK_RUN_CONFIG"))`.
4. BizHawk starts with `--config=`, `--lua=` and the ROM last (`--gdi` after `EmuHawkMono.sh`), its output going to
   `<prefix>.emuhawk.log` (at most 16 MiB; the last 200 lines are kept for error messages).
5. The wait looks for `<prefix>.err` first (`RunError` with its text), then `<prefix>.done`. A process that exits with neither
   raises `RunError` with its exit code and what it printed. Nothing in `timeout` seconds stops the process and raises
   `RunTimeout`.
6. After `.done` the process gets `exit_grace` seconds to exit on its own (BizHawk releases its files as it does), then is stopped.
   The ROM's SHA-256 must be unchanged. `<prefix>.run.json` records the command line, the pid, the hash, the exit code and what was
   not muted.

The script's half of the contract is small: write `<out>.done` when it finished, or `<out>.err` with the message when it failed,
and call `client.exit()` when it is done. What it dumps, and when, is its own.

For a caller that has its own launcher (a script that places the window, say), the pieces work alone: `write_lua_config`,
`make_run_config` and `wait_for_done(out, timeout, process=...)`.

## What the guarantees mean

Consistent with [`@uppercut-labs/emulator-session`](../emulator-session), the TypeScript session for other emulators.

- **Muted by default.** Sound is turned off in the run's config copy (`SoundEnabled`, `SoundEnabledNormal`, `SoundEnabledRWFF`). With no
  `base_config` there is no copy, `muted` is false and `warnings` says so.
- **Background running.** `RunInBackground` is set in the copy, so the emulator keeps running and keeps its input without focus.
- **No focus taken.** On Windows the window is shown with `SW_SHOWNOACTIVATE`, and nothing here raises or activates a window.
  Windows may still give a new window focus as it first appears; this package cannot change that. On other systems there is
  nothing to ask for.
- **Stops only its own process.** `stop()` sends SIGTERM (terminate on Windows) to the process it started, and SIGKILL after a grace
  period. On POSIX the process leads a group of its own, so the children of a launcher script die with it. It never looks a
  process up by name, so a second EmuHawk of yours or anyone's is left alone.

## Pieces

| Name | What it does |
| --- | --- |
| `install_bizhawk(destination, url, sha256, replace=False, fetch=None)` | Downloads a release zip, checks its SHA-256 against your pin, unpacks it in a scratch folder next to `destination` and moves it into place. A bad pin, a member that would land outside the folder, or an existing `destination` (without `replace`) raises `InstallError` and leaves nothing behind. |
| `find_emuhawk(root)`, `validate_install(root)` | The launcher under a folder (`EmuHawk.exe` first on Windows, `EmuHawkMono.sh` first elsewhere), and with its `Lua` folder. |
| `lua_value(v)`, `lua_chunk(v)`, `write_lua_config(path, mapping)` | Python to Lua source: `nil`, booleans, numbers, strings, paths (forward slashes), lists and tables. Quotes, backslashes and control characters are escaped; other characters are kept as they are, because Lua versions do not share a `\u` escape. NaN, infinity and unknown types raise. |
| `make_run_config(base, target, edits=, cores=, sound=False, background=True)` | The per-run config copy. A path in an edit is a tuple (`("CoreSettings", "BizHawk.Emulation.Cores.Nintendo.Gameboy.Gameboy", "Muted")`) because a core's name holds dots. A step that is not in the config raises `ConfigError` unless `create=True`. |
| `edit_config_in_place(path, edits, keep_copy=)` | For a one-off set-up of a shared config. Refused while EmuHawk is running (BizHawk rewrites its config on exit). |
| `start_run(...)`, `Run.wait()`, `Run.stop()`, `run_capture(...)`, `wait_for_done(out, ...)` | The run described above. |
| `uppercut_bizhawk_driver.fake_emuhawk` | A stand-in program for tests: it reads the table the driver wrote, then writes `.done`, writes `.err`, exits, hangs or ignores SIGTERM as the table says. |

Command line:

```sh
uppercut-bizhawk-driver install tools/bizhawk --url <release zip URL> --sha256 <the pin>
uppercut-bizhawk-driver edit-config tools/bizhawk/config.ini --set PreferredCores/PSX=Octoshock --copy-to evidence/psx.config.ini
uppercut-bizhawk-driver lua settings.json            # prints the Lua file a script would read
```

## Not here

- The Lua scripts. A capture script is written for a system and a game; this package only starts it and waits for it. The
  contract above is all a script needs.
- Window captures and finding a window: BizHawk's own `client.screenshot` runs inside the script. For other emulators see
  `emulator-session` and [`mac-window-tools`](../mac-window-tools).
- A BizHawk release's address and hash. They are yours to pin.

## What runs where

The run tests start a stand-in program: a shell script that starts `fake_emuhawk` on Linux and macOS, and the Python interpreter run
on `fake_emuhawk.py` on Windows. They need no BizHawk, no ROM beyond a few made-up bytes, no display and no network.

Tested at 1.0.0: `uv run pytest` passes on macOS 27.0 (Apple M6, Python 3.13.18; 57 tests) and on Windows 11 Pro (10.0.26300,
Python 3.14.0; 56 passed, 1 skipped). The skipped test is the SIGTERM one: Windows stops a process with `TerminateProcess`, which
cannot be ignored. On Windows the run tests start the stand-in with the driver's real `STARTUPINFO`, and the stand-in reads it
back with `GetStartupInfoW`: `STARTF_USESHOWWINDOW` is set and `wShowWindow` is 4 (`SW_SHOWNOACTIVATE`). The Windows run found two
faults, both fixed: `out` in `<prefix>.cfg.lua` was written with backslashes where the rest of the file uses forward slashes, and
one test compared a path with `/` separators.

The tests use the stand-in in place of `EmuHawk.exe`, so they run without BizHawk installed.

```sh
uv sync --all-packages && uv run pytest
```

## Licence

MIT, see `LICENSE`.
