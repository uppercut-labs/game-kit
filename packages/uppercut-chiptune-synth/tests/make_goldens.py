"""Regenerates golden.json from the three original generator scripts.

    python make_goldens.py <closed-form-copy-a.py> <closed-form-copy-b.py> <accumulated-copy.py> golden.json

The first two are the NumPy scripts (they need NumPy installed here, and only here); the third is the
stdlib script. Each is loaded from the path given and its own functions are called on the synthetic
inputs in chiptune_cases.py. The run fails if the two NumPy copies disagree on a case, or if any case the copies
share (the write_wav ones) differs between the NumPy and stdlib scripts.
"""

import importlib.util
import json
import sys

import numpy as np

import chiptune_cases as cases


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    a, b, c, out = sys.argv[1:5]
    res = cases.copy_cases(load("copy_a", a), load("copy_b", b), load("copy_c", c), np)
    golden, agreed = {}, 0
    for label, by_copy in sorted(res.items()):
        hashes = set(by_copy.values())
        if len(hashes) != 1:
            print("copies disagree:", label, by_copy)
            return 1
        golden[label] = hashes.pop()
        agreed += len(by_copy) > 1
    with open(out, "w") as fh:
        json.dump(golden, fh, indent=1, sort_keys=True)
        fh.write("\n")
    print(f"{len(golden)} cases, {agreed} rendered by more than one copy with identical bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
