#!/usr/bin/env python3
"""The end-to-end runner's own test: runs pih_e2e.py with examples/e2e_adapter.py (a made-up port, no SDL).

  python e2e_selftest.py <package dir>

It must pass on the well-behaved port, fail on the one that misreads a controller (naming the check that caught
it), and fail when the adapter can't be loaded. Python 3.8 or later, no packages; the same on every system.
"""
import os
import shutil
import subprocess
import sys
import tempfile


def run(pkg, *extra):
    cmd = [sys.executable, os.path.join(pkg, "e2e", "pih_e2e.py"), "--adapter", os.path.join(pkg, "examples", "e2e_adapter.py"),
           "--profiles", os.path.join(pkg, "profiles"), "--opt", "profiles=" + os.path.join(pkg, "profiles"), *extra]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def main():
    pkg = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    work = tempfile.mkdtemp(prefix="pih-e2e-selftest-")
    failures = []

    code, out = run(pkg, "--work", work)
    if code != 0 or "FAIL" in out:
        failures.append("the well-behaved port should pass every check:\n" + out)
    for must in ("hot-plug: player 1's input stops when the controller goes", "menu: confirm: a dualsense backs out",
                 "menu: noise: a xbox360 stays in", "prompts: playstation and xbox are drawn differently",
                 "prompts: a deck shows the steam prompts the family forced shows"):
        if must not in out:
            failures.append("missing check: " + must)

    code, out = run(pkg, "--work", work, "--opt", "broken=reads")
    if code != 1 or "FAIL  " not in out or "every input the port read equals the script's" not in out:
        failures.append("a port that misreads a controller should fail the transparency check (exit %d):\n%s" % (code, out))

    code, out = run(pkg, "--work", work, "--only", "dualsense,xbox360,switchpro", "--quick")
    if code != 0:
        failures.append("--only and --quick should pass:\n" + out)

    shutil.rmtree(work, ignore_errors=True)
    for line in failures:
        print("e2e selftest FAILED: " + line)
    if not failures:
        print("e2e selftest: 3 runs as expected")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
