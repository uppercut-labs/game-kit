import pytest

from uppercut_retro_pixels import flatten, rle_pack, rle_runs, rle_unpack, rle_unpack_hex


def test_runs_split_at_changes_and_at_the_cap():
    assert rle_runs([5, 5, 5, 7, 5]) == [(3, 5), (1, 7), (1, 5)]
    assert rle_runs([]) == []
    assert rle_runs([1] * 600) == [(255, 1), (255, 1), (90, 1)]
    assert rle_runs([1] * 5, max_run=2) == [(2, 1), (2, 1), (1, 1)]
    with pytest.raises(ValueError):
        rle_runs([1], max_run=256)


def test_pack_and_unpack_round_trip_one_and_two_byte_values():
    bytes_values = [0] * 300 + [9] * 3 + [255] + [0] * 7
    assert rle_unpack(rle_pack(bytes_values)) == bytes_values
    words = [0x1234] * 260 + [0xFFFF, 0, 0] + [0x8001] * 2
    for endian in ("big", "little"):
        assert rle_unpack(rle_pack(words, value_bytes=2, endian=endian), value_bytes=2, endian=endian) == words


def test_pack_layout():
    assert rle_pack([7, 7, 8]) == bytes([2, 7, 1, 8])
    assert rle_pack([0xABCD] * 2, value_bytes=2) == bytes([2, 0xAB, 0xCD])
    assert rle_pack([0xABCD] * 2, value_bytes=2, endian="little") == bytes([2, 0xCD, 0xAB])


def test_unpack_ignores_a_cut_off_record_and_honours_limit():
    assert rle_unpack(bytes([2, 7, 3])) == [7, 7]
    assert rle_unpack(bytes([2, 7, 5, 8, 9, 1]), limit=4) == [7, 7, 8, 8]
    assert rle_unpack(bytes([200, 1, 200, 2]), limit=250) == [1] * 200 + [2] * 50
    assert rle_unpack(bytes([2, 7]), limit=0) == []


def test_unpack_hex_reads_a_logged_dump():
    text = "03 ffff\n0100 00"  # whitespace anywhere is ignored; a trailing partial record is dropped
    assert rle_unpack_hex("03FFFF 010000") == [0xFFFF] * 3 + [0]
    assert rle_unpack_hex(text) == [0xFFFF] * 3 + [0]
    assert rle_unpack_hex("02ab", value_bytes=1) == [0xAB] * 2


def test_flatten_expands_runs():
    assert flatten([(2, 1), (3, 4)]) == [1, 1, 4, 4, 4]


def test_bad_value_width():
    with pytest.raises(ValueError):
        rle_pack([1], value_bytes=3)
