"""Python values as Lua source, for the config table a BizHawk Lua script reads."""

from __future__ import annotations

import math
from pathlib import Path, PurePath
from typing import Any, Mapping, Union

PathLike = Union[str, Path]


def lua_string(text: str) -> str:
    """A double-quoted Lua string. Backslash, quote and control characters are escaped the way every Lua 5.x reads them (a
    control character as `\\ddd`, decimal); other characters, accented ones included, stay as they are: Lua 5.1 and 5.4
    do not share a `\\u` escape, so none is used."""
    out = ['"']
    for ch in text:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif ord(ch) < 32 or ord(ch) == 127:
            out.append("\\%03d" % ord(ch))
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def lua_value(value: Any) -> str:
    """A Lua literal for `value`.

    None is `nil`; bools, ints and finite floats are themselves; a str or a path is a string (a path's backslashes become
    slashes, as BizHawk's Lua takes them either way); a list or tuple is an array; a mapping is a table with `[key] = value` entries
    whose keys are strings or ints. NaN and infinity, and any other type, raise `TypeError` or `ValueError`."""
    if value is None:
        return "nil"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        if not math.isfinite(value):
            raise ValueError(f"Lua has no literal for {value!r}")
        return repr(value)
    if isinstance(value, PurePath):
        return lua_string(str(value).replace("\\", "/"))
    if isinstance(value, str):
        return lua_string(value)
    if isinstance(value, (list, tuple)):
        return "{" + ", ".join(lua_value(v) for v in value) + "}"
    if isinstance(value, Mapping):
        items = []
        for key, item in value.items():
            if not isinstance(key, (str, int)):  # a bool is an int
                raise TypeError(f"a Lua table key here is a str or an int, not {type(key).__name__}")
            items.append(f"[{lua_value(key)}] = {lua_value(item)}")
        return "{" + ", ".join(items) + "}"
    raise TypeError(f"no Lua literal for {type(value).__name__}")


def lua_chunk(value: Any) -> str:
    """`return <value>` and a newline: a file a script reads with `dofile`."""
    return "return " + lua_value(value) + "\n"


def write_lua_config(path: PathLike, value: Mapping[str, Any]) -> Path:
    """Write `return { ... }` to `path` (UTF-8, made with its folder if need be) and return the path."""
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(lua_chunk(value), encoding="utf-8", newline="\n")
    return target
