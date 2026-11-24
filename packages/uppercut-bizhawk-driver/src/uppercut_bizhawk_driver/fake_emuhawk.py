"""A stand-in for EmuHawk, for tests: `python -m uppercut_bizhawk_driver.fake_emuhawk [--gdi] [--config=F] [--lua=F] [--chromeless] ROM`.

It reads the config table the driver wrote (the file named by the environment variable `BIZHAWK_RUN_CONFIG`, or by
`FAKE_EMUHAWK_ENV`), takes the output prefix from its `out` key and behaves as its `fake_mode` key says:

- `done` (default): print a line, write `<out>_frames.jsonl`, `<out>.argv.json` and `<out>.done`, exit 0;
- `err`: write `<out>.err` and exit 0;
- `exit`: exit with code `fake_code` (default 3) and write nothing;
- `hang`: wait to be stopped, writing nothing;
- `done_hang`: write `.done`, then wait to be stopped;
- `stubborn`: ignore SIGTERM and wait (the stop has to escalate to SIGKILL);
- `edit_rom`: change the ROM file, then write `.done`.

`fake_delay` seconds are slept first. No BizHawk code or file is used. It is a fixture, not an emulator."""

from __future__ import annotations

import json
import os
import re
import signal
import sys
import time
from pathlib import Path


def _lua_strings(text: str) -> "dict[str, str]":
    """The `["key"] = "value"` and `["key"] = number` entries of the one-level table the driver writes."""
    found: "dict[str, str]" = {}
    for key, value in re.findall(r'\["((?:[^"\\]|\\.)*)"\] = ("(?:[^"\\]|\\.)*"|-?[0-9.]+|true|false)', text):
        if value.startswith('"'):
            value = re.sub(r"\\(.)", r"\1", value[1:-1])
        found[key] = value
    return found


def main(argv: "list[str]") -> int:
    args = [a for a in argv if a.startswith("--")]
    rom = [a for a in argv if not a.startswith("--")][-1]
    env_name = os.environ.get("FAKE_EMUHAWK_ENV", "BIZHAWK_RUN_CONFIG")
    cfg_path = os.environ.get(env_name)
    if not cfg_path:
        print(f"fake EmuHawk: {env_name} is not set", file=sys.stderr)
        return 4
    table = _lua_strings(Path(cfg_path).read_text(encoding="utf-8"))
    out = table["out"]
    mode = table.get("fake_mode", "done")
    print("fake EmuHawk starting", flush=True)
    for n in range(int(table.get("fake_noise", 0))):
        print(f"noise line {n}", flush=True)
    time.sleep(float(table.get("fake_delay", 0)))
    config_arg = next((a[len("--config="):] for a in args if a.startswith("--config=")), None)
    record = {
        "argv": argv,
        "rom": rom,
        "config": json.loads(Path(config_arg).read_text(encoding="utf-8")) if config_arg and Path(config_arg).is_file() else None,
        "env_name": env_name,
        "table": table,
    }
    if mode == "exit":
        return int(table.get("fake_code", 3))
    if mode == "stubborn":
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
    if mode in ("hang", "stubborn"):
        while True:
            time.sleep(0.05)
    if mode == "err":
        Path(f"{out}.err").write_text("script error: boom\n", encoding="utf-8")
        return 0
    if mode == "edit_rom":
        Path(rom).write_bytes(b"changed")
    Path(f"{out}.argv.json").write_text(json.dumps(record), encoding="utf-8")
    Path(f"{out}_frames.jsonl").write_text('{"frame": 1}\n', encoding="utf-8")
    Path(f"{out}.done").write_text("", encoding="utf-8")
    if mode == "done_hang":
        while True:
            time.sleep(0.05)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
