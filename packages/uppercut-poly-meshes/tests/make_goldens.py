"""Regenerates golden.json from the four original generator scripts.

    python make_goldens.py <copy-a.py> <copy-b.py> <copy-c.py> <copy-d.py> golden.json

copy-a, copy-b and copy-c are the three pure-Python generators that write float vertex arrays
(their `emit()` is run into a temporary folder); copy-d is the script that builds meshes with trimesh
and writes fixed-point and macro-initialised arrays (its write functions are given meshes built here).
It needs NumPy and trimesh installed, and only for this. Each case's text is compared with the
package's own by running the tests afterwards.
"""

import importlib.util
import json
import sys

import numpy as np

import cases


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    a, b, c, d, out = sys.argv[1:6]
    res = cases.copy_cases(load("copy_a", a), load("copy_b", b), load("copy_c", c), load("copy_d", d), np)
    with open(out, "w") as fh:
        json.dump(res, fh, indent=1, sort_keys=True)
        fh.write("\n")
    print(f"{len(res)} cases written")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
