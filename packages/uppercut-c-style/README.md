# uppercut-c-style

Checks the C sources of a git work tree against a fixed-width integer style. Every integer is spelled with an exact-width alias (`u8`, `i16`, ...), never `int`, `long`, `short`, `unsigned`, `signed`, an SDK name such as `s32` or a `<stdint.h>` name such as `uint32_t`. Pointer stars sit on the type (`const u8* bytes`, not `u8 *bytes`). A `for` loop over a byte range (an upper bound of 256 or less) uses the 8-bit alias, and a loop that would run to 255 on it needs an explicit exit before the index wraps. Each repository passes its own settings: the aliases it uses, the functions whose signatures belong to an external ABI, and the paths to skip.

```sh
pip install uppercut-c-style
```

## Command line

Run it anywhere inside the work tree:

```sh
uppercut-c-style                       # the whole tree
uppercut-c-style platforms/ps2 shared  # only these git pathspecs, relative to the root
uppercut-c-style --root path/to/repo --config path/to/settings.json
python -m uppercut_c_style
```

Each finding is one line on standard output, `path:line: message`, with the path relative to the root:

```text
shared/audio.c:12: use a fixed-width type for int
shared/audio.c:40: attach the pointer to its type
platforms/ps2/menu.c:88: use u8 for the byte-range loop
platforms/ps2/menu.c:97: use an explicit exit before the u8 index wraps
```

A clean run prints `Checked N C sources and headers`. The exit code is 0 when the sources pass and 1 for findings, for no C sources at all, or for a settings or git error (one line on standard error).

The root is `--root`, else the directory of `--config`, else the git top level of the current directory. Pathspecs and settings paths are relative to the root.

## Settings

Put a `[tool.uppercut-c-style]` table in the root's `pyproject.toml`, or the same keys at the top level of `uppercut-c-style.toml` or in the object of `uppercut-c-style.json`. The first of those three that exists is used, and `--config` names any other file. With none, the defaults apply.

```toml
[tool.uppercut-c-style]
exclude = ["shared/lab_types.h", "third_party/*"]

[tool.uppercut-c-style.abi]
"shared/sega/audio_sega.c" = ["stream_pcm"]
```

| Key | Default | Meaning |
| --- | --- | --- |
| `types` | `["u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64"]` | The exact-width aliases the code uses. They count as type names for the pointer check and may be loop indexes |
| `byte-type` | `"u8"` | The alias a byte-range loop must use. It must be one of `types` |
| `abi` | none | File paths (relative, with `/`) mapped to function names whose signatures follow an external ABI. Plain C integer types are allowed in those signatures; the function bodies are checked as usual |
| `exclude` | none | Glob patterns of paths to skip, such as vendored code or the header that defines the aliases. `*` also matches `/` |
| `include` | the whole tree | Git pathspecs checked when none are given on the command line |
| `suffixes` | `[".c", ".h", ".inl"]` | File extensions checked |
| `untracked` | `true` | Also check new files that git does not ignore, so a file is checked before its first commit |
| `label` | `""` | A word for the messages: `"lab"` gives `use a lab fixed-width type for int` |

An unknown key, or a value of the wrong kind, is an error rather than being ignored.

## Python

```python
from uppercut_c_style import StyleConfig, check_repo, find_config

config = find_config("path/to/repo")            # or StyleConfig(exclude=("shared/types.h",))
result = check_repo("path/to/repo", config)
for finding in result.findings:                 # Finding(path, line, message); str() gives path:line: message
    print(finding)
print(result.checked, result.ok)
```

| Name | What it does |
| --- | --- |
| `check_repo(root, config=None, pathspecs=())` | Lists the sources with `list_sources` and checks them. `config` defaults to `find_config(root)` |
| `check_paths(root, names, config=None)` | Checks the named files (relative to `root`) and returns a `CheckResult(findings, checked)` |
| `list_sources(root, config, pathspecs=())` | The files git lists under `root` that the check reads, after `include`, `exclude` and `suffixes` |
| `find_config(root)`, `load_config(path)` | Read the settings as described above |
| `config_from_mapping(table)` | A `StyleConfig` from a dictionary that uses the file keys |
| `StyleConfig(...)` | The settings as a frozen dataclass: `types`, `byte_type`, `abi`, `exclude`, `include`, `suffixes`, `label`, `untracked` |
| `strip_literals(source)` | Blanks out comments, strings and character constants and keeps every newline |
| `CStyleError` | Raised for bad settings, a root that is not a git work tree, or no git on `PATH` |

## What it checks

- Comments, string literals and character constants are blanked out first, so they never produce a finding and line numbers stay right.
- `int main(int argc, ...)` is allowed, and so are `<inttypes.h>`-style casts such as `(int32_t)value`, because an SDK typedef of the same width can need a different underlying C type.
- The pointer check knows `char`, `void`, `float`, `double`, `FILE`, `size_t`, `int`, `unsigned`, the configured aliases, every name ending in `_t`, and every `typedef` name declared in the checked files.
- A loop bound written as a macro is looked up in the file and in the local headers it includes with quotes, inside the root. A macro defined twice with different values is skipped, for a person to review.

## Working on it

From the repository root:

```sh
uv sync --all-packages && uv run pytest packages/uppercut-c-style
```

The tests build small git repositories of made-up C in a temporary directory. They need `git` on `PATH`.

## License

MIT
