#!/usr/bin/env python3
"""Find direct AArch64 BL call sites to an image-relative address."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from elf_rtti import Elf64


def sign_extend(value: int, bits: int) -> int:
    sign = 1 << (bits - 1)
    return (value & (sign - 1)) - (value & sign)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("library", type=Path)
    parser.add_argument("target", type=lambda value: int(value, 0))
    args = parser.parse_args()

    elf = Elf64(args.library.read_bytes())
    text = next((section for section in elf.sections if section.name == ".text"), None)
    if text is None:
        raise SystemExit(".text section not found")
    raw = elf.data[text.offset : text.offset + text.size]
    for offset in range(0, len(raw) - 4, 4):
        instruction = struct.unpack_from("<I", raw, offset)[0]
        if instruction & 0xFC000000 != 0x94000000:
            continue
        address = text.address + offset
        immediate = sign_extend(instruction & 0x03FFFFFF, 26) << 2
        if address + immediate == args.target:
            print(f"call=0x{address:x} target=0x{args.target:x}")


if __name__ == "__main__":
    main()
