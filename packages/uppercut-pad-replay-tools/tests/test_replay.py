import pytest

from cases import INVALID, PLAYBACK
from uppercut_pad_replay_tools import Event, Replay, ReplayError, parse_replay, read_replay


def test_event_and_tap_helpers_write_the_six_field_file():
    replay = Replay().event(0, 7).tap(10, 7, 8).tap(120, 7, 8, hold=3)
    assert replay.text(header=None) == (
        "0 7 0000 128 128 1\n"
        "10 7 0008 128 128 1\n"
        "11 7 0000 128 128 1\n"
        "120 7 0008 128 128 1\n"
        "123 7 0000 128 128 1\n"
    )
    assert replay.last_frame == 123


def test_header_is_a_comment_and_can_be_left_out():
    replay = Replay().event(0, 0)
    assert replay.text().splitlines()[0] == "# frame slot buttons_hex lx ly connected"
    assert replay.text(header="made up").splitlines()[0] == "# made up"
    assert replay.text(header=None) == "0 0 0000 128 128 1\n"


def test_events_are_listed_by_frame_then_slot_and_the_last_call_for_a_frame_and_slot_wins():
    replay = Replay()
    replay.event(5, 3, 1).event(5, 1, 2).event(0, 7).event(5, 3, 4, lx=10)
    assert [(e.frame, e.slot, e.buttons, e.lx) for e in replay.events] == [
        (0, 7, 0, 128),
        (5, 1, 2, 128),
        (5, 3, 4, 10),
    ]


def test_several_slots_in_one_call():
    replay = Replay().tap(360, [0, 1, 4], 8)
    assert [(e.frame, e.slot, e.buttons) for e in replay.events] == [
        (360, 0, 8), (360, 1, 8), (360, 4, 8), (361, 0, 0), (361, 1, 0), (361, 4, 0)
    ]


def test_a_release_overrides_a_later_press_added_first_like_the_scenario_scripts_do():
    # tap(f) puts the release at f+1; an event already placed at f+1 for that slot is replaced, as in a plain list that
    # is sorted by (frame, slot) and read in order.
    replay = Replay().event(11, 2, 0x40).tap(10, 2, 8)
    assert [e.buttons for e in replay.events] == [8, 0]


def test_disconnect_is_written_at_rest():
    assert Replay().disconnect(9, 2).text(header=None) == "9 2 0000 128 128 0\n"
    assert Event(9, 2, 0xFF, 1, 2, 0).line() == "9 2 0000 128 128 0"


def test_builder_refuses_values_the_reader_would_refuse(tmp_path):
    replay = Replay(slots=4)
    with pytest.raises(ReplayError):
        replay.event(0, 4)
    with pytest.raises(ReplayError):
        replay.event(0, 0, buttons=0x10000)
    with pytest.raises(ReplayError):
        replay.event(0, 0, lx=256)
    with pytest.raises(ReplayError):
        replay.event(-1, 0)
    with pytest.raises(ReplayError):
        replay.event(0, 0, connected=2)
    with pytest.raises(ReplayError):
        replay.tap(0, 0, 8, hold=0)
    with pytest.raises(ReplayError):
        Replay(slots=0)
    with pytest.raises(ReplayError):
        replay.write(tmp_path / "empty.input")


def test_write_then_read_round_trips(tmp_path):
    replay = Replay().event(0, 0).tap(10, 0, 0x4080).event(30, 5, 0, 12, 245).disconnect(40, 5)
    path = replay.write(tmp_path / "out.input")
    assert path.read_bytes().endswith(b"\n") and b"\r" not in path.read_bytes()
    assert read_replay(path) == replay.events


def test_parse_accepts_what_the_c_reader_accepts():
    events = parse_replay(PLAYBACK)
    assert [(e.frame, e.slot, e.buttons, e.lx, e.ly, e.connected) for e in events] == [
        (0, 0, 8, 0, 255, 1),
        (0, 7, 0x4000, 240, 30, 1),
        (3, 0, 0, 128, 128, 1),
        (5, 7, 0, 128, 128, 0),  # a disconnect is read at rest whatever else the line says
        (6, 4, 0x80, 128, 128, 1),
    ]
    assert parse_replay(PLAYBACK.encode()) == events


@pytest.mark.parametrize("label,contents", INVALID, ids=[label for label, _ in INVALID])
def test_parse_refuses_what_the_c_reader_refuses(label, contents):
    with pytest.raises(ReplayError):
        parse_replay(contents)


def test_parse_reports_the_line():
    with pytest.raises(ReplayError) as info:
        parse_replay("0 0 0008 0 255 1\n# c\ninvalid\n")
    assert info.value.line == 3


def test_nul_and_long_lines():
    with pytest.raises(ReplayError) as info:
        parse_replay(b"0 0 0008 0 255 1\0junk\n")
    assert info.value.line == 1 and "NUL" in str(info.value)
    line = "0 0 0000 128 128 1".ljust(255)
    assert len(parse_replay(line + "\n")) == 1
    assert len(parse_replay(line)) == 1  # no line break at the end
    with pytest.raises(ReplayError, match="over 255"):
        parse_replay(line + " \n")


def test_empty_input():
    for text in ("", "\n", "# only a comment"):
        with pytest.raises(ReplayError, match="no events"):
            parse_replay(text)


def test_event_cap_is_a_setting():
    text = "".join(f"{n} 0 0001 128 128 1\n" for n in range(4097))
    with pytest.raises(ReplayError) as info:
        parse_replay(text, max_events=4096)
    assert info.value.line == 4097
    assert len(parse_replay(text)) == 4097
    assert len(parse_replay(text[: text.index("4096 0")], max_events=4096)) == 4096


def test_slots_is_a_setting():
    with pytest.raises(ReplayError):
        parse_replay("0 2 0000 128 128 1\n", slots=2)
    assert parse_replay("0 15 0001 128 128 1\n", slots=16)[0].slot == 15
    with pytest.raises(ReplayError):
        parse_replay("0 0 0000 128 128 1\n", slots=256)


def test_a_idle_session_is_a_short_file():
    # a recorder that writes only changes: eight pads over ten minutes plus one press stay far below any cap
    replay = Replay()
    for slot in range(8):
        replay.event(0, slot, connected=1 if slot < 4 else 0)
    replay.tap(20000, 3, 8, hold=2)
    assert len(parse_replay(replay.text(), max_events=4096)) == 10
