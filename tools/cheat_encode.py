#!/usr/bin/env python3
"""Cheat code encoders: GBA codes in every format the real devices use, made
from what they do, for the cheat test ROM's cases (tests/cheat_engine.py).

The ciphers are the inverses of mGBA's decoders (src/gba/cheats/gameshark.c,
parv3.c, codebreaker.c), which source/cheatcode.c ports; the seed tables are
read from source/cheatcode.c. Each format is a class whose methods return
code lines; a Code object collects them and encrypts each as it is added,
so a DEADFACE line or a CodeBreaker key line changes the cipher for the
lines after it, as the devices read them:

    code = Code(GSA)                 # GameShark v1/v2, encrypted
    code += GSA.write(4, 0x02030000, 0x12345678)
    code.text()                      # '1A2B3C4D 5E6F7A8B'

Formats: GSA (GameShark v1/v2, Action Replay v1/v2) and PAR (Action Replay
v3, GameShark SP), each encrypted or raw; CB (CodeBreaker, plain or after a
type-9 key line encrypted); VBA (raw "AAAAAAAA:VVVV"). tests/cheat_encode_test.py
checks them; tests/cheat_engine.sh checks every case's codes decode as meant
with mGBA's own decoders (tools/cheat_oracle.c) and with Torii's
(tools/cheat_corpus.c).
"""

import re
from pathlib import Path
from typing import ClassVar

ROOT = Path(__file__).resolve().parent.parent
M32 = 0xFFFFFFFF
DELTA = 0x9E3779B9

GSA_SEEDS = (0x09F4FBBD, 0x9681884A, 0x352027E9, 0xF3DEE5A7)
PAR_SEEDS = (0x7AA9648F, 0x7FAE6994, 0xC0EFAAD5, 0x42712C57)


def _tables():
    text = (ROOT / "source" / "cheatcode.c").read_text(encoding="utf-8")
    out = {}
    for name in ("gsa1_t1", "gsa1_t2", "par3_t1", "par3_t2"):
        body = re.search(
            r"static const u8 " + name + r"\[256\] = \{(.*?)\};", text, re.DOTALL
        )[1]
        values = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", body)]
        assert len(values) == 256, name
        out[name] = values
    return out


TABLES = _tables()


# --- GameShark and Action Replay v3: TEA ----------------------------------


def tea_decrypt(op1, op2, seeds):
    """mGBA's GBACheatDecryptGameShark"""
    total = (DELTA * 32) & M32
    for _ in range(32):
        op2 = (
            op2
            - (
                (((op1 << 4) + seeds[2]) ^ (op1 + total) ^ ((op1 >> 5) + seeds[3]))
                & M32
            )
        ) & M32
        op1 = (
            op1
            - (
                (((op2 << 4) + seeds[0]) ^ (op2 + total) ^ ((op2 >> 5) + seeds[1]))
                & M32
            )
        ) & M32
        total = (total - DELTA) & M32
    return op1, op2


def tea_encrypt(op1, op2, seeds):
    """The inverse of tea_decrypt"""
    total = 0
    for _ in range(32):
        total = (total + DELTA) & M32
        op1 = (
            op1
            + (
                (((op2 << 4) + seeds[0]) ^ (op2 + total) ^ ((op2 >> 5) + seeds[1]))
                & M32
            )
        ) & M32
        op2 = (
            op2
            + (
                (((op1 << 4) + seeds[2]) ^ (op1 + total) ^ ((op1 >> 5) + seeds[3]))
                & M32
            )
        ) & M32
    return op1, op2


def gs_reseed(params, t1, t2):
    """mGBA's GBACheatReseedGameShark: the seeds a DEADFACE line sets"""
    s0, s1 = params >> 8 & 0xFF, params & 0xFF
    seeds = [0, 0, 0, 0]
    for y in range(4):
        for x in range(4):
            z = (t1[(s0 + x) & 0xFF] + t2[(s1 + y) & 0xFF]) & 0xFF
            seeds[y] = ((seeds[y] << 8) | z) & M32
    return tuple(seeds)


# --- CodeBreaker ----------------------------------------------------------


class CBKey:
    """A type-9 key line's cipher state, as mGBA's _cbReseed sets it"""

    def __init__(self, op1, op2):
        self.rng = (op2 & 0xFF) ^ 0x1111
        self.table = list(range(0x30))
        for _ in range(0x50):
            x = self._swap_index()
            y = self._swap_index()
            self.table[x], self.table[y] = self.table[y], self.table[x]
        self.rng = 0x4EFAD1C3
        for _ in range((op1 >> 24) & 0xF):
            self.rng = self._rand()
        seed2, seed3 = self._rand(), self._rand()
        self.rng = (op2 >> 8) ^ 0xF254
        for _ in range(op2 >> 8):
            self.rng = self._rand()
        seed0, seed1 = self._rand(), self._rand()
        self.seeds = (seed0, seed1, seed2, seed3)
        self.master = op1

    def _rand(self):
        roll = (self.rng * 0x41C64E6D + 0x3039) & M32
        roll2 = (roll * 0x41C64E6D + 0x3039) & M32
        roll3 = (roll2 * 0x41C64E6D + 0x3039) & M32
        mix = (roll << 14) & 0xC0000000
        mix |= (roll2 >> 1) & 0x3FFF8000
        mix |= (roll3 >> 16) & 0x7FFF
        self.rng = roll3
        return mix

    def _swap_index(self):
        def ror(v, n):
            return ((v >> n) | (v << (32 - n))) & M32

        roll = self._rand()
        count = 0x30
        if roll == count:
            roll = 0
        if roll < count:
            return roll
        bit = 1
        while count < 0x10000000 and count < roll:
            count = (count << 4) & M32
            bit = (bit << 4) & M32
        while count < 0x80000000 and count < roll:
            count = (count << 1) & M32
            bit = (bit << 1) & M32
        while True:
            mask = 0
            if roll >= count:
                roll -= count
            if roll >= count >> 1:
                roll -= count >> 1
                mask |= ror(bit, 1)
            if roll >= count >> 2:
                roll -= count >> 2
                mask |= ror(bit, 2)
            if roll >= count >> 3:
                roll -= count >> 3
                mask |= ror(bit, 3)
            if not roll or not (bit >> 4):
                break
            bit >>= 4
            count >>= 4
        mask &= 0xE0000000
        if not mask or not (bit & 7):
            return roll
        if mask & ror(bit, 3):
            roll += count >> 3
        if mask & ror(bit, 2):
            roll += count >> 2
        if mask & ror(bit, 1):
            roll += count >> 1
        if roll >= 0x30:
            raise ValueError(
                "this key's table swap indexes past the table: pick another key"
            )
        return roll

    @staticmethod
    def _load(op1, op2):
        return [
            op1 >> 24 & 0xFF,
            op1 >> 16 & 0xFF,
            op1 >> 8 & 0xFF,
            op1 & 0xFF,
            op2 >> 8 & 0xFF,
            op2 & 0xFF,
        ]

    @staticmethod
    def _store(b):
        return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3], (b[4] << 8) | b[5]

    def _swap_bits(self, b, i):
        ox, bx = i >> 3, i & 7
        oy, by = self.table[i] >> 3, self.table[i] & 7
        x = (b[ox] >> bx) & 1
        y = (b[oy] >> by) & 1
        b[ox] = (b[ox] & ~(1 << bx) & 0xFF) | (y << bx)
        b[oy] = (b[oy] & ~(1 << by) & 0xFF) | (x << by)

    def decrypt(self, op1, op2):
        """mGBA's _cbDecrypt"""
        s, m = self.seeds, self.master
        b = self._load(op1, op2)
        for i in range(0x2F, -1, -1):
            self._swap_bits(b, i)
        op1, op2 = self._store(b)
        op1 ^= s[0]
        op2 = (op2 ^ s[1]) & 0xFFFF
        b = self._load(op1, op2)
        m8, m0 = (m >> 8) & 0xFF, m & 0xFF
        for i in range(5):
            b[i] ^= m8 ^ b[i + 1]
        b[5] ^= m8
        for i in range(5, 0, -1):
            b[i] ^= m0 ^ b[i - 1]
        b[0] ^= m0
        op1, op2 = self._store(b)
        return op1 ^ s[2], (op2 ^ s[3]) & 0xFFFF

    def encrypt(self, op1, op2):
        """The inverse of decrypt"""
        s, m = self.seeds, self.master
        op1 ^= s[2]
        op2 = (op2 ^ s[3]) & 0xFFFF
        b = self._load(op1, op2)
        m8, m0 = (m >> 8) & 0xFF, m & 0xFF
        b[0] ^= m0
        for i in range(1, 6):
            b[i] ^= m0 ^ b[i - 1]
        b[5] ^= m8
        for i in range(4, -1, -1):
            b[i] ^= m8 ^ b[i + 1]
        op1, op2 = self._store(b)
        op1 ^= s[0]
        op2 = (op2 ^ s[1]) & 0xFFFF
        b = self._load(op1, op2)
        for i in range(0x30):
            self._swap_bits(b, i)
        return self._store(b)


# --- The formats ----------------------------------------------------------


def _check(address, width=1):
    assert 0 <= address <= M32 and width in (1, 2, 4), (hex(address), width)


class GSA:
    """GameShark v1/v2 (and Action Replay v1/v2) code types, as mGBA's
    GBACheatAddGameSharkRaw reads them: each returns [(op1, op2)]"""

    name = "GSAv1"
    encrypted = True
    seeds = GSA_SEEDS
    tables = ("gsa1_t1", "gsa1_t2")

    @staticmethod
    def write(width, address, value):
        _check(address, width)
        return [
            (
                {1: 0, 2: 1, 4: 2}[width] << 28 | (address & 0x0FFFFFFF),
                value & (1 << 8 * width) - 1,
            )
        ]

    @staticmethod
    def write_list(value, addresses):
        """Type 3: value (32-bit) to each address. mGBA's decoder writes the
        first line's op2 as an address too (value at value): see the
        cases"""
        lines = [(0x30000000 | len(addresses), value)]
        rest = list(addresses)
        while rest:
            pair = rest[:2]
            rest = rest[2:]
            lines.append((pair[0], pair[1] if len(pair) > 1 else 0))
        return lines

    @staticmethod
    def rom_patch(address, value):
        """Type 6: the halfword at ROM address (0x08000000-based)"""
        assert address & 1 == 0 and 0x08000000 <= address < 0x0A000000
        return [(0x60000000 | ((address - 0x08000000) >> 1), value & 0xFFFF)]

    @staticmethod
    def button(width, address, value):
        """Type 8: write while the device's button is held"""
        assert address & 0x00F00000 == 0, hex(address)
        return [
            (
                0x80000000
                | {1: 0x00100000, 2: 0x00200000}[width]
                | (address & 0x0F0FFFFF),
                value,
            )
        ]

    @staticmethod
    def if_eq(address, value):
        """Type D: the next code runs if the halfword at address is value"""
        return [(0xD0000000 | (address & 0x0FFFFFFF), value & 0xFFFF)]

    @staticmethod
    def if_eq_range(address, value, count):
        """Type E: the next count codes run if the halfword at address is value"""
        return [(0xE0000000 | (count & 0xFF) << 16 | (value & 0xFFFF), address)]

    @staticmethod
    def hook(address, game_id=0x0000):
        """Type F: the master code's hook (Torii skips it)"""
        return [(0xF0000000 | (address & 0x01FFFFFF), game_id)]

    @staticmethod
    def marker(game_id=0x4C0FFEE):
        """A game-ID line ending in 001DC0DE: both decoders tell the format
        from it (mGBA rejects the line itself, Torii drops it)"""
        return [(0x40000000 | game_id & 0x0FFFFFFF, 0x001DC0DE)]

    @staticmethod
    def deadface(params):
        return [(0xDEADFACE, params & 0xFFFF)]


class GSARaw(GSA):
    name = "GSAv1-raw"
    encrypted = False


class PAR:
    """Action Replay v3 (GameShark SP) code types, as mGBA's
    GBACheatAddProActionReplayRaw reads them"""

    name = "PARv3"
    encrypted = True
    seeds = PAR_SEEDS
    tables = ("par3_t1", "par3_t2")

    @staticmethod
    def addr(address):
        """The address field: 20 bits and the region in bits 20-23"""
        assert address & 0xF0F00000 == 0, hex(address)
        return (address & 0xFFFFF) | ((address >> 4) & 0x00F00000)

    @staticmethod
    def _w(width):
        return {1: 0, 2: 0x02000000, 4: 0x04000000}[width]

    @classmethod
    def write(cls, width, address, value, count=1):
        """A write; 8- and 16-bit ones fill count units"""
        assert count == 1 or width < 4
        hi = (count - 1) << (8 * width) if width < 4 else 0
        return [
            (cls._w(width) | cls.addr(address), (value & (1 << 8 * width) - 1) | hi)
        ]

    @classmethod
    def write_ptr(cls, width, address, value, offset=0):
        """Write value at (the word at address) + offset (units of width)"""
        hi = (offset // width) << (8 * width) if width < 4 else 0
        assert width == 4 or offset % width == 0
        return [
            (
                0x40000000 | cls._w(width) | cls.addr(address),
                (value & (1 << 8 * width) - 1) | hi,
            )
        ]

    @classmethod
    def add(cls, width, address, value):
        return [
            (
                0x80000000 | cls._w(width) | cls.addr(address),
                value & (1 << 8 * width) - 1,
            )
        ]

    @staticmethod
    def io(code_width, offset, value):
        """C6 (code_width 1) or C7 (2) IO write at 0x04000000 + offset; see
        the cases for how the decoders read the widths"""
        return [
            ({1: 0xC6000000, 2: 0xC7000000}[code_width] | (offset & 0xFFFFFF), value)
        ]

    COND: ClassVar[dict] = {
        "EQ": 1,
        "NE": 2,
        "LT": 3,
        "GT": 4,
        "ULT": 5,
        "UGT": 6,
        "AND": 7,
    }
    ACTION: ClassVar[dict] = {"next": 0, "next2": 0x40000000, "block": 0x80000000}

    @classmethod
    def cond(cls, kind, width, address, value, action="next"):
        """A conditional; width 8 is the never-true one"""
        w = {1: 0, 2: 0x02000000, 4: 0x04000000, 8: 0x06000000}[width]
        return [
            (
                cls.ACTION[action] | cls.COND[kind] << 27 | w | cls.addr(address),
                value & M32,
            )
        ]

    @staticmethod
    def endif():
        return [(0, 0x40000000)]

    @staticmethod
    def else_():
        return [(0, 0x60000000)]

    @classmethod
    def slide(cls, width, address, value, count, addr_step=1, value_step=0):
        """Fill or slide: count writes, address up addr_step units, value up value_step"""
        return [
            (
                0,
                {1: 0x80000000, 2: 0x82000000, 4: 0x84000000}[width]
                | cls.addr(address),
            ),
            (
                value & (1 << 8 * width) - 1,
                (value_step & 0xFF) << 24 | (count & 0xFF) << 16 | (addr_step & 0xFFFF),
            ),
        ]

    @classmethod
    def button(cls, width, address, value):
        return [
            (
                0,
                {1: 0x10000000, 2: 0x12000000, 4: 0x14000000}[width]
                | cls.addr(address),
            ),
            (value, 0),
        ]

    @staticmethod
    def rom_patch(slot, address, value):
        """Patch 1-4 (slot 1-4): the halfword at ROM address"""
        assert (
            address & 1 == 0 and 0x08000000 <= address < 0x0A000000 and 1 <= slot <= 4
        )
        return [
            (0, (0x18000000 + (slot - 1) * 0x02000000) | ((address - 0x08000000) >> 1)),
            (value & 0xFFFF, 0),
        ]

    @staticmethod
    def slowdown(amount):
        return [(0, 0x08000000 | amount & 0xFFFFFF)]

    @staticmethod
    def hook(address):
        return [(0xC4000000 | (address & 0x01FFFFFE), 0)]

    @staticmethod
    def marker(op1=0x4C0FFEE0):
        return [(op1, 0x001DC0DE)]

    @staticmethod
    def deadface(params):
        return [(0xDEADFACE, params & 0xFFFF)]


class PARRaw(PAR):
    name = "PARv3-raw"
    encrypted = False

    @staticmethod
    def marker(op1=0):
        # 001DC0DE reads as GameShark raw first; a DEADFACE line with a 16-bit
        # parameter is certain only for Action Replay v3
        return [(0xDEADFACE, 0x0000)]


class CB:
    """CodeBreaker code types, as mGBA's GBACheatAddCodeBreaker reads them:
    each returns [(op1, op2)] with a 16-bit op2"""

    name = "CodeBreaker"

    @staticmethod
    def _t(kind, address, value):
        return [(kind << 28 | (address & 0x0FFFFFFF), value & 0xFFFF)]

    @classmethod
    def write8(cls, address, value):
        return cls._t(3, address, value)

    @classmethod
    def write16(cls, address, value):
        return cls._t(8, address, value)

    @classmethod
    def slide(cls, address, value, count, addr_step=2, value_step=0):
        """Type 4: count halfwords, address up addr_step bytes, value up value_step"""
        return cls._t(4, address, value) + [
            ((value_step & 0xFFFF) << 16 | (count & 0xFFFF), addr_step & 0xFFFF)
        ]

    @classmethod
    def super(cls, address, data):
        """Type 5: the bytes of data (an even count) from address"""
        assert len(data) % 2 == 0
        lines = cls._t(5, address, len(data) // 2)
        data = bytes(data) + bytes(-len(data) % 6)
        for i in range(0, len(data), 6):
            b = data[i : i + 6]
            lines.append(
                ((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3], (b[4] << 8) | b[5])
            )
        return lines

    OPS: ClassVar[dict] = {
        "OR": 2,
        "AND": 6,
        "ADD": 0xE,
        "EQ": 7,
        "NE": 0xA,
        "GT": 0xB,
        "LT": 0xC,
        "IFAND": 0xF,
    }

    @classmethod
    def op(cls, kind, address, value):
        """OR, AND or ADD a halfword, or a conditional on the next code: EQ,
        NE, GT, LT (signed) or IFAND"""
        return cls._t(cls.OPS[kind], address, value)

    @staticmethod
    def keys(mask):
        """D0000020: the next code runs while every key in mask is held"""
        return [(0xD0000020, mask & 0xFFFF)]

    @classmethod
    def hook(cls, address):
        return cls._t(1, address, 0x000A)

    @classmethod
    def game_id(cls, crc=0x1234, unknown=0x0001):
        return [(0x00000000 | crc, unknown)]

    @staticmethod
    def key(op1, op2):
        """The type-9 key line: the codes after it are encrypted"""
        assert op1 >> 28 == 9
        return [(op1, op2 & 0xFFFF)]


class VBA:
    name = "VBA"

    @staticmethod
    def write(width, address, value):
        return [(address, value & (1 << 8 * width) - 1, width)]


class Code:
    """One cheat's code lines in one format, encrypted as they're added"""

    def __init__(self, fmt, seeds=None):
        self.fmt = fmt
        self.lines = []
        self.seeds = seeds if seeds is not None else getattr(fmt, "seeds", None)
        self.cb = None

    def __iadd__(self, raw):
        for line in raw:
            self.add(line)
        return self

    def add(self, line):
        fmt = self.fmt
        if fmt is VBA:
            address, value, width = line
            self.lines.append(f"{address:08X}:{value:0{width * 2}X}")
        elif fmt is CB:
            op1, op2 = line
            if self.cb is not None:
                op1, op2 = self.cb.encrypt(op1, op2)
            elif op1 >> 28 == 9:
                self.lines.append(f"{op1:08X} {op2:04X}")
                self.cb = CBKey(op1, op2)
                return
            self.lines.append(f"{op1:08X} {op2:04X}")
        else:
            op1, op2 = line
            out = tea_encrypt(op1, op2, self.seeds) if fmt.encrypted else (op1, op2)
            self.lines.append(f"{out[0]:08X} {out[1]:08X}")
            if op1 == 0xDEADFACE:
                t1, t2 = (TABLES[t] for t in fmt.tables)
                self.seeds = gs_reseed(op2, t1, t2)

    def text(self):
        return "+".join(self.lines)


def cht(cheats):
    """A libretro .cht file of (desc, code text) cheats, every one picked,
    in the shape mGBA's reader takes (LF lines, nothing else)"""
    out = [f"cheats = {len(cheats)}", ""]
    for i, (desc, code) in enumerate(cheats):
        out += [
            f'cheat{i}_desc = "{desc}"',
            f'cheat{i}_code = "{code}"',
            f"cheat{i}_enable = true",
            "",
        ]
    return "\n".join(out)
