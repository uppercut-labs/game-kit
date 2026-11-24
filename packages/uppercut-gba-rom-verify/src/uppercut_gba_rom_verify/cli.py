"""Command line: verify a GBA ROM and its devkitARM ELF, for a cartridge identity given by flags or a JSON config."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Optional, Sequence

from ._elf import validate_elf_file
from ._errors import ValidationError
from ._rom import RomConfig, validate_rom_file

CONFIG_KEYS = ("title", "game_code", "maker_code")
_SHA256_PATTERN = re.compile(r"^[0-9a-fA-F]{64}$")


def _sha256_argument(value: str) -> str:
    if not _SHA256_PATTERN.match(value):
        raise argparse.ArgumentTypeError("expected 64 hexadecimal characters")
    return value.lower()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="uppercut-gba-rom-verify",
        description=(
            "Validate a GBA ROM header (logo, title, game code, maker code, fixed byte, "
            "complement, size, alignment) and a devkitARM ELF32 executable, then report the ROM's SHA-256."
        ),
    )
    parser.add_argument("rom", help="path to the .gba ROM")
    parser.add_argument("elf", help="path to the ELF executable the ROM was built from")
    parser.add_argument(
        "--config",
        metavar="PATH",
        help=f"JSON object with the keys {', '.join(CONFIG_KEYS)}; command line flags override it",
    )
    parser.add_argument("--title", help="expected header title, up to 12 ASCII characters")
    parser.add_argument("--game-code", help="expected 4-character game code")
    parser.add_argument("--maker-code", help="expected 2-character maker code")
    parser.add_argument(
        "--expect-sha256",
        type=_sha256_argument,
        metavar="HEX",
        help="fail unless the ROM's SHA-256 equals this value",
    )
    return parser


def load_config_file(path: str) -> dict[str, str]:
    """Read the JSON config. Raises ValueError naming the problem."""
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except OSError as error:
        raise ValueError(f"cannot read config {path}: {error.strerror or error}") from error
    except json.JSONDecodeError as error:
        raise ValueError(f"config {path} is not valid JSON: {error.msg}") from error
    if not isinstance(data, dict):
        raise ValueError(f"config {path} must hold a JSON object")
    unknown = sorted(set(data) - set(CONFIG_KEYS))
    if unknown:
        raise ValueError(f"config {path} has unknown keys: {', '.join(unknown)}")
    for key, value in data.items():
        if not isinstance(value, str):
            raise ValueError(f"config {path} key {key!r} must be a string")
    return data


def config_from_args(args: argparse.Namespace) -> RomConfig:
    """Combine the config file and flags into a RomConfig. Raises ValueError when incomplete or invalid."""
    values: dict[str, str] = {}
    if args.config is not None:
        values.update(load_config_file(args.config))
    flags = {"title": args.title, "game_code": args.game_code, "maker_code": args.maker_code}
    values.update({key: value for key, value in flags.items() if value is not None})

    missing = [key for key in CONFIG_KEYS if key not in values]
    if missing:
        names = ", ".join("--" + key.replace("_", "-") for key in missing)
        raise ValueError(f"missing cartridge identity: pass {names} or --config")
    return RomConfig(values["title"], values["game_code"], values["maker_code"])


def _print_failure(label: str, error: ValidationError) -> None:
    for detail in error.errors:
        print(f"FAIL {label}: {detail}", file=sys.stderr)


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        config = config_from_args(args)
    except ValueError as error:
        parser.error(str(error))

    failed = False
    try:
        rom_info = validate_rom_file(args.rom, config, expected_sha256=args.expect_sha256)
    except ValidationError as error:
        _print_failure("ROM", error)
        failed = True
    else:
        print(f"PASS ROM size: {rom_info.size} bytes, 4-byte aligned")
        print(
            "PASS ROM header: Nintendo logo, "
            f"{rom_info.title}/{rom_info.game_code}/{rom_info.maker_code}"
        )
        print(f"PASS ROM checksum: 0x{rom_info.complement:02X}")
        print(f"PASS ROM SHA-256: {rom_info.sha256}")

    try:
        elf_info = validate_elf_file(args.elf)
    except ValidationError as error:
        _print_failure("ELF", error)
        failed = True
    else:
        print(
            "PASS ELF: ELF32 little-endian ARM EABI"
            f"{elf_info.eabi_version} executable, {elf_info.size} bytes"
        )

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
