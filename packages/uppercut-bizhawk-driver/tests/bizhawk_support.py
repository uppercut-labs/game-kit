"""Helpers for the tests: a reader for the Lua subset the driver writes, a fake BizHawk folder, and a made-up config.ini."""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from typing import Any, Tuple


class LuaReader:
    """Reads back `return <value>` for the literals `lua_value` writes: nil, booleans, numbers, strings (with `\\ddd` escapes),
    and tables with array items or `[key] = value` entries. It exists so the tests check the output against the input, not against
    a second copy of the writer."""

    def __init__(self, text: str) -> None:
        self.s, self.i = text, 0

    def parse_chunk(self) -> Any:
        self.ws()
        assert self.s.startswith("return", self.i)
        self.i += 6
        value = self.value()
        self.ws()
        assert self.i == len(self.s), f"trailing text: {self.s[self.i:]!r}"
        return value

    def ws(self) -> None:
        while self.i < len(self.s) and self.s[self.i] in " \t\r\n":
            self.i += 1

    def value(self) -> Any:
        self.ws()
        c = self.s[self.i]
        if c == "{":
            return self.table()
        if c == '"':
            return self.string()
        for word, val in (("nil", None), ("true", True), ("false", False)):
            if self.s.startswith(word, self.i):
                self.i += len(word)
                return val
        j = self.i
        while j < len(self.s) and self.s[j] in "+-0123456789.eE":
            j += 1
        text = self.s[self.i:j]
        self.i = j
        return float(text) if any(ch in text for ch in ".eE") else int(text)

    def string(self) -> str:
        assert self.s[self.i] == '"'
        self.i += 1
        out = []
        while self.s[self.i] != '"':
            ch = self.s[self.i]
            if ch == "\\":
                nxt = self.s[self.i + 1]
                if nxt.isdigit():
                    out.append(chr(int(self.s[self.i + 1:self.i + 4])))
                    self.i += 4
                    continue
                out.append({"n": "\n", "r": "\r", "t": "\t", "\\": "\\", '"': '"'}[nxt])
                self.i += 2
                continue
            out.append(ch)
            self.i += 1
        self.i += 1
        return "".join(out)

    def table(self) -> Any:
        self.i += 1
        items: list = []
        entries: dict = {}
        while True:
            self.ws()
            if self.s[self.i] == "}":
                self.i += 1
                break
            if self.s[self.i] == "[":
                self.i += 1
                key = self.value()
                self.ws()
                assert self.s[self.i] == "]"
                self.i += 1
                self.ws()
                assert self.s[self.i] == "="
                self.i += 1
                entries[key] = self.value()
            else:
                items.append(self.value())
            self.ws()
            if self.s[self.i] == ",":
                self.i += 1
        return entries if entries or not items else items


def read_lua(text: str) -> Any:
    return LuaReader(text).parse_chunk()


def fake_bizhawk(root: Path) -> "Path | list[str]":
    """A folder that looks like an install: `EmuHawkMono.sh` (which starts the fake) and a `Lua` folder. Returns the launcher: the
    script on Linux and macOS; on Windows, where a script would leave the fake as a grandchild that `stop` cannot reach, a command
    prefix that runs the fake directly."""
    root.mkdir(parents=True, exist_ok=True)
    (root / "Lua").mkdir(exist_ok=True)
    if os.name == "nt":
        from uppercut_bizhawk_driver import fake_emuhawk

        # the base interpreter, run on the file: a virtual environment's python.exe is a launcher that starts the real one,
        # which would then not be the process the driver started
        return [getattr(sys, "_base_executable", sys.executable), str(Path(fake_emuhawk.__file__))]
    launcher = root / "EmuHawkMono.sh"
    launcher.write_text(f'#!/bin/sh\nexec "{sys.executable}" -m uppercut_bizhawk_driver.fake_emuhawk "$@"\n', encoding="utf-8")
    launcher.chmod(0o755)
    (root / "Lua").mkdir(exist_ok=True)
    return launcher


def made_up_config() -> dict:
    return {
        "PreferredCores": {"SNES": "Snes9x", "PSX": "Nymashock"},
        "CoreSettings": {"BizHawk.Emulation.Cores.Nintendo.Gameboy.Gameboy": {"Muted": False, "DisplayBG": True}},
        "SoundEnabled": True,
        "SoundEnabledNormal": True,
        "SoundEnabledRWFF": True,
        "RunInBackground": False,
        "Unrelated": {"Keep": [1, 2, 3]},
    }


def write_config(path: Path, bom: bool = False) -> Path:
    path.write_text(json.dumps(made_up_config(), indent=1), encoding="utf-8-sig" if bom else "utf-8")
    return path


def split_path(text: str) -> Tuple[str, ...]:
    return tuple(text.split("/"))
