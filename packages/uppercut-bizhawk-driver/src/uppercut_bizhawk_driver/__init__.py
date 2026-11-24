"""Drive BizHawk from Python."""

__version__ = "0.1.0"

from .config import (  # noqa: E402
    SOUND_KEYS, dump_config, edit_config_in_place, emuhawk_running, load_config, make_run_config, mute, parse_assignment, prefer_core,
    run_in_background, set_path,
)
from .errors import BizhawkError, ConfigError, InstallError, RunError, RunTimeout  # noqa: E402
from .install import find_emuhawk, install_bizhawk, safe_extract, sha256_file, validate_install  # noqa: E402
from .lua import lua_chunk, lua_string, lua_value, write_lua_config  # noqa: E402
from .run import DEFAULT_ENV_VAR, Run, RunResult, build_command, run_capture, start_run, wait_for_done, write_evidence  # noqa: E402

__all__ = [
    "BizhawkError", "ConfigError", "DEFAULT_ENV_VAR", "InstallError", "Run", "RunError", "RunResult", "RunTimeout", "SOUND_KEYS",
    "build_command", "dump_config", "edit_config_in_place", "emuhawk_running", "find_emuhawk", "install_bizhawk", "load_config",
    "lua_chunk", "lua_string", "lua_value", "make_run_config", "mute", "parse_assignment", "prefer_core", "run_capture",
    "run_in_background", "safe_extract", "set_path", "sha256_file", "start_run", "validate_install", "wait_for_done", "write_evidence",
    "write_lua_config",
]
