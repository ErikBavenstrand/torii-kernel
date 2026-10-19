#!/usr/bin/env python3
"""Writes a tiny NES ROM for tests/nes.sh: usage nes_rom.py OUT.nes

Written for Torii's tests (no third-party code), so it can live in the
repo. NROM, 16 KB of PRG and 8 KB of blank CHR. The program waits for the
PPU to warm up, sets the backdrop colour to red ($16), turns the background
on and loops: the screen PocketNES draws is all red once it runs the game.
"""

import struct
import sys

BASE = 0xC000  # the 16 KB PRG bank, mirrored at $8000


def program() -> bytes:
    code = bytearray()

    def emit(*b: int) -> None:
        code.extend(b)

    def sta(addr: int) -> None:
        emit(0x8D, addr & 0xFF, addr >> 8)

    def lda(value: int) -> None:
        emit(0xA9, value)

    emit(0x78, 0xD8, 0xA2, 0xFF, 0x9A)  # SEI, CLD, LDX #$FF, TXS
    lda(0x00)
    sta(0x2000)  # no NMI
    sta(0x2001)  # rendering off
    for _ in range(2):  # two vblanks: the PPU is ready
        emit(0x2C, 0x02, 0x20, 0x10, 0xFB)  # BIT $2002, BPL back to BIT
    lda(0x3F)
    sta(0x2006)
    lda(0x00)
    sta(0x2006)  # VRAM address $3F00, the backdrop colour
    lda(0x16)
    sta(0x2007)  # red
    lda(0x00)
    sta(0x2006)
    sta(0x2006)
    sta(0x2005)
    sta(0x2005)  # address and scroll back to 0
    lda(0x0A)
    sta(0x2001)  # background on, left column too
    loop = BASE + len(code)
    emit(0x4C, loop & 0xFF, loop >> 8)  # JMP loop
    rti = BASE + len(code)
    emit(0x40)  # RTI, for NMI and IRQ

    prg = bytearray(0x4000)
    prg[: len(code)] = code
    prg[0x3FFA:] = struct.pack("<HHH", rti, BASE, rti)  # NMI, RESET, IRQ
    return bytes(prg)


def main() -> None:
    header = b"NES\x1a" + bytes([1, 1, 0, 0]) + bytes(8)  # 1 PRG, 1 CHR, NROM
    with open(sys.argv[1], "wb") as f:
        f.write(header + program() + bytes(0x2000))


if __name__ == "__main__":
    main()
