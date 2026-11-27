import socket
import struct
import threading
import time
from pathlib import Path

import pytest

from uppercut_pcsx2_pine import (
    PineClient,
    PineConnectionError,
    PineProtocolError,
    PineTimeoutError,
    TcpTransport,
    UnixTransport,
    default_socket_path,
)
from servers import needs_unix

BASE = 0x100000


def free_port() -> int:
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def test_default_socket_path_follows_pcsx2():
    env = {"XDG_RUNTIME_DIR": "/run/user/7", "TMPDIR": "/var/tmp/x/"}
    assert default_socket_path(environ=env, platform="linux") == Path("/run/user/7/pcsx2.sock")
    assert default_socket_path(environ=env, platform="darwin") == Path("/var/tmp/x/pcsx2.sock")
    assert default_socket_path(environ={}, platform="linux") == Path("/tmp/pcsx2.sock")
    assert default_socket_path(environ={"XDG_RUNTIME_DIR": ""}, platform="linux") == Path("/tmp/pcsx2.sock")
    assert default_socket_path(environ={"XDG_RUNTIME_DIR": "/r"}, platform="darwin") == Path("/tmp/pcsx2.sock")


def test_non_default_slot_adds_a_suffix():
    env = {"XDG_RUNTIME_DIR": "/run/user/7"}
    assert default_socket_path(28011, environ=env, platform="linux") == Path("/run/user/7/pcsx2.sock")
    assert default_socket_path(28711, environ=env, platform="linux") == Path("/run/user/7/pcsx2.sock.28711")


def test_connection_refused_is_a_connection_error():
    client = PineClient.tcp(port=free_port(), timeout=0.5)
    with pytest.raises(PineConnectionError, match="cannot talk to PCSX2 PINE at 127.0.0.1"):
        client.status()
    with pytest.raises(OSError):  # callers that catch OSError keep working
        client.status()


@needs_unix
def test_missing_unix_socket_is_a_connection_error(tmp_path):
    client = PineClient.unix(tmp_path / "nothing.sock")
    with pytest.raises(PineConnectionError, match="nothing.sock"):
        client.status()


@needs_unix
def test_unix_transport_default_path_uses_the_slot(monkeypatch):
    monkeypatch.setenv("XDG_RUNTIME_DIR", "/run/example")
    monkeypatch.setattr("sys.platform", "linux")
    assert UnixTransport(slot=28712).path == Path("/run/example/pcsx2.sock.28712")


def test_persistent_connection_is_reused(server):
    port = server.start_tcp()
    with PineClient.tcp(port=port) as client:
        for _ in range(5):
            client.status()
    assert server.connections == 1


def test_non_persistent_connection_opens_one_per_request(server):
    port = server.start_tcp()
    client = PineClient.tcp(port=port, persistent=False)
    for _ in range(3):
        client.status()
    assert server.connections == 3


def test_reconnects_after_the_server_drops_the_connection(server):
    port = server.start_tcp()
    client = PineClient.tcp(port=port, timeout=1)
    assert client.status().label == "running"
    server.reply_hook = lambda body: b""  # close without answering
    with pytest.raises(PineConnectionError, match="closed before"):
        client.status()
    server.reply_hook = None
    assert client.status().label == "running"
    assert server.connections == 2
    client.close()


def test_truncated_reply_is_a_connection_error(server):
    port = server.start_tcp()
    server.reply_hook = lambda body: struct.pack("<I", 9) + b"\x00\x00"  # promises 9 bytes, sends 6
    client = PineClient.tcp(port=port, timeout=0.3)
    with pytest.raises((PineConnectionError, PineTimeoutError)):
        client.status()


@pytest.mark.parametrize("length", [0, 4, 450_000, 0xFFFFFFFF])
def test_bad_reply_length_is_a_protocol_error(server, length):
    port = server.start_tcp()
    server.reply_hook = lambda body: struct.pack("<I", length) + b"\x00" * 8
    client = PineClient.tcp(port=port, timeout=0.5)
    with pytest.raises(PineProtocolError, match="invalid PINE reply length"):
        client.status()


def test_protocol_error_drops_the_connection_so_the_next_call_is_in_step(server):
    port = server.start_tcp()
    server.reply_hook = lambda body: struct.pack("<I", 0) + b"\x00" * 8
    client = PineClient.tcp(port=port, timeout=0.5)
    with pytest.raises(PineProtocolError):
        client.status()
    server.reply_hook = None
    assert client.status().label == "running"


def test_unknown_result_byte_is_a_protocol_error(server):
    port = server.start_tcp()
    server.reply_hook = lambda body: struct.pack("<IB", 5, 0x07)
    with pytest.raises(PineProtocolError, match="0x07"):
        PineClient.tcp(port=port).status()


def test_a_silent_server_times_out():
    # A server that accepts every connection and never answers. Accepting matters: a connection left in the
    # backlog and then closed by the client is reset by macOS on the next connect, which is not silence.
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    port = listener.getsockname()[1]
    held = []

    def accept_and_hold():
        while True:
            try:
                connection, _ = listener.accept()
            except OSError:
                return
            held.append(connection)

    threading.Thread(target=accept_and_hold, daemon=True).start()
    try:
        client = PineClient.tcp(port=port, timeout=0.2)
        started = time.monotonic()
        with pytest.raises(PineTimeoutError, match="did not answer"):
            client.status()
        assert time.monotonic() - started < 3
        with pytest.raises(TimeoutError):
            client.status()
    finally:
        listener.close()
        for connection in held:
            connection.close()


def test_a_reply_split_across_packets_is_reassembled(server):
    port = server.start_tcp()
    server.ram[0:8] = bytes(range(1, 9))
    server.base = BASE
    whole = server.execute(b"\x03" + struct.pack("<I", BASE))

    def dribble(body):
        return None

    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    slow_port = listener.getsockname()[1]

    def serve():
        connection, _ = listener.accept()
        with connection:
            size = struct.unpack("<I", connection.recv(4))[0]
            connection.recv(size - 4)
            for byte in whole:
                connection.sendall(bytes([byte]))
                time.sleep(0.002)

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    try:
        assert PineClient.tcp(port=slow_port, timeout=2).read64(BASE) == 0x0807060504030201
    finally:
        thread.join(timeout=3)
        listener.close()
    assert dribble is not None and port


def test_threads_share_one_client_without_mixing_replies(server):
    port = server.start_tcp()
    client = PineClient.tcp(port=port, timeout=5)
    for index in range(256):
        server.ram[index] = index
    failures = []

    def worker(seed):
        for round_ in range(40):
            offset = (seed * 7 + round_) % 200
            if client.read8(BASE + offset) != offset:
                failures.append((seed, offset))

    threads = [threading.Thread(target=worker, args=(n,)) for n in range(8)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    client.close()
    assert failures == []
    assert server.connections == 1


def test_transports_are_context_managers(server):
    port = server.start_tcp()
    with TcpTransport(port=port) as transport:
        assert transport.request(struct.pack("<IB", 5, 0x0F))[0] == 0


def test_stopping_the_server_closes_open_connections(server):
    port = server.start_tcp()
    client = PineClient.tcp(port=port, timeout=1)
    assert client.status().label == "running"
    server.stop()
    with pytest.raises(PineConnectionError):
        client.status()
