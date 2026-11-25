# The replay format

A replay is a plain text file. Each line says what one pad does from one frame on. It is the file a port records from its
real pads and plays back later, and the file a test writes by hand or with a script.

```
# frame slot buttons_hex lx ly connected
0 0 0000 128 128 1
0 1 0000 128 128 0
30 0 0008 128 128 1
33 0 0000 128 128 1
100 0 4000 255 128 1
```

## Lines

- **Event line**: six fields separated by spaces or tabs, in this order.
- **Comment**: a line whose first non-blank character is `#`. A `#` after the fields is not a comment: it is an
  error (see below).
- **Blank line**: ignored.
- Lines end with `\n` or `\r\n`. The last line needs no line break.
- A line is at most 255 bytes, line break not counted. A longer line is an error.
- A NUL byte anywhere in the file is an error.

## The six fields

| # | Name | Written as | Range | Meaning |
| --- | --- | --- | --- | --- |
| 1 | `frame` | decimal | 0 to 4294967295 | The frame the event takes effect on. The game's own frame counter, a host frame, a tick: whatever the player and the writer agree on. |
| 2 | `slot` | decimal | 0 to slots-1 | Which pad. A slot is a physical position (a port, a multitap leg); the reader takes `slots` as an option, 8 by default, at most 255. |
| 3 | `buttons` | hex, at most 4 digits, `0x` prefix allowed | 0000 to ffff | Which buttons are held, as the port's own bit set. The format gives the bits no meaning. |
| 4 | `lx` | decimal | 0 to 255 | Horizontal stick axis, 128 at rest. |
| 5 | `ly` | decimal | 0 to 255 | Vertical stick axis, 128 at rest. |
| 6 | `connected` | decimal | 0 or 1 | 1: a pad is there and these are its values. 0: no pad. A line with 0 is an absent pad whatever the other fields say. |

A number is plain digits (hex digits for field 3): no sign, no `+`, no text glued on, no value past its range.

## Meaning

- An event sets the slot's state from its frame until a later event for the same slot replaces it. There is no
  "release" event: a release is a new event with the buttons cleared.
- Before a slot's first event it is absent (no buttons, sticks at 128, not connected). A slot the file never names stays
  absent.
- Frames must not go down from one line to the next, across all slots. Several lines may share a frame, and they apply in
  file order, so a later line for the same slot and frame wins.
- Playing is `sample(frame)`: apply every event up to and including `frame`, then read the slots. Frames must be asked for
  in rising order.

## What the strict reader refuses

The reader refuses the whole file on its first bad line, so a typo cannot turn into a half replay that passes for a good
one. It reports the status and the line number.

| Status | Cause |
| --- | --- |
| `PADR_ERR_FIELD` | a field missing, not a plain number, or out of range (slot 8 with eight slots, `10000`, `256`, `-1`, `+0`, `4294967296`) |
| `PADR_ERR_TRAILING` | anything after the sixth field, a trailing `# comment` included |
| `PADR_ERR_ORDER` | a frame lower than the line before |
| `PADR_ERR_NUL` | a NUL byte |
| `PADR_ERR_LINE_LONG` | a line over 255 bytes |
| `PADR_ERR_TOO_MANY` | more events than the cap (`PadrOptions.max_events`, 32768 by default) |
| `PADR_ERR_EMPTY` | no event line at all |
| `PADR_ERR_OPEN`, `PADR_ERR_READ` | the file could not be opened or read |

The event cap is a setting because ports differ: one port ran at 32768 events, a console lab at 4096, which is enough only
if the recorder writes changes and not heartbeats (next section).

## What the recorder writes

The recorder is called once per frame with every slot's physical state. It writes a line for a slot the first time it is
seen and again each time that slot's buttons, sticks or connection change, and nothing else. An idle pad costs no lines,
however long the session, so a trace of eight pads over an hour stays far below any cap. (An earlier recorder wrote a line
per slot every 60 frames as a heartbeat: eight pads over about 8.5 minutes were 4096 lines, and the file could no longer
be played back by a reader with a cap of 4096.)

- A slot that reports "unreadable" this frame keeps its earlier state and writes nothing.
- A slot that is not fresh is recorded as absent: `frame slot 0000 128 128 0`.
- The file is created exclusively: an existing trace is never overwritten.
- The file is flushed on frames divisible by `flush_every` (60 by default), so a crash loses about a second.

## Button names (optional)

For capture scripts and logs a port may keep a table of names for its bits, and `padr_buttons_parse` and
`padr_buttons_format` turn `Left+Cross` into a mask and back. The table is the caller's: this package ships none.

## Capture scripts

A capture script is a different, smaller file that some ports' emulator hooks read: `wait N`, `shot name`, `quit`, one per
line (the Python package `uppercut-pad-replay-tools` writes them). It is not read by this package.
