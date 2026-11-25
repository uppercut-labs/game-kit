# uppercut-ps2-match

Tooling for a PlayStation 2 matching decompilation. It compiles one C file at a time with the compiler you give it, links each function at its original address, and says whether the bytes equal the original's. Around that sit the pieces a matching project keeps rewriting: data-fragment placement, a C file splitter, assembly helpers for splat output, a toolchain fetch pinned by SHA-256, and an audit that a public repository holds no game data. Standard library only.

It ships tools only. No compiler or SDK binaries, no game code or data, no SDK headers. Where the compiler comes from, its URLs and its hashes are your configuration.

```sh
pip install uppercut-ps2-match
```

## Commands

```sh
uppercut-ps2-match verify verify.json src/main/foo.c --work build/verify
uppercut-ps2-match csplit src/main/foo.c
uppercut-ps2-match fetch toolchain.json --dest tools/ [--host linux-x86-64] [--dry-run]
uppercut-ps2-match audit --root . [--config audit.json] [--publish] [--extra DIR=PREFIX] [--out audit.md]
```

`verify` exits 0 when every function of every file matches, 1 when one does not, and 2 for a usage or configuration error. `audit` exits 1 when any file is a finding. `python -m uppercut_ps2_match` runs the same commands.

## verify

For each C file the verifier preprocesses and compiles it, assembles the result, links every function section at its address in the original, and compares. A function matches when its bytes equal the original's and the original's remaining bytes up to the next function are zero padding. The report per function:

```json
{"name": "f", "addr": "0x100000", "size": 16, "orig_size": 32, "diff_words": 0, "padding_ok": true,
 "data": {".rodata.F0": "0x300010+4"}, "data_ok": true, "status": "match"}
```

`status` is `match`, `mismatch` or `no-address` (the function has no address in your symbols). A file that does not compile or link gives one `compile-error` or `link-error` result with the tools' output.

Data a function uses (literals, switch tables, statics, common variables, externs with no address) is not given addresses by hand. Each is first linked at a probe address, and the original's instructions that refer to it (the same `lui`/`%lo`, `$gp`-relative or `jal` instruction in both builds) say where it really sits. `datafrag` gives every data label its own section so each piece is placed by itself, and data that only other data points at (a table of strings) is solved through those pointers. `data_ok` says whether the object's own initialised data equals the original where it was placed; it is reported, not required for a match.

### Configuration

```json
{
  "image": "original/main.elf",
  "symbols": ["config/symbol_addrs.txt"],
  "symbols_from_elf": false,
  "function_symbols": [],
  "gp": "0x5d2a70",
  "include_dir": "include",
  "defines": "-DVERSION_A",
  "flags": "-O2 -G8",
  "fragments": true,
  "toolchain": {
    "cpp": "/path/to/cpp -lang-c -Iinclude",
    "cc1": "/path/to/cc1 -quiet -ffunction-sections",
    "assembler": "/path/to/as -EL -mabi=eabi",
    "linker": ["/path/to/ld", "-EL"],
    "runner": ["env"],
    "path_map": []
  }
}
```

Relative paths are taken from the file's folder. `symbols` are splat-style `name = 0xADDR;` files, read in order (the first address of a name wins). With `symbols_from_elf`, the image's own symbol table is read first, and the `function_symbols` files add only the lines whose comment says `type:func`. The three compiler commands are strings run with `sh -c` in the folder of the compile, through `runner` (`["docker", "exec", "box"]`, a qemu wrapper, or the default `env` for a compiler that runs here). `path_map` translates host folders into the runner's, as `[host prefix, runner prefix]` pairs. The linker runs on the host.

### Python

```python
from uppercut_ps2_match import Toolchain, Verifier, read_symbol_file

tc = Toolchain(cpp="...", cc1="...", assembler="...", linker=["ld", "-EL"])
verifier = Verifier("original/main.elf", read_symbol_file("symbol_addrs.txt"), tc, gp=0x5D2A70, include_dir="include")
for result in verifier.check("src/foo.c", "build/verify/foo", "-O2 -G8"):
    print(result["name"], result["status"])
```

`check(..., prefer=(lo, hi))` resolves a function name that appears at several addresses (file statics) to the one in that range.

## Pieces

| Module | What it does |
| --- | --- |
| `elf32` | A dependency-free reader for 32-bit little-endian ELF: sections, symbols, `REL` relocations, program headers (`Elf`, `Image.bytes_at`). |
| `mips` | `refs(words, base, gp)`: the addresses a function builds (`lui` + offset, `$gp`-relative, `jal`/`j`). |
| `datafrag` | `split(asm)`: cut compiler assembly at every data label, one section per label; `code_shape(object)`: the instruction stream with relocated fields masked. |
| `csplit` | `split(text)`: a C file into its prelude and its function definitions; `data_definition`, `prototype`. |
| `symbols` | Read splat symbol files and an ELF symbol table; function starts and repeated names. |
| `asmtu` | Helpers for splat assembly: VU0 macro lines to `.word`, drop matched functions from a translation unit, strip attributes and `static`. |
| `fetch` | `load_packages`, `fetch_toolchain`: download, check every SHA-256, unpack safely. |
| `audit` | `AuditConfig`, `audit_tree`: find binaries, assets, disc images, disassembly and compiler dumps. |

## fetch

A toolchain is a JSON file you keep with your project. This package contains no addresses and no hashes of its own.

```json
{
  "packages": [
    {"name": "compiler", "url": "https://example.invalid/compiler.tar.xz", "sha256": "<archive digest>",
     "files": {"lib/cc1": "<digest of the file the build runs>", "bin/as": "<digest>"}},
    {"name": "binutils", "hosts": {"linux-x86-64": {"url": "...", "sha256": "..."}, "macos-arm64": {"url": "...", "sha256": "..."}}}
  ]
}
```

Each archive is checked against its SHA-256 before it is unpacked, and each file named in `files` after. A package is moved into place only when both pass, so a mismatch leaves nothing of it behind. An archive holding a link or a path outside its folder is refused. A package already in the destination is checked and left alone. `--dry-run` prints the plan and downloads nothing.

## audit

Checks every file (git's tracked and untracked-but-not-ignored files in a checkout, or every file of a plain folder) for binaries, assets by type, logos and artwork, assembly and disassembly listings, `INCLUDE_ASM`, long C initialisers and base64 runs, and compiler dumps. Screenshots (a PNG, JPEG or WebP under `screenshots/`, at most 2 MB, not named like artwork) are listed and allowed. A file of your own that trips a check and is not game data goes in `known` with its SHA-256:

```json
{
  "known": {"assets/ui.ttf": ["<sha256>", "our own UI font"]},
  "publish": ["src/", "include/", "README.md"],
  "publish_exclude": [],
  "screenshot_dir": "screenshots",
  "screenshot_max": 2097152,
  "max_literals": 1024,
  "table_extension": ".tbl",
  "table_row": "^[0-9]+ [A-Za-z_0-9]+$"
}
```

`publish` limits the first root to the paths you intend to publish (a path ending in `/` is a folder). `table_extension` and `table_row` check a generator table: every line must match the row pattern.

## Not included

- The build driver of a particular project (the `main`/data object steps, splat configuration, progress stamps). Those hold project names and layouts; this package is the part that does not.
- A compiler, an assembler, a linker or any SDK header. Bring your own.
- Tests against a real EE-GCC or on Windows: the tests use synthetic object files and stand-in `cpp`, `cc1`, `as` and `ld` programs (`tests/fake_tool.py`), so they run anywhere. The command-running code is plain `subprocess` on a `sh -c` string, which needs a POSIX `sh` on the host or in the runner.

## Tests

```sh
uv sync --all-packages && uv run pytest packages/uppercut-ps2-match
```
