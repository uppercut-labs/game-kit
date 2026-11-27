"""Compute, fix and verify the header checksums of Nintendo 64, Mega Drive and 32X, and Game Boy Advance ROM images."""

from ._checks import RomHeaderError, Verification
from .cli import main
from .gba import compute_gba_complement, fix_gba_complement, verify_gba_complement
from .megadrive import compute_megadrive_checksum, fix_megadrive_checksum, verify_megadrive_checksum
from .n64 import N64Checksums, compute_n64_checksums, fix_n64_checksums, verify_n64_checksums

__all__ = [
    "N64Checksums",
    "RomHeaderError",
    "Verification",
    "compute_gba_complement",
    "compute_megadrive_checksum",
    "compute_n64_checksums",
    "fix_gba_complement",
    "fix_megadrive_checksum",
    "fix_n64_checksums",
    "main",
    "verify_gba_complement",
    "verify_megadrive_checksum",
    "verify_n64_checksums",
]
__version__ = "1.0.0"
