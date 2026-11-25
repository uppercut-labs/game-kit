"""Audit a tree for what a public decompilation repository must never contain: game data, disc images, disassembly.

Every file (git-tracked and untracked-but-not-ignored, or every file of a plain folder) is checked for:

- binaries: NUL bytes, or the magic of an ELF, an ISO 9660 image, an archive, an image, audio or video;
- assets, disc images and build products, by extension (images, audio, video, fonts, archives, `.iso`/`.bin`/`.cue`/`.chd`,
  `.elf`/`.irx`/`.o`/`.a`, ...);
- logos and artwork: any image or icon, and files named logo, banner or boxart, except screenshots (below);
- assembly: `.s`/`.asm`/`.inc` files, `.incbin`, `INCLUDE_ASM`/`INCLUDE_RODATA`, and text with 10 or more lines that look like a
  disassembly listing (`/* offset vram word */` comments, objdump's `addr: word  insn`, `glabel`);
- disc data in source: a C initialiser with more than `max_literals` numeric literals, or a run of 2,000+ base64 or hex characters;
- compiler dumps (gcc `-da` RTL, `.i` preprocessor output).

Screenshots are allowed: a PNG, JPEG or WebP file in a folder named `screenshots`, whose content matches its extension, at most
2 MB and not named like artwork, is listed in the report and is not a finding. Any other image is a finding. Nothing is skipped
silently. A file of your own that trips a check and is not game data (a licensed font, shaders you compiled) goes in `known` with
the SHA-256 it was checked as: the report lists it, and a file at that path with other content is a finding as usual.
"""

from __future__ import annotations

import hashlib
import re
import subprocess
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Mapping, Optional, Pattern, Sequence, Set, Tuple, Union

PathLike = Union[str, Path]

ASSET_EXT: Dict[str, Set[str]] = {
    "image": {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tga", ".tif", ".tiff", ".webp", ".svg", ".ico", ".icns", ".psd", ".tim", ".tm2", ".tex", ".dds", ".xcf", ".kra"},
    "audio": {".wav", ".mp3", ".ogg", ".flac", ".aiff", ".vag", ".vb", ".vh", ".adpcm", ".at3", ".sq", ".hd", ".bd", ".mid", ".midi"},
    "video": {".mp4", ".mov", ".avi", ".mkv", ".webm", ".pss", ".m2v", ".ipu"},
    "font": {".ttf", ".otf", ".fnt", ".woff", ".woff2"},
    "disc image": {".iso", ".bin", ".cue", ".chd", ".img", ".mdf", ".mds", ".nrg", ".cso", ".zso", ".ecm"},
    "executable or object": {".elf", ".irx", ".erx", ".o", ".a", ".so", ".dll", ".exe", ".dylib", ".lib", ".obj", ".pyc"},
    "archive": {".zip", ".7z", ".rar", ".tar", ".gz", ".tgz", ".xz", ".bz2", ".zst", ".lz4"},
    "assembly": {".s", ".asm", ".inc", ".mips"},
    "compiler dump": {".i", ".rtl", ".jump", ".cse", ".gcse", ".loop", ".cse2", ".bp", ".flow", ".combine", ".regmove", ".sched", ".lreg", ".greg",
                      ".sched2", ".flow2", ".jump2", ".mach", ".dbr", ".addressof", ".range", ".blob", ".orig", ".both", ".endlive", ".recount"},
}
MAGIC: List[Tuple[int, bytes, str]] = [
    (0, b"\x7fELF", "ELF binary"), (0x8001, b"CD001", "ISO 9660 image"), (0, b"\x89PNG", "PNG image"), (0, b"\xff\xd8\xff", "JPEG image"),
    (0, b"GIF8", "GIF image"), (0, b"TIM2", "PS2 TIM2 image"), (0, b"RIFF", "RIFF audio/video"), (0, b"VAGp", "PS2 VAG audio"),
    (0, b"\x00\x00\x01\xba", "MPEG/PSS video"), (0, b"PK\x03\x04", "zip archive"), (0, b"\x1f\x8b", "gzip archive"), (0, b"\xfd7zXZ", "xz archive"),
    (0, b"7z\xbc\xaf", "7z archive"), (0, b"OggS", "Ogg audio"), (0, b"ID3", "MP3 audio"),
]
SCREENSHOT_MAGIC: Dict[str, List[bytes]] = {".png": [b"\x89PNG\r\n\x1a\n"], ".jpg": [b"\xff\xd8\xff"], ".jpeg": [b"\xff\xd8\xff"], ".webp": [b"RIFF"]}
NAMED_ART = re.compile(r"(logo|banner|boxart|box-art|splash|artwork|cover-?art|key-?art)", re.I)
LISTING = re.compile(r"/\*\s*[0-9A-Fa-f]{4,8}\s+[0-9A-Fa-f]{8}\s+[0-9A-Fa-f]{8}\s*\*/|^\s*[0-9a-fA-F]{6,8}:\s+[0-9a-fA-F]{8}\s+\w|^\s*glabel\s", re.M)
ASM_MACRO = re.compile(r"\bINCLUDE_(ASM|RODATA)\s*\(|^\s*\.incbin\b", re.M)
BLOB = re.compile(r"[A-Za-z0-9+/=]{2000,}|(?:\\x[0-9a-fA-F]{2}){1000,}")
INIT = re.compile(r"=\s*\{")
NUM = re.compile(r"\b(?:0x[0-9a-fA-F]+|\d+)\b")
KINDS = ["binary", "image", "audio", "video", "font", "disc image", "executable or object", "archive", "logo or artwork", "assembly", "disc data", "compiler dump"]


@dataclass
class AuditConfig:
    """What to check and what to allow.

    - `known`: repo path to `(sha256, why)` for a file that trips a check and is not game data.
    - `publish`: when set, only these paths count (a path ending in `/` is a folder); for auditing a larger working tree as if it were the
      public set. `publish_exclude` removes paths from it.
    - `screenshot_dir`, `screenshot_max`: where screenshots may live, and their size limit in bytes.
    - `max_literals`: the most numeric literals one C initialiser may hold.
    - `table_extension`, `table_row`: for a generator table (a `.tbl` of `number name` rows that a script turns into code): the
      extension, and a regular expression every line must match. A table holds names and numbers, never code. Unset, tables are not checked."""

    known: Mapping[str, Tuple[str, str]] = field(default_factory=dict)
    publish: Optional[Sequence[str]] = None
    publish_exclude: Sequence[str] = ()
    screenshot_dir: str = "screenshots"
    screenshot_max: int = 2 * 1024 * 1024
    max_literals: int = 1024
    table_extension: Optional[str] = None
    table_row: Optional[Pattern[str]] = None
    table_row_name: str = "rows of the table format"
    """How a finding names what a table line should be."""


@dataclass
class AuditReport:
    findings: Dict[str, List[Tuple[str, str]]]
    counts: Dict[str, int]
    methods: List[str]
    screenshots: List[Tuple[str, str]]
    known: List[Tuple[str, str]]
    total: int

    @property
    def found(self) -> int:
        return sum(len(v) for v in self.findings.values())

    @property
    def exit_status(self) -> int:
        """1 when any file is a finding, else 0."""
        return 1 if self.found else 0

    def markdown(self, generated_by: Optional[str] = None, known_heading: str = "Known files") -> str:
        """The report. `generated_by` names the command that made it (a line near the top); `known_heading` titles the known-files list."""
        by = f"Generated by `{generated_by}`. " if generated_by else ""
        lines = ["# Repo audit", "", f"{by}Scanned {self.total:,} files: " + "; ".join(self.methods) + ".", "", "| Check | Findings |", "| --- | ---: |"]
        for k in KINDS:
            lines.append(f"| {k} | {len(self.findings.get(k, []))} |")
        lines += ["", "**Result: " + ("clean." if not self.found else f"{self.found} findings.") + "**", ""]
        for k in KINDS:
            items = self.findings.get(k, [])
            if items:
                lines += [f"## {k}", ""]
                lines += [f"- `{rel}`: {why}" for rel, why in items[:200]]
                if len(items) > 200:
                    lines.append(f"- ... and {len(items) - 200} more")
                lines.append("")
        if self.known:
            lines += [f"## {known_heading} (not game data, {len(self.known)})", ""]
            lines += [f"- `{rel}`: {why}" for rel, why in self.known]
            lines.append("")
        if self.screenshots:
            lines += [f"## Screenshots (allowed, {len(self.screenshots)})", ""]
            lines += [f"- `{rel}`: {why}" for rel, why in self.screenshots]
            lines.append("")
        lines += ["## File types scanned", "", ", ".join(f"{e} {n:,}" for e, n in sorted(self.counts.items(), key=lambda x: -x[1])), ""]
        return "\n".join(lines)


def list_files(root: Path) -> Tuple[List[str], str]:
    """Paths relative to `root`: git's tracked and untracked-but-not-ignored files when `root` is a git checkout, else every file."""
    try:
        r = subprocess.run(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "."], cwd=root, capture_output=True, check=True)
        files = sorted({p for p in r.stdout.decode().split("\0") if p and (root / p).is_file()})
        return files, "git (tracked and untracked, not ignored)"
    except (subprocess.CalledProcessError, FileNotFoundError):
        return sorted(str(p.relative_to(root)).replace("\\", "/") for p in root.rglob("*") if p.is_file()), "directory walk"


def largest_initializer(text: str) -> int:
    best = 0
    for m in INIT.finditer(text):
        i, depth = m.end(), 1
        while depth and i < len(text):
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        best = max(best, len(NUM.findall(text[m.end() : i])))
    return best


def screenshot_status(rel: str, data: bytes, cfg: AuditConfig) -> Optional[Tuple[bool, str]]:
    """`(True, why)` for an allowed screenshot, `(False, why)` for an image that needs review, None for a file that is not an image."""
    ext = Path(rel).suffix.lower()
    if ext not in ASSET_EXT["image"]:
        return None
    if cfg.screenshot_dir not in Path(rel).parts[:-1]:
        return False, f"image outside a {cfg.screenshot_dir}/ folder: review it (a screenshot goes in {cfg.screenshot_dir}/)"
    if ext not in SCREENSHOT_MAGIC:
        return False, f"{ext} in {cfg.screenshot_dir}/: screenshots are PNG, JPEG or WebP"
    if not any(data.startswith(m) for m in SCREENSHOT_MAGIC[ext]) or (ext == ".webp" and data[8:12] != b"WEBP"):
        return False, f"{cfg.screenshot_dir}/ file whose content is not {ext[1:].upper()}"
    if len(data) > cfg.screenshot_max:
        return False, f"screenshot of {len(data):,} bytes, over {cfg.screenshot_max:,}"
    if NAMED_ART.search(Path(rel).name):
        return False, "screenshot named like a logo or artwork: review it"
    return True, f"screenshot, {len(data):,} bytes"


def audit_file(path: Path, rel: str, cfg: AuditConfig, allowed: Optional[List[Tuple[str, str]]] = None,
               known: Optional[List[Tuple[str, str]]] = None) -> List[Tuple[str, str]]:
    """The findings for one file, as `(kind, why)`. `allowed` and `known` collect the screenshots and known files seen."""
    found: List[Tuple[str, str]] = []
    ext = Path(rel).suffix.lower()
    data = path.read_bytes()
    if rel in cfg.known and hashlib.sha256(data).hexdigest() == cfg.known[rel][0]:
        if known is not None:
            known.append((rel, cfg.known[rel][1]))
        return found
    shot = screenshot_status(rel, data, cfg)
    if shot and shot[0]:
        if allowed is not None:
            allowed.append((rel, shot[1]))
        return found
    if shot:
        found.append(("image", shot[1]))
    for kind, exts in ASSET_EXT.items():
        if ext in exts and not (kind == "image" and shot):
            found.append((kind, f"extension {ext}"))
    if NAMED_ART.search(Path(rel).name):
        found.append(("logo or artwork", "file name"))
    for off, magic, what in MAGIC:
        if data[off : off + len(magic)] == magic:
            found.append(("binary", what))
    if b"\0" in data[:65536]:
        found.append(("binary", "NUL bytes"))
    if any(k == "binary" for k, _ in found):
        return found
    text = data.decode("utf-8", errors="replace")
    n = len(LISTING.findall(text))
    if n >= 10:
        found.append(("assembly", f"{n} disassembly-listing lines"))
    if ASM_MACRO.search(text) and ext in (".c", ".h", ".s", ".inc"):
        found.append(("assembly", "INCLUDE_ASM/INCLUDE_RODATA/.incbin"))
    if BLOB.search(text):
        found.append(("disc data", "long base64/hex run"))
    if ext in (".c", ".h"):
        n = largest_initializer(text)
        if n > cfg.max_literals:
            found.append(("disc data", f"initializer with {n} literals"))
    if cfg.table_extension and cfg.table_row is not None and ext == cfg.table_extension:
        bad = [i for i, line in enumerate(text.splitlines(), 1) if not cfg.table_row.match(line)]
        if bad:
            found.append(("assembly", f"generator table with {len(bad)} lines that are not {cfg.table_row_name} (first: line {bad[0]})"))
    return found


def in_publish(rel: str, cfg: AuditConfig) -> bool:
    if cfg.publish is None:
        return True
    return rel not in cfg.publish_exclude and any(rel == p or (p.endswith("/") and rel.startswith(p)) for p in cfg.publish)


def audit_tree(roots: Sequence[Tuple[PathLike, str]], cfg: Optional[AuditConfig] = None, *, publish_first: bool = False) -> AuditReport:
    """Audit one or more trees. Each root is `(folder, prefix)`: the prefix is put in front of the paths the report shows. With
    `publish_first`, the first tree is limited to `cfg.publish`."""
    cfg = cfg or AuditConfig()
    findings: Dict[str, List[Tuple[str, str]]] = defaultdict(list)
    counts: Dict[str, int] = defaultdict(int)
    methods: List[str] = []
    screenshots: List[Tuple[str, str]] = []
    known: List[Tuple[str, str]] = []
    total = 0
    for index, (folder, prefix) in enumerate(roots):
        root = Path(folder).resolve()
        limit = publish_first and index == 0
        files, method = list_files(root)
        methods.append(f"`{root.name}` ({method}{', PUBLISH set' if limit else ''})")
        for rel in files:
            if limit and not in_publish(rel, cfg):
                continue
            total += 1
            counts[Path(rel).suffix.lower() or "(none)"] += 1
            allowed: List[Tuple[str, str]] = []
            kn: List[Tuple[str, str]] = []
            for kind, why in audit_file(root / rel, rel, cfg, allowed, kn):
                findings[kind].append((prefix + rel, why))
            screenshots += [(prefix + r, why) for r, why in allowed]
            known += [(prefix + r, why) for r, why in kn]
    return AuditReport(dict(findings), dict(counts), methods, screenshots, known, total)
