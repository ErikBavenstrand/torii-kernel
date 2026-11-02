#!/usr/bin/env python3
"""Writes a tiny Game Boy ROM for tests/gb_save.sh: usage gb_rom.py OUT.gb [8|cheat|poke|pokegbc]

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


def cheat_rom() -> bytes:
    """tests/emu_cheats.sh's game: MBC5, four 16 KB banks, no RAM.

    The program stores what the routine at $4000 returns at $C001 and loops
    (bank 1: LD A,$E0; RET; bank 2 has $77 there), and the test reads WRAM
    from the emulator's memory (Jagoomba's XGB_RAM, 0x03005890). A Game Genie
    code for 4001 with the compare $E0 makes the byte $1F, in bank 1 alone.
    """
    data = bytearray(0x10000)
    data[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])
    data[0x104:0x108] = bytes([0xCE, 0xED, 0x66, 0x66])
    data[0x134:0x13C] = b"CHEATTST"
    data[0x147] = 0x19  # MBC5
    data[0x148] = 0x01  # 64 KB
    data[0x14D] = (-sum(data[0x134:0x14D]) - (0x14D - 0x134)) & 0xFF
    code = bytes(
        [0xCD, 0x00, 0x40]  # CALL $4000
        + [0xEA, 0x01, 0xC0]  # LD ($C001),A
        + [0x18, 0xFE]  # JR $
    )
    data[0x150 : 0x150 + len(code)] = code
    data[0x4000:0x4003] = bytes([0x3E, 0xE0, 0xC9])
    data[0x8000:0x8003] = bytes([0x3E, 0x77, 0xC9])
    return bytes(data)


def poke_rom(gbc: bool = False) -> bytes:
    """tests/emu_cheats.sh's RAM-write game: 32 KB, no MBC.

    A GBC game (gbc) first maps WRAM bank 3 at D000 (SVBK) and watches $D100;
    the GB one watches $C100. Each time it sees $55 there it clears it and
    counts at $C102, up to $F0: a cheat that writes $55 every frame (a GameShark
    code) counts to the top within a few seconds, one that writes once counts 1.
    """
    watch = 0xD100 if gbc else 0xC100
    data = bytearray(0x8000)
    data[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])
    data[0x104:0x108] = bytes([0xCE, 0xED, 0x66, 0x66])
    data[0x134:0x13C] = b"POKETEST"
    data[0x143] = 0x80 if gbc else 0x00
    data[0x14D] = (-sum(data[0x134:0x14D]) - (0x14D - 0x134)) & 0xFF
    lo, hi = watch & 0xFF, watch >> 8
    code = bytes([0x3E, 0x03, 0xE0, 0x70]) if gbc else b""  # LD A,3; LDH ($70),A: SVBK
    code += bytes(
        [0xFA, lo, hi, 0xFE, 0x55, 0x20, 0xF9]  # loop: LD A,(watch); CP $55; JR NZ,loop
        + [0xAF, 0xEA, lo, hi]  # XOR A; LD (watch),A
        + [
            0xFA,
            0x02,
            0xC1,
            0xFE,
            0xF0,
            0x30,
            0x01,
            0x3C,
            0xEA,
            0x02,
            0xC1,
        ]  # count, saturating
        + [0x18, 0xE8]  # JR loop
    )
    data[0x150 : 0x150 + len(code)] = code
    return bytes(data)


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[2] in ("poke", "pokegbc"):
        with open(sys.argv[1], "wb") as f:
            f.write(poke_rom(sys.argv[2] == "pokegbc"))
        sys.exit(0)
    if len(sys.argv) > 2 and sys.argv[2] == "cheat":
        with open(sys.argv[1], "wb") as f:
            f.write(cheat_rom())
        sys.exit(0)
    with open(sys.argv[1], "wb") as f:
        f.write(rom(int(sys.argv[2]) if len(sys.argv) > 2 else 32))
