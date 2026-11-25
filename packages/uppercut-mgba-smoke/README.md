# uppercut-mgba-smoke

Runs a Game Boy Advance ROM in native [mGBA](https://mgba.io) as a smoke test. mGBA starts in a throwaway sandbox (its own config folder and its own save, state, cheat and screenshot folders, muted, no autosave), a Lua script of yours plays the game, and the runner then checks the result and the screenshots before it publishes them. The checks catch a script that failed, a missing or extra screenshot, a blank screenshot, two screenshots with the same pixels, a PNG that is truncated or the wrong size, and a screenshot that no longer matches a reviewed SHA-256. Standard library only; mGBA is not bundled.

Everything about the game is configuration: the ROM, your script, the names of the screenshots, and the RAM addresses and struct offsets your script reads. The package contains none.

```sh
pip install uppercut-mgba-smoke
```

You need an mGBA with its Qt frontend (the one with a scripting window). On macOS the default way to load the script is the Tools > Scripting menu, driven through AppleScript, so the process that runs this needs **Accessibility** access. An mGBA build with a `--script` option can load it from the command line instead (`loader="argument"`), which works on any system.

## Python

```python
from pathlib import Path
from uppercut_mgba_smoke import SmokeConfig, SmokeError, run_smoke

config = SmokeConfig(
    mgba=Path("/Applications/mGBA.app/Contents/MacOS/mGBA"),
    rom=Path("game.gba"),
    lua=Path("tests/smoke.lua"),
    output=Path("validation/native"),
    screenshots=("01-title.png", "02-play.png", "03-end.png"),
    env={"GAME_STATE": "02000010"},      # RAM addresses and offsets for your script, as strings
    reviewed_sha256={"03-end.png": "<64 hex digits>"},   # optional pinned screenshots
)
try:
    result = run_smoke(config)
except SmokeError as error:
    raise SystemExit(f"FAIL mGBA smoke: {error}")
print(result.screenshots)               # name -> SHA-256 of each published PNG
```

`run_smoke` raises `SmokeError` for everything that goes wrong; its message ends with the last 40 lines of mGBA's log. Nothing is published unless the script passed and every screenshot validated. Screenshots and `mgba-smoke-result.txt` (containing `PASS`) are copied into `output` through temporary files and renames, the result file last.

| `SmokeConfig` field | Meaning |
| --- | --- |
| `mgba`, `rom`, `lua`, `output`, `screenshots` | required: the executable, the ROM, your script, the folder to publish into, and the exact PNG names the script must produce |
| `timeout` | seconds to wait for the result (default 90) |
| `env` | extra environment for the script, such as `{"GAME_STATE": "02000010"}` |
| `env_prefix` | for example `"MY_GAME"`: also sets `MY_GAME_OUTPUT` and `MY_GAME_RESULT`, so a script written before this package keeps working |
| `reviewed_sha256` | screenshot name to the SHA-256 of its PNG file |
| `loader` | `"applescript"` (default, macOS) or `"argument"` (`--script`) |
| `scale`, `width`, `height`, `min_colors` | window scale (`-2`), expected screenshot size (240 by 160), and how many distinct colours a screenshot needs to not count as blank (4) |
| `process_name`, `check_running` | the single-instance check: `pgrep -x mGBA`; the run is refused while another mGBA is up, and a `pgrep` that fails or is missing is an error, not "none running" |
| `mgba_options` | extra `-C name=value` options after the isolating ones |

## The Lua helper

The runner writes a helper library into the sandbox and sets `MGBA_SMOKE_HELPER` to it. A script loads it and needs no paths of its own:

```lua
local smoke = dofile(os.getenv("MGBA_SMOKE_HELPER"))
local state = smoke.env_hex("GAME_STATE")
local KEY = smoke.KEY

smoke.run(function()
    smoke.reset()                                  -- no keys, emu:reset(), 120 frames
    smoke.expect_title("EXAMPLE GAME", "AGB-EXMP")
    smoke.expect_memory(state, 0, "title screen")  -- RAM probe: a byte (or 16, 32 bits)
    smoke.shot("01-title")                         -- <output folder>/01-title.png
    smoke.tap(KEY.A)                               -- press 4 frames, release, 3 more
    smoke.expect_memory(state, 1, "after A")       -- failures name the last input
    smoke.shot("02-play")
end)
```

| Function | Does |
| --- | --- |
| `frames(n)`, `tap(key, hold, release)`, `hold(key, n)`, `reset(settle)` | frame stepping and key input |
| `shot(name)` | save `<name>.png` into the output folder |
| `read(address, width)`, `expect_equal(actual, expected, label)`, `expect_memory(address, expected, label, width)`, `expect_title(title, code)`, `fail(label, expected, actual)` | RAM probe and assertions; a failed one raises an error that includes what the last `tap` did |
| `env(name)`, `env_number(name)`, `env_hex(name)` | read the environment, with a clear error when it is missing or not a number |
| `run(body)` | run `body`, then write `PASS`, or `FAIL` and the traceback, to the result file through a temporary name and a rename |
| `KEY` | mGBA's key numbers: `A B SELECT START RIGHT LEFT UP DOWN R L` |

The helper is `uppercut_mgba_smoke.HELPER_LUA`; `write_helper(path)` writes it anywhere. Your script does not have to use it: the runner only needs a file that ends up holding `PASS\n` or `FAIL\n...` at `MGBA_SMOKE_RESULT`.

## Finding a game's state in the ELF

If your script reads a struct in RAM, the offsets can come from the debug ELF instead of being typed in:

```python
from uppercut_mgba_smoke import find_symbol, parse_dwarf_structs, read_dwarf, read_symbols, require_in_ram, require_struct

state = find_symbol(read_symbols(nm, elf), "s_state")      # address and size
require_in_ram(state, "s_state")                           # wholly inside GBA RAM
world = require_struct(parse_dwarf_structs(read_dwarf(readelf, elf)), "World", ["cords", "count"])
env = {"GAME_STATE": f"{state.address:08X}", "GAME_CORDS": str(world.members["cords"])}
```

`nm` and `readelf` are the ones from your toolchain (devkitARM's `arm-none-eabi-nm` and `-readelf`). The names of the symbol and the structs are yours.

## Other pieces

`validate_png(path)` and `validate_screenshot_set(folder, names, ...)` are the screenshot checks on their own. `build_command(config, sandbox)`, `build_environment(config, sandbox)` and `make_sandbox()` give the command line, environment and folders without starting anything. `parse_result(text)` reads the result file.

## Notes

- Nothing here needs mGBA to test. The package's tests build the command lines and environments, run the whole runner against a stand-in executable, and check PNG, ELF and result parsing; one optional test runs the Lua helper under the `lupa` package when it is installed. It has not been run inside a real mGBA, so check `dofile` and the loader on your build once.
- mGBA keeps its settings under `XDG_CONFIG_HOME` (the sandbox's `config` folder), which is what keeps a run from reading or changing yours.
- Screenshots are 240 by 160, 8-bit RGB PNGs, which is what mGBA writes. Other sizes are a parameter.
- Only mGBA's Qt frontend can run a script this way; the SDL frontend cannot.

## License

MIT
