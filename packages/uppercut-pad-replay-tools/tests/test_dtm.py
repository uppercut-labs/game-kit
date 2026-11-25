import struct

import pytest

from uppercut_pad_replay_tools import DtmBuilder, DtmError, GCButton, Replay, dtm_header, gc_pad, replay_to_dtm


def test_pad_bytes():
    assert gc_pad() == bytes([0x00, 0x40, 0, 0, 128, 128, 128, 128])
    assert gc_pad(GCButton.START | GCButton.A) == struct.pack("<H6B", 0x4003, 0, 0, 128, 128, 128, 128)
    assert gc_pad(0, connected=False)[:2] == b"\x00\x00"
    assert gc_pad(0x4000)[:2] == b"\x00\x40"
    assert gc_pad(stick=(1, 2), c_stick=(3, 4), trigger_l=5, trigger_r=6) == bytes([0, 0x40, 5, 6, 1, 2, 3, 4])
    with pytest.raises(DtmError):
        gc_pad(stick=(256, 0))


def test_the_bit_order_is_dolphins():
    assert [int(b) for b in (GCButton.START, GCButton.A, GCButton.B, GCButton.X, GCButton.Y, GCButton.Z)] == [
        1, 2, 4, 8, 16, 32
    ]
    assert GCButton.CONNECTED == 0x4000 and GCButton.GET_ORIGIN == 0x8000


def test_header_fields_sit_at_the_offsets_dolphin_reads():
    header = dtm_header("ID-abc", 1800, [0, 2], author="Made Up")
    assert len(header) == 256
    assert header[:4] == b"DTM\x1a"
    assert header[4:10] == b"ID-abc"
    assert header[10] == 0 and header[12] == 0
    assert header[11] == 0b0101  # ports 1 and 3
    assert struct.unpack_from("<Q", header, 13) == (1800,)
    assert struct.unpack_from("<Q", header, 21) == (3600,)  # an input per pad per frame
    assert header[49:57] == b"Made Up" + b"\0"
    assert struct.unpack_from("<Q", header, 237) == (0xFFFFFFFFFFFFFFFF,)
    assert header[29:37] == bytes(8)  # lag count


def test_short_game_ids_are_padded_and_bad_arguments_refused():
    assert dtm_header("AB", 0, [0])[4:10] == b"AB\0\0\0\0"
    for call in (
        lambda: dtm_header("seven77", 0, [0]),
        lambda: dtm_header("é", 0, [0]),
        lambda: dtm_header("A", 0, []),
        lambda: dtm_header("A", 0, [4]),
        lambda: dtm_header("A", 0, [1, 1]),
        lambda: dtm_header("A", -1, [0]),
    ):
        with pytest.raises(DtmError):
            call()


def test_builder_writes_pads_in_port_order(tmp_path):
    builder = DtmBuilder("ID-abc", ports=(2, 0))
    builder.add_frame({0: gc_pad(GCButton.START)})
    builder.add_frame({2: gc_pad(GCButton.A), 0: gc_pad(GCButton.B)})
    data = builder.to_bytes()
    assert len(builder) == 2 and len(data) == 256 + 2 * 2 * 8
    body = data[256:]
    assert body[0:8] == gc_pad(GCButton.START)  # port 1 first
    assert body[8:16] == gc_pad()  # port 3 at rest
    assert body[16:24] == gc_pad(GCButton.B) and body[24:32] == gc_pad(GCButton.A)
    assert builder.write(tmp_path / "x.dtm").read_bytes() == data
    with pytest.raises(DtmError):
        builder.add_frame({1: gc_pad()})
    with pytest.raises(DtmError):
        builder.add_frame({0: b"short"})


def test_replay_to_dtm_follows_the_replay():
    replay = Replay().event(0, 0).tap(2, 0, 0x0001, hold=2).tap(3, 1, 0x0002).event(4, 1, 0, 200, 50)
    movie = replay_to_dtm(replay.events, 6, game_id="ID-abc", ports={0: 0, 1: 2})
    data = movie.to_bytes()
    pads = [data[256 + i * 8 : 256 + i * 8 + 8] for i in range(12)]
    port1 = pads[0::2]
    port3 = pads[1::2]
    assert port1 == [gc_pad(), gc_pad(), gc_pad(GCButton.START), gc_pad(GCButton.START), gc_pad(), gc_pad()]
    assert port3 == [gc_pad(), gc_pad(), gc_pad(), gc_pad(GCButton.A), gc_pad(stick=(200, 50)), gc_pad(stick=(200, 50))]
    assert data[11] == 0b0101


def test_replay_to_dtm_with_a_button_map_and_a_disconnect():
    replay = Replay().event(0, 0, 0x4001).disconnect(2, 0)
    movie = replay_to_dtm(
        replay.events, 3, game_id="G", ports={0: 3}, buttons={0x0001: GCButton.START, 0x4000: GCButton.Y}
    )
    pads = [movie.to_bytes()[256 + i * 8 : 256 + i * 8 + 8] for i in range(3)]
    assert pads == [gc_pad(GCButton.START | GCButton.Y), gc_pad(GCButton.START | GCButton.Y), gc_pad()]


def test_replay_to_dtm_refuses_two_slots_on_a_port():
    with pytest.raises(DtmError):
        replay_to_dtm([], 1, game_id="G", ports={0: 0, 1: 0})
    with pytest.raises(DtmError):
        replay_to_dtm([], 1, game_id="G", ports={})
