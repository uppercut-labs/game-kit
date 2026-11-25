"""The package against the four source copies. golden.json holds the SHA-256 of the C text each original
script produced for the shapes in mesh_cases.py; make_goldens.py rebuilds it."""

import json
from pathlib import Path

import pytest

import mesh_cases as cases

GOLDEN = json.loads((Path(__file__).parent / "golden.json").read_text())
PACKAGE = cases.package_cases()


def test_every_case_is_covered_both_ways():
    assert set(PACKAGE) == set(GOLDEN)


@pytest.mark.parametrize("label", sorted(GOLDEN))
def test_package_text_matches_the_copy(label):
    assert PACKAGE[label] == GOLDEN[label]
