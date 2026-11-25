"""The JSON file the command line reads to build a `Verifier`.

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
    "cpp": "/tools/gcc/cpp -lang-c -Iinclude",
    "cc1": "/tools/gcc/cc1 -quiet -ffunction-sections",
    "assembler": "/tools/gcc/as -EL -mabi=eabi",
    "linker": ["/tools/binutils/mips-ps2-decompals-ld", "-EL"],
    "runner": ["env"],
    "path_map": []
  }
}
```

Relative paths are taken from the file's folder. `symbols` are splat-style `name = 0xADDR;` files, read in order (the first address of a
name wins). With `symbols_from_elf`, the image's own symbol table is read first, and `function_symbols` files add only the lines whose
comment says `type:func`. `gp` may be a number or a hex string.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Mapping, Optional, Set, Tuple, Union

from .elf32 import Image
from .errors import ConfigError
from .symbols import merge, read_symbol_file, symbols_from_elf
from .verify import DEFAULT_DEFINES, Toolchain, Verifier

PathLike = Union[str, Path]


@dataclass
class VerifyConfig:
    verifier: Verifier
    flags: str
    assembler: Optional[str] = None


def _need(data: Mapping[str, Any], key: str) -> Any:
    if key not in data:
        raise ConfigError(f"the config has no {key!r}")
    return data[key]


def _path(base: Path, value: str) -> Path:
    p = Path(value)
    return p if p.is_absolute() else base / p


def parse_toolchain(data: Mapping[str, Any]) -> Toolchain:
    return Toolchain(
        cpp=str(_need(data, "cpp")),
        cc1=str(_need(data, "cc1")),
        assembler=str(_need(data, "assembler")),
        linker=[str(x) for x in data.get("linker", ["ld", "-EL"])],
        defines=str(data.get("defines", DEFAULT_DEFINES)),
        runner=[str(x) for x in data.get("runner", ["env"])],
        path_map=[(str(a), str(b)) for a, b in data.get("path_map", [])],
    )


def load_verify_config(path: PathLike) -> VerifyConfig:
    cfg_path = Path(path)
    try:
        data = json.loads(cfg_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ConfigError(f"{cfg_path} is not JSON: {error}") from error
    base = cfg_path.resolve().parent
    image_path = _path(base, str(_need(data, "image")))
    gp_value = _need(data, "gp")
    try:
        gp = int(gp_value, 0) if isinstance(gp_value, str) else int(gp_value)
    except ValueError as error:
        raise ConfigError(f"gp is a number or a hex string, not {gp_value!r}") from error
    tables: List[Dict[str, int]] = []
    funcs: Dict[str, Set[int]] = {}
    if data.get("symbols_from_elf"):
        syms, funcs = symbols_from_elf(image_path)
        tables.append(syms)
        for f in data.get("function_symbols", []):
            tables.append(read_symbol_file(_path(base, f), require="type:func"))
    for f in data.get("symbols", []):
        tables.append(read_symbol_file(_path(base, f)))
    include = data.get("include_dir")
    verifier = Verifier(
        Image.open(image_path), merge(*tables), parse_toolchain(_need(data, "toolchain")), gp=gp, funcs=funcs,
        include_dir=_path(base, include) if include else None, defines=str(data.get("defines", "")),
        fragments=bool(data.get("fragments", True)),
    )
    return VerifyConfig(verifier, str(data.get("flags", "-O2 -G8")), data.get("assembler"))
