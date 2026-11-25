"""The Lua half: a helper library the smoke script loads, and the reader for the result file it writes.

The helper gives a script frame stepping, key taps, RAM reads, screenshots, assertions and an atomic PASS or FAIL
result file. It reads the output folder and the result path from the environment the runner sets
(``MGBA_SMOKE_OUTPUT`` and ``MGBA_SMOKE_RESULT``), so a script contains no paths. A script loads it with::

    local smoke = dofile(os.getenv("MGBA_SMOKE_HELPER"))
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .errors import SmokeError

HELPER_VERSION = 1

HELPER_LUA = r'''-- uppercut-mgba-smoke helper, version 1. Load it with: local smoke = dofile(os.getenv("MGBA_SMOKE_HELPER"))
local smoke = { VERSION = 1 }

-- mGBA's GBA key numbers, for emu:addKey, emu:clearKey and the helpers below.
smoke.KEY = { A = 0, B = 1, SELECT = 2, START = 3, RIGHT = 4, LEFT = 5, UP = 6, DOWN = 7, R = 8, L = 9 }

function smoke.env(name)
    return (assert(os.getenv(name), name .. " is required"))
end

function smoke.env_number(name)
    return (assert(tonumber(smoke.env(name)), name .. " must be numeric"))
end

function smoke.env_hex(name)
    return (assert(tonumber(smoke.env(name), 16), name .. " must be hexadecimal"))
end

local output_dir = smoke.env("MGBA_SMOKE_OUTPUT")
local result_path = smoke.env("MGBA_SMOKE_RESULT")

-- What the last tap did, so a failed expectation can say which input led to it.
smoke.last_input = "no input yet"

function smoke.frames(count)
    for _ = 1, count do
        emu:runFrame()
    end
end

-- Press a key for hold_frames (default 4), release it, and run release_frames more (default 3).
function smoke.tap(key, hold_frames, release_frames)
    hold_frames = hold_frames or 4
    release_frames = release_frames or 3
    local before = emu:currentFrame()
    emu:addKey(key)
    smoke.frames(hold_frames)
    emu:clearKey(key)
    smoke.frames(release_frames)
    smoke.last_input = string.format("key=%d hold=%d release=%d frame=%d->%d",
        key, hold_frames, release_frames, before, emu:currentFrame())
end

-- Hold a key for the given number of frames, then release it and run one more.
function smoke.hold(key, frame_count)
    emu:addKey(key)
    smoke.frames(frame_count)
    emu:clearKey(key)
    emu:runFrame()
end

-- Reset the console with no keys down and run settle_frames (default 120) so the game reaches its first screen.
function smoke.reset(settle_frames)
    emu:setKeys(0)
    emu:reset()
    smoke.frames(settle_frames or 120)
end

-- Save <output folder>/<name>.png. The runner then checks the PNG.
function smoke.shot(name)
    emu:screenshot(output_dir .. "/" .. name .. ".png")
end

function smoke.read(address, width)
    if width == 8 then return emu:read8(address) end
    if width == 16 then return emu:read16(address) end
    if width == 32 then return emu:read32(address) end
    error("read width must be 8, 16 or 32, got " .. tostring(width))
end

function smoke.fail(label, expected, actual)
    error(string.format("%s: expected %s, got %s [%s]", label, tostring(expected), tostring(actual), smoke.last_input), 0)
end

function smoke.expect_equal(actual, expected, label)
    if actual ~= expected then
        smoke.fail(label, expected, actual)
    end
end

-- Read a byte (or 16 or 32 bits) of console memory and require it to equal expected.
function smoke.expect_memory(address, expected, label, width)
    smoke.expect_equal(smoke.read(address, width or 8), expected, label)
end

function smoke.expect_title(title, game_code)
    smoke.expect_equal(emu:getGameTitle(), title, "ROM title")
    if game_code ~= nil then
        smoke.expect_equal(emu:getGameCode(), game_code, "ROM game code")
    end
end

-- Run body(), then write PASS, or FAIL and the error with its traceback, to the result file. The file is written
-- under a temporary name and renamed, so the runner never reads half of it. Returns true when body passed.
function smoke.run(body)
    local ok, failure = xpcall(body, debug.traceback)
    emu:setKeys(0)
    local temporary = result_path .. ".tmp"
    os.remove(temporary)
    local file = assert(io.open(temporary, "w"))
    if ok then
        file:write("PASS\n")
    else
        file:write("FAIL\n", tostring(failure), "\n")
    end
    assert(file:close())
    assert(os.rename(temporary, result_path))
    return ok
end

return smoke
'''


@dataclass(frozen=True)
class LuaResult:
    passed: bool
    detail: str
    """The text after ``FAIL``, or an empty string for a pass."""


def parse_result(text: str) -> LuaResult:
    """Read the result file a script wrote: exactly ``PASS`` and a newline, or ``FAIL`` and a newline then the error."""
    if text == "PASS\n":
        return LuaResult(True, "")
    if text.startswith("FAIL\n"):
        return LuaResult(False, text[len("FAIL\n") :].rstrip())
    shown = text if len(text) <= 200 else text[:200] + "..."
    raise SmokeError(f"the Lua smoke result is neither PASS nor FAIL: {shown!r}")


def write_helper(path: Path) -> Path:
    """Write the helper library to ``path`` (the runner puts it in the sandbox) and return the path."""
    path = Path(path)
    path.write_text(HELPER_LUA, encoding="utf-8", newline="\n")
    return path
