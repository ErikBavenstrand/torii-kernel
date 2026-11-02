#!/usr/bin/env python3
"""Game Genie codes made from an address, value and compare, for tests/emu_cheats.sh.

    emu_cheat_codes.py gb ADDR VALUE [COMPARE]    (hex) -> XXX-YYY[-ZZZ]
    emu_cheat_codes.py nes ADDR VALUE [COMPARE]   (hex) -> six or eight letters

The NES one is made from the bit table of nesgg.txt (https://www.nesdev.org/nesgg.txt),
not from the kernel's shuffle; the GB one inverts gambatte's and mGBA's reading.
"""

import sys

MAP6 = ["1678", "H234", "-IJK", "LABC", "DMNO", "5EFG"]
MAP8 = ["1678", "H234", "-IJK", "LABC", "DMNO", "%EFG", "!^&*", "5@#$"]
LETTERS = "APZLGITYEOXUKSVN"


def nes_genie(addr: int, value: int, compare: int | None = None) -> str:
    assert 0x8000 <= addr <= 0xFFFF
    table = MAP6 if compare is None else MAP8
    bits = {}
    for i in range(8):
        bits["12345678"[i]] = (value >> (7 - i)) & 1
    for i in range(15):
        bits["ABCDEFGHIJKLMNO"[i]] = (addr >> (14 - i)) & 1
    for i in range(8):
        bits["!@#$%^&*"[i]] = ((compare or 0) >> (7 - i)) & 1
    bits["-"] = 0 if compare is None else 1
    out = ""
    for letter_map in table:
        n = 0
        for b in range(4):
            n |= bits[letter_map[b]] << (3 - b)
        out += LETTERS[n]
    return out


def gb_genie(addr: int, value: int, compare: int | None = None) -> str:
    assert addr < 0x8000
    d = [value >> 4, value & 15, (addr >> 8) & 15, (addr >> 4) & 15, addr & 15, ((addr >> 12) & 15) ^ 15]
    code = "%X%X%X-%X%X%X" % tuple(d)
    if compare is not None:
        y = compare ^ 0x45
        x = ((y << 2) | (y >> 6)) & 0xFF
        z = x ^ 0xFF
        code += "-%X%X%X" % (z >> 4, 0, z & 15)
    return code


if __name__ == "__main__":
    kind, *args = sys.argv[1:]
    a = [int(x, 16) for x in args]
    print((gb_genie if kind == "gb" else nes_genie)(*a))
