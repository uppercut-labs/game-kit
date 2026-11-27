import os
import socket

import pytest

from port_proof_synthetic import FakeDebugServer
from uppercut_port_proof import DebugClient, DebugError, Press, run_presses, wait_frame, wait_ready


def test_a_call_is_one_json_line_and_one_connection():
    with FakeDebugServer() as server:
        client = DebugClient("127.0.0.1", server.port)
        assert client.call("frame") == {"id": 1, "frame": 7}
        assert client.call("press", buttons=65527, frames=4) == {"id": 2, "ok": True}
        assert client.frame() == 14
    press = [r for r in server.requests if r["cmd"] == "press"][0]
    assert press == {"buttons": 65527, "frames": 4, "id": 2, "cmd": "press"}


def test_a_call_that_gets_no_answer_is_tried_again():
    with FakeDebugServer(drop_first=2) as server:
        client = DebugClient("127.0.0.1", server.port, retry_delay=0.01)
        assert client.call("frame")["frame"] == 7
    assert server.commands() == ["frame"] * 3


def test_a_call_gives_up_after_its_retries_with_a_debug_error_that_is_a_connection_error():
    with FakeDebugServer(drop_first=99) as server:
        client = DebugClient("127.0.0.1", server.port, retries=3, retry_delay=0.001)
        with pytest.raises(DebugError, match="did not answer frame") as caught:
            client.call("frame")
        assert isinstance(caught.value, ConnectionError) and isinstance(caught.value, ValueError)
    assert server.commands() == ["frame"] * 3


def test_a_server_that_never_listens_is_a_debug_error():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    with pytest.raises(DebugError, match="nothing listens"):
        DebugClient("127.0.0.1", port, timeout=0.2)
    client = DebugClient("127.0.0.1", port, wait=False, retries=2, retry_delay=0.001)
    with pytest.raises(DebugError):
        client.call("frame")


def test_wait_frame_returns_the_frame_it_read():
    frames = iter([3, 9, 12, 40])
    assert wait_frame(lambda: next(frames), 10, interval=0) == 12


def test_wait_frame_times_out_and_notices_a_program_that_left():
    with pytest.raises(TimeoutError, match="Frame 100 not reached; last frame 5"):
        wait_frame(lambda: 5, 100, timeout=0.05, interval=0.01)
    with pytest.raises(DebugError, match="exited while waiting for frame 100 \\(last frame 5\\)"):
        wait_frame(lambda: 5, 100, timeout=5, interval=0, alive=lambda: False)


def test_wait_ready_retries_a_ping_until_it_answers():
    state = {"n": 0}

    def ping():
        state["n"] += 1
        if state["n"] < 3:
            raise ConnectionRefusedError("not yet")

    wait_ready(ping, interval=0)
    assert state["n"] == 3
    with pytest.raises(TimeoutError, match="the debug server did not start"):
        wait_ready(lambda: (_ for _ in ()).throw(OSError("no")), timeout=0.05, interval=0.01)
    calls = []
    with pytest.raises(DebugError, match="exited before the debug server answered"):
        wait_ready(lambda: calls.append(1), interval=0, alive=lambda: False)
    assert calls == []  # a program that is already gone is not pinged


def test_run_presses_presses_holds_releases_and_takes_screenshots(tmp_path):
    lines = []
    with FakeDebugServer(step=10) as server:
        client = DebugClient("127.0.0.1", server.port)
        last = run_presses(
            client, [Press(25, ("start",), 2), Press(70, ("cross", "up"), 3)], 120,
            shot_every=50, out_dir=tmp_path / "shots", log=lines.append, poll=0, quit_at_end=True)
    assert last >= 120
    cmds = server.commands()
    assert cmds.count("press") == 2 and cmds.count("clear_input") == 2 and cmds[-1] == "quit"
    presses = [r for r in server.requests if r["cmd"] == "press"]
    assert [(p["buttons"], p["frames"]) for p in presses] == [(0xFFF7, 2), (0xFFFF & ~(1 << 14) & ~(1 << 4), 3)]
    shots = [r for r in server.requests if r["cmd"] == "screenshot"]
    assert [os.path.basename(r["path"]) for r in shots] == ["shot_000050.png", "shot_000100.png"]
    assert (tmp_path / "shots").is_dir()
    # a press is released before the next one starts
    assert cmds.index("clear_input") < cmds.index("press", cmds.index("press") + 1)
    assert lines[0].startswith("connected at frame ") and lines[-1].startswith("done at frame ")
    assert any("press start for 2 (asked for frame 25)" in l for l in lines)
    assert sum(1 for l in lines if "shot shot_" in l) == 2


def test_run_presses_with_no_shots_and_no_quit(tmp_path):
    with FakeDebugServer(step=50) as server:
        run_presses(DebugClient("127.0.0.1", server.port), [], 100, shot_every=0, log=lambda l: None, poll=0)
    assert set(server.commands()) == {"frame"}


def test_quit_that_closes_without_an_answer_is_not_an_error():
    def handler(request):
        return None

    with FakeDebugServer(step=100, handler=handler) as server:
        client = DebugClient("127.0.0.1", server.port, retries=2, retry_delay=0.001)
        server.drop_first = 0
        original = server.answer

        def answer(request):
            return None if request["cmd"] == "quit" else original(request)

        server.answer = answer  # a program that closes the connection as it exits
        run_presses(client, [], 100, shot_every=0, log=lambda l: None, poll=0, quit_at_end=True)
    assert server.commands()[-1] == "quit"
