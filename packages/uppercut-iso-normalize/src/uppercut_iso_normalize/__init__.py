"""Make a PlayStation 2 disc image byte-reproducible: fixed dates, ISO 9660 version suffixes, and a checked SYSTEM.CNF and ELF."""

from ._normalize import IsoNormalizeError, normalize_disc, ps2_system_cnf
from .cli import main, normalize_file

__all__ = ["IsoNormalizeError", "main", "normalize_disc", "normalize_file", "ps2_system_cnf"]
__version__ = "0.1.0"
