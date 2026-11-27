# save-block

Saved data that survives a power cut, a wrong disc and a bad card. A game gives this package its settings or progress
as a block of bytes and two storage callbacks; it keeps the block in two alternating slots, each a versioned record with
a checksum, loads the newest valid one, and reads every save back before calling it done. A save that is torn, damaged
or from another version is refused instead of being read as if it were good, and the previous save is never touched
until the new one has verified.

It has no platform code: storage is the caller's `read` and `write` callbacks (a file, a memory-card slot, EEPROM,
battery RAM), so the same logic runs on a console and, with the RAM storage in the package, in a plain unit test.

Version 1.0.0. C (C99) with CMake, no platform headers. Uses [det-core](../det-core) for FNV-1a. MIT licence.

## Install

This is a C library, so it is not on a package index. A project takes it at a release tag, with CMake `FetchContent`
(CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(save_block
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        save-block@1.0.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/save-block)
FetchContent_MakeAvailable(save_block)

target_link_libraries(my-game PRIVATE save_block::save_block)
```

`examples/consumer` is that, complete, and the kit's own check builds it. For an offline build set
`-DFETCHCONTENT_SOURCE_DIR_SAVE_BLOCK=<a checkout of game-kit>`. A project that vendors the folder uses
`add_subdirectory(<path>/save-block)`.

**det-core.** save-block gets its checksum from det-core and finds it in this order: a `det_core::det_core` target the
project already made (declare det-core first with its own `FetchContent_Declare` at the tag you want and save-block
uses that one); then the `det-core` folder next to it (a clone or checkout of game-kit has both); then a download of
`det-core@1.0.0` (when the folder was packed on its own; needs git and network, or
`-DFETCHCONTENT_SOURCE_DIR_DET_CORE=<a checkout of game-kit>`). A build system other than CMake needs `include/` of both
packages on the include path, and `src/record.c`, `src/arcade.c` and optionally `src/memory.c` compiled.

## The record

Every number is little-endian. A format fixes the magic, the version and the payload size; every record of a format has
the same length.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic, chosen by the game (`'A','R','C','1'`) |
| 4 | 2 | version, chosen by the game |
| 6 | 2 | payload size |
| 8 | 4 | sequence: counts saves, compared with wrap-around arithmetic (0 is newer than 4294967295) |
| 12 | n | payload |
| 12 + n | 4 | checksum: FNV-1a over every byte before it |

## Using it

```c
#include "sb/record.h"

static const sb_format format = { { 'S', 'A', 'V', '1' }, 1u /* version */, 16u /* payload bytes */ };

/* the storage: two slots, however the platform provides them */
static sb_result my_read(void* context, uint8_t slot, uint8_t* buffer, size_t capacity, size_t* length);
static sb_result my_write(void* context, uint8_t slot, const uint8_t* buffer, size_t length);

sb_store store = { format, { &my_device, my_read, my_write }, NULL, NULL };

uint8_t payload[16];
/* ... lay the game's data out with sb/codec.h ... */
sb_result r = sb_save(&store, payload);   /* SB_OK, or why not: sb_result_text(r) */

r = sb_load(&store, payload, NULL);        /* newest valid record; SB_EMPTY the first time */
```

### Storage callbacks

- `read(context, slot, buffer, capacity, &length)`: copy up to `capacity` bytes of slot 0 or 1 into `buffer`, set
  `*length` to the slot's full size, return `SB_OK`. A slot that was never written returns `SB_EMPTY`. A card that is
  missing, unformatted, full or failing returns `SB_NO_MEDIA`, `SB_UNFORMATTED`, `SB_FULL`, `SB_UNAVAILABLE` or `SB_IO`.
- `write(context, slot, buffer, length)`: replace the slot with `length` bytes and return `SB_OK` or the same kind of
  result.

### What a save does

1. Reads both slots and finds the newest valid record (the higher sequence; a tie prefers slot 0).
2. Stops with that result, writing nothing, if a slot reports a device error (anything above `SB_INVALID`) or holds a
   record of another version, so a failing card is not written to blindly and an older game never overwrites a newer
   game's save.
3. Writes the payload with the next sequence number into the other slot, then reads that slot back and compares every
   byte. A write that was reported done but reads back different returns `SB_VERIFY`.

A load takes the newest valid slot. A slot that is empty, torn, damaged or from another version is skipped while the
other is good. If neither is good, the result is the more serious of the two. The results are ordered:
`SB_OK`, `SB_EMPTY`, `SB_INVALID`, `SB_UNSUPPORTED_VERSION`, `SB_NO_MEDIA`, `SB_UNFORMATTED`, `SB_FULL`,
`SB_UNAVAILABLE`, `SB_IO`, `SB_VERIFY`, and `SB_BAD_ARGUMENT` for a NULL pointer, a missing callback or a payload size
of 0 or more than `SB_MAX_PAYLOAD_BYTES`.

`sb_store.validate` is an optional check of the payload (return non-zero when it is acceptable). It runs on every loaded
record, which then counts as damaged if it fails, and on a payload before a save, which `sb_save` then refuses with
`SB_INVALID`.

`SB_MAX_PAYLOAD_BYTES` (512 by default; set it with `-DSB_MAX_PAYLOAD_BYTES=<n>` in CMake) sets the stack buffers: a
load or a save uses about `2 * (SB_MAX_PAYLOAD_BYTES + 16)` bytes of stack. It must be at least 68 to use the arcade
profile.

### Lower-level calls

`sb_encode`, `sb_decode` and `sb_record_bytes` build and check one record with no storage, for a game that wants the
framing and not the slots. `sb_decode` checks, in order: too short or the wrong magic is `SB_INVALID`; another version
is `SB_UNSUPPORTED_VERSION`; a wrong length, payload size or checksum is `SB_INVALID`.

### sb/codec.h

Little- and big-endian `sb_put_u16le`, `sb_put_u32le`, `sb_put_u16be`, `sb_put_u32be` and the matching `sb_get_*`, and a
bounded `sb_writer` and `sb_reader` that lay a payload out field by field. Reading or writing past the end does nothing
and sets a flag to test once at the end. Write fields at named offsets, never a struct dump: padding, enum size and byte
order differ between compilers and consoles.

### sb/memory.h: storage for tests

`sb_memory` is two slots in RAM with the callbacks ready (`sb_memory_storage`) and switches that make it fail like a real
device: `read_error[slot]` (a missing or failing card), `write_error` (a full card), `truncate_to` (the power goes
mid-write: a short slot and `SB_IO`) and `corrupt_at` (a write that succeeds and reads back with a flipped bit). Tests
run in memory, with no files and no hardware.

## The arcade profile (optional)

`sb/arcade.h` is what most arcade-style games keep between runs, and is separate from the record code:

- **Settings**: a display mode and two volumes (`sb_arcade_settings`), checked against limits the game gives
  (`sb_arcade_limits`: highest display mode, highest volume, highest level).
- **A hiscore table** of 8 rows (`sb_arcade_record`: score, level, three printable ASCII initials), best first.
  `sb_hiscore_rank` says which row a score takes (a higher score, or an equal score with a higher level; a zero score or
  level never enters), and `sb_hiscore_insert` puts it there and pushes the rest down.
- **An initials screen** as a state machine with no drawing or input code: `sb_initials_begin` with the run's score and
  level, then `sb_initials_update` once per frame with a bit set of `SB_INPUT_LEFT`, `RIGHT`, `UP`, `DOWN`, `CONFIRM`
  and `CANCEL`. Letters wrap A to Z, confirm writes the row into the table, cancel ends without writing.
- **A 68-byte block**: `sb_arcade_encode` and `sb_arcade_decode` (3 settings bytes, 1 reserved, then 8 rows of score
  u32, initials, level). `sb_arcade_save` and `sb_arcade_load` put it in the two slots in one call. A game with more to
  store makes the block the front of a larger payload (the tests show it) and adds its own fields with `sb_writer`.

## Where this differs from the copies it was written from

Several console projects kept a copy of this code, one per platform. Only one of them had alternating records, a
newest-valid load and a read-back check, and it is what this package is based on; the rest kept a single record and no
read-back. Where they differed, this is what was kept:

- **Layout.** The record layout above is that copy's, byte for byte, so a save written by it loads here when the magic,
  version and payload size are the same. Several of the other copies wrote a C struct straight to the device, so the
  layout depended on the compiler's padding; here a payload is laid out field by field.
- **Checksum.** The copies agreed on FNV-1a but differed in what it covers: some over the payload only, two over the
  whole struct with the checksum field zeroed in place. Kept: every byte before the checksum, with the checksum at the
  end, so nothing has to be zeroed and the field is never skipped.
- **Version mismatch.** A slot from another version is not damaged and not good: a load uses the other slot, a save
  refuses to overwrite it. Kept from the alternating-record copy; the single-record copies just reported an error.
- **Device errors.** A device error on either slot stops a save before anything is written. Kept.
- **Result texts** named the memory card of one console. These are neutral (`sb_result_text`); a game that wants its own
  words maps the result itself.
- **Byte helpers.** Several copies retyped little-endian put and get helpers, with a big-endian twin in one. Kept: both,
  plus the bounded writer and reader.
- **Not carried over:** the games' own profile fields (their meaning of the display mode and volumes, extra tables) stay
  in the game, and the file and directory names, device initialisation and operation-scoped events of each platform's
  storage API stay in that platform's callbacks.

## Layout

```
include/sb/record.h   the record, the two slots, sb_save and sb_load
include/sb/codec.h    byte order helpers, bounded writer and reader
include/sb/memory.h   RAM storage with fault injection, for tests
include/sb/arcade.h   the optional arcade profile
src/                  record.c memory.c arcade.c
tests/                one test program per module, C99 -Wall -Wextra -Wpedantic -Werror
examples/consumer/    a project using the package at a tag
```

## Tests

```bash
./build-and-test.sh          # macOS and Linux
.\build-and-test.ps1         # Windows
```

or `./tools/check-c.sh` from the game-kit root. The tests need no display, hardware or network. They pin the record
bytes (checksum worked out by an independent program), flip every single bit of a record and check none is accepted,
cut a record at every length, and run the save and load cases that matter on real devices: alternation, a torn write, a
medium that reads back different, a card that disappears, a newer game's save, the sequence wrapping past 4294967295,
and the half-way point of the sequence circle.

## Licence

MIT
