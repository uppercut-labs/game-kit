import hashlib

import pytest

from uppercut_iso_normalize import IsoNormalizeError, normalize_disc, ps2_system_cnf
from uppercut_iso_normalize._normalize import LONG_DATE, PRIMARY, SECTOR_SIZE, SHORT_DATE
from iso_images import (
    DATE_B,
    LONG_B,
    ROOT_SECTOR,
    SECTOR,
    build_disc,
    build_iso,
    dir_record,
    file_bytes,
    make_elf,
    read_root,
    rr_for,
    rr_px,
    rr_tf,
    system_cnf,
)

ELF = make_elf()


def normalized(**options):
    image = build_disc(ELF, **options)
    normalize_disc(image, ELF, elf_name="GAME.ELF")
    return image


def test_every_date_in_the_directory_and_the_volume_descriptor_is_fixed():
    image = normalized()
    for record in read_root(image):
        assert record["date"] == SHORT_DATE
    assert image[PRIMARY + 174 : PRIMARY + 181] == SHORT_DATE
    assert image[PRIMARY + 813 : PRIMARY + 830] == LONG_DATE
    assert image[PRIMARY + 830 : PRIMARY + 847] == LONG_DATE


def test_rock_ridge_timestamps_are_fixed_in_both_the_short_and_the_long_form():
    for long_form in (False, True):
        image = normalized(long_form=long_form)
        for record in read_root(image)[2:]:
            use = record["system_use"]
            at = use.index(b"TF")
            flags = use[at + 4]
            stamp = LONG_DATE if flags & 0x80 else SHORT_DATE
            assert use[at + 5 : at + 5 + 3 * len(stamp)] == stamp * 3
    # The "." record carries its own timestamps, written in the short form.
    assert read_root(normalized())[0]["system_use"].count(SHORT_DATE) == 3


def test_files_get_the_version_suffix_and_the_dot_entries_do_not():
    records = read_root(normalized())
    assert [record["name"] for record in records] == [b"\x00", b"\x01", b"SYSTEM.CNF;1", b"GAME.ELF;1"]


def test_record_lengths_stay_even_and_match_the_new_names():
    image = normalized()
    for record in read_root(image):
        assert record["raw"][0] == len(record["raw"])
        assert len(record["raw"]) % 2 == 0
        assert len(record["raw"]) == 33 + len(record["name"]) + (len(record["name"]) % 2 == 0) + len(record["system_use"])


def test_file_contents_and_extents_are_untouched():
    before = build_disc(ELF)
    after = normalized()
    for old, new in zip(read_root(before)[2:], read_root(after)[2:]):
        assert (old["extent"], old["size"]) == (new["extent"], new["size"])
        assert file_bytes(before, old) == file_bytes(after, new)
    assert before[SECTOR * 20 :] == after[SECTOR * 20 :]


def test_only_the_directory_sector_and_the_volume_dates_change():
    before = build_disc(ELF)
    after = normalized()
    changed = {offset // SECTOR for offset in range(len(before)) if before[offset] != after[offset]}
    assert changed <= {16, ROOT_SECTOR}
    assert changed == {16, ROOT_SECTOR}


def test_images_that_differ_only_in_their_dates_come_out_byte_identical():
    first = build_disc(ELF)
    second = build_disc(ELF, dates=(DATE_B, LONG_B), long_form=False)
    assert first != second
    normalize_disc(first, ELF, elf_name="GAME.ELF")
    normalize_disc(second, ELF, elf_name="GAME.ELF")
    assert first == second


# SHA-256 of the whole normalized image, written by the standalone script this module was ported from, with its file
# name changed to GAME.ELF. They pin every byte of the output, including the layout of the rewritten directory.
GOLDEN = {
    "plain": ({}, "a1905fc35b960d57b4df737489cec4bc0b10d9232359fbc7b1698e92153bf817"),
    "long-form timestamps": ({"long_form": True}, "886624b7f0a703d2912caad6174d32be276d73c8197d1cb4d006c395153d372a"),
    "no Rock Ridge": ({"rock_ridge": False}, "4105be9811e9f09c855a8537353ec6eda583d293f68019d52a082388ae2830a9"),
}


@pytest.mark.parametrize("label", sorted(GOLDEN))
def test_the_normalized_image_matches_the_original_script_byte_for_byte(label):
    options, digest = GOLDEN[label]
    assert hashlib.sha256(bytes(normalized(**options))).hexdigest() == digest


def test_a_disc_without_rock_ridge_entries_is_normalized_too():
    image = normalized(rock_ridge=False)
    assert [record["date"] for record in read_root(image)] == [SHORT_DATE] * 4


def test_a_second_run_is_refused_and_changes_nothing():
    image = normalized()
    again = bytearray(image)
    with pytest.raises(IsoNormalizeError, match="disc must contain only"):
        normalize_disc(again, ELF, elf_name="GAME.ELF")
    assert again == image


def test_another_elf_name_and_system_cnf_lines():
    elf = make_elf(16, fill=0x11)
    image = build_iso(
        [(b"SYSTEM.CNF", system_cnf("MAIN.ELF", ver="2.00", vmode="PAL")), (b"MAIN.ELF", elf)],
    )
    normalize_disc(image, elf, elf_name="MAIN.ELF", system_cnf=ps2_system_cnf("MAIN.ELF", ver="2.00", vmode="PAL"))
    assert [record["name"] for record in read_root(image)][2:] == [b"SYSTEM.CNF;1", b"MAIN.ELF;1"]


def test_ps2_system_cnf_lines():
    assert ps2_system_cnf("GAME.ELF") == [b"BOOT2 = cdrom0:\\GAME.ELF;1", b"VER = 1.00", b"VMODE = NTSC"]
    assert ps2_system_cnf("A.ELF", ver="1.10", vmode="PAL")[1:] == [b"VER = 1.10", b"VMODE = PAL"]


def test_a_line_feed_only_system_cnf_is_accepted_because_lines_are_compared():
    image = build_iso([(b"SYSTEM.CNF", system_cnf(newline="\n")), (b"GAME.ELF", ELF)])
    normalize_disc(image, ELF, elf_name="GAME.ELF")


@pytest.mark.parametrize("name", ["game.elf", "GAME", "TOOLONGNAME.ELF", "GAME.ELFS", "", "GA ME.ELF", "GAME.ELF;1"])
def test_an_elf_name_that_is_not_an_iso_9660_level_1_name_is_refused(name):
    with pytest.raises(IsoNormalizeError, match="level 1"):
        normalize_disc(build_disc(ELF), ELF, elf_name=name)


# Everything below leaves the image exactly as it was.


def refuses(image, message, *, elf=ELF, **options):
    before = bytes(image)
    with pytest.raises(IsoNormalizeError, match=message):
        normalize_disc(image, elf, elf_name=options.pop("elf_name", "GAME.ELF"), **options)
    assert bytes(image) == before


def test_refuses_an_image_that_is_not_a_whole_number_of_sectors():
    refuses(build_disc(ELF) + b"\x00", "primary ISO9660 volume")


def test_refuses_a_missing_primary_descriptor_or_terminator():
    image = build_disc(ELF)
    image[PRIMARY + 1] = 0
    refuses(image, "primary ISO9660 volume")
    image = build_disc(ELF)
    image[PRIMARY + SECTOR_SIZE] = 0
    refuses(image, "primary ISO9660 volume")
    refuses(bytearray(), "primary ISO9660 volume")


def test_refuses_a_root_directory_that_is_not_one_sector():
    image = build_disc(ELF)
    image[PRIMARY + 156 + 10 : PRIMARY + 156 + 14] = (2 * SECTOR).to_bytes(4, "little")
    refuses(image, "single-sector")
    image = build_disc(ELF)
    image[PRIMARY + 156 + 2 : PRIMARY + 156 + 6] = (len(image) // SECTOR).to_bytes(4, "little")
    refuses(image, "single-sector")
    image = build_disc(ELF)
    image[PRIMARY + 156 + 2 : PRIMARY + 156 + 6] = (17).to_bytes(4, "little")
    refuses(image, "single-sector")


def with_filler(image, filler_size):
    """The disc with extra "." records after the first two, so the file records start ``filler_size`` bytes later."""
    records = read_root(image)
    filler = []
    remaining = filler_size
    while remaining > 0:
        take = min(remaining, 254)
        if remaining - take and remaining - take < 38:
            take = remaining - 38
        assert take >= 38 and take % 2 == 0, "filler must be an even size of at least 38 bytes"
        # "ST" ends the system use area, so the zeros after it are never read as entries.
        filler.append(dir_record(b"\x00", ROOT_SECTOR, SECTOR, flags=2, system_use=b"ST\x04\x01" + b"\x00" * (take - 38)))
        remaining -= len(filler[-1])
    blob = b"".join([records[0]["raw"], records[1]["raw"], *filler, records[2]["raw"], records[3]["raw"]])
    assert len(blob) <= SECTOR
    image[ROOT_SECTOR * SECTOR : ROOT_SECTOR * SECTOR + SECTOR] = blob.ljust(SECTOR, b"\x00")
    return blob


def test_refuses_a_record_that_overruns_the_directory_sector():
    image = build_disc(ELF, rock_ridge=False)
    blob = with_filler(image, 1780)
    last = len(blob) - len(read_root(image)[-1]["raw"])
    image[ROOT_SECTOR * SECTOR + last] = 255
    assert last + 255 > SECTOR
    refuses(image, "overruns")


def test_refuses_a_record_whose_name_runs_past_its_end():
    image = build_disc(ELF)
    first_file = ROOT_SECTOR * SECTOR + sum(len(record["raw"]) for record in read_root(image)[:2])
    image[first_file + 32] = 250
    refuses(image, "invalid ISO9660 directory record")


def test_refuses_a_record_that_is_too_short():
    image = build_disc(ELF)
    image[ROOT_SECTOR * SECTOR] = 20
    refuses(image, "invalid ISO9660 directory record")


def broken_system_use(system_use):
    """The synthetic disc with ``system_use`` on the SYSTEM.CNF record."""
    image = build_iso([(b"SYSTEM.CNF", system_cnf()), (b"GAME.ELF", ELF)], rock_ridge=False)
    records = read_root(image)
    directory = b"".join(
        [
            records[0]["raw"],
            records[1]["raw"],
            dir_record(records[2]["name"], records[2]["extent"], records[2]["size"], system_use=system_use),
            records[3]["raw"],
        ]
    )
    image[ROOT_SECTOR * SECTOR : ROOT_SECTOR * SECTOR + SECTOR] = directory.ljust(SECTOR, b"\x00")
    return image


@pytest.mark.parametrize(
    "system_use, message",
    [
        (b"XX" + bytes([2, 1]), "invalid Rock Ridge entry"),
        (b"XX" + bytes([200, 1]) + b"\x00" * 4, "invalid Rock Ridge entry"),
        (b"CE" + bytes([28, 1]) + bytes(24), "continuation"),
        (b"TF" + bytes([4, 1]), "missing Rock Ridge timestamp flags"),
        (rr_tf(0x07, [bytes(7)] * 2), "invalid Rock Ridge timestamps"),
        (rr_tf(0x07, [bytes(7)] * 4), "invalid Rock Ridge timestamps"),
    ],
)
def test_refuses_malformed_rock_ridge_entries(system_use, message):
    refuses(broken_system_use(system_use), message)


def test_accepts_the_same_disc_with_well_formed_rock_ridge_entries():
    image = broken_system_use(rr_for(b"SYSTEM.CNF"))
    normalize_disc(image, ELF, elf_name="GAME.ELF")


def test_refuses_extra_missing_and_repeated_files():
    refuses(build_iso([(b"SYSTEM.CNF", system_cnf()), (b"GAME.ELF", ELF), (b"README.TXT", b"x")]), "disc must contain only")
    refuses(build_iso([(b"SYSTEM.CNF", system_cnf())]), "disc must contain only")
    refuses(build_iso([(b"SYSTEM.CNF", system_cnf()), (b"GAME.ELF", ELF), (b"GAME.ELF", ELF)]), "invalid PS2 disc file extent")
    refuses(build_iso([(b"SYSTEM.CNF", system_cnf()), (b"OTHER.ELF", ELF)]), "disc must contain only")


def test_refuses_a_hidden_or_directory_file_entry_and_an_extent_outside_the_image():
    image = build_disc(ELF)
    first_file = ROOT_SECTOR * SECTOR + sum(len(record["raw"]) for record in read_root(image)[:2])
    flagged = bytearray(image)
    flagged[first_file + 25] = 0x02
    refuses(flagged, "invalid PS2 disc file extent")
    hidden = bytearray(image)
    hidden[first_file + 25] = 0x80
    refuses(hidden, "invalid PS2 disc file extent")
    outside = bytearray(image)
    outside[first_file + 10 : first_file + 14] = (10**6).to_bytes(4, "little")
    refuses(outside, "invalid PS2 disc file extent")
    inside_directory = bytearray(image)
    inside_directory[first_file + 2 : first_file + 6] = (ROOT_SECTOR).to_bytes(4, "little")
    refuses(inside_directory, "invalid PS2 disc file extent")


def test_refuses_a_system_cnf_that_points_elsewhere():
    for text in (
        system_cnf("OTHER.ELF"),
        system_cnf(ver="1.01"),
        system_cnf(vmode="PAL"),
        b"BOOT2 = cdrom0:\\GAME.ELF;1\r\n",
        system_cnf() + b"EXTRA = 1\r\n",
    ):
        refuses(build_iso([(b"SYSTEM.CNF", text), (b"GAME.ELF", ELF)]), "does not point to GAME.ELF;1")


def test_refuses_an_elf_that_is_not_a_32_bit_little_endian_mips_executable():
    cases = {
        "too short": ELF[:51],
        "bad magic": b"\x00" + ELF[1:],
        "64-bit": ELF[:4] + b"\x02" + ELF[5:],
        "big-endian": ELF[:5] + b"\x02" + ELF[6:],
        "not an executable": ELF[:16] + b"\x01\x00" + ELF[18:],
        "not MIPS": ELF[:18] + b"\x28\x00" + ELF[20:],
    }
    for label, elf in cases.items():
        image = build_iso([(b"SYSTEM.CNF", system_cnf()), (b"GAME.ELF", elf)])
        refuses(image, "MIPS executable ELF", elf=elf)


def test_refuses_a_disc_whose_elf_is_not_the_input_elf():
    refuses(build_disc(ELF), "differs from the input ELF", elf=make_elf(64, fill=0xCD))
    refuses(build_disc(ELF), "differs from the input ELF", elf=make_elf(65))


def test_refuses_a_directory_that_no_longer_fits_its_sector_once_the_suffixes_are_added():
    image = build_disc(ELF, rock_ridge=False)
    plain = with_filler(image, 0)
    # Grow the filler until the two records for the files end two bytes short of the sector: the ";1" suffixes add four.
    size = SECTOR - len(plain) - 2
    blob = with_filler(image, size)
    assert SECTOR - 4 < len(blob) <= SECTOR - 2
    refuses(image, "exceeds one sector")


def test_a_directory_that_still_fits_after_the_suffixes_is_accepted():
    image = build_disc(ELF, rock_ridge=False)
    plain = with_filler(image, 0)
    blob = with_filler(image, SECTOR - len(plain) - 4)
    assert len(blob) == SECTOR - 4
    normalize_disc(image, ELF, elf_name="GAME.ELF")
