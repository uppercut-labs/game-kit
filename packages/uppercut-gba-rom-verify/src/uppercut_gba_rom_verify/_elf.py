"""devkitARM ELF checks: an ELF32 little-endian ARM EABI5 executable with a loadable entry segment."""

from __future__ import annotations

import struct
from dataclasses import dataclass

from ._errors import PathInput, ValidationError, read_artifact

ELF_HEADER_SIZE = 52
ELF_PROGRAM_HEADER_SIZE = 32
ELF_MAGIC = b"\x7fELF"
ELFCLASS32 = 1
ELFDATA2LSB = 1
EV_CURRENT = 1
ET_EXEC = 2
EM_ARM = 40
EF_ARM_EABIMASK = 0xFF000000
EF_ARM_EABI_VER5 = 0x05000000
PT_LOAD = 1
PF_X = 0x1


@dataclass(frozen=True)
class ElfInfo:
    size: int
    entry_point: int
    flags: int
    eabi_version: int
    load_segment_count: int


def validate_elf_bytes(elf: bytes) -> ElfInfo:
    """Validate a devkitARM ELF32 ARM EABI5 executable.

    Checks the identity bytes, type, machine, EABI flags, program header table and that
    the entry point lies in an executable PT_LOAD segment's file-backed bytes.
    """
    errors: list[str] = []
    elf_size = len(elf)

    if elf_size < ELF_HEADER_SIZE:
        raise ValidationError(
            f"ELF is too small for an ELF32 header: found {elf_size} bytes"
        )

    if elf[0:4] != ELF_MAGIC:
        errors.append("ELF magic is invalid")
    if elf[4] != ELFCLASS32:
        errors.append(f"ELF class must be ELF32, found class {elf[4]}")
    if elf[5] != ELFDATA2LSB:
        errors.append(f"ELF byte order must be little-endian, found encoding {elf[5]}")
    if elf[6] != EV_CURRENT:
        errors.append(f"ELF identification version must be 1, found {elf[6]}")

    can_parse_header = (
        elf[0:4] == ELF_MAGIC
        and elf[4] == ELFCLASS32
        and elf[5] == ELFDATA2LSB
    )
    if not can_parse_header:
        raise ValidationError(errors)

    (
        elf_type,
        machine,
        version,
        entry_point,
        program_header_offset,
        _section_header_offset,
        flags,
        header_size,
        program_header_entry_size,
        program_header_count,
        _section_header_entry_size,
        _section_header_count,
        _section_name_index,
    ) = struct.unpack_from("<HHIIIIIHHHHHH", elf, 16)

    if elf_type != ET_EXEC:
        errors.append(f"ELF type must be executable, found {elf_type}")
    if machine != EM_ARM:
        errors.append(f"ELF machine must be ARM ({EM_ARM}), found {machine}")
    if version != EV_CURRENT:
        errors.append(f"ELF header version must be 1, found {version}")
    if header_size != ELF_HEADER_SIZE:
        errors.append(
            f"ELF header size must be {ELF_HEADER_SIZE} bytes, found {header_size}"
        )

    eabi_bits = flags & EF_ARM_EABIMASK
    if eabi_bits != EF_ARM_EABI_VER5:
        errors.append(
            "ELF ARM ABI must be EABI5: "
            f"expected flags 0x{EF_ARM_EABI_VER5:08X}, found 0x{eabi_bits:08X}"
        )

    if entry_point == 0:
        errors.append("ELF entry point must be nonzero")
    if program_header_count == 0:
        errors.append("ELF must contain at least one program header")
    if program_header_entry_size != ELF_PROGRAM_HEADER_SIZE:
        errors.append(
            "ELF program header size must be "
            f"{ELF_PROGRAM_HEADER_SIZE} bytes, found {program_header_entry_size}"
        )
    if program_header_offset < ELF_HEADER_SIZE:
        errors.append(
            "ELF program header table must begin after the ELF header, "
            f"found offset {program_header_offset}"
        )

    program_table_size = program_header_entry_size * program_header_count
    program_table_end = program_header_offset + program_table_size
    can_parse_program_headers = (
        program_header_count > 0
        and program_header_entry_size == ELF_PROGRAM_HEADER_SIZE
        and program_header_offset >= ELF_HEADER_SIZE
        and program_table_end <= elf_size
    )
    if program_header_count > 0 and program_table_end > elf_size:
        errors.append(
            "ELF program header table extends past end of file: "
            f"ends at {program_table_end}, file has {elf_size} bytes"
        )

    load_segment_count = 0
    executable_entry_segment = False
    if can_parse_program_headers:
        for index in range(program_header_count):
            offset = program_header_offset + index * program_header_entry_size
            (
                segment_type,
                file_offset,
                virtual_address,
                _physical_address,
                file_size,
                memory_size,
                segment_flags,
                _alignment,
            ) = struct.unpack_from("<IIIIIIII", elf, offset)
            if segment_type != PT_LOAD:
                continue

            load_segment_count += 1
            if file_size > memory_size:
                errors.append(
                    f"ELF PT_LOAD segment {index} has file size {file_size} "
                    f"larger than memory size {memory_size}"
                )
            if file_offset > elf_size or file_size > elf_size - file_offset:
                errors.append(
                    f"ELF PT_LOAD segment {index} extends past end of file"
                )

            segment_end = virtual_address + memory_size
            if segment_end > 0x100000000:
                errors.append(
                    f"ELF PT_LOAD segment {index} address range overflows 32 bits"
                )
            file_backed_end = virtual_address + file_size
            entry_address = entry_point & ~1
            if (
                segment_flags & PF_X
                and file_size > 0
                and file_backed_end <= 0x100000000
                and virtual_address <= entry_address < file_backed_end
            ):
                executable_entry_segment = True

        if load_segment_count == 0:
            errors.append("ELF must contain at least one PT_LOAD segment")
        elif not executable_entry_segment:
            errors.append(
                "ELF entry point must be covered by an executable PT_LOAD segment"
            )

    if errors:
        raise ValidationError(errors)

    return ElfInfo(
        size=elf_size,
        entry_point=entry_point,
        flags=flags,
        eabi_version=5,
        load_segment_count=load_segment_count,
    )


def validate_elf_file(path: PathInput) -> ElfInfo:
    """Read and validate an ELF file. See ``validate_elf_bytes``."""
    return validate_elf_bytes(read_artifact(path, "ELF"))
