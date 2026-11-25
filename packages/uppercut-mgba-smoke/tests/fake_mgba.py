"""A stand-in for the mGBA executable: a small Python script that behaves the way the runner expects mGBA to.

It is driven by a JSON plan in its own environment (``FAKE_MGBA_PLAN``): it reads the paths the runner gave it (the
``-C screenshotPath=...`` option and ``MGBA_SMOKE_RESULT``), writes the named PNGs and the result file, records its
arguments and environment, and exits or hangs as told. No emulation happens."""

from __future__ import annotations

import json
import stat
import sys
from pathlib import Path
from typing import Any, Optional

from pngs import make_png

SCRIPT = '''#!{python}
import json, os, struct, sys, time, zlib
plan = json.load(open(os.environ["FAKE_MGBA_PLAN"]))
args = sys.argv[1:]
options = dict(a.split("=", 1) for flag, a in zip(args, args[1:]) if flag == "-C")
record = {{"args": args, "options": options, "env": {{k: v for k, v in os.environ.items() if k.startswith(("MGBA_SMOKE", "XDG", "FAKE", "TEST_GAME"))}}}}
open(plan["record"], "w").write(json.dumps(record))
if plan.get("log"):
    print(plan["log"], flush=True)
for name, hex_data in plan.get("shots", {{}}).items():
    open(os.path.join(options["screenshotPath"], name), "wb").write(bytes.fromhex(hex_data))
if plan.get("result") is not None:
    path = os.environ["MGBA_SMOKE_RESULT"]
    open(path + ".tmp", "w").write(plan["result"])
    os.rename(path + ".tmp", path)
if plan.get("hang"):
    time.sleep(60)
sys.exit(plan.get("exit", 0))
'''


def install(folder: Path, *, shots: dict[str, bytes], result: Optional[str] = "PASS\n", **extra: Any) -> tuple[Path, dict[str, str]]:
    """Write the fake executable and its plan into ``folder``. Returns the executable and the environment variables
    (``FAKE_MGBA_PLAN``) the test must export before running it."""
    executable = folder / "fake-mgba"
    executable.write_text(SCRIPT.format(python=sys.executable), encoding="utf-8")
    executable.chmod(executable.stat().st_mode | stat.S_IXUSR)
    plan = folder / "plan.json"
    record = folder / "record.json"
    plan.write_text(
        json.dumps({"shots": {name: data.hex() for name, data in shots.items()}, "result": result, "record": str(record), **extra}),
        encoding="utf-8",
    )
    return executable, {"FAKE_MGBA_PLAN": str(plan)}


def distinct_shots(names: list[str]) -> dict[str, bytes]:
    return {name: make_png(seed=index + 1) for index, name in enumerate(names)}


def recorded(folder: Path) -> dict[str, Any]:
    return json.loads((folder / "record.json").read_text(encoding="utf-8"))
