import struct

import pytest

from uppercut_pcsx2_pine import (
    PineClient,
    PineCommandFailed,
    PineProtocolError,
    PineStatus,
    PineWriteDenied,
    WriteAllowlist,
)
from servers import make_ram, needs_unix

BASE = 0x100000


@pytest.fixture(params=["tcp", pytest.param("unix", marks=needs_unix)])
def client(request):
    return request.getfixturevalue(request.param)


def test_reads_every_width_little_endian(client):
    ram = make_ram()
    assert client.read8(BASE + 3) == ram[3]
    assert client.read16(BASE + 4) == int.from_bytes(ram[4:6], "little")
    assert client.read32(BASE + 8) == int.from_bytes(ram[8:12], "little")
    assert client.read64(BASE + 16) == int.from_bytes(ram[16:24], "little")
    assert client.read(BASE + 16, 64) == client.read64(BASE + 16)


def test_writes_every_width_and_reads_back(client, server):
    client.write8(BASE + 0x100, 0xAB)
    client.write16(BASE + 0x110, 0xBEEF)
    client.write32(BASE + 0x120, 0xDEADBEEF)
    client.write64(BASE + 0x130, 0x0102030405060708)
    assert server.ram[0x100] == 0xAB
    assert bytes(server.ram[0x110:0x112]) == b"\xef\xbe"
    assert bytes(server.ram[0x120:0x124]) == b"\xef\xbe\xad\xde"
    assert bytes(server.ram[0x130:0x138]) == bytes([8, 7, 6, 5, 4, 3, 2, 1])
    assert client.read64(BASE + 0x130) == 0x0102030405060708


def test_read_many_mixes_widths_in_one_message(client, server):
    ram = make_ram()
    values = client.read_many([(BASE, 8), (BASE + 2, 16), (BASE + 4, 32), (BASE + 8, 64)])
    assert values == [ram[0], int.from_bytes(ram[2:4], "little"), int.from_bytes(ram[4:8], "little"), int.from_bytes(ram[8:16], "little")]
    assert len(server.requests) == 1
    assert len(server.requests[0]) == 4 * 5


def test_read_many_splits_long_lists(client, server):
    values = client.read_many([(BASE + i, 8) for i in range(10)], max_per_message=4)
    assert values == list(make_ram()[:10])
    assert [len(r) for r in server.requests] == [20, 20, 10]


def test_read_many_of_nothing_sends_nothing(client, server):
    assert client.read_many([]) == []
    assert server.requests == []


@pytest.mark.parametrize("length", [0, 1, 7, 8, 9, 15, 16, 31, 100])
@pytest.mark.parametrize("offset", [0, 1, 5])
def test_read_bytes_any_length_and_alignment(client, length, offset):
    assert client.read_bytes(BASE + offset, length) == bytes(make_ram()[offset : offset + length])


def test_read_bytes_uses_64_bit_reads_for_the_bulk(client, server):
    client.read_bytes(BASE, 21)
    commands = server.requests[0]
    opcodes = [commands[i] for i in range(0, len(commands), 5)]
    assert opcodes == [3, 3, 2, 0]  # 8 + 8 + 4 + 1


def test_read_bytes_chunks_into_several_messages(client, server):
    data = client.read_bytes(BASE, 1000, chunk=256)
    assert data == bytes(make_ram()[:1000])
    assert len(server.requests) == 4


def test_read_bytes_of_the_fake_ram_edge_and_beyond(client):
    assert len(client.read_bytes(BASE + 0xFF0, 16)) == 16
    with pytest.raises(PineCommandFailed):
        client.read_bytes(BASE + 0xFF8, 16)


def test_write_bytes_roundtrip_and_widths(client, server):
    payload = bytes(range(1, 14))  # 13 bytes: 8 + 4 + 1
    client.write_bytes(BASE + 0x40, payload)
    assert bytes(server.ram[0x40:0x4D]) == payload
    assert [server.requests[0][i] for i in (0, 13, 22)] == [7, 6, 4]


def test_write_bytes_of_nothing_sends_nothing(client, server):
    client.write_bytes(BASE, b"")
    assert server.requests == []


def test_batch_mixes_reads_and_writes(client, server):
    results = client.batch().write32(BASE + 0x200, 0x11223344).read32(BASE + 0x200).read8(BASE + 0x203).execute()
    assert results == [0x11223344, 0x11]
    assert len(server.requests) == 1


def test_empty_batch_sends_nothing(client, server):
    assert client.batch().execute() == []
    assert server.requests == []


def test_batch_refuses_what_cannot_fit_one_reply(client):
    batch = client.batch()
    with pytest.raises(ValueError, match="reply"):
        for _ in range(56_300):
            batch.read64(BASE)
    assert len(batch) == 56_249  # 5 + 8 * 56249 is the last reply under PCSX2's 450,000 byte bound


def test_batch_refuses_what_cannot_fit_one_request(client):
    batch = client.batch()
    with pytest.raises(ValueError, match="request"):
        for _ in range(60_000):
            batch.write64(BASE, 0)
    assert len(batch) == 49_999  # 4 + 13 * 49999 is the last request under 650,000 bytes


def test_strings_and_identity(client, server):
    assert client.version() == "PCSX2 v0.0.0-test"
    assert client.title() == "Synthetic Test Game"
    assert client.game_id() == "TEST-00000"
    assert client.uuid() == "00000000"
    assert client.game_version() == "1.00"
    assert client.identity() == {
        "pcsx2_version": "PCSX2 v0.0.0-test",
        "title": "Synthetic Test Game",
        "game_id": "TEST-00000",
        "uuid": "00000000",
        "game_version": "1.00",
        "status": "running",
    }


def test_non_ascii_title(client, server):
    server.title = "Ōkami ★"
    assert client.title() == "Ōkami ★"


def test_status_values(client, server):
    server.status = PineStatus.PAUSED
    assert client.status() is PineStatus.PAUSED
    server.status = PineStatus.SHUTDOWN
    assert client.status().label == "shutdown"


def test_without_a_game_memory_and_strings_fail_but_status_and_version_answer(client, server):
    server.vm = False
    assert client.status() is PineStatus.SHUTDOWN
    assert client.version() == "PCSX2 v0.0.0-test"
    with pytest.raises(PineCommandFailed):
        client.read32(BASE)
    with pytest.raises(PineCommandFailed):
        client.title()
    with pytest.raises(PineCommandFailed):
        client.save_state(1)


def test_save_and_load_state_send_the_slot_byte(client, server):
    client.save_state(3)
    client.load_state(255)
    client.save_state(0)
    assert server.saved_slots == [3, 0]
    assert server.loaded_slots == [255]
    assert server.requests[0] == b"\x09\x03"
    assert server.requests[1] == b"\x0a\xff"


def test_bad_state_slots_send_nothing(client, server):
    for slot in (-1, 256):
        with pytest.raises(ValueError):
            client.save_state(slot)
    assert server.requests == []


def test_unknown_status_value_is_a_protocol_error(client, server):
    server.reply_hook = lambda body: struct.pack("<IBI", 9, 0, 7) if body == b"\x0f" else None
    with pytest.raises(PineProtocolError, match="unknown PINE status 7"):
        client.status()


def test_reply_of_the_wrong_size_is_a_protocol_error(client, server):
    server.reply_hook = lambda body: struct.pack("<IBH", 7, 0, 1)
    with pytest.raises(PineProtocolError, match="expected 4"):
        client.read32(BASE)


# The write allowlist.


def test_no_allowlist_means_unrestricted(tcp):
    tcp.write8(BASE, 1)


def test_empty_allowlist_makes_the_client_read_only(server):
    port = server.start_tcp()
    with PineClient.tcp(port=port, write_allowlist=[]) as read_only:
        assert read_only.read8(BASE) == 0
        with pytest.raises(PineWriteDenied, match="allowlist"):
            read_only.write8(BASE, 1)
        with pytest.raises(PineWriteDenied):
            read_only.write_bytes(BASE, b"\x01")
        with pytest.raises(PineWriteDenied):
            read_only.batch().write32(BASE, 1)
    assert [len(r) for r in server.requests] == [5]  # only the read went out


def test_denied_write_is_a_permission_error_too(server):
    port = server.start_tcp()
    with PineClient.tcp(port=port, write_allowlist=[]) as read_only, pytest.raises(PermissionError):
        read_only.write8(BASE, 1)


def test_allowlist_ranges_are_start_inclusive_end_exclusive(server):
    port = server.start_tcp()
    allowed = [(BASE + 0x10, BASE + 0x20), (BASE + 0x100, BASE + 0x104)]
    with PineClient.tcp(port=port, write_allowlist=allowed) as guarded:
        guarded.write8(BASE + 0x10, 1)
        guarded.write64(BASE + 0x18, 2)  # ends exactly at the range end
        guarded.write32(BASE + 0x100, 3)
        for address, bits in [(BASE + 0x0F, 8), (BASE + 0x20, 8), (BASE + 0x19, 64), (BASE + 0x102, 32), (BASE + 0x104, 8)]:
            with pytest.raises(PineWriteDenied):
                guarded.write(address, 0, bits)
        guarded.write_bytes(BASE + 0x10, bytes(16))
        with pytest.raises(PineWriteDenied):
            guarded.write_bytes(BASE + 0x10, bytes(17))
    assert len(server.requests) == 4


def test_a_write_range_must_sit_inside_one_allowed_range():
    allowlist = WriteAllowlist([(0x100, 0x110), (0x110, 0x120)])
    assert allowlist.allows(0x100, 16)
    assert not allowlist.allows(0x108, 16)  # spans two ranges


def test_allowlist_validates_its_ranges():
    with pytest.raises(ValueError):
        WriteAllowlist([(0x10, 0x10)])
    with pytest.raises(ValueError):
        WriteAllowlist([(0x20, 0x10)])
    with pytest.raises(ValueError):
        WriteAllowlist([(0, 0x2_0000_0000)])
    allowlist = WriteAllowlist()
    allowlist.add(0x1000, 0x2000)
    assert allowlist.ranges == [(0x1000, 0x2000)]


def test_allowlist_object_can_be_shared_and_extended_later(server):
    port = server.start_tcp()
    allowlist = WriteAllowlist()
    with PineClient.tcp(port=port, write_allowlist=allowlist) as guarded:
        with pytest.raises(PineWriteDenied):
            guarded.write8(BASE, 1)
        allowlist.add(BASE, BASE + 1)
        guarded.write8(BASE, 1)


def test_the_largest_reply_pcsx2_allows_goes_through(client):
    values = client.read_many([(BASE, 64)] * 56_249, max_per_message=100_000)
    assert len(values) == 56_249
    with pytest.raises(ValueError, match="reply"):
        client.read_many([(BASE, 64)] * 56_250, max_per_message=100_000)
