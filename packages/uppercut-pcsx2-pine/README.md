# uppercut-pcsx2-pine

A client for [PCSX2](https://pcsx2.net)'s PINE socket. It reads and writes PlayStation 2 memory (8, 16, 32 and 64 bits, single or batched), asks for the title, status, game id, disc CRC and versions, saves and loads states, and can refuse every write outside a list of allowed ranges. It speaks over a Unix socket (Linux and macOS) or a TCP port on the local machine (Windows). Standard library only.

The wire format was checked against PCSX2's own server source (`pcsx2/PINE.cpp`): opcodes 0 to 15, the length framing, the 650,000-byte request and 449,999-byte reply limits, the 0xFF failure byte, and the socket name PCSX2 gives each slot. The client contains no game addresses; those stay in your code.

```sh
pip install uppercut-pcsx2-pine
```

PINE is off in a stock PCSX2: turn on *Enable PINE* in its settings (or `EnablePINE = true` in the ini) and start a game.

## Python

```python
from uppercut_pcsx2_pine import PineClient, PineCommandFailed, PineConnectionError, signed

# Linux and macOS: the Unix socket PCSX2 made. Windows: PineClient.tcp(port=28011).
with PineClient.unix(slot=28011) as pine:
    print(pine.identity())          # version, title, game id, uuid, game version, status
    hp = signed(pine.read16(0x00345678), 16)
    x, y = pine.read_many([(0x00345680, 32), (0x00345684, 32)])
    blob = pine.read_bytes(0x00345700, 0x240)
    pine.save_state(1)
```

Reads return unsigned integers. `signed(value, bits)` turns one into a two's-complement number.

### Batches

A batch is one PINE message: every command in it runs before the next message is handled. Reads answer in order.

```python
x, y = (
    pine.batch()
    .write32(0x00345800, 1)   # writes are allowed in a batch; they answer nothing
    .read32(0x00345680)
    .read32(0x00345684)
    .execute()
)
```

`read_many` and `read_bytes` build batches for you. A request is limited to 650,000 bytes and a reply to 449,999, so a long list becomes several messages (`read_bytes` moves 32 KiB at a time) and is not one atomic snapshot; a hand-built batch that cannot fit one message raises `ValueError` as you add to it. `read_bytes` uses 64-bit reads for the bulk and 32, 16 and 8-bit reads for the tail, so any length and alignment works.

### Write allowlist

```python
from uppercut_pcsx2_pine import PineClient, PineWriteDenied

read_only = PineClient.unix(write_allowlist=[])                       # no writes at all
guarded = PineClient.unix(write_allowlist=[(0x00400000, 0x00400100)])  # start inclusive, end exclusive
try:
    guarded.write8(0x00500000, 1)
except PineWriteDenied:
    ...  # nothing was sent
```

With no `write_allowlist` argument every write is allowed. A write must lie inside one range. `PineWriteDenied` is also a `PermissionError`. A `WriteAllowlist` object can be passed instead of a list and extended later with `add(start, end)`.

### Save states

`save_state(slot)` and `load_state(slot)` take a slot from 0 to 255. PCSX2 answers at once and does the work later on its emulation thread, so a caller that needs the file must wait for it to appear (or for `status()` to read running again).

### Errors

| Error | When |
| --- | --- |
| `PineConnectionError` | the socket is missing or refused the connection, or closed before a whole reply. Also a `ConnectionError` and so an `OSError` |
| `PineTimeoutError` | no answer within `timeout` (default 2 s). A `PineConnectionError` and a `TimeoutError` |
| `PineCommandFailed` | PCSX2 answered 0xFF. It does this for memory, string and state commands while no game is running. `status()` and `version()` still answer |
| `PineProtocolError` | a reply the protocol does not allow: bad length, unknown result byte, wrong payload size, unknown status |
| `PineWriteDenied` | a write outside the allowlist. A `PermissionError` too |
| `ValueError`, `TypeError` | an address outside 32 bits, a value too wide for its width, a width other than 8, 16, 32 or 64 |

All of them except the last two are `PineError`. After any error but `PineCommandFailed` and `PineWriteDenied` the connection is dropped and the next call opens a new one.

### Sockets

| | |
| --- | --- |
| `PineClient.unix(path=None, slot=28011)` | Unix socket. With no path, `default_socket_path(slot)`: `$TMPDIR` on macOS, `$XDG_RUNTIME_DIR` elsewhere, then `/tmp`; the file is `pcsx2.sock`, plus `.<slot>` when the slot is not 28011 |
| `PineClient.tcp(host="127.0.0.1", port=28011)` | TCP, what PCSX2 on Windows listens on |
| `PineClient()` | the Unix socket, or TCP on Windows |

Connections are kept open between calls by default; pass `persistent=False` to open one per request. One client can be shared by threads: requests are serialised.

## Testing your own code

`uppercut_pcsx2_pine.fake_server.FakePineServer` is a stand-in server for tests. It runs in your process on TCP or a Unix socket, follows PCSX2's rules (a bad command fails the whole message, memory commands fail without a game, the size limits are the real ones), keeps memory in a `bytearray` and records the requests it received. No emulator, BIOS or game is needed.

```python
from uppercut_pcsx2_pine import PineClient
from uppercut_pcsx2_pine.fake_server import FakePineServer

with FakePineServer(bytearray(0x1000), base=0x100000) as server:
    with PineClient.tcp(port=server.start_tcp()) as pine:
        pine.write32(0x100000, 0xDEADBEEF)
        assert pine.read8(0x100003) == 0xDE
```

## Notes

- The PINE protocol belongs to PCSX2 (GPL-3.0-or-later). This package is an independent client written from the wire format and shares no PCSX2 code.
- Memory reads and writes go through PCSX2's own memory mapping, so addresses are the guest's virtual addresses.
- PCSX2 does not report a failed save or load over PINE; check the file.

## License

MIT
