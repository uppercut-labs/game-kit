import re

import pytest

from uppercut_mgba_smoke import HELPER_LUA, HELPER_VERSION, SmokeError, parse_result, write_helper


def test_parse_result():
    assert parse_result("PASS\n").passed
    failed = parse_result("FAIL\ntitle: expected 1, got 2\nstack traceback:\n  x\n")
    assert not failed.passed
    assert failed.detail == "title: expected 1, got 2\nstack traceback:\n  x"


@pytest.mark.parametrize("text", ["", "PASS", "PASS\nextra\n", "pass\n", "FAIL", "OK\n", "x" * 500])
def test_anything_else_is_an_error(text):
    with pytest.raises(SmokeError, match="neither PASS nor FAIL"):
        parse_result(text)


def test_error_message_stays_short():
    with pytest.raises(SmokeError) as raised:
        parse_result("x" * 5000)
    assert len(str(raised.value)) < 400


def test_write_helper_round_trips(tmp_path):
    path = write_helper(tmp_path / "helper.lua")
    assert path.read_text(encoding="utf-8") == HELPER_LUA
    assert b"\r" not in path.read_bytes()


def test_helper_text_has_the_api():
    for name in ("frames", "tap", "hold", "reset", "shot", "read", "fail", "expect_equal", "expect_memory", "expect_title", "run", "env", "env_number", "env_hex"):
        assert re.search(rf"^function smoke\.{name}\(", HELPER_LUA, re.MULTILINE), name
    assert HELPER_LUA.rstrip().endswith("return smoke")
    assert f"VERSION = {HELPER_VERSION}" in HELPER_LUA
    assert HELPER_LUA.startswith(f"-- uppercut-mgba-smoke helper, version {HELPER_VERSION}")


def test_helper_key_numbers_are_mgbas():
    keys = dict(re.findall(r"(\w+) = (\d+)", re.search(r"smoke\.KEY = \{(.*?)\}", HELPER_LUA).group(1)))
    assert keys == {"A": "0", "B": "1", "SELECT": "2", "START": "3", "RIGHT": "4", "LEFT": "5", "UP": "6", "DOWN": "7", "R": "8", "L": "9"}


def test_helper_reads_its_paths_from_the_environment_not_from_the_text():
    assert 'smoke.env("MGBA_SMOKE_OUTPUT")' in HELPER_LUA
    assert 'smoke.env("MGBA_SMOKE_RESULT")' in HELPER_LUA
    assert "/Users/" not in HELPER_LUA and "C:\\" not in HELPER_LUA


def test_helper_writes_its_result_atomically():
    body = HELPER_LUA[HELPER_LUA.index("function smoke.run") :]
    assert body.index('result_path .. ".tmp"') < body.index("os.rename(temporary, result_path)")
    assert '"PASS\\n"' in body and '"FAIL\\n"' in body
    assert "debug.traceback" in body


def test_helper_has_balanced_blocks():
    """A cheap syntax guard that does not need a Lua interpreter: every block opener is closed."""
    code = re.sub(r"--[^\n]*", "", HELPER_LUA)
    code = re.sub(r'"(?:\\.|[^"\\])*"', '""', code)
    words = re.findall(r"\b(function|if|for|while|do|end|repeat|until)\b", code)
    depth = 0
    previous = None
    for word in words:
        if word in ("function", "if", "repeat"):
            depth += 1
        elif word == "do" and previous not in ("for", "while"):
            depth += 1
        elif word in ("for", "while"):
            depth += 1
        elif word == "do":
            pass
        elif word in ("end", "until"):
            depth -= 1
        previous = word if word in ("for", "while") else None
        assert depth >= 0
    assert depth == 0
