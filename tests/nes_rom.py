#!/usr/bin/env python3
"""Writes a tiny NES ROM for tests/nes.sh: usage nes_rom.py OUT.nes [save] [pal] [cheat] [poke]

Written for Torii's tests (no third-party code), so it can live in the
repo. NROM, 16 KB of PRG and 8 KB of blank CHR. The program waits for the
PPU to warm up, sets the backdrop colour to red ($16), turns the background
on and loops: the screen PocketNES draws is all red once it runs the game.
With save (tests/nes_save.sh) it has battery RAM: before the loop it writes
$5A to $6000 if $6000 isn't $5A yet, else $A5 to $6001, so a second run
shows that the first run's save came back. With pal (tests/nes_region.sh)
its header is NES 2.0's, saying the game is for PAL consoles (byte 12).
With cheat (tests/emu_cheats.sh) the backdrop is green ($2A) until a Game
Genie code patches the byte at cheat_at() to $16. With poke the program
loops on $0300 (poke_loop): a RAM write of $55 there every frame makes it red.
"""

import struct
import sys

BASE = 0xC000  # the 16 KB PRG bank, mirrored at $8000


def program(save: bool = False, cheat: bool = False, poke: bool = False) -> bytes:
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
    lda(0x2A if cheat else 0x16)
    sta(0x2007)  # red (green with cheat: tests/emu_cheats.sh's Game Genie makes it red)
    lda(0x00)
    sta(0x2006)
    sta(0x2006)
    sta(0x2005)
    sta(0x2005)  # address and scroll back to 0
    lda(0x0A)
    sta(0x2001)  # background on, left column too
    if save:
        emit(0xAD, 0x00, 0x60)  # LDA $6000
        emit(0xC9, 0x5A)  # CMP #$5A
        emit(0xF0, 0x08)  # BEQ found (+8)
        lda(0x5A)
        sta(0x6000)  # the first run's mark
        emit(0x4C)  # JMP done
        done = len(code)
        emit(0x00, 0x00)
        lda(0xA5)  # found:
        sta(0x6001)  # the save came back
        here = BASE + len(code)
        code[done : done + 2] = struct.pack("<H", here)
    if poke:
        poke_loop(code)
    loop = BASE + len(code)
    emit(0x4C, loop & 0xFF, loop >> 8)  # JMP loop
    rti = BASE + len(code)
    emit(0x40)  # RTI, for NMI and IRQ

    prg = bytearray(0x4000)
    prg[: len(code)] = code
    prg[0x3FFA:] = struct.pack("<HHH", rti, BASE, rti)  # NMI, RESET, IRQ
    return bytes(prg)


def poke_loop(code: bytearray) -> None:
    """Each frame: copy $0300 to $0301, clear $0300, wait for vblank, then set
    the backdrop red if the copy is $55, else green. A RAM write that lasts
    one frame shows once; one made every frame (a cheat's) keeps it red."""
    top = BASE + len(code)
    code += bytes([0xAD, 0x00, 0x03, 0x8D, 0x01, 0x03])  # LDA $0300; STA $0301
    code += bytes([0xA9, 0x00, 0x8D, 0x00, 0x03])  # LDA #0; STA $0300
    code += bytes([0x2C, 0x02, 0x20, 0x10, 0xFB])  # wait for vblank
    code += bytes(
        [0xA9, 0x3F, 0x8D, 0x06, 0x20, 0xA9, 0x00, 0x8D, 0x06, 0x20]
    )  # PPUADDR $3F00
    code += bytes(
        [0xAD, 0x01, 0x03, 0xC9, 0x55, 0xF0, 0x05]
    )  # LDA $0301; CMP #$55; BEQ red
    code += bytes([0xA9, 0x2A, 0x4C])  # green
    store = BASE + len(code) + 2 + 2  # past this JMP and the red LDA
    code += bytes([store & 0xFF, store >> 8])
    code += bytes([0xA9, 0x16])  # red:
    code += bytes([0x8D, 0x07, 0x20])  # STA $2007
    code += bytes(
        [
            0xA9,
            0x00,
            0x8D,
            0x06,
            0x20,
            0x8D,
            0x06,
            0x20,
            0x8D,
            0x05,
            0x20,
            0x8D,
            0x05,
            0x20,
        ]
    )
    code += bytes([0x4C, top & 0xFF, top >> 8])  # JMP top


def cheat_at() -> int:
    """The CPU address of the colour's byte in the cheat version's program"""
    return BASE + program(cheat=True).index(bytes([0xA9, 0x2A, 0x8D, 0x07, 0x20])) + 1


def main() -> None:
    save, pal = "save" in sys.argv[2:], "pal" in sys.argv[2:]
    cheat = "cheat" in sys.argv[2:]
    poke = "poke" in sys.argv[2:]
    # 1 PRG, 1 CHR, NROM; battery RAM with save; with pal, NES 2.0 (flags 7
    # bits 2-3 = 2) and the PAL timing (byte 12 = 1)
    header = b"NES\x1a" + bytes([1, 1, 0x02 if save else 0, 0x08 if pal else 0])
    header += bytes(4) + bytes([1 if pal else 0]) + bytes(3)
    with open(sys.argv[1], "wb") as f:
        f.write(header + program(save, cheat, poke) + bytes(0x2000))


if __name__ == "__main__":
    main()
