#!/usr/bin/env python3
"""Writes the NES ROM of the NES cheats' proof (tests/nes_cheat_proof.py):
usage nes_cheat_rom.py OUT.nes nrom|uxrom|mmc1|mmc3

Written for Torii's tests (no third-party code), so it can live in the repo.
NROM-256 (32 KB of PRG), or 128 KB of PRG on UxROM (mapper 2), MMC1 (1) or
MMC3 (4), with 8 KB of CHR. Each PRG bank (16 KB, or MMC3's 8 KB) holds a byte
of its own at its offset 0x10 (unique), a byte at 0x11 that two banks share
(TWIN, banks 1 and 3) and 99 at 0x12. Once a frame, after the vertical blank
starts, the program maps each switchable bank into each window it reads and
copies those three bytes to RAM at RESULTS (reads(), in order), then the fixed
banks' bytes. It watches WATCHED: each starts at EE, and a value found there
is recorded, the frame counted (up to CAP) and EE put back, so a write made
every frame counts to CAP. The ROM never maps a bank fixed elsewhere into a
switchable window (UxROM's and MMC1's last 16 KB, MMC3's last two 8 KB).
"""

import struct
import sys

RESULTS = 0x0400
SENTINEL = 0xEE
CAP = 0x40
TWIN = 0x5A
WATCHED = [0x0300, 0x0380, 0x07F0, 0x6000]
MAPPER = {"nrom": 0, "uxrom": 2, "mmc1": 1, "mmc3": 4}


def bank_size(mapper: str) -> int:
    return 0x2000 if mapper == "mmc3" else 0x4000


def prg_size(mapper: str) -> int:
    return 0x8000 if mapper == "nrom" else 0x20000


def unique(bank: int) -> int:
    return (0x13 + bank * 0x0B) & 0xFF


def twin(bank: int) -> int:
    return TWIN if bank in (1, 3) else 0x30 + bank


def reads(mapper: str) -> list[tuple[int, int, int]]:
    """(CPU address, PRG offset, slot) of each byte the program reads, the slot its result's offset."""
    out = []
    n = prg_size(mapper) // bank_size(mapper)
    windows: list[tuple[int, list[int]]] = []  # (CPU base, banks shown there)
    if mapper == "nrom":
        windows = [(0x8000, [0]), (0xC000, [1])]
    elif mapper in ("uxrom", "mmc1"):
        windows = [(0x8000, list(range(n - 1))), (0xC000, [n - 1])]
    else:
        windows = [
            (0x8000, list(range(n - 2))),
            (0xA000, list(range(n - 2))),
            (0xC000, [n - 2]),
            (0xE000, [n - 1]),
        ]
    for base, banks in windows:
        for b in banks:
            for k in range(3):
                out.append(
                    (base + 0x10 + k, b * bank_size(mapper) + 0x10 + k, len(out))
                )
    return out


def layout(mapper: str) -> dict[str, int]:
    slots = {"ready": 0xFF}
    for cpu, off, slot in reads(mapper):
        slots[f"rom {cpu:04X} bank {off // bank_size(mapper)}"] = slot
    for i, addr in enumerate(WATCHED):
        slots[f"ram {addr:04X} value"] = 0xC0 + i
        slots[f"ram {addr:04X} frames"] = 0xD0 + i
    assert max(s for _, _, s in reads(mapper)) < 0xC0
    return slots


def program(mapper: str, org: int) -> bytes:
    code = bytearray()

    def emit(*b: int) -> None:
        code.extend(b)

    def sta(addr: int) -> None:
        emit(0x8D, addr & 0xFF, addr >> 8)

    def lda_abs(addr: int) -> None:
        emit(0xAD, addr & 0xFF, addr >> 8)

    def lda(v: int) -> None:
        emit(0xA9, v)

    table = (
        org + 0x1000
    )  # UxROM: bytes 0-255, to write a bank number to the byte that holds it

    def select(window: int, bank: int) -> None:
        if mapper == "uxrom":
            lda(bank)
            sta(table + bank)
        elif mapper == "mmc1":
            lda(bank)
            for i in range(5):
                sta(0xE000)
                if i < 4:
                    emit(0x4A)  # LSR A
        elif mapper == "mmc3":
            lda(6 if window == 0x8000 else 7)
            sta(0x8000)
            lda(bank)
            sta(0x8001)

    emit(0x78, 0xD8, 0xA2, 0xFF, 0x9A)  # SEI, CLD, LDX #$FF, TXS
    lda(0)
    sta(0x2000)
    sta(0x2001)
    if mapper == "mmc1":
        lda(0x80)
        sta(0x8000)  # reset: PRG mode 3, the last bank fixed at C000
    if mapper == "mmc3":
        lda(0)
        sta(0xA001)  # PRG-RAM: MMC3 leaves it to the mapper's protect bits
        lda(0x80)
        sta(0xA001)  # on, writable
    # the results to 0, then each watched byte to the sentinel
    emit(0xA9, 0x00, 0xA2, 0x00)  # LDA #0; LDX #0
    emit(
        0x9D, RESULTS & 0xFF, RESULTS >> 8, 0xE8, 0xD0, 0xFA
    )  # STA RESULTS,X; INX; BNE
    lda(SENTINEL)
    for addr in WATCHED:
        sta(addr)
    frame = org + len(code)
    emit(0x2C, 0x02, 0x20, 0x10, 0xFB)  # BIT $2002; BPL: wait for the vertical blank
    last = None
    for cpu, off, slot in reads(mapper):
        window = cpu & 0xE000 if mapper == "mmc3" else cpu & 0xC000
        bank = off // bank_size(mapper)
        switchable = (mapper in ("uxrom", "mmc1") and window == 0x8000) or (
            mapper == "mmc3" and window in (0x8000, 0xA000)
        )
        if switchable and last != (window, bank):
            select(window, bank)
            last = (window, bank)
        lda_abs(cpu)
        sta(RESULTS + slot)
    for i, addr in enumerate(WATCHED):
        lda_abs(addr)
        emit(0xC9, SENTINEL)  # CMP
        skip = len(code)
        emit(0xF0, 0)  # BEQ skip
        sta(RESULTS + 0xC0 + i)
        lda_abs(RESULTS + 0xD0 + i)
        emit(0xC9, CAP)
        full = len(code)
        emit(0xB0, 0)  # BCS full
        emit(0x18, 0x69, 0x01)  # CLC; ADC #1
        sta(RESULTS + 0xD0 + i)
        code[full + 1] = len(code) - (full + 2)
        lda(SENTINEL)
        sta(addr)
        code[skip + 1] = len(code) - (skip + 2)
    lda(0x42)
    sta(RESULTS + 0xFF)
    emit(0x4C, frame & 0xFF, frame >> 8)
    assert len(code) < 0x1000, len(code)
    return bytes(code)


def rom(mapper: str) -> bytes:
    size = prg_size(mapper)
    prg = bytearray(size)
    bs = bank_size(mapper)
    for b in range(size // bs):
        prg[b * bs + 0x10] = unique(b)
        prg[b * bs + 0x11] = twin(b)
        prg[b * bs + 0x12] = 0x99
    # the program in the last 8 KB (CPU E000-FFFF in every mapper here), past the bank bytes
    org = 0xE000 + 0x100
    code = program(mapper, org)
    at = size - 0x2000 + 0x100
    prg[at : at + len(code)] = code
    table = size - 0x2000 + 0x1100
    prg[table : table + 256] = bytes(range(256))
    rti = 0xFFF0
    prg[size - 0x10] = 0x40  # RTI
    prg[size - 6 :] = struct.pack("<HHH", rti, org, rti)  # NMI, RESET, IRQ
    m = MAPPER[mapper]
    header = (
        b"NES\x1a" + bytes([size // 0x4000, 1, (m & 0xF) << 4, m & 0xF0]) + bytes(8)
    )
    return header + bytes(prg) + bytes(0x2000)


if __name__ == "__main__":
    with open(sys.argv[1], "wb") as f:
        f.write(rom(sys.argv[2]))
