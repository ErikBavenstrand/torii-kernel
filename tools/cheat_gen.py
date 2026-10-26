#!/usr/bin/env python3
"""Random GBA cheat programs in every format, made of valid encodings
(tools/cheat_encode.py), for the cheat validation suite:

- tests/cheat_fuzz.py runs seeded programs on the cheat test ROM
  (tests/roms/cheattest) in mGBA's engine and through Torii (Space.rom():
  every write lands in a region the ROM watches, every read is one of its
  counters, the keys or a watched region);
- tools/cheat_sample.py writes tests/cheats/sample/, the committed corpus
  test-host decodes with both decoders (Space.anywhere(): addresses all over
  the GBA's memory, and the code shapes where Torii rightly differs).

A program is a list of cheats; a cheat a list of codes (each code its
encoder lines), laid out as both decoders read them. With intended=False the
programs leave out what Torii means to do unlike mGBA (tests/
cheat_expected.toml's "allow" rules: GameShark lists, a conditional on a
multi-op code, open or empty-THEN Action Replay blocks, C6/C7, slowdown,
padding, CodeBreaker super codes), so any difference in a run is a finding.
"""

import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cheat_encode import CB, GSA, PAR, VBA, Code, GSARaw, PARRaw

FORMATS = {
    "gsa": GSA,
    "gsa-raw": GSARaw,
    "par": PAR,
    "par-raw": PARRaw,
    "cb": CB,
    "cb-enc": CB,
    "vba": VBA,
}
CB_KEY = (0x9A2B3C4D, 0x5E6F)

# The cheat test ROM's layout (tests/roms/cheattest/main.c)
E = 0x02030000
W, CNT, C, A, P, F = E, E + 0x100, E + 0x200, E + 0x300, E + 0x400, E + 0x500
IW = 0x03004000
KEYINPUT = 0x04000130
KEYS = ["A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"]


def keyinput(keys):
    held = 0
    for k in keys:
        held |= 1 << KEYS.index(k)
    return 0x3FF & ~held


@dataclass
class Region:
    start: int
    size: int
    widths: tuple = (1, 2, 4)
    odd: bool = True  # unaligned addresses for wider writes


@dataclass
class Space:
    """Where a program may write and what it may test"""

    writes: list  # [Region]
    reads: list  # [(width, address, [likely values])]
    rom: list  # ROM addresses a patch may change (halfword aligned)
    pointers: list = field(default_factory=list)  # [(pointer address, room in bytes)]
    rom_writes: bool = True  # VBA and GameShark writes to the ROM (patches)

    def write_at(self, rng, width, room=1):
        regs = [r for r in self.writes if width in r.widths and r.size >= width * room]
        r = rng.choice(regs)
        span = r.size - width * room
        a = r.start + rng.randrange(0, span + 1)
        if not r.odd or rng.random() < 0.85:
            a &= ~(width - 1)
        return a

    def read(self, rng):
        w, a, likely = rng.choice(self.reads)
        if likely and rng.random() < 0.8:
            v = rng.choice(likely)
        else:
            v = rng.getrandbits(8 * w)
        return w, a, v & ((1 << 8 * w) - 1)

    @staticmethod
    def rom_space(rom_table, rom_table2, keys=True):
        """The cheat test ROM's watched regions; keys=False leaves out tests
        on the keys (a run that holds the device's button holds other keys
        in Torii than in mGBA)"""
        p_values = range(4, 14)
        reads = [
            (1, CNT + 0, list(p_values)),
            (2, CNT + 2, [p * 0x123 for p in p_values]),
            (4, CNT + 4, [p * 0x1234567 & 0xFFFFFFFF for p in p_values]),
            (2, CNT + 8, [((p & 15) - 8) & 0xFFFF for p in p_values]),
            (1, CNT + 0xA, [((p & 15) - 8) & 0xFF for p in p_values]),
            (2, CNT + 0xC, [1 << (p & 15) for p in p_values]),
            (4, CNT + 0x10, [0x7FFFFFF8 + p for p in p_values]),
            (
                2,
                KEYINPUT,
                [keyinput(k) for k in ([], ["A"], ["B"], ["A", "B"], ["R", "SELECT"])],
            ),
            (2, CNT + 0xE, [keyinput(k) for k in ([], ["A"], ["B"])]),
            (1, W + 0x41, list(range(0x30, 0x80, 8))),
            (2, W + 0x40, [n << 8 | 0x20 for n in range(40, 112, 8)]),
            (1, IW + 0x10, []),
            # Unaligned, and in the other watched regions: what the frame
            # wrote there, read as each engine's bus reads it
            (2, W + 0x41, []),
            (4, W + 0x42, []),
            (4, IW + 0x13, []),
            (2, 0x05000002, []),
            (1, 0x05000003, []),
            (2, 0x06000004, []),
            (4, 0x07000004, []),
            (2, 0x0400000C, []),
            (1, 0x0400004A, []),
        ]
        writes = [
            Region(W, 0x100),
            Region(C, 0x100),
            Region(A, 0x100),
            Region(F, 0x100),
            Region(P + 0x80, 0x40),
            Region(IW, 0x100),
            Region(0x05000000, 0x20),
            Region(0x06000000, 0x20),
            Region(0x07000000, 0x20, widths=(2, 4)),
            Region(0x0400000C, 4, odd=False),
            Region(0x04000048, 4, odd=False),
        ]
        if not keys:
            reads = [r for r in reads if r[1] not in (KEYINPUT, CNT + 0xE)]
        # The ROM's data tables only: a random patch to its code could crash it
        rom = [rom_table + i for i in range(0, 16, 2)] + [
            rom_table2 + i for i in range(0, 16, 2)
        ]
        return Space(writes, reads, rom, pointers=[(P, 16), (P + 4, 8)])

    @staticmethod
    def anywhere():
        regions = [
            Region(0x02000000, 0x40000),
            Region(0x03000000, 0x7E00),
            Region(0x04000000, 0x400, odd=False),
            Region(0x05000000, 0x400),
            Region(0x06000000, 0x18000),
            Region(0x07000000, 0x400),
            Region(0x0E000000, 0x10000, widths=(1,)),
        ]
        reads = [(w, 0x02000000 + i * 0x40, []) for w in (1, 2, 4) for i in range(8)]
        reads.append((2, KEYINPUT, [keyinput(["A"]), keyinput(["R", "L"])]))
        rom = [0x08000000 + 2 * i for i in range(0x100, 0x200)]
        return Space(
            regions, reads, rom, pointers=[(0x02000100, 0x100), (0x03000200, 0x40)]
        )


def rnd_value(rng, width):
    r = rng.random()
    if r < 0.3:
        return rng.randrange(0, 0x10)
    if r < 0.4:
        return (1 << 8 * width) - 1
    return rng.getrandbits(8 * width)


class Gen:
    def __init__(self, rng, fmt, space, intended=False):
        self.rng, self.fmt, self.space, self.intended = rng, fmt, space, intended
        self.enc = FORMATS[fmt]
        self.patched = set()

    def single(self, covered=False):
        """One code that is one op in both decoders; covered: a conditional
        covers it (no ROM patch then, unless intended: Torii counts a patch
        where it lies, mGBA skips it)"""
        rng, sp, f = self.rng, self.space, self.fmt
        if f in ("gsa", "gsa-raw"):
            choices = ["write"] * 6 + ["patch"]
        elif f in ("par", "par-raw"):
            choices = ["write"] * 4 + ["fill", "slide", "ptr", "add", "add", "patch"]
        elif f.startswith("cb"):
            choices = ["write"] * 4 + ["slide", "cb-or", "cb-and", "cb-add"]
        else:
            choices = ["write"] * 4 + ["patch"]
        kind = rng.choice(choices)
        if kind == "patch" and covered and not self.intended:
            kind = "write"
        if kind == "patch":
            free = [a for a in sp.rom if a not in self.patched]
            if (
                not free
                or not sp.rom_writes
                or (f.startswith("par") and len(self.patched) >= 4)
            ):
                kind = "write"
        e = self.enc
        if kind == "write":
            if f.startswith("cb"):
                w = rng.choice((1, 2))
                a = sp.write_at(rng, w)
                return (
                    CB.write8(a, rnd_value(rng, 1))
                    if w == 1
                    else CB.write16(a, rnd_value(rng, 2))
                )
            w = rng.choice((1, 2, 4))
            a = sp.write_at(rng, w)
            if f == "vba":
                return VBA.write(w, a, rnd_value(rng, w))
            if f.startswith("par"):
                return e.write(w, a, rnd_value(rng, w))
            return e.write(w, a, rnd_value(rng, w))
        if kind == "patch":
            free = [a for a in sp.rom if a not in self.patched]
            a = rng.choice(free)
            self.patched.add(a)
            v = rnd_value(rng, 2)
            if f == "vba":
                return VBA.write(2, a, v)
            if f.startswith("par"):
                # Slots 1-4 in turn: the device has four
                return PAR.rom_patch(len(self.patched) % 4 + 1, a, v)
            return e.rom_patch(a, v)
        if kind == "fill":
            w = rng.choice((1, 2))
            n = rng.randrange(1, 9)
            a = sp.write_at(rng, w, n)
            return e.write(w, a & ~(w - 1), rnd_value(rng, w), count=n)
        if kind == "slide":
            if f.startswith("cb"):
                n = rng.randrange(1, 7)
                step = rng.choice((2, 4))
                a = sp.write_at(rng, 2, n * step // 2 + 1) & ~1
                return CB.slide(a, rnd_value(rng, 2), n, step, rng.randrange(0, 0x40))
            w = rng.choice((1, 2, 4))
            n = rng.randrange(1, 7)
            step = rng.randrange(1, 3)
            a = sp.write_at(rng, w, n * step + 1) & ~(w - 1)
            return e.slide(w, a, rnd_value(rng, w), n, step, rng.randrange(0, 0x20))
        if kind == "ptr":
            ptr, room = rng.choice(sp.pointers)
            w = rng.choice((1, 2, 4))
            off = rng.randrange(0, room // w) * w if w < 4 else 0
            return e.write_ptr(w, ptr, rnd_value(rng, w), off)
        if kind == "add":
            w = rng.choice((1, 2, 4))
            a = sp.write_at(rng, w) & ~(w - 1)
            return e.add(w, a, rnd_value(rng, w))
        # CodeBreaker's or, and, add: halfwords
        a = sp.write_at(rng, 2) & ~1
        return CB.op(kind[3:].upper(), a, rnd_value(rng, 2))

    def codes(self, budget):
        """Units for one cheat, at most about budget of them: a unit is a code, or a
        conditional with the codes it covers (its chunks of encoder lines), which
        tests/cheat_fuzz.py's minimizing keeps or drops whole"""
        out = []
        rng, f = self.rng, self.fmt
        while len(out) < budget:
            r = rng.random()
            if f in ("gsa", "gsa-raw"):
                if r < 0.15:
                    out.append([self.gsa_cond()])
                elif r < 0.25:
                    out.append(self.gsa_range())
                elif r < 0.32:
                    w = rng.choice((1, 2))
                    a = self.space.write_at(rng, w)
                    out.append([GSA.button(w, a, rnd_value(rng, w))])
                elif self.intended and r < 0.36:
                    out.append(
                        [
                            GSA.write_list(
                                rnd_value(rng, 4),
                                [self.space.write_at(rng, 4) & ~3 for _ in range(3)],
                            )
                        ]
                    )
                else:
                    out.append([self.single()])
            elif f in ("par", "par-raw"):
                if r < 0.2:
                    out.append(self.par_cond())
                elif r < 0.26:
                    w = rng.choice((1, 2, 4))
                    out.append(
                        [
                            PAR.button(
                                w,
                                self.space.write_at(rng, w) & ~(w - 1),
                                rnd_value(rng, w),
                            )
                        ]
                    )
                elif self.intended and r < 0.32:
                    out.append(
                        [
                            rng.choice(
                                [
                                    PAR.slowdown(0x100),
                                    [(0x80000000, 0)],
                                    PAR.io(1, 0xC, 0x45),
                                    PAR.io(2, 0x48, 0x1B2D),
                                ]
                            )
                        ]
                    )
                else:
                    out.append([self.single()])
            elif f.startswith("cb"):
                if r < 0.25:
                    out.append(self.cb_cond())
                elif self.intended and r < 0.3:
                    n = rng.randrange(1, 6)
                    a = self.space.write_at(rng, 2, n + 1) & ~1
                    out.append(
                        [CB.super(a, bytes(rng.getrandbits(8) for _ in range(2 * n)))]
                    )
                else:
                    out.append([self.single()])
            else:
                out.append([self.single()])
        return out

    def gsa_cond(self):
        _, a, v = self.space.read(self.rng)
        a &= ~1
        return GSA.if_eq(a, v & 0xFFFF) + self.single(covered=True)

    def gsa_range(self):
        _, a, v = self.space.read(self.rng)
        n = self.rng.randrange(1, 4)
        return [GSA.if_eq_range(a & ~1, v & 0xFFFF, n)] + [
            self.single(covered=True) for _ in range(n)
        ]

    def par_cond(self, depth=0):
        rng = self.rng
        kind = rng.choice(list(PAR.COND))
        w, a, v = self.space.read(rng)
        if rng.random() < 0.05:
            w = 8
        action = rng.choice(["next", "next", "next2", "block", "block"])
        if depth and action == "block":
            # A block in a block ends the outer one (in mGBA as in Torii), so
            # the outer's ELSE and ENDIF would be stray lines both reject
            action = rng.choice(["next", "next2"])
        head = PAR.cond(kind, w, a, v, action)
        if action == "next":
            return [head, self.single(covered=True)]
        if action == "next2":
            return [head, self.single(covered=True), self.single(covered=True)]

        def body(n):
            # Covered codes, now and then a conditional of their own
            out = []
            for _ in range(n):
                if depth < 2 and rng.random() < 0.25:
                    out += self.par_cond(depth + 1)
                else:
                    out.append(self.single(covered=True))
            return out

        out = [head, *body(rng.randrange(0 if self.intended else 1, 4))]
        if rng.random() < 0.5:
            out.append(PAR.else_())
            out += body(rng.randrange(1, 3))
        if not self.intended or rng.random() < 0.8:
            out.append(PAR.endif())
        return out

    def cb_cond(self):
        rng = self.rng
        if rng.random() < 0.25:
            keys = rng.sample(KEYS, rng.randrange(1, 3))
            mask = 0
            for k in keys:
                mask |= 1 << KEYS.index(k)
            return [CB.keys(mask), self.single(covered=True)]
        kind = rng.choice(["EQ", "NE", "GT", "LT", "IFAND"])
        _, a, v = self.space.read(rng)
        return [CB.op(kind, a & ~1, v & 0xFFFF), self.single(covered=True)]


def program_units(rng, fmt, space, n_cheats, budget, intended=False):
    """[(desc, [unit])]: n_cheats cheats of at most about budget units"""
    g = Gen(rng, fmt, space, intended)
    return [
        (f"{fmt} {i}", g.codes(rng.randrange(1, budget + 1))) for i in range(n_cheats)
    ]


def encode(fmt, cheats, rng=None, intended=False):
    """[(desc, [unit])] as [(desc, code text)] in format fmt: the GameShark
    formats with their marker line in each cheat, cb-enc with the key line in
    the first and its cipher carried on (as both decoders carry it)"""
    out = []
    seeds = prev_cb = None
    for i, (desc, units) in enumerate(cheats):
        enc = FORMATS[fmt]
        code = Code(enc, seeds=seeds)
        if fmt in ("gsa", "gsa-raw", "par", "par-raw"):
            code += enc.marker()
        if fmt == "cb-enc" and i == 0:
            code += CB.key(*CB_KEY)
        if fmt == "cb-enc" and i > 0:
            code.cb = prev_cb
        if intended and fmt in ("gsa", "par") and rng.random() < 0.1:
            code += enc.deadface(rng.getrandbits(16))
        for unit in units:
            for chunk in unit:
                code += chunk
        seeds = code.seeds
        prev_cb = code.cb
        out.append((desc, code.text()))
    return out


def program(rng, fmt, space, n_cheats, budget, intended=False):
    """[(desc, code text)]: n_cheats cheats of at most about budget units"""
    return encode(
        fmt, program_units(rng, fmt, space, n_cheats, budget, intended), rng, intended
    )
