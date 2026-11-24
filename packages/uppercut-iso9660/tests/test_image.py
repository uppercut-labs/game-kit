import hashlib
import io
import struct

import pytest

from images import SECTOR, build_iso, directory, file, make_elf, ps2_disc, record, wrap
from uppercut_iso9660 import (
    COOKED,
    RAW_2336,
    RAW_MODE1,
    RAW_MODE2,
    DirectoryEntry,
    IsoError,
    IsoImage,
    IsoNotFound,
    is_iso9660,
)

PAYLOAD = bytes((i * 7 + 3) % 256 for i in range(5000))  # three sectors, the last one partly full


def tree() -> bytearray:
    return build_iso(
        [
            file("README.TXT;1", b"hello\n"),
            file("DATA.BIN;1", PAYLOAD),
            file("NOEXT.;1", b"no extension"),
            directory("ASSETS", file("ONE.DAT;1", b"1" * 10), directory("DEEP", file("TWO.DAT;1", b"2" * 3000))),
            directory("EMPTYDIR"),
            file("ZERO.BIN;1", b""),
        ]
    )


@pytest.fixture
def image_path(tmp_path):
    path = tmp_path / "disc.iso"
    path.write_bytes(tree())
    return path


def test_lists_the_root_in_disc_order_without_dot_entries(image_path):
    with IsoImage(image_path) as image:
        names = [(e.name, e.is_dir) for e in image.listdir()]
    assert names == [("README.TXT", False), ("DATA.BIN", False), ("NOEXT", False), ("ASSETS", True), ("EMPTYDIR", True), ("ZERO.BIN", False)]


def test_entry_fields(image_path):
    with IsoImage(image_path) as image:
        readme = image.find("readme.txt")
    assert readme == DirectoryEntry("README.TXT", "README.TXT;1", "README.TXT", 1, readme.lba, 6, 0)
    assert not readme.is_dir and not readme.multi_extent


def test_a_name_without_an_extension_loses_its_trailing_dot(image_path):
    with IsoImage(image_path) as image:
        entry = image.find("NOEXT")
        assert entry.identifier == "NOEXT.;1" and entry.name == "NOEXT"
        assert image.find("noext.;1") == entry


def test_walk_visits_every_entry_directories_first_depth_first(image_path):
    with IsoImage(image_path) as image:
        paths = [e.path for e in image.walk()]
        files = [e.path for e in image.files()]
    assert paths == [
        "README.TXT", "DATA.BIN", "NOEXT", "ASSETS", "EMPTYDIR", "ZERO.BIN",
        "ASSETS/ONE.DAT", "ASSETS/DEEP", "ASSETS/DEEP/TWO.DAT",
    ]
    assert files == ["README.TXT", "DATA.BIN", "NOEXT", "ZERO.BIN", "ASSETS/ONE.DAT", "ASSETS/DEEP/TWO.DAT"]


def test_walk_can_start_in_a_subdirectory(image_path):
    with IsoImage(image_path) as image:
        assert [e.path for e in image.walk("assets")] == ["ASSETS/ONE.DAT", "ASSETS/DEEP", "ASSETS/DEEP/TWO.DAT"]
        assert [e.name for e in image.listdir("ASSETS/DEEP")] == ["TWO.DAT"]


@pytest.mark.parametrize("path", ["ASSETS/DEEP/TWO.DAT", "assets/deep/two.dat", "\\ASSETS\\DEEP\\TWO.DAT;1", "/assets//deep/two.dat"])
def test_find_ignores_case_separators_and_version(image_path, path):
    with IsoImage(image_path) as image:
        assert image.find(path).path == "ASSETS/DEEP/TWO.DAT"
        assert image.read_file(path) == b"2" * 3000


def test_find_failures(image_path):
    with IsoImage(image_path) as image:
        with pytest.raises(IsoNotFound):
            image.find("MISSING.TXT")
        with pytest.raises(FileNotFoundError):
            image.find("ASSETS/NOPE/X")
        with pytest.raises(IsoNotFound):
            image.find("README.TXT/INSIDE")
        with pytest.raises(IsoError, match="not a directory"):
            image.listdir("README.TXT")
        with pytest.raises(IsoError, match="is a directory"):
            image.read_file("ASSETS")
        assert image.find("") == image.root and image.find("/") == image.root


def test_read_whole_files_and_extents(image_path):
    with IsoImage(image_path) as image:
        assert image.read_file("DATA.BIN") == PAYLOAD
        assert image.read_file("ZERO.BIN") == b""
        entry = image.find("DATA.BIN")
        assert image.read_extent(entry, 2040, 20) == PAYLOAD[2040:2060]  # across a sector boundary
        assert image.read_extent(entry, 4990) == PAYLOAD[4990:]
        assert image.read_extent(entry, 0, 0) == b""


def test_read_at_works_from_a_sector_number_and_may_pass_the_end_of_the_file(image_path):
    with IsoImage(image_path) as image:
        entry = image.find("DATA.BIN")
        assert image.read_at(entry.lba, 2040, 20) == PAYLOAD[2040:2060]
        assert image.read_at(entry.lba, 0, 0) == b""
        tail = image.read_at(entry.lba, 4096, 2048)  # the last sector, past the file's 5000 bytes
        assert tail[: 5000 - 4096] == PAYLOAD[4096:] and len(tail) == 2048
        with pytest.raises(IsoError, match="outside the image"):
            image.read_at(image.sector_count, 0, 1)
        with pytest.raises(IsoError, match="negative"):
            image.read_at(entry.lba, -1, 1)


def test_read_bounds(image_path):
    with IsoImage(image_path) as image:
        entry = image.find("DATA.BIN")
        for offset, length in [(5000, 1), (4999, 2), (-1, 1), (0, -1)]:
            with pytest.raises(IsoError, match="outside"):
                image.read_extent(entry, offset, length)
        with pytest.raises(IsoError, match="over the limit of 100"):
            image.read_file("DATA.BIN", limit=100)
        assert image.read_file("README.TXT", limit=6) == b"hello\n"


def test_iter_file_pieces(image_path):
    with IsoImage(image_path) as image:
        pieces = list(image.iter_file("DATA.BIN", chunk=2000))
    assert [len(p) for p in pieces] == [2000, 2000, 1000]
    assert b"".join(pieces) == PAYLOAD


def test_sha256_and_extract(image_path, tmp_path):
    with IsoImage(image_path) as image:
        digest = hashlib.sha256(PAYLOAD).hexdigest()
        assert image.sha256("DATA.BIN") == digest
        result = image.extract("data.bin", tmp_path / "out.bin")
    assert (result.size, result.sha256) == (5000, digest)
    assert (tmp_path / "out.bin").read_bytes() == PAYLOAD
    assert sorted(p.name for p in tmp_path.iterdir()) == ["disc.iso", "out.bin"]


def test_a_failed_extract_leaves_nothing_and_keeps_the_old_file(tmp_path):
    cooked = tree()
    with IsoImage(io.BytesIO(bytes(cooked))) as probe:
        entry = probe.find("DATA.BIN")
    path = tmp_path / "short.iso"
    path.write_bytes(bytes(cooked[: (entry.lba + 1) * SECTOR]))  # the file's later sectors are cut off... but the volume says it is longer
    with pytest.raises(IsoError, match="shorter than its declared volume size"):
        IsoImage(path)
    # With the declared size patched to match, the file's own extent runs past the end.
    patched = bytearray(cooked[: (entry.lba + 1) * SECTOR])
    patched[16 * SECTOR + 80 : 16 * SECTOR + 88] = struct.pack("<I", entry.lba + 1) + struct.pack(">I", entry.lba + 1)
    path.write_bytes(bytes(patched))
    destination = tmp_path / "out.bin"
    destination.write_bytes(b"previous")
    with IsoImage(path) as image, pytest.raises(IsoError, match="outside the image"):
        image.extract("DATA.BIN", destination)
    assert destination.read_bytes() == b"previous"
    assert sorted(p.name for p in tmp_path.iterdir()) == ["out.bin", "short.iso"]


def test_a_directory_spanning_sectors_is_read_in_full():
    many = [file(f"FILE{i:03d}.DAT;1", bytes([i % 256]) * (i + 1)) for i in range(70)]
    image = IsoImage(io.BytesIO(bytes(build_iso(many, directory_sectors=3))))
    names = [e.name for e in image.listdir()]
    assert names == [f"FILE{i:03d}.DAT" for i in range(70)]
    assert image.read_file("FILE069.DAT") == bytes([69]) * 70


@pytest.mark.parametrize("layout", [COOKED, RAW_MODE1, RAW_MODE2, RAW_2336], ids=lambda layout: layout.name)
def test_every_layout_reads_the_same_disc(layout):
    cooked = bytes(tree())
    data = cooked if layout is COOKED else wrap(cooked, layout.name)
    with IsoImage(io.BytesIO(data)) as image:
        assert image.layout == layout
        assert [e.path for e in image.walk()] == [e.path for e in IsoImage(io.BytesIO(cooked)).walk()]
        assert image.read_file("DATA.BIN") == PAYLOAD
        assert image.read_file("ASSETS/DEEP/TWO.DAT") == b"2" * 3000
        assert image.sha256("DATA.BIN") == hashlib.sha256(PAYLOAD).hexdigest()
        assert image.volume_id == "SYNTHETIC"


def test_raw_and_cooked_sector_data_are_identical():
    cooked = bytes(tree())
    plain = IsoImage(io.BytesIO(cooked))
    raw = IsoImage(io.BytesIO(wrap(cooked, "raw-2352-mode2")))
    assert plain.sector_count == raw.sector_count
    assert plain.read_sectors(0, plain.sector_count) == raw.read_sectors(0, raw.sector_count)


def test_sector_reads_are_bounds_checked(image_path):
    with IsoImage(image_path) as image:
        count = image.sector_count
        assert len(image.read_sectors(count - 1, 1)) == SECTOR
        assert image.read_sectors(count, 0) == b""
        for lba, n in [(count, 1), (count - 1, 2), (-1, 1)]:
            with pytest.raises(IsoError, match="outside the image"):
                image.read_sectors(lba, n)


def test_volume_facts(image_path):
    with IsoImage(image_path) as image:
        assert image.volume_id == "SYNTHETIC"
        assert image.system_id == "SYNTHETIC SYSTEM"
        assert image.volume_blocks == image.sector_count
        assert image.image_size == image_path.stat().st_size


# Damaged and unusual images.


def test_not_an_iso_is_refused(tmp_path):
    path = tmp_path / "junk.bin"
    path.write_bytes(bytes(40 * SECTOR))
    with pytest.raises(IsoError, match="no ISO 9660 volume descriptor"):
        IsoImage(path)
    assert not is_iso9660(path)
    assert not is_iso9660(b"short")
    assert is_iso9660(bytes(tree()))
    assert is_iso9660(tmp_path / "missing") is False


def test_a_file_object_is_left_open():
    stream = io.BytesIO(bytes(tree()))
    with IsoImage(stream):
        pass
    assert not stream.closed


def test_a_path_is_closed_with_the_image(image_path):
    image = IsoImage(image_path)
    handle = image._file
    image.close()
    assert handle.closed


def test_a_bad_descriptor_after_the_first_is_refused():
    data = bytearray(tree())
    data[17 * SECTOR + 1 : 17 * SECTOR + 6] = b"WRONG"
    data[16 * SECTOR] = 2  # the first descriptor is not primary, so the search moves on and meets the damage
    with pytest.raises(IsoError, match="valid ISO 9660 volume descriptor"):
        IsoImage(io.BytesIO(bytes(data)))


def test_no_primary_descriptor_before_the_terminator():
    data = bytearray(tree())
    data[16 * SECTOR] = 255
    with pytest.raises(IsoError, match="primary volume descriptor was not found"):
        IsoImage(io.BytesIO(bytes(data)))


def test_the_primary_descriptor_may_follow_other_descriptors():
    data = bytearray(tree())
    boot = bytearray(SECTOR)
    boot[0:7] = b"\x00CD001\x01"
    primary = bytes(data[16 * SECTOR : 17 * SECTOR])
    data[16 * SECTOR : 17 * SECTOR] = boot
    data[17 * SECTOR : 18 * SECTOR] = primary  # this sector held the terminator; the root directory is not touched
    assert IsoImage(io.BytesIO(bytes(data))).volume_id == "SYNTHETIC"


def test_a_volume_longer_than_the_file_is_refused():
    data = bytes(tree())
    with pytest.raises(IsoError, match="shorter than its declared volume size"):
        IsoImage(io.BytesIO(data[:-SECTOR]))


def test_other_logical_block_sizes_are_refused():
    data = bytearray(tree())
    data[16 * SECTOR + 128 : 16 * SECTOR + 132] = struct.pack("<H", 4096) + struct.pack(">H", 4096)
    with pytest.raises(IsoError, match="2048-byte"):
        IsoImage(io.BytesIO(bytes(data)))


def test_volume_size_disagreement_is_strict_only():
    data = bytearray(tree())
    count = len(data) // SECTOR
    data[16 * SECTOR + 84 : 16 * SECTOR + 88] = struct.pack(">I", count + 5)
    with pytest.raises(IsoError, match="volume size is invalid"):
        IsoImage(io.BytesIO(bytes(data)))
    assert IsoImage(io.BytesIO(bytes(data)), strict=False).volume_blocks == count


def test_zero_volume_size_is_refused_even_when_lenient():
    data = bytearray(tree())
    data[16 * SECTOR + 80 : 16 * SECTOR + 84] = bytes(4)
    with pytest.raises(IsoError, match="volume size is invalid"):
        IsoImage(io.BytesIO(bytes(data)), strict=False)


def test_byte_order_disagreement_in_a_record_is_strict_only():
    bad = record(b"BAD.DAT;1", 30, 10, little_only=(99, 11))
    data = bytearray(tree())
    root = IsoImage(io.BytesIO(bytes(data))).root
    start = root.lba * SECTOR
    end = start
    while data[end]:
        end += data[end]
    data[end : end + len(bad)] = bad
    with pytest.raises(IsoError, match="byte-order copies disagree"):
        IsoImage(io.BytesIO(bytes(data))).listdir()
    entries = IsoImage(io.BytesIO(bytes(data)), strict=False).listdir()
    assert entries[-1].name == "BAD.DAT" and (entries[-1].lba, entries[-1].size) == (30, 10)


def test_a_record_that_runs_off_its_sector_is_refused():
    data = bytearray(tree())
    root = IsoImage(io.BytesIO(bytes(data))).root
    start = root.lba * SECTOR
    end = start
    while data[end]:
        end += data[end]
    data[end] = 40
    data[end + 32] = 200  # the identifier claims to be longer than the record
    with pytest.raises(IsoError, match="truncated ISO directory record"):
        IsoImage(io.BytesIO(bytes(data))).listdir()


def test_a_directory_that_points_back_does_not_loop():
    data = bytearray(tree())
    image = IsoImage(io.BytesIO(bytes(data)))
    assets = image.find("ASSETS")
    # Make ASSETS/DEEP's directory record point at ASSETS itself.
    deep = image.find("ASSETS/DEEP")
    position = assets.lba * SECTOR
    while data[position]:
        record_bytes = data[position : position + data[position]]
        if record_bytes[32] == 4 and record_bytes[33:37] == b"DEEP":
            data[position + 2 : position + 10] = struct.pack("<I", assets.lba) + struct.pack(">I", assets.lba)
        position += data[position]
    looped = IsoImage(io.BytesIO(bytes(data)))
    paths = [e.path for e in looped.walk()]
    assert deep.lba != assets.lba
    assert paths.count("ASSETS/DEEP") == 1 and len(paths) < 20


def test_too_many_entries_is_an_error(image_path):
    with IsoImage(image_path) as image, pytest.raises(IsoError, match="more than 3 entries"):
        list(image.walk(max_entries=3))


def test_multi_extent_files_are_refused():
    data = build_iso([file("BIG.DAT;1", b"x" * 10, flags=0x80)])
    image = IsoImage(io.BytesIO(bytes(data)))
    assert image.find("BIG.DAT").multi_extent
    with pytest.raises(IsoError, match="several extents"):
        image.read_file("BIG.DAT")


def test_an_oversized_directory_is_refused():
    data = bytearray(tree())
    image = IsoImage(io.BytesIO(bytes(data)))
    start = image.root.lba * SECTOR
    size_field = 2 + 8  # the root record in the descriptor holds the directory's size at byte 10
    data[16 * SECTOR + 156 + 10 : 16 * SECTOR + 156 + 18] = struct.pack("<I", 1 << 30) + struct.pack(">I", 1 << 30)
    assert start and size_field
    with pytest.raises(IsoError, match="larger than"):
        IsoImage(io.BytesIO(bytes(data))).listdir()


def test_version_suffixes_and_missing_ones():
    data = build_iso([file("PLAIN.TXT", b"p"), file("V2.TXT;2", b"v"), file("ODD;X.TXT;1", b"o")])
    image = IsoImage(io.BytesIO(bytes(data)))
    entries = {e.name: e for e in image.listdir()}
    assert entries["PLAIN.TXT"].version is None and entries["PLAIN.TXT"].identifier == "PLAIN.TXT"
    assert entries["V2.TXT"].version == 2
    assert image.read_file("plain.txt;1") == b"p"  # a requested version is ignored in lookups
    assert "ODD;X.TXT" in entries  # only a trailing ;digits is a version


def test_rock_ridge_entries_do_not_disturb_the_names():
    data = build_iso([file("GAME.ELF", make_elf()), directory("DIR", file("IN.TXT", b"i"))], rock_ridge_names=True)
    image = IsoImage(io.BytesIO(bytes(data)))
    assert [e.path for e in image.walk()] == ["GAME.ELF", "DIR", "DIR/IN.TXT"]
    assert image.read_file("DIR/IN.TXT") == b"i"


def test_ps2_disc_helper_builds_what_the_ps2_tests_need():
    image = IsoImage(io.BytesIO(bytes(ps2_disc())))
    assert sorted(e.name for e in image.listdir()) == ["SLUS_218.47", "SYSTEM.CNF"]
