"""Make a PlayStation 2 disc image byte-reproducible: fixed dates, ISO 9660 version suffixes, and a checked SYSTEM.CNF and ELF.
Also builds that disc from an ELF with pycdlib."""

from ._build import build_disc, build_file, system_cnf_bytes
from ._normalize import IsoNormalizeError, normalize_disc, ps2_system_cnf
from .cli import build_main, main, normalize_file

__all__ = [
    "IsoNormalizeError",
    "build_disc",
    "build_file",
    "build_main",
    "main",
    "normalize_disc",
    "normalize_file",
    "ps2_system_cnf",
    "system_cnf_bytes",
]
__version__ = "1.1.0"
