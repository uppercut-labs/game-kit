import math
from pathlib import Path, PureWindowsPath

import pytest

from bizhawk_support import read_lua
from uppercut_bizhawk_driver import lua_chunk, lua_string, lua_value, write_lua_config


def test_scalars():
    assert lua_value(None) == "nil"
    assert (lua_value(True), lua_value(False)) == ("true", "false")
    assert lua_value(7) == "7" and lua_value(-3) == "-3" and lua_value(0.5) == "0.5" and lua_value(1e22) == "1e+22"
    assert lua_value(True) != "1"  # a bool is not the int it subclasses


def test_strings_escape_what_lua_reads_the_same_in_every_version():
    assert lua_string('a"b\\c') == '"a\\"b\\\\c"'
    assert lua_string("two\nlines\tand\r") == '"two\\nlines\\tand\\r"'
    assert lua_string("bell\x07 nul\x00 del\x7f") == '"bell\\007 nul\\000 del\\127"'
    assert lua_string("caf\u00e9 \u30b2\u30fc\u30e0") == '"caf\u00e9 \u30b2\u30fc\u30e0"'  # kept as characters: no \u escape


def test_paths_use_forward_slashes():
    assert lua_value(Path("a/b c/d.png")) == '"a/b c/d.png"'
    assert lua_value(PureWindowsPath("E:\\captures\\run 1")) == '"E:/captures/run 1"'
    assert lua_value("E:\\x") == '"E:\\\\x"'  # a plain string is the caller's: nothing is guessed about it


def test_tables():
    assert lua_value([]) == "{}" and lua_value({}) == "{}"
    assert lua_value([1, "a", None, True]) == '{1, "a", nil, true}'
    assert lua_value({"a": 1, 3000: "title"}) == '{["a"] = 1, [3000] = "title"}'
    assert lua_value(((300, "P1 Start", 2),)) == '{{300, "P1 Start", 2}}'


@pytest.mark.parametrize("bad", [float("nan"), math.inf, -math.inf])
def test_what_lua_has_no_literal_for_is_refused(bad):
    with pytest.raises(ValueError):
        lua_value(bad)
    with pytest.raises(ValueError):
        lua_value({"x": [bad]})


@pytest.mark.parametrize("bad", [object(), b"bytes", {1, 2}, {(1, 2): 3}, {1.5: 2}])
def test_other_types_are_refused(bad):
    with pytest.raises(TypeError):
        lua_value(bad)


def test_a_config_table_reads_back_as_what_was_written(tmp_path):
    spec = {
        "out": tmp_path / "run 1" / "cap",
        "frames": 600,
        "pass": "full",
        "layers": {"bg": True, "obj": False},
        "inputs": [{"from": 10, "to": 20, "buttons": ["A", "Start"]}],
        "shots": {"from": 300, "every": 30},
        "states": {3000: "title", 6000: 'q"uote\\'},
        "speed": 6400.5,
        "note": "line one\nline two \u00e9\x01",
        "none": None,
    }
    path = write_lua_config(tmp_path / "x" / "cfg.lua", spec)
    assert path.read_text(encoding="utf-8").startswith("return {")
    expected = dict(spec, out=str(spec["out"]).replace("\\", "/"))
    expected["none"] = None
    got = read_lua(path.read_text(encoding="utf-8"))
    got_none = got.pop("none", None)  # nil entries have no key in a Lua table
    expected.pop("none")
    assert got == expected and got_none is None
    assert lua_chunk([1, 2]) == "return {1, 2}\n"
