"""Run a Game Boy Advance ROM in native mGBA in an isolated sandbox, let a Lua script press keys, step frames, probe
RAM and take screenshots, then check and publish the screenshots. Standard library only; mGBA is not bundled."""

from .config import RESERVED_ENV, Sandbox, SmokeConfig, build_command, build_environment, make_sandbox
from .elf import (
    EWRAM_END,
    EWRAM_START,
    DwarfStruct,
    Symbol,
    find_symbol,
    parse_dwarf_structs,
    read_dwarf,
    read_symbols,
    require_in_ram,
    require_struct,
)
from .errors import SmokeError
from .host import SCRIPT_LOADER, ensure_accessibility, ensure_no_running_mgba, load_lua_script
from .lua import HELPER_LUA, HELPER_VERSION, LuaResult, parse_result, write_helper
from .png import GBA_HEIGHT, GBA_WIDTH, PngImage, validate_png, validate_screenshot_set
from .runner import RESULT_NAME, SmokeRun, publish_results, run_smoke

__all__ = [
    "EWRAM_END",
    "EWRAM_START",
    "GBA_HEIGHT",
    "GBA_WIDTH",
    "HELPER_LUA",
    "HELPER_VERSION",
    "RESERVED_ENV",
    "RESULT_NAME",
    "SCRIPT_LOADER",
    "DwarfStruct",
    "LuaResult",
    "PngImage",
    "Sandbox",
    "SmokeConfig",
    "SmokeError",
    "SmokeRun",
    "Symbol",
    "build_command",
    "build_environment",
    "ensure_accessibility",
    "ensure_no_running_mgba",
    "find_symbol",
    "load_lua_script",
    "make_sandbox",
    "parse_dwarf_structs",
    "parse_result",
    "publish_results",
    "read_dwarf",
    "read_symbols",
    "require_in_ram",
    "require_struct",
    "run_smoke",
    "validate_png",
    "validate_screenshot_set",
    "write_helper",
]
