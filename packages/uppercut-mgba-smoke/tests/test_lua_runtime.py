"""Runs the Lua helper under a real Lua (the ``lupa`` package) against a fake ``emu``. Skipped when lupa is absent."""

import pytest

lupa = pytest.importorskip("lupa")

from uppercut_mgba_smoke import HELPER_LUA  # noqa: E402

FAKE_EMU = """
emu = { frame = 0, keys = {}, mem = {}, log = {}, title = "EXAMPLE", code = "AGB-EXMP" }
function emu:runFrame() self.frame = self.frame + 1 end
function emu:currentFrame() return self.frame end
function emu:addKey(k) table.insert(self.log, "add" .. k) end
function emu:clearKey(k) table.insert(self.log, "clear" .. k) end
function emu:setKeys(k) table.insert(self.log, "setKeys" .. k) end
function emu:reset() table.insert(self.log, "reset") end
function emu:read8(a) return self.mem[a] or 0 end
function emu:read16(a) return (self.mem[a] or 0) + 256 * (self.mem[a + 1] or 0) end
function emu:read32(a) return self:read16(a) + 65536 * self:read16(a + 2) end
function emu:screenshot(p) table.insert(self.log, "shot:" .. p) end
function emu:getGameTitle() return self.title end
function emu:getGameCode() return self.code end
"""


@pytest.fixture
def lua(tmp_path, monkeypatch):
    monkeypatch.setenv("MGBA_SMOKE_OUTPUT", str(tmp_path / "shots"))
    monkeypatch.setenv("MGBA_SMOKE_RESULT", str(tmp_path / "result.txt"))
    monkeypatch.setenv("GAME_STATE", "02000010")
    runtime = lupa.LuaRuntime(unpack_returned_tuples=True)
    runtime.execute(FAKE_EMU)
    helper = tmp_path / "helper.lua"
    helper.write_text(HELPER_LUA)
    smoke = runtime.eval(f'dofile("{helper}")')
    return runtime, smoke, tmp_path / "result.txt"


def test_tap_presses_runs_frames_and_releases(lua):
    runtime, smoke, _ = lua
    smoke.tap(smoke.KEY.START)
    assert list(runtime.eval("emu.log").values()) == ["add3", "clear3"]
    assert runtime.eval("emu.frame") == 7
    smoke.tap(0, 12, 2)
    assert runtime.eval("emu.frame") == 7 + 14
    assert "key=0 hold=12 release=2" in smoke.last_input


def test_env_helpers(lua):
    _, smoke, _ = lua
    assert smoke.env_hex("GAME_STATE") == 0x02000010
    with pytest.raises(lupa.LuaError, match="MISSING_VAR is required"):
        smoke.env("MISSING_VAR")
    with pytest.raises(lupa.LuaError, match="must be numeric"):
        smoke.env_number("PATH")


def test_run_writes_pass(lua):
    runtime, smoke, result = lua
    assert smoke.run(runtime.eval("function() emu.mem[100] = 5 end")) is True
    assert result.read_text() == "PASS\n"
    assert not (result.parent / "result.txt.tmp").exists()


def test_run_writes_fail_with_the_expectation_and_the_last_input(lua):
    runtime, smoke, result = lua

    def failing():
        smoke.tap(smoke.KEY.A)
        smoke.expect_memory(0x02000010, 7, "screen byte")

    assert smoke.run(failing) is False
    text = result.read_text()
    assert text.startswith("FAIL\nscreen byte: expected 7, got 0 [key=0 hold=4")
    assert "stack traceback" in text


def test_expect_memory_widths_and_title(lua):
    runtime, smoke, _ = lua
    runtime.execute("emu.mem[16] = 1; emu.mem[17] = 2; emu.mem[18] = 3; emu.mem[19] = 4")
    smoke.expect_memory(16, 1, "byte")
    smoke.expect_memory(16, 0x0201, "half", 16)
    smoke.expect_memory(16, 0x04030201, "word", 32)
    smoke.expect_title("EXAMPLE", "AGB-EXMP")
    with pytest.raises(lupa.LuaError, match="ROM title: expected OTHER, got EXAMPLE"):
        smoke.expect_title("OTHER")
    with pytest.raises(lupa.LuaError, match="width must be 8, 16 or 32"):
        smoke.read(0, 64)


def test_shot_and_reset(lua):
    runtime, smoke, _ = lua
    smoke.shot("01-title")
    smoke.reset(5)
    log = list(runtime.eval("emu.log").values())
    assert log[0].endswith("/shots/01-title.png") and log[0].startswith("shot:")
    assert log[1:] == ["setKeys0", "reset"]
    assert runtime.eval("emu.frame") == 5
