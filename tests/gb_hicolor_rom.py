#!/usr/bin/env python3
"""Writes a tiny hi-colour Game Boy Color ROM for tests/gb_hicolor.sh: usage gb_hicolor_rom.py OUT.gbc

Written for Torii's tests (no third-party code), so it can live in the
repo. The program switches to double speed, fills the screen with one tile
whose eight pixels run colours 0,0,1,1,2,2,3,3, gives tile column X the
BG palette X mod 8, and then, from a STAT interrupt in each line's HBlank,
rewrites BG palette LY mod 8 with the four colours of row LY of a table
(TABLE, 8 bytes a line): every line on the screen shows colours of its
own, hundreds a frame, as the hi-colour games do. Each palette is
rewritten every 8 lines, so the 576 colours a frame are all mid-frame
writes (jagoombacolor#20).
"""

import sys

TABLE = 0x1000
HANDLER = 0x0200


def colour(line: int, k: int) -> int:
    """Row LINE's colour K, as BGR555: a different one on every line."""
    r = (line * 31 // 143) & 31
    g = (k * 9 + line // 3) & 31
    b = (31 - r + 7 * k) & 31
    return r | g << 5 | b << 10


def main_program() -> bytes:
    c = bytearray()
    c += bytes([0xF3])  # DI
    c += bytes([0x31, 0xFE, 0xFF])  # LD SP,$FFFE
    c += bytes([0x3E, 0x30, 0xE0, 0x00])  # LD A,$30; LDH (P1),A
    c += bytes(
        [0x3E, 0x01, 0xE0, 0x4D]
    )  # LD A,1; LDH (KEY1),A: prepare the speed switch
    c += bytes([0x10, 0x00])  # STOP: double speed
    c += bytes([0xF0, 0x44, 0xFE, 0x90, 0x20, 0xFA])  # wait for LY 144
    c += bytes([0xAF, 0xE0, 0x40])  # XOR A; LDH (LCDC),A: LCD off
    # tile 0: rows of 00112233 (low plane $33, high plane $0F)
    c += bytes([0x21, 0x00, 0x80, 0x06, 0x08])  # LD HL,$8000; LD B,8
    c += bytes([0x3E, 0x33, 0x22, 0x3E, 0x0F, 0x22, 0x05, 0x20, 0xF7])
    # map at $9800: tile 0 everywhere
    c += bytes([0x21, 0x00, 0x98, 0x11, 0x00, 0x04])  # LD HL,$9800; LD DE,$400
    c += bytes([0xAF, 0x22, 0x1B, 0x7A, 0xB3, 0x20, 0xF9])
    # attributes (VRAM bank 1): palette = tile column mod 8
    c += bytes([0x3E, 0x01, 0xE0, 0x4F])  # LDH (VBK),1
    c += bytes([0x21, 0x00, 0x98, 0x11, 0x00, 0x04])
    c += bytes([0x7D, 0xE6, 0x07, 0x22, 0x1B, 0x7A, 0xB3, 0x20, 0xF6])
    c += bytes([0xAF, 0xE0, 0x4F])  # LDH (VBK),0
    # the eight palettes from the table's first rows
    c += bytes([0x3E, 0x80, 0xE0, 0x68])  # LDH (BCPS),$80: index 0, auto-increment
    c += bytes([0x21, TABLE & 0xFF, TABLE >> 8, 0x06, 0x40])  # LD HL,TABLE; LD B,64
    c += bytes([0x2A, 0xE0, 0x69, 0x05, 0x20, 0xFA])
    c += bytes([0x3E, 0x08, 0xE0, 0x41])  # LDH (STAT),$08: HBlank interrupt
    c += bytes([0x3E, 0x02, 0xE0, 0xFF])  # LDH (IE),2: STAT
    c += bytes([0xAF, 0xE0, 0x0F])  # LDH (IF),0
    c += bytes([0x3E, 0x91, 0xE0, 0x40])  # LDH (LCDC),$91: on, BG, tiles at $8000
    c += bytes([0xFB])  # EI
    c += bytes([0x76, 0x18, 0xFD])  # HALT; JR back
    return bytes(c)


def handler() -> bytes:
    c = bytearray()
    c += bytes([0xF5, 0xE5, 0xC5])  # PUSH AF, HL, BC
    c += bytes([0xF0, 0x44, 0x6F])  # LDH A,(LY); LD L,A
    c += bytes(
        [0xE6, 0x07, 0x87, 0x87, 0x87, 0xF6, 0x80, 0xE0, 0x68]
    )  # BCPS = $80 | (LY & 7) * 8
    c += bytes([0x26, 0x00, 0x29, 0x29, 0x29])  # HL = LY * 8
    c += bytes([0x01, TABLE & 0xFF, TABLE >> 8, 0x09])  # HL += TABLE
    c += bytes([0x0E, 0x69])  # LD C,BCPD
    c += bytes([0x2A, 0xE2] * 8)  # 8x LD A,(HL+); LD (C),A
    c += bytes([0xC1, 0xE1, 0xF1, 0xD9])  # POP BC, HL, AF; RETI
    return bytes(c)


def rom() -> bytes:
    data = bytearray(0x8000)
    data[0x40] = 0xD9  # VBlank: RETI
    data[0x48:0x4B] = bytes([0xC3, HANDLER & 0xFF, HANDLER >> 8])  # STAT: JP HANDLER
    data[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])  # NOP, JP $0150
    data[0x104:0x108] = bytes(
        [0xCE, 0xED, 0x66, 0x66]
    )  # what Jagoomba checks of the header (gbx_id)
    title = b"HICOLOR"
    data[0x134 : 0x134 + len(title)] = title
    data[0x143] = 0xC0  # GBC only
    data[0x147] = 0x00  # ROM only
    data[0x148] = 0x00  # 32 KB
    data[0x149] = 0x00  # no RAM
    data[0x14D] = (-sum(data[0x134:0x14D]) - (0x14D - 0x134)) & 0xFF
    code = main_program()
    data[0x150 : 0x150 + len(code)] = code
    code = handler()
    data[HANDLER : HANDLER + len(code)] = code
    for line in range(256):
        for k in range(4):
            v = colour(line, k)
            data[TABLE + line * 8 + k * 2] = v & 0xFF
            data[TABLE + line * 8 + k * 2 + 1] = v >> 8
    return bytes(data)


if __name__ == "__main__":
    with open(sys.argv[1], "wb") as f:
        f.write(rom())
