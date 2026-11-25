import struct

import pytest

from uppercut_pcsx2_pine import PineCommandFailed, PineProtocolError, PineStatus, signed
from uppercut_pcsx2_pine import protocol as p


@pytest.mark.parametrize("bits, opcode", [(8, 0), (16, 1), (32, 2), (64, 3)])
def test_read_command_layout(bits, opcode):
    assert p.read_command(0x0012_3456, bits) == bytes([opcode]) + struct.pack("<I", 0x123456)


@pytest.mark.parametrize("bits, opcode", [(8, 4), (16, 5), (32, 6), (64, 7)])
def test_write_command_layout_is_little_endian(bits, opcode):
    value = (0x0102030405060708) & ((1 << bits) - 1)
    command = p.write_command(0x2000, value, bits)
    assert command[0] == opcode
    assert command[1:5] == struct.pack("<I", 0x2000)
    assert command[5:] == value.to_bytes(bits // 8, "little")


def test_opcodes_match_the_pcsx2_server():
    assert (p.VERSION, p.SAVE_STATE, p.LOAD_STATE, p.TITLE, p.GAME_ID, p.UUID, p.GAME_VERSION, p.STATUS) == tuple(range(8, 16))


def test_frame_counts_its_own_length_field():
    assert p.frame_request(b"\x0f") == struct.pack("<I", 5) + b"\x0f"


def test_frame_refuses_an_oversized_request():
    with pytest.raises(ValueError, match="650000"):
        p.frame_request(bytes(p.MAX_REQUEST_SIZE))
    assert len(p.frame_request(bytes(p.MAX_REQUEST_SIZE - 4))) == p.MAX_REQUEST_SIZE


@pytest.mark.parametrize("bits", [0, 7, 12, 128])
def test_widths_other_than_8_16_32_64_are_refused(bits):
    with pytest.raises(ValueError, match="8, 16, 32 or 64"):
        p.read_command(0, bits)


def test_address_range_and_type():
    p.read_command(0xFFFFFFFC, 32)
    with pytest.raises(ValueError):
        p.read_command(0xFFFFFFFD, 32)  # the last byte would be past 4 GiB
    with pytest.raises(ValueError):
        p.read_command(-1, 8)
    with pytest.raises(TypeError):
        p.read_command("0x10", 8)
    with pytest.raises(TypeError):
        p.read_command(True, 8)


def test_value_must_fit_the_width():
    p.write_command(0, 0xFF, 8)
    with pytest.raises(ValueError, match="8 unsigned bits"):
        p.write_command(0, 0x100, 8)
    with pytest.raises(ValueError):
        p.write_command(0, -1, 32)


@pytest.mark.parametrize("slot", [-1, 256, True])
def test_state_slot_is_one_byte(slot):
    with pytest.raises(ValueError, match="one byte"):
        p.state_command(p.SAVE_STATE, slot)
    assert p.state_command(p.LOAD_STATE, 255) == b"\x0a\xff"


def test_parse_reply_results():
    assert p.parse_reply(b"\x00abc") == b"abc"
    with pytest.raises(PineCommandFailed):
        p.parse_reply(b"\xff")
    with pytest.raises(PineProtocolError, match="0x01"):
        p.parse_reply(b"\x01")
    with pytest.raises(PineProtocolError, match="empty"):
        p.parse_reply(b"")


@pytest.mark.parametrize("total", [0, 4, 450_000, 10_000_000])
def test_reply_length_bounds(total):
    with pytest.raises(PineProtocolError, match="invalid PINE reply length"):
        p.check_reply_length(total)


def test_reply_length_accepts_the_limits():
    assert p.check_reply_length(5) == 5
    assert p.check_reply_length(p.MAX_REPLY_SIZE) == p.MAX_REPLY_SIZE


def test_parse_string():
    assert p.parse_string(struct.pack("<I", 6) + b"Hello\0") == "Hello"
    assert p.parse_string(struct.pack("<I", 1) + b"\0") == ""
    assert p.parse_string(struct.pack("<I", 3) + "é\0".encode()) == "é"  # two bytes of text and the NUL


def test_parse_string_rejects_bad_sizes():
    with pytest.raises(PineProtocolError, match="truncated"):
        p.parse_string(b"\x01")
    with pytest.raises(PineProtocolError, match="says 9"):
        p.parse_string(struct.pack("<I", 9) + b"abc\0")


def test_parse_string_replaces_invalid_utf8():
    assert p.parse_string(struct.pack("<I", 4) + b"a\xffb\0") == "a\ufffdb"


def test_split_values_checks_the_total():
    assert p.split_values(b"\x01\x02\x03\x04\x05\x06\x07", [8, 16, 32]) == [1, 0x0302, 0x07060504]
    with pytest.raises(PineProtocolError, match="expected 7"):
        p.split_values(b"\x01", [8, 16, 32])


def test_signed():
    assert signed(0xFFFF, 16) == -1
    assert signed(0x7FFF, 16) == 0x7FFF
    assert signed(0x80, 8) == -128
    assert signed(0xFFFFFFFFFFFFFFFF, 64) == -1


def test_status_label():
    assert [s.label for s in PineStatus] == ["running", "paused", "shutdown"]
