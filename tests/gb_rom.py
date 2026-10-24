#!/usr/bin/env python3
"""Writes a tiny Game Boy ROM for tests/gb_save.sh: usage gb_rom.py OUT.gb [8]

Written for Torii's tests (no third-party code), so it can live in the
repo. MBC5 with 32 KB of battery-backed RAM (cart type $1B, RAM size $03),
the size Jagoomba keeps in EWRAM, or with 8, 8 KB (RAM size $02), which it
writes through to the cart. At start the program writes the marker $5A to
the start of each RAM bank, then loops. The test reads
the cart's SRAM for it (tests/jagoomba_sram.py).
"""

import sys


def program(banks: int) -> bytes:
    code = bytearray()

    def emit(*b: int) -> None:
        code.extend(b)

    def ld_a(value: int) -> None:
        emit(0x3E, value)

    def ld_mem_a(addr: int) -> None:
        emit(0xEA, addr & 0xFF, addr >> 8)

    ld_a(0x0A)
    ld_mem_a(0x0000)  # RAM on
    for bank in range(banks):
        ld_a(bank)
        ld_mem_a(0x4000)  # RAM bank
        ld_a(0x5A)
        ld_mem_a(0xA000)
    emit(0x18, 0xFE)  # JR $ (loop)
    return bytes(code)


def rom(ram_kb: int = 32) -> bytes:
    data = bytearray(0x8000)
    data[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])  # NOP, JP $0150
    data[0x104:0x108] = bytes(
        [0xCE, 0xED, 0x66, 0x66]
    )  # what Jagoomba checks of the header (gbx_id)
    data[0x134:0x13C] = b"SAVETEST"
    data[0x147] = 0x1B  # MBC5+RAM+BATTERY
    data[0x148] = 0x00  # 32 KB ROM
    data[0x149] = 0x03 if ram_kb == 32 else 0x02  # 32 or 8 KB RAM
    data[0x14D] = (-sum(data[0x134:0x14D]) - (0x14D - 0x134)) & 0xFF
    code = program(ram_kb // 8)
    data[0x150 : 0x150 + len(code)] = code
    return bytes(data)


if __name__ == "__main__":
    with open(sys.argv[1], "wb") as f:
        f.write(rom(int(sys.argv[2]) if len(sys.argv) > 2 else 32))
