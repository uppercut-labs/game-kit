"""The package against the three source copies. golden.json holds the SHA-256 of what each original
script produced for the inputs in cases.py (make_goldens.py rebuilds it and refuses to write it if the
copies disagree where they should agree)."""

import json
from pathlib import Path

import pytest

import cases

GOLDEN = json.loads((Path(__file__).parent / "golden.json").read_text())
RENDERED = cases.package_cases()


def test_every_case_is_covered_both_ways():
    assert set(RENDERED) == set(GOLDEN)


@pytest.mark.parametrize("label", sorted(GOLDEN))
def test_package_reproduces_the_copy(label):
    import hashlib

    assert hashlib.sha256(RENDERED[label]).hexdigest() == GOLDEN[label]


def test_the_three_copies_agree_on_the_wav_finish():
    """write_wav (edge fade, 0.78 peak limit, 16-bit mono) is where all three copies agree."""
    shared = [k for k in GOLDEN if k.startswith("shared.write_wav.")]
    assert len(shared) == 6


def test_the_two_voicings_differ_where_the_copies_differ():
    songs = [k for k in RENDERED if k.endswith(".song.drive-pulse")]
    assert len(songs) == 2
    assert RENDERED[songs[0]] != RENDERED[songs[1]]
