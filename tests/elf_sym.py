#!/usr/bin/env python3
"""Prints the address of each symbol named on the command line, from a
32-bit little-endian ELF: usage elf_sym.py FILE.elf NAME... (hex, one per
line, in order; exit 1 if one is missing). The tests use it to read an
emulator's variables, as nm would, without devkitARM in the container."""

import struct
import sys


def symbols(path: str) -> dict[str, int]:
    with open(path, "rb") as f:
        data = f.read()
    (shoff,) = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", data, 0x2E)
    sections = [
        struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize)
        for i in range(shnum)
    ]
    found: dict[str, int] = {}
    for (
        _name,
        kind,
        _flags,
        _addr,
        offset,
        size,
        link,
        _info,
        _align,
        entsize,
    ) in sections:
        if kind != 2:  # SHT_SYMTAB
            continue
        str_off = sections[link][4]
        for i in range(size // entsize):
            name, value, *_ = struct.unpack_from("<IIIBBH", data, offset + i * entsize)
            end = data.index(b"\0", str_off + name)
            found.setdefault(data[str_off + name : end].decode(), value)
    return found


if __name__ == "__main__":
    table = symbols(sys.argv[1])
    for wanted in sys.argv[2:]:
        if wanted not in table:
            sys.exit(f"elf_sym: no symbol {wanted}")
        print(f"0x{table[wanted]:08X}")
