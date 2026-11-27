# det-core

The small numeric core that a deterministic game simulation stands on: exact-width types, saturating Q16.16
fixed-point math, 32-bit FNV-1a hashing and a one-word xorshift32 random number generator. Every function is integer
only, so the same inputs give the same outputs on every compiler, CPU and console. That is what lets a replay, a
network lockstep or a regression hash of a whole run be trusted.

Nothing in this package knows any game, and it includes no platform headers: only `<stddef.h>` and `<stdint.h>`.

Version 1.0.0. Header-only C (C99) with CMake. MIT licence.

## Install

This is a C library, so it is not on a package index. A project takes it at a release tag, with CMake `FetchContent`
(CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(det_core
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        det-core@1.0.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/det-core)
FetchContent_MakeAvailable(det_core)

target_link_libraries(my-game PRIVATE det_core::det_core)
```

`examples/consumer` is that, complete, with a deterministic-replay test, and the kit's own check builds it. For an
offline build set `-DFETCHCONTENT_SOURCE_DIR_DET_CORE=<a checkout of game-kit>`. A project that vendors the folder
uses `add_subdirectory(<path>/det-core)`. A build system other than CMake only needs `include/` on the include path.

## API

`#include "det/det.h"` for everything, or one header at a time.

### `det/types.h`

- `DET_STATIC_ASSERT(name, condition)`: a C99 compile-time assert (a typedef with a negative array size).
- `DET_VERSION`: the package version as a string.
- `u8 i8 u16 i16 u32 i32 u64 i64`: short aliases for the `<stdint.h>` types, and only when `DET_SHORT_TYPES` is
  defined before the first include (a project with its own typedefs would otherwise collide). They are checked
  with `DET_STATIC_ASSERT` to have the right size.

### `det/fixed.h`: Q16.16 in an `int32_t`

1.0 is `DET_FX_ONE` (65536). Operations clamp to `INT32_MIN`..`INT32_MAX` instead of overflowing.

| Function | Result |
| --- | --- |
| `det_fx_from_int(n)`, `det_fx_to_int(v)`, `det_fx_round_to_int(v)` | integer to fixed (saturating); fixed to integer by floor; by nearest, halves away from zero |
| `det_fx_from_ratio(n, d)`, `det_fx_mul(a, b)`, `det_fx_div(n, d)` | the quotient or product as fixed, truncated toward zero. A zero divisor gives `INT32_MAX` (`INT32_MIN` for a negative numerator) |
| `det_fx_abs`, `det_fx_min`, `det_fx_max`, `det_fx_clamp`, `det_fx_sign`, `det_i32_clamp` | `det_fx_abs(INT32_MIN)` is `INT32_MAX`; `det_fx_sign` gives `DET_FX_ONE`, `-DET_FX_ONE` or 0 |
| `det_fx_lerp(a, b, t)` | `a + (b - a) * t`, with `t` a fixed-point fraction, not clamped |
| `det_isqrt_u64(v)`, `det_fx_sqrt(v)`, `det_fx_hypot(x, y)` | floor square root of a 64-bit integer; of a fixed value (0 for zero or negative); `sqrt(x*x + y*y)` without overflow |
| `det_fx_per_tick(per_second, ticks_per_second)` | a rate per second as a rate per tick, integer division toward zero |
| `DET_FX_ONE`, `DET_FX_HALF`, `DET_FX_PI`, `DET_FX_TWO_PI`, `DET_FX_SHIFT` | constants (`DET_FX_TWO_PI` is 2*pi rounded: 411775, one more than twice `DET_FX_PI`) |

### `det/hash.h`: 32-bit FNV-1a

A hash is a plain `uint32_t`: start it with `det_hash32_begin()` (the offset basis 2166136261) and feed it through.
Pieces give the same answer as one call over the joined bytes. Multi-byte values are folded little-endian, so a
hash does not depend on the host's byte order or struct padding.

- `det_hash32_u8`, `det_hash32_u16`, `det_hash32_u32`, `det_hash32_i32`: fold one value.
- `det_hash32_bytes(hash, data, length)`: fold a buffer.
- `det_fnv1a32(data, length)`: the hash of a whole buffer. `""` is `0x811c9dc5`, `"a"` is `0xe40c292c`, `"foobar"`
  is `0xbf9cf968`.

It is a checksum and a regression pin, not a cryptographic signature.

### `det/rng.h`: xorshift32

The generator is one `uint32_t` (`det_rng`), so it copies, compares and hashes like an integer.

- `det_rng_seed(seed)`: the generator for a seed. Zero would stick forever, so a zero seed gives `DET_RNG_FALLBACK`
  (`0x6d2b79f5`), and no step ever produces zero.
- `det_rng_next(rng)`: the state after one step (shifts 13, 17, 5). From seed 1 the first state is 270369.
- `det_rng_bounded(&rng, n)`: a number in `[0, n)` from the high bits (a multiply and shift, no division). A zero `n`
  or a NULL generator gives 0 and does not advance.
- `det_rng_range_i32(&rng, min, max)`: a number in `[min, max]`; `max <= min` gives `min` without advancing. Any pair
  of `int32_t` values works, including the full range.
- `det_rng_chance(&rng, numerator, denominator)`: true with probability `numerator / denominator`. The edges (a
  zero side, or `numerator >= denominator`) do not advance the generator.

The sequence for a seed is part of the package's contract: it does not change within major version 1, and the tests pin it.
A project that already has a generator with recorded replays should keep it, because switching would change them.

## Where the behaviour differs from the copies it was written from

The code is the common part of several copies kept in console game projects. They agreed on the arithmetic and
differed in small ways. Where they did, this is the version kept:

- **Zero guard.** The main copy of the generator never reaches the all-zero state. Several other small copies (in
  software renderers and game-mode code) had no guard in the step itself: some guarded the seed where they were
  started, others relied on a non-zero constant, and a zero seed would have frozen any of them. Kept: the guard in
  the step and in `det_rng_seed`, which gives the same numbers for every non-zero state.
- **Bounded numbers.** The main copy takes the high bits (`(state * n) >> 32`); those copies used `state % n`,
  which favours low values and depends on the low bits, the weakest ones in xorshift. Kept: the high-bits version.
  A game that used `% n` gets a different sequence from the same seed.
- **Range overflow.** The copied `range_i32` computed `max - min + 1` in signed arithmetic, which is undefined once the
  span reaches 2^31. Kept: the span is computed unsigned, so the full `int32_t` range works. Results are identical
  wherever the old code was defined.
- **`det_fx_to_int`.** The copies used `value >> 16`, whose result for a negative value is left to the compiler.
  Kept: an explicit floor. Results are identical on every compiler that shifts arithmetically, which is all of
  them in use.
- **`det_fx_per_tick`.** The copies divided by a constant 60. Kept: the tick rate is a parameter.
- **FNV-1a.** The copies agreed on the constants. Besides the header, a checksum over a byte buffer was typed out
  again in several adapters, one in JavaScript and one in Python, and two of the C versions zeroed a stored checksum
  field in place while hashing (that belongs to a save format, not here). The JavaScript copy could continue from a
  previous hash; kept: `det_hash32_bytes` takes the running hash.
- **Names.** The copies carried a project prefix; this package uses `det_` and `DET_`.

## Layout

```
include/det/types.h    fixed-width aliases and DET_STATIC_ASSERT
include/det/fixed.h    Q16.16
include/det/hash.h     FNV-1a
include/det/rng.h      xorshift32
include/det/det.h      all four
tests/                 one test per header, C99 -Wall -Wextra -Wpedantic -Werror
examples/consumer/     a project using the package at a tag
```

## Tests

```bash
./build-and-test.sh          # macOS and Linux
.\build-and-test.ps1         # Windows
```

or `./tools/check-c.sh` from the game-kit root. The tests need no display, hardware or network. They pin the FNV-1a
test vectors, the xorshift32 sequence, saturation at every edge and a floor/round/truncate table, and the example
consumer checks that a seeded run replays bit for bit.

## Licence

MIT
