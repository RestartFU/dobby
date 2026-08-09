#!/usr/bin/env python3
"""Find AArch64 ADRP+ADD references to a string in a stripped ELF64 image."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from elf_rtti import Elf64


def sign_extend(value: int, bits: int) -> int:
    sign = 1 << (bits - 1)
    return (value & (sign - 1)) - (value & sign)


def adrp_target(instruction: int, address: int) -> tuple[int, int] | None:
    if instruction & 0x9F000000 != 0x90000000:
        return None
    immediate = ((instruction >> 5) & 0x7FFFF) << 2
    immediate |= (instruction >> 29) & 0x3
    page = (address & ~0xFFF) + (sign_extend(immediate, 21) << 12)
    return page, instruction & 0x1F


def add_target(instruction: int, page: int, register: int) -> int | None:
    if instruction & 0xFF000000 != 0x91000000:
        return None
    if (instruction >> 5) & 0x1F != register:
        return None
    immediate = (instruction >> 10) & 0xFFF
    if instruction & (1 << 22):
        immediate <<= 12
    return page + immediate


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("library", type=Path)
    parser.add_argument("text", help="exact UTF-8 string to locate")
    parser.add_argument("--lookahead", type=int, default=12)
    args = parser.parse_args()

    elf = Elf64(args.library.read_bytes())
    needle = args.text.encode()
    targets: list[int] = []
    search_at = 0
    while True:
        found = elf.data.find(needle, search_at)
        if found < 0:
            break
        address = elf.file_offset_to_address(found)
        if address is not None:
            targets.append(address)
            print(f"string=0x{address:x}")
        search_at = found + 1

    text = next((section for section in elf.sections if section.name == ".text"), None)
    if text is None:
        raise SystemExit(".text section not found")
    raw = elf.data[text.offset : text.offset + text.size]
    for offset in range(0, len(raw) - 4, 4):
        instruction = struct.unpack_from("<I", raw, offset)[0]
        instruction_address = text.address + offset
        adrp = adrp_target(instruction, instruction_address)
        if adrp is None:
            continue
        page, register = adrp
        for step in range(1, args.lookahead + 1):
            next_offset = offset + step * 4
            if next_offset + 4 > len(raw):
                break
            candidate = struct.unpack_from("<I", raw, next_offset)[0]
            resolved = add_target(candidate, page, register)
            if resolved in targets:
                print(
                    f"xref=0x{text.address + next_offset:x} "
                    f"adrp=0x{instruction_address:x} target=0x{resolved:x}"
                )


if __name__ == "__main__":
    main()
