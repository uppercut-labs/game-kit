import pytest

from uppercut_pad_replay_tools import ButtonTable

TABLE = ButtonTable({"Select": 0x0001, "Start": 0x0008, "Left": 0x0080, "Cross": 0x4000, "Square": 0x8000})


def test_mask_from_names():
    assert TABLE.mask("Left+Cross") == 0x4080
    assert TABLE.mask("start + square") == 0x8008
    assert TABLE.mask("none") == 0
    assert TABLE.mask("0x0100+Start") == 0x0108


def test_unknown_names():
    for text in ("Circle", "Start+", "", "0x", "0x10000"):
        with pytest.raises(KeyError):
            TABLE.mask(text)


def test_names_from_mask():
    assert TABLE.names(0x4080) == "Left+Cross"
    assert TABLE.names(0) == "none"
    assert TABLE.names(0x0108) == "Start+0x100"


def test_the_table_checks_itself():
    for bad in ({"": 1}, {"a+b": 1}, {"none": 1}, {"x": 0}, {"x": 0x10000}, {"A": 1, "a": 2}):
        with pytest.raises(ValueError):
            ButtonTable(bad)
