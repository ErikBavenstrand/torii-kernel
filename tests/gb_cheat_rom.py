#!/usr/bin/env python3
"""Writes the Game Boy ROM of the GB cheats' proof (tests/gb_cheat_proof.sh):
usage gb_cheat_rom.py OUT.gb mbc1|mbc3|mbc5 [gbc]

Written for Torii's tests (no third-party code), so it can live in the repo.
128 KB (8 banks) on the given MBC, no cart RAM. Once a frame, a little after
the vertical blank starts (LY 146, after a cheat device's writes at 144), the
program reads what the cheats could change and writes it to WRAM at C000
(RESULTS, below), then waits for the next frame:

- ROM: the bytes at 4010, 4011 and 4012 of each bank 1-7 (switched in through
  2000), and bank 0's at 0710 and 0780 (0780 is one the emulators' checksum
  samples). Each bank has its own byte at 4010 (BANK_BYTE), banks 2 and 5
  share one at 4011 (TWIN) that the others don't, and every bank has 99 at 4012.
- RAM: each watched address (C100, C180, FF90, D100, and on a GBC D100 in
  each WRAM bank 1-7) is set to EE at start; when the program finds another
  value there it records it, counts the frame (up to CAP) and puts EE back. A
  write made every frame counts to CAP; one that never comes leaves 00.

Bank 1 of ROM, and on a GBC WRAM bank 3 (HOME_WRAM), are mapped whenever the
frame ends. The results block is the same in any emulator that runs the
cheats alike, frame timing aside: tests/gb_cheat_proof.py compares them.
"""

import sys

RESULTS = 0xC000
SENTINEL = 0xEE
CAP = 0x40
HOME_WRAM = 3
TWIN = 0x5A
MBC = {"mbc1": 0x01, "mbc3": 0x11, "mbc5": 0x19}


def bank_byte(bank: int) -> int:
    return 0x10 + bank * 0x11


def twin_byte(bank: int) -> int:
    return TWIN if bank in (2, 5) else 0x30 + bank


BANK0 = {0x0710: 0xB0, 0x0780: 0xB8}


def watched(gbc: bool) -> list[tuple[int, int]]:
    """(address, WRAM bank or 0) of each watched byte, in result order."""
    out = [(0xC100, 0), (0xC180, 0), (0xFF90, 0)]
    if gbc:
        out += [(0xD100, b) for b in range(1, 8)]
    else:
        out += [(0xD100, 0)]
    return out


def layout(gbc: bool) -> dict[str, int]:
    """Where each result is, as offsets from RESULTS."""
    slots = {"ready": 0x0F}
    for b in range(1, 8):
        slots[f"rom 4010 bank {b}"] = 0x10 + b
        slots[f"rom 4011 bank {b}"] = 0x18 + b
        slots[f"rom 4012 bank {b}"] = 0x20 + b
    slots["rom 0710"] = 0x30
    slots["rom 0780"] = 0x31
    for i, (addr, bank) in enumerate(watched(gbc)):
        name = f"ram {addr:04X}" + (f" bank {bank}" if bank else "")
        slots[name + " value"] = 0x40 + i
        slots[name + " frames"] = 0x50 + i
    return slots


class Asm:
    def __init__(self, org: int):
        self.org = org
        self.code = bytearray()
        self.labels: dict[str, int] = {}
        self.fixups: list[tuple[int, str]] = []

    def b(self, *data: int) -> None:
        self.code += bytes(data)

    def label(self, name: str) -> None:
        self.labels[name] = self.org + len(self.code)

    def jr(self, op: int, name: str) -> None:
        self.b(op, 0)
        self.fixups.append((len(self.code) - 1, name))

    def ld_a_mem(self, addr: int) -> None:
        self.b(0xFA, addr & 0xFF, addr >> 8)

    def ld_mem_a(self, addr: int) -> None:
        self.b(0xEA, addr & 0xFF, addr >> 8)

    def ld_a(self, n: int) -> None:
        self.b(0x3E, n)

    def bytes(self) -> bytes:
        for at, name in self.fixups:
            rel = self.labels[name] - (self.org + at + 1)
            assert -128 <= rel < 128, name
            self.code[at] = rel & 0xFF
        return bytes(self.code)


def program(gbc: bool) -> bytes:
    a = Asm(0x150)
    n = [0]

    def unique() -> str:
        n[0] += 1
        return f"l{n[0]}"

    a.b(0xF3)  # DI
    a.b(0x31, 0xFE, 0xFF)  # LD SP,$FFFE
    # the results block to 0
    a.b(
        0x21, RESULTS & 0xFF, RESULTS >> 8, 0x06, 0x80, 0xAF
    )  # LD HL,RESULTS; LD B,$80; XOR A
    a.label("clear")
    a.b(0x22, 0x05)  # LD (HL+),A; DEC B
    a.jr(0x20, "clear")
    # every watched byte to the sentinel
    for addr, bank in watched(gbc):
        if bank:
            a.ld_a(bank)
            a.b(0xE0, 0x70)  # LDH (SVBK),A
        a.ld_a(SENTINEL)
        a.ld_mem_a(addr)
    if gbc:
        a.ld_a(HOME_WRAM)
        a.b(0xE0, 0x70)
    a.label("frame")
    # wait for LY 146: out of 146 first, then into it
    a.label("out")
    a.b(0xF0, 0x44, 0xFE, 146)  # LDH A,(LY); CP 146
    a.jr(0x28, "out")
    a.label("in")
    a.b(0xF0, 0x44, 0xFE, 146)
    a.jr(0x20, "in")
    # the ROM banks
    for bank in range(1, 8):
        a.ld_a(bank)
        a.ld_mem_a(0x2000)
        for k, off in enumerate((0x4010, 0x4011, 0x4012)):
            a.ld_a_mem(off)
            a.ld_mem_a(RESULTS + 0x10 + 8 * k + bank)
    a.ld_a(1)
    a.ld_mem_a(0x2000)
    a.ld_a_mem(0x0710)
    a.ld_mem_a(RESULTS + 0x30)
    a.ld_a_mem(0x0780)
    a.ld_mem_a(RESULTS + 0x31)
    # the watched bytes
    for i, (addr, bank) in enumerate(watched(gbc)):
        skip, full = unique(), unique()
        if bank:
            a.ld_a(bank)
            a.b(0xE0, 0x70)
        a.ld_a_mem(addr)
        a.b(0xFE, SENTINEL)  # CP SENTINEL
        a.jr(0x28, skip)
        a.ld_mem_a(RESULTS + 0x40 + i)
        a.ld_a_mem(RESULTS + 0x50 + i)
        a.b(0xFE, CAP)
        a.jr(0x30, full)  # JR NC: at the cap
        a.b(0x3C)  # INC A
        a.ld_mem_a(RESULTS + 0x50 + i)
        a.label(full)
        a.ld_a(SENTINEL)
        a.ld_mem_a(addr)
        a.label(skip)
    if gbc:
        a.ld_a(HOME_WRAM)
        a.b(0xE0, 0x70)
    a.ld_a(0x42)
    a.ld_mem_a(RESULTS + 0x0F)
    a.b(0xC3, 0, 0)  # JP frame (patched below)
    code = bytearray(a.bytes())
    frame = a.labels["frame"]
    code[-2:] = bytes([frame & 0xFF, frame >> 8])
    return bytes(code)


def rom(mbc: str, gbc: bool) -> bytes:
    data = bytearray(0x20000)
    data[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])  # NOP; JP $0150
    data[0x104:0x108] = bytes(
        [0xCE, 0xED, 0x66, 0x66]
    )  # what Jagoomba checks of the header
    title = b"CHEATPROOF"
    data[0x134 : 0x134 + len(title)] = title
    data[0x143] = 0x80 if gbc else 0x00
    data[0x147] = MBC[mbc]
    data[0x148] = 0x02  # 128 KB
    data[0x149] = 0x00  # no RAM
    data[0x14D] = (-sum(data[0x134:0x14D]) - (0x14D - 0x134)) & 0xFF
    code = program(gbc)
    assert 0x150 + len(code) < 0x0700, len(code)
    data[0x150 : 0x150 + len(code)] = code
    for addr, value in BANK0.items():
        data[addr] = value
    for bank in range(1, 8):
        at = bank * 0x4000
        data[at + 0x10] = bank_byte(bank)
        data[at + 0x11] = twin_byte(bank)
        data[at + 0x12] = 0x99
    return bytes(data)


if __name__ == "__main__":
    out, mbc, *rest = sys.argv[1:]
    with open(out, "wb") as f:
        f.write(rom(mbc, rest == ["gbc"]))
