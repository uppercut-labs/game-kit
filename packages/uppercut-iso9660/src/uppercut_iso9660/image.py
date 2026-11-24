"""Read-only ISO 9660 images: cooked 2048-byte sectors and raw CD dumps (2352 and 2336 bytes per sector)."""

from __future__ import annotations

import hashlib
import io
import os
import re
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Iterator, Optional, Union

from .errors import IsoError, IsoNotFound

SECTOR_SIZE = 2048
"""The user data in one sector, and the only logical block size this package reads."""
PRIMARY_SECTOR = 16
"""Where the volume descriptors start."""
DESCRIPTOR_LIMIT = 32
"""How many sectors from PRIMARY_SECTOR to search for the primary descriptor."""
MAX_DIRECTORY_SIZE = 16 * 1024 * 1024
MAX_ENTRIES = 400_000
CHUNK = 1 << 22

_VERSION = re.compile(r";(\d+)$")


@dataclass(frozen=True)
class SectorLayout:
    """Where a sector's 2048 bytes of user data sit in the image."""

    name: str
    stride: int
    """Bytes from one sector to the next in the file."""
    skip: int
    """Bytes before the user data inside a sector (sync, header, sub-header)."""


COOKED = SectorLayout("cooked-2048", 2048, 0)
RAW_MODE1 = SectorLayout("raw-2352-mode1", 2352, 16)
RAW_MODE2 = SectorLayout("raw-2352-mode2", 2352, 24)
RAW_2336 = SectorLayout("raw-2336-mode2", 2336, 8)
LAYOUTS = (COOKED, RAW_MODE1, RAW_MODE2, RAW_2336)


@dataclass(frozen=True)
class DirectoryEntry:
    path: str
    """The path from the root with ``/`` separators and no version suffix; empty for the root."""
    identifier: str
    """The name as written on the disc, for example ``SYSTEM.CNF;1``."""
    name: str
    """The identifier without its ``;version`` suffix and without a trailing dot."""
    version: Optional[int]
    lba: int
    size: int
    flags: int

    @property
    def is_dir(self) -> bool:
        return bool(self.flags & 0x02)

    @property
    def multi_extent(self) -> bool:
        return bool(self.flags & 0x80)


@dataclass(frozen=True)
class ExtractResult:
    size: int
    sha256: str


def _both_endian_32(record: bytes, offset: int, strict: bool, what: str) -> int:
    little = int.from_bytes(record[offset : offset + 4], "little")
    big = int.from_bytes(record[offset + 4 : offset + 8], "big")
    if strict and little != big:
        raise IsoError(f"ISO {what} byte-order copies disagree ({little} and {big})")
    return little


def parse_record(record: bytes, parent: str, *, strict: bool = True) -> DirectoryEntry:
    """Decode one directory record. ``parent`` is the path of its directory."""
    if len(record) < 34 or record[0] != len(record) or 33 + record[32] > len(record):
        raise IsoError("invalid ISO directory record length")
    identifier = record[33 : 33 + record[32]].decode("latin-1")
    lba = _both_endian_32(record, 2, strict, "extent")
    size = _both_endian_32(record, 10, strict, "size")
    flags = record[25]
    match = _VERSION.search(identifier)
    name = identifier[: match.start()] if match else identifier
    if name.endswith(".") and len(name) > 1:
        name = name[:-1]
    path = f"{parent}/{name}" if parent else name
    return DirectoryEntry(path, identifier, name, int(match.group(1)) if match else None, lba, size, flags)


class IsoImage:
    """An ISO 9660 image opened read-only.

    ``IsoImage(path)`` or ``IsoImage(binary_file_object)``. The sector layout is detected by looking for the primary
    volume descriptor at sector 16 in each of the four layouts; ``layout`` says which matched. With ``strict`` (the
    default) a record whose two byte-order copies of an extent or size disagree is an error; without it the
    little-endian copy is used.

    Only plain ISO 9660 is read: no Joliet names, no Rock Ridge names, no UDF. Names are the level-1 identifiers.
    """

    def __init__(self, source: Union[str, "os.PathLike[str]", BinaryIO], *, strict: bool = True) -> None:
        self.strict = strict
        if isinstance(source, (str, os.PathLike)):
            self._owns = True
            self._file: BinaryIO = open(source, "rb")
        else:
            self._owns = False
            self._file = source
        try:
            self._file.seek(0, os.SEEK_END)
            self.image_size = self._file.tell()
            self.layout = self._detect_layout()
            descriptor = self._read_primary_descriptor()
            self.volume_id = descriptor[40:72].decode("latin-1").strip()
            self.system_id = descriptor[8:40].decode("latin-1").strip()
            self.volume_blocks = self._parse_volume(descriptor)
            self.root = self._root_entry(descriptor)
        except BaseException:
            self.close()
            raise
        self._directories: dict[tuple[int, str], list[DirectoryEntry]] = {}

    # Opening.

    @classmethod
    def open(cls, path: Union[str, "os.PathLike[str]"], *, strict: bool = True) -> "IsoImage":
        return cls(path, strict=strict)

    def close(self) -> None:
        if self._owns:
            self._file.close()

    def __enter__(self) -> "IsoImage":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def _detect_layout(self) -> SectorLayout:
        for layout in LAYOUTS:
            offset = PRIMARY_SECTOR * layout.stride + layout.skip
            if offset + 7 > self.image_size:
                continue
            self._file.seek(offset)
            head = self._file.read(7)
            if head[1:6] == b"CD001" and head[6] == 1:
                return layout
        raise IsoError("no ISO 9660 volume descriptor at sector 16 (tried 2048, 2352 and 2336 byte layouts)")

    @property
    def sector_count(self) -> int:
        """How many whole sectors the file holds."""
        return self.image_size // self.layout.stride

    def _read_primary_descriptor(self) -> bytes:
        for sector in range(PRIMARY_SECTOR, PRIMARY_SECTOR + DESCRIPTOR_LIMIT):
            data = self.read_sectors(sector, 1)
            if data[1:6] != b"CD001" or data[6] != 1:
                raise IsoError("image lacks a valid ISO 9660 volume descriptor")
            if data[0] == 1:
                return data
            if data[0] == 255:
                break
        raise IsoError("the ISO 9660 primary volume descriptor was not found")

    def _parse_volume(self, descriptor: bytes) -> int:
        blocks = int.from_bytes(descriptor[80:84], "little")
        if blocks == 0 or (self.strict and blocks != int.from_bytes(descriptor[84:88], "big")):
            raise IsoError("ISO volume size is invalid")
        if descriptor[128:132] != SECTOR_SIZE.to_bytes(2, "little") + SECTOR_SIZE.to_bytes(2, "big"):
            raise IsoError("only 2048-byte ISO logical blocks are supported")
        if blocks > self.sector_count:
            raise IsoError(f"ISO image is shorter than its declared volume size ({self.sector_count} of {blocks} sectors)")
        return blocks

    def _root_entry(self, descriptor: bytes) -> DirectoryEntry:
        length = descriptor[156]
        record = parse_record(descriptor[156 : 156 + length], "", strict=self.strict)
        if not record.is_dir:
            raise IsoError("the ISO root record is not a directory")
        return DirectoryEntry("", "", "", None, record.lba, record.size, record.flags)

    # Raw access.

    def read_sectors(self, lba: int, count: int = 1) -> bytes:
        """The user data (2048 bytes each) of ``count`` sectors from ``lba``. Raises ``IsoError`` past the end."""
        if lba < 0 or count < 0 or lba + count > self.sector_count:
            raise IsoError(f"sectors {lba} to {lba + count - 1} are outside the image ({self.sector_count} sectors)")
        if count == 0:
            return b""
        layout = self.layout
        if layout.stride == SECTOR_SIZE:
            self._file.seek(lba * SECTOR_SIZE)
            data = self._file.read(count * SECTOR_SIZE)
        else:
            parts = []
            for sector in range(lba, lba + count):
                self._file.seek(sector * layout.stride + layout.skip)
                parts.append(self._file.read(SECTOR_SIZE))
            data = b"".join(parts)
        if len(data) != count * SECTOR_SIZE:
            raise IsoError("the image ended while it was being read")
        return data

    def read_extent(self, entry: DirectoryEntry, offset: int = 0, length: Optional[int] = None) -> bytes:
        """``length`` bytes of a file starting ``offset`` bytes in (default: to the end)."""
        if entry.multi_extent:
            raise IsoError(f"{entry.path or '/'} has several extents, which are not supported")
        if length is None:
            length = entry.size - offset
        if offset < 0 or length < 0 or offset + length > entry.size:
            raise IsoError(f"read of {length} bytes at {offset} is outside {entry.path or '/'} ({entry.size} bytes)")
        if length == 0:
            return b""
        first = offset // SECTOR_SIZE
        last = (offset + length + SECTOR_SIZE - 1) // SECTOR_SIZE
        data = self.read_sectors(entry.lba + first, last - first)
        start = offset - first * SECTOR_SIZE
        return data[start : start + length]

    # Directories.

    def listdir(self, directory: Union[DirectoryEntry, str, None] = None) -> list[DirectoryEntry]:
        """The entries of a directory (the root by default), in disc order, without ``.`` and ``..``."""
        entry = self._as_entry(directory) if directory is not None else self.root
        if not entry.is_dir:
            raise IsoError(f"{entry.path} is not a directory")
        key = (entry.lba, entry.path)
        listing = self._directories.get(key)
        if listing is None:
            listing = self._directories[key] = self._parse_directory(entry)
        return list(listing)

    def _parse_directory(self, directory: DirectoryEntry) -> list[DirectoryEntry]:
        if directory.size > MAX_DIRECTORY_SIZE:
            raise IsoError(f"directory {directory.path or '/'} is larger than {MAX_DIRECTORY_SIZE} bytes")
        data = self.read_extent(directory)
        entries: list[DirectoryEntry] = []
        position = 0
        while position < len(data):
            length = data[position]
            if length == 0:
                position = (position // SECTOR_SIZE + 1) * SECTOR_SIZE
                continue
            record = data[position : position + length]
            if len(record) < 34 or 33 + record[32] > len(record):
                raise IsoError(f"truncated ISO directory record in {directory.path or '/'}")
            position += length
            if record[32] == 1 and record[33] in (0, 1):
                continue
            entries.append(parse_record(bytes(record), directory.path, strict=self.strict))
        return entries

    def walk(self, directory: Union[DirectoryEntry, str, None] = None, *, max_entries: int = MAX_ENTRIES) -> Iterator[DirectoryEntry]:
        """Every entry below a directory (the root by default), directories before their contents, depth first.

        A directory that points back at one already visited is skipped, so a damaged tree cannot loop.
        """
        start = self._as_entry(directory) if directory is not None else self.root
        seen = {start.lba}
        stack = [start]
        count = 0
        while stack:
            current = stack.pop()
            children = self.listdir(current)
            subdirectories = []
            for child in children:
                count += 1
                if count > max_entries:
                    raise IsoError(f"more than {max_entries} entries; the directory tree is damaged or too large")
                yield child
                if child.is_dir and child.lba not in seen:
                    seen.add(child.lba)
                    subdirectories.append(child)
            stack.extend(reversed(subdirectories))

    def files(self) -> Iterator[DirectoryEntry]:
        """``walk()`` without the directories."""
        return (entry for entry in self.walk() if not entry.is_dir)

    def find(self, path: Union[str, DirectoryEntry]) -> DirectoryEntry:
        """Look a path up, ignoring case, with ``/`` or ``\\`` separators. A ``;version`` suffix is accepted and
        ignored. Raises ``IsoNotFound``."""
        if isinstance(path, DirectoryEntry):
            return path
        entry = self.root
        for part in [p for p in path.replace("\\", "/").split("/") if p]:
            wanted = _VERSION.sub("", part).upper()
            if wanted.endswith(".") and len(wanted) > 1:
                wanted = wanted[:-1]
            if not entry.is_dir:
                raise IsoNotFound(path)
            for candidate in self.listdir(entry):
                if candidate.name.upper() == wanted:
                    entry = candidate
                    break
            else:
                raise IsoNotFound(path)
        return entry

    def _as_entry(self, path: Union[str, DirectoryEntry]) -> DirectoryEntry:
        return self.find(path)

    # Files.

    def read_file(self, path: Union[str, DirectoryEntry], *, limit: Optional[int] = None) -> bytes:
        """The whole file. With ``limit``, a larger file raises ``IsoError`` before anything is read."""
        entry = self._file_entry(path)
        if limit is not None and entry.size > limit:
            raise IsoError(f"{entry.path} is {entry.size} bytes, over the limit of {limit}")
        return self.read_extent(entry)

    def iter_file(self, path: Union[str, DirectoryEntry], *, chunk: int = CHUNK) -> Iterator[bytes]:
        """The file in pieces of at most ``chunk`` bytes."""
        entry = self._file_entry(path)
        for offset in range(0, entry.size, chunk):
            yield self.read_extent(entry, offset, min(chunk, entry.size - offset))

    def sha256(self, path: Union[str, DirectoryEntry]) -> str:
        """The SHA-256 of a file's bytes, read in pieces."""
        digest = hashlib.sha256()
        for piece in self.iter_file(path):
            digest.update(piece)
        return digest.hexdigest()

    def extract(self, path: Union[str, DirectoryEntry], destination: Union[str, "os.PathLike[str]"]) -> ExtractResult:
        """Copy a file out of the image and return its size and SHA-256.

        The bytes go to a temporary file beside ``destination`` that is renamed into place only when the whole file
        was read, so a failure leaves any earlier ``destination`` as it was and no partial file.
        """
        entry = self._file_entry(path)
        target = Path(destination)
        temporary = target.with_name(f".{target.name}.{os.getpid()}.tmp")
        digest = hashlib.sha256()
        written = 0
        try:
            with temporary.open("wb") as handle:
                for piece in self.iter_file(entry):
                    handle.write(piece)
                    digest.update(piece)
                    written += len(piece)
            os.replace(temporary, target)
        finally:
            temporary.unlink(missing_ok=True)
        return ExtractResult(written, digest.hexdigest())

    def _file_entry(self, path: Union[str, DirectoryEntry]) -> DirectoryEntry:
        entry = self.find(path)
        if entry.is_dir:
            raise IsoError(f"{entry.path or '/'} is a directory")
        return entry


def is_iso9660(source: Union[str, "os.PathLike[str]", bytes]) -> bool:
    """True when ``source`` (a path, or the bytes of an image) has a primary volume descriptor in a known layout."""
    try:
        if isinstance(source, (bytes, bytearray)):
            with IsoImage(io.BytesIO(bytes(source)), strict=False):
                return True
        with IsoImage(source, strict=False):
            return True
    except (IsoError, OSError, struct.error):
        return False
