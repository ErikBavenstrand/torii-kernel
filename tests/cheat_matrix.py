"""The cheat type matrix for tests/cheat_engine.py: every code type of every
format, at every width it has, writing to every kind of memory the cheat
test ROM watches (EWRAM, IWRAM, palette, VRAM, OAM, IO), aligned and not;
fills, slides, pointer writes, add, or and and; every conditional's
comparator at every width with each of its actions (next code, next two, a
block, a block with an ELSE), nested blocks, the device's button and
CodeBreaker's key tests; ROM patches. Each case is one .cht of several
cheats, each writing to slots of its own (Case.slots names the cheat a
difference is in), run in mGBA's engine and through Torii like the hand-made
cases.
"""

from typing import ClassVar

from cheat_encode import CB, GSA, PAR, VBA, Code, GSARaw, PARRaw

E = 0x02030000
W, CNT, C, A, P, F = E, E + 0x100, E + 0x200, E + 0x300, E + 0x400, E + 0x500
IW = 0x03004000
PAL, VRAM, OAM = 0x05000000, 0x06000000, 0x07000000
KEYINPUT = 0x04000130

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
KEYS = ["A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"]


def keyinput(keys):
    held = 0
    for k in keys.split("+") if keys else []:
        held |= 1 << KEYS.index(k)
    return 0x3FF & ~held


class Slots:
    """Hands out each cheat's addresses in the watched regions"""

    REGIONS: ClassVar[dict] = {
        "ewram": (W, 0x100),
        "cond": (C, 0x100),
        "arith": (A, 0x100),
        "fill": (F, 0x100),
        "iwram": (IW, 0x100),
        "palette": (PAL, 0x20),
        "vram": (VRAM, 0x20),
        "oam": (OAM, 0x20),
    }

    def __init__(self):
        self.next = {k: v[0] for k, v in self.REGIONS.items()}
        self.taken = []

    def get(self, region, size, what, align=4):
        a = (self.next[region] + align - 1) & ~(align - 1)
        start, length = self.REGIONS[region]
        assert a + size <= start + length, (region, what)
        self.next[region] = a + size
        self.taken.append((a, a + size, what))
        return a

    def mark(self, start, size, what):
        self.taken.append((start, start + size, what))


def build(fmt_key, cheats):
    """[(desc, chunks)] as [(desc, code text, None)] in format fmt_key, the
    GameShark formats with their marker line in each cheat, cb-enc with the
    key line in the first and its cipher carried on"""
    enc = FORMATS[fmt_key]
    out, cb = [], None
    for i, (desc, chunks) in enumerate(cheats):
        code = Code(enc)
        if enc in (GSA, GSARaw, PAR, PARRaw):
            code += enc.marker()
        if fmt_key == "cb-enc":
            if i == 0:
                code += CB.key(*CB_KEY)
            else:
                code.cb = cb
        for ch in chunks:
            code += ch
        cb = code.cb
        out.append((desc, code.text(), None))
    return out


def write_of(fmt_key, width, address, value):
    enc = FORMATS[fmt_key]
    if enc is CB:
        return CB.write8(address, value) if width == 1 else CB.write16(address, value)
    return enc.write(width, address, value)


def widths(fmt_key):
    return (1, 2) if fmt_key.startswith("cb") else (1, 2, 4)


VALUES = {1: 0xA5, 2: 0xC3D2, 4: 0x8E7F6A5B}


def writes(Case, fmt_key):
    """Each width to each region, aligned and (for a halfword or word) not"""
    s = Slots()
    cheats = []
    for region in ("ewram", "iwram", "palette", "vram", "oam"):
        for w in widths(fmt_key):
            for odd in (0, 1, 3) if w > 1 else (0,):
                if odd == 3 and w != 4:
                    continue
                what = f"w{w}{'+' + str(odd) if odd else ''} {region}"
                # an unaligned store lands aligned down, in the same w bytes
                a = s.get(region, w, what, align=w) + odd
                cheats.append((what, [write_of(fmt_key, w, a, VALUES[w] ^ odd)]))
    io = [(1, 0x0400000C), (2, 0x0400000E), (4, 0x04000048)]
    for w, a in io:
        if w in widths(fmt_key):
            s.mark(a, w, f"w{w} io")
            cheats.append(
                (f"w{w} io", [write_of(fmt_key, w, a, VALUES[w] & 0x1F1F1F1F)])
            )
    return Case(
        f"m-{fmt_key}-writes",
        FORMATS[fmt_key],
        None,
        cheats=build(fmt_key, cheats),
        slots=s.taken,
        note="every width to every watched region; a byte store to OAM does nothing",
    )


def par_ops(Case, fmt_key):
    enc = FORMATS[fmt_key]
    s = Slots()
    cheats = []
    for w, n in ((1, 8), (2, 4)):
        for region in ("fill", "vram"):
            what = f"fill w{w}x{n} {region}"
            a = s.get(region, w * n, what, align=w)
            cheats.append((what, [enc.write(w, a, VALUES[w] & 0x7F7F, count=n)]))
    for w, n, step, vstep in (
        (1, 6, 2, 3),
        (2, 5, 1, 0x11),
        (4, 3, 1, 0x21),
        (2, 4, 3, 0),
    ):
        what = f"slide w{w}x{n} step {step} +{vstep}"
        a = s.get("fill", w * (n * step + 1), what, align=w)
        cheats.append((what, [enc.slide(w, a, VALUES[w], n, step, vstep)]))
    for w in (1, 2, 4):
        for ptr, room, region in ((P, 0x10, "ewram"), (P + 4, 8, "iwram")):
            off = (room - w) // w * w if w < 4 else 0
            what = f"ptr w{w} {region}"
            cheats.append((what, [enc.write_ptr(w, ptr, VALUES[w], off)]))
        s.mark(0x02030480, 0x40, "ptr ewram")
        s.mark(0x03004080, 0x10, "ptr iwram")
    for w, v in (
        (1, 0x10),
        (1, 0xF0),
        (2, 0x1234),
        (2, 0xFFFF),
        (4, 0xF0000001),
        (4, 0x00010000),
    ):
        for region in ("arith", "iwram"):
            what = f"add w{w} {v:X} {region}"
            a = s.get(region, w, what, align=w)
            cheats.append((what, [enc.add(w, a, v)]))
    what = "add w2 palette"
    cheats.append((what, [enc.add(2, s.get("palette", 2, what, align=2), 0x0421)]))
    return Case(
        f"m-{fmt_key}-ops", enc, None, cheats=build(fmt_key, cheats), slots=s.taken
    )


def cb_ops(Case, fmt_key):
    s = Slots()
    cheats = []
    for n, step, vstep in ((5, 2, 0x0101), (4, 4, 0), (3, 6, 0xFFFF)):
        what = f"slide x{n} step {step} +{vstep:X}"
        a = s.get("fill", n * step + 2, what, align=2)
        cheats.append((what, [CB.slide(a, 0x1000, n, step, vstep)]))
    for kind, v in (("OR", 0x8001), ("AND", 0x0FF0), ("ADD", 0x1111), ("ADD", 0xFFFF)):
        for region in ("arith", "iwram", "palette"):
            what = f"{kind} {v:X} {region}"
            a = s.get(region, 2, what, align=2)
            cheats.append((what, [CB.op(kind, a, v)]))
    return Case(
        f"m-{fmt_key}-ops", CB, None, cheats=build(fmt_key, cheats), slots=s.taken
    )


# Conditionals: per width, what to read and compare with so the test is
# true at some checkpoints and false at others (the ROM's counters, main.c).
# A signed test narrower than a word compares at its width in Torii, as the
# devices do, where mGBA zero-extends: those read counters that stay
# positive here, and the unsigned ones the negative counters; k-*-signed-*
# check signed narrow tests against equivalent unsigned ones.
COND_READS = {
    1: {
        "EQ": (CNT, 8),
        "NE": (CNT, 8),
        "AND": (CNT, 4),
        "LT": (CNT, 8),
        "GT": (CNT, 8),
        "ord": (CNT + 0xA, 2),
    },
    2: {
        "EQ": (CNT + 2, 8 * 0x123),
        "NE": (CNT + 2, 8 * 0x123),
        "AND": (CNT + 0xC, 0x0180),
        "LT": (CNT + 2, 8 * 0x123),
        "GT": (CNT + 2, 8 * 0x123),
        "ord": (CNT + 8, 2),
    },
    4: {
        "EQ": (CNT + 4, 8 * 0x1234567),
        "NE": (CNT + 4, 8 * 0x1234567),
        "AND": (CNT + 4, 0x01000000),
        "ord": (CNT + 0x10, 0x7FFFFFFE),
    },
}


def cond_read(width, kind):
    r = COND_READS[width]
    return r[kind] if kind in r else r["ord"]


def par_conds(Case, fmt_key):
    enc = FORMATS[fmt_key]
    out = []
    for width in (1, 2, 4):
        for action in ("next", "next2", "block", "block-else"):
            s = Slots()
            cheats = []
            for kind in PAR.COND:
                what = f"if-{kind} w{width} {action}"
                slot = s.get("cond", 8, what)
                addr, value = cond_read(width, kind)
                cheats.append(
                    (what, par_cond_body(enc, kind, width, addr, value, action, slot))
                )
            out.append(
                Case(
                    f"m-{fmt_key}-if-w{width}-{action}",
                    enc,
                    None,
                    cheats=build(fmt_key, cheats),
                    slots=s.taken,
                )
            )
    # Width 8: never true, each action
    s = Slots()
    cheats = []
    for action in ("next", "next2", "block", "block-else"):
        what = f"if-never {action}"
        slot = s.get("cond", 8, what)
        cheats.append((what, par_cond_body(enc, "EQ", 8, CNT, 0, action, slot)))
    out.append(
        Case(
            f"m-{fmt_key}-if-never",
            enc,
            None,
            cheats=build(fmt_key, cheats),
            slots=s.taken,
        )
    )
    # Keys, nesting
    s = Slots()
    cheats = []
    for kind, keys in (("EQ", "A"), ("NE", "B"), ("AND", "")):
        what = f"if-{kind} keys"
        slot = s.get("cond", 8, what)
        value = 0x0001 if kind == "AND" else keyinput(keys)
        cheats.append(
            (what, par_cond_body(enc, kind, 2, KEYINPUT, value, "block-else", slot))
        )
    what = "nested next in block and else"
    slot = s.get("cond", 16, what)
    cheats.append(
        (
            what,
            [
                PAR.cond("UGT", 1, CNT, 5, "block"),
                PAR.cond("AND", 2, CNT + 0xC, 0x0180),
                PAR.write(2, slot, 0x5050),
                PAR.write(1, slot + 2, 0x52),
                PAR.else_(),
                PAR.cond("EQ", 1, CNT, 4, "next2"),
                PAR.write(2, slot + 4, 0x5454),
                PAR.write(2, slot + 6, 0x5656),
                PAR.write(2, slot + 8, 0x5858),
                PAR.endif(),
                PAR.write(2, slot + 10, 0x5A5A),
            ],
        )
    )
    what = "block in block"
    slot = s.get("cond", 16, what)
    cheats.append(
        (
            what,
            [
                PAR.cond("ULT", 1, CNT, 9, "block"),
                PAR.write(2, slot, 0x6060),
                PAR.cond("UGT", 1, CNT, 6, "block"),
                PAR.write(2, slot + 2, 0x6262),
                PAR.else_(),
                PAR.write(2, slot + 4, 0x6464),
                PAR.endif(),
                PAR.write(2, slot + 6, 0x6666),
            ],
        )
    )
    what = "next2 over fill and ptr"
    slot = s.get("cond", 16, what)
    cheats.append(
        (
            what,
            [
                PAR.cond("NE", 1, CNT, 7, "next2"),
                PAR.write(1, slot, 0x77, count=4),
                PAR.write_ptr(2, P, 0x7878, 2),
                PAR.write(2, slot + 8, 0x7979),
            ],
        )
    )
    s.mark(0x02030480, 0x40, "next2 over fill and ptr")
    out.append(
        Case(
            f"m-{fmt_key}-if-keys-nested",
            enc,
            None,
            cheats=build(fmt_key, cheats),
            slots=s.taken,
            keys={56: "A", 72: "B", 88: "-"},
        )
    )
    return out


def par_cond_body(enc, kind, width, addr, value, action, slot):
    v = 0x1000 | (slot & 0xFF)
    act = "block" if action.startswith("block") else action
    body = [enc.cond(kind, width, addr, value, act), enc.write(2, slot, v)]
    if action in ("next2", "block", "block-else"):
        body.append(enc.write(2, slot + 2, v + 1))
    if action == "block-else":
        body += [enc.else_(), enc.write(2, slot + 4, v + 2)]
    if action.startswith("block"):
        body.append(enc.endif())
    body.append(enc.write(2, slot + 6, v + 3))  # after what the test covers
    return body


def gsa_conds(Case, fmt_key):
    enc = FORMATS[fmt_key]
    s = Slots()
    cheats = []
    for addr, value, what in (
        (CNT + 2, 8 * 0x123, "if-eq counter"),
        (CNT + 0xC, 0x0100, "if-eq bit"),
        (KEYINPUT, keyinput("A"), "if-eq keys"),
    ):
        slot = s.get("cond", 8, what)
        cheats.append(
            (
                what,
                [
                    enc.if_eq(addr, value),
                    enc.write(2, slot, 0x0D0D),
                    enc.write(2, slot + 2, 0x0E0E),
                ],
            )
        )
    for n in (1, 2, 3):
        what = f"if-eq-range {n}"
        slot = s.get("cond", 8, what)
        body = [enc.if_eq_range(CNT + 0, 9, n)]
        body += [
            enc.write(1 if i == 1 else 2, slot + 2 * i, 0x20 + i) for i in range(n)
        ]
        body.append(enc.write(2, slot + 6, 0x2F2F))
        cheats.append((what, body))
    what = "if-eq w4 covered"
    slot = s.get("cond", 8, what)
    cheats.append(
        (
            what,
            [
                enc.if_eq(CNT + 2, 9 * 0x123),
                enc.write(4, slot, 0x44332211),
                enc.write(1, slot + 5, 0x55),
            ],
        )
    )
    return Case(
        f"m-{fmt_key}-if",
        enc,
        None,
        cheats=build(fmt_key, cheats),
        slots=s.taken,
        keys={56: "A", 72: "-"},
    )


def cb_conds(Case, fmt_key):
    s = Slots()
    cheats = []
    for kind, addr, value in (
        ("EQ", CNT + 2, 8 * 0x123),
        ("NE", CNT + 2, 8 * 0x123),
        ("GT", CNT + 2, 8 * 0x123),
        ("LT", CNT + 2, 8 * 0x123),
        ("IFAND", CNT + 0xC, 0x0180),
        ("EQ", KEYINPUT, keyinput("B")),
    ):
        what = f"if-{kind} {addr:X}"
        slot = s.get("cond", 8, what)
        cheats.append(
            (
                what,
                [
                    CB.op(kind, addr, value),
                    CB.write16(slot, 0x0C0C),
                    CB.write8(slot + 2, 0x0D),
                ],
            )
        )
    for mask, what in (
        (0x0001, "keys A"),
        (0x0003, "keys A+B"),
        (0x0104, "keys R+SELECT"),
    ):
        slot = s.get("cond", 8, what)
        cheats.append(
            (
                what,
                [CB.keys(mask), CB.write16(slot, 0x0E0E), CB.write16(slot + 2, 0x0F0F)],
            )
        )
    what = "chain"
    slot = s.get("cond", 8, what)
    cheats.append(
        (
            what,
            [
                CB.op("GT", CNT + 2, 5 * 0x123),
                CB.op("LT", CNT + 2, 10 * 0x123),
                CB.write16(slot, 0x1212),
                CB.write16(slot + 2, 0x1313),
            ],
        )
    )
    what = "if over slide"
    slot = s.get("cond", 16, what)
    cheats.append(
        (
            what,
            [
                CB.op("NE", CNT + 2, 7 * 0x123),
                CB.slide(slot, 0x1400, 4, 2, 1),
                CB.write16(slot + 10, 0x1515),
            ],
        )
    )
    return Case(
        f"m-{fmt_key}-if",
        CB,
        None,
        cheats=build(fmt_key, cheats),
        slots=s.taken,
        keys={48: "A", 64: "A+B", 80: "B", 96: "-"},
    )


def buttons(Case, fmt_key):
    enc = FORMATS[fmt_key]
    s = Slots()
    cheats = []
    ws = (1, 2) if enc in (GSA, GSARaw) else (1, 2, 4)
    for w in ws:
        for region in ("ewram", "iwram"):
            what = f"button w{w} {region}"
            a = s.get(region, w, what, align=w)
            cheats.append((what, [enc.button(w, a, VALUES[w])]))
    return Case(
        f"m-{fmt_key}-button",
        enc,
        None,
        cheats=build(fmt_key, cheats),
        slots=s.taken,
        button={48: 1, 64: 0, 80: 1, 88: 0},
    )


def rom(Case, fmt_key, sym):
    enc = FORMATS[fmt_key]
    t1, t2 = sym["rom_table"], sym["rom_table2"]
    cheats = []
    if enc in (PAR, PARRaw):
        for slot, (a, v) in enumerate(
            ((t1 + 2, 0x1A1A), (t1 + 8, 0x2B2B), (t2 + 0, 0x3C3C), (t2 + 14, 0x4D4D)), 1
        ):
            cheats.append((f"patch{slot}", [PAR.rom_patch(slot, a, v)]))
    elif enc in (GSA, GSARaw):
        for a, v in (
            (t1 + 2, 0x1A1A),
            (t1 + 8, 0x2B2B),
            (t2 + 0, 0x3C3C),
            (t2 + 14, 0x4D4D),
            (sym["get_magic"], 0x2077),
        ):
            cheats.append((f"patch {a:X}", [GSA.rom_patch(a, v)]))
    else:
        for w, a, v in (
            (1, t1 + 3, 0xAB),
            (2, t1 + 6, 0xCDEF),
            (4, t2 + 8, 0x01234567),
        ):
            cheats.append((f"rom w{w} {a:X}", [VBA.write(w, a, v)]))
    return Case(f"m-{fmt_key}-rom", enc, None, cheats=build(fmt_key, cheats))


def cases(Case, sym):
    out = []
    for fmt_key in FORMATS:
        out.append(writes(Case, fmt_key))
        if fmt_key.startswith("par"):
            out.append(par_ops(Case, fmt_key))
            out += par_conds(Case, fmt_key)
            out.append(buttons(Case, fmt_key))
            out.append(rom(Case, fmt_key, sym))
        elif fmt_key.startswith("gsa"):
            out.append(gsa_conds(Case, fmt_key))
            out.append(buttons(Case, fmt_key))
            out.append(rom(Case, fmt_key, sym))
        elif fmt_key.startswith("cb"):
            out.append(cb_ops(Case, fmt_key))
            out.append(cb_conds(Case, fmt_key))
        else:
            out.append(rom(Case, fmt_key, sym))
    return out + known(Case, sym)


def known(Case, sym):
    """Codes where mGBA or Torii reads unlike the device: each runs in Torii
    against mGBA's run of codes it reads as the device reads them (equiv),
    or against mGBA's own run where mGBA is right. tests/cheat_expected.toml
    names the known bugs among them."""
    out = []
    t2 = sym["rom_table2"]

    def case(name, fmt, cheats, equiv=None, **kw):
        out.append(Case(name, fmt, None, cheats=cheats, equiv=equiv, **kw))

    for key, enc in (("par", PAR), ("par-raw", PARRaw)):
        case(
            f"k-{key}-empty-then-else",
            enc,
            build(
                key,
                [
                    (
                        "empty then",
                        [
                            enc.cond("EQ", 1, CNT, 8, "block"),
                            enc.else_(),
                            enc.write(2, C, 0x1111),
                            enc.endif(),
                            enc.write(2, C + 2, 0x2222),
                        ],
                    )
                ],
            ),
            build(
                key,
                [
                    (
                        "as next",
                        [
                            enc.cond("NE", 1, CNT, 8),
                            enc.write(2, C, 0x1111),
                            enc.write(2, C + 2, 0x2222),
                        ],
                    )
                ],
            ),
            note="an empty THEN: the ELSE runs while the condition is false",
        )
        case(
            f"k-{key}-c6",
            enc,
            build(key, [("C6", [enc.io(2, 0x0E, 0x1F83)])]),
            build(key, [("w2", [enc.write(2, 0x0400000E, 0x1F83)])]),
            note="C6 is a 16-bit IO write on the device",
        )
        case(
            f"k-{key}-c7",
            enc,
            build(key, [("C7", [enc.io(4, 0x48, 0x1B2D3F1E)])]),
            build(key, [("w4", [enc.write(4, 0x04000048, 0x1B2D3F1E)])]),
            note="C7 is a 32-bit IO write on the device",
        )
    case(
        "k-par-raw-padding-next2",
        PARRaw,
        build(
            "par-raw",
            [
                (
                    "next2 over padding",
                    [
                        PAR.cond("EQ", 1, CNT, 8, "next2"),
                        PAR.write(2, C, 0x1111),
                        [(0x80000000, 0)],
                        PAR.write(2, C + 2, 0x2222),
                    ],
                )
            ],
        ),
        note="next-two covers the write and the padding line, not the write after",
    )
    for key, enc in (("gsa", GSA), ("gsa-raw", GSARaw)):
        case(
            f"k-{key}-slowdown",
            enc,
            build(
                key,
                [
                    (
                        "slowdown",
                        [[(0x80F00000, 0x0010)], enc.write(2, W + 0x30, 0x5D5D)],
                    )
                ],
            ),
            build(key, [("write", [enc.write(2, W + 0x30, 0x5D5D)])]),
            note="a GameShark slowdown line does nothing on a flash cart; the rest runs",
        )
    # Signed tests narrower than a word: at their width on the devices (and
    # in Torii); mGBA zero-extends, so it runs unsigned tests on the ROM's
    # matching positive counter instead (s8/s16 at CNT + 0xA/8 are p - 8)
    for key, enc in (("par", PAR), ("par-raw", PARRaw)):
        case(
            f"k-{key}-signed-lt8",
            enc,
            build(
                key,
                [
                    (
                        "s8 < 2",
                        [enc.cond("LT", 1, CNT + 0xA, 2), enc.write(2, C, 0x1111)],
                    )
                ],
            ),
            build(
                key,
                [("p < 10", [enc.cond("ULT", 1, CNT, 10), enc.write(2, C, 0x1111)])],
            ),
        )
        case(
            f"k-{key}-signed-gt16",
            enc,
            build(
                key,
                [
                    (
                        "s16 > -2",
                        [enc.cond("GT", 2, CNT + 8, 0xFFFE), enc.write(2, C, 0x1111)],
                    )
                ],
            ),
            build(
                key, [("p > 6", [enc.cond("UGT", 1, CNT, 6), enc.write(2, C, 0x1111)])]
            ),
        )
    case(
        "k-cb-signed",
        CB,
        build(
            "cb",
            [
                ("s16 > 2", [CB.op("GT", CNT + 8, 2), CB.write16(C, 0x1111)]),
                ("s16 < -2", [CB.op("LT", CNT + 8, 0xFFFE), CB.write16(C + 2, 0x2222)]),
            ],
        ),
        build(
            "cb",
            [
                ("p > 10", [CB.op("GT", CNT + 2, 10 * 0x123), CB.write16(C, 0x1111)]),
                ("p < 6", [CB.op("LT", CNT + 2, 6 * 0x123), CB.write16(C + 2, 0x2222)]),
            ],
        ),
    )
    # A DEADFACE line in a cheat Torii reads in its own format (cheatN_format)
    seeded = Code(PAR)
    seeded += PAR.marker()
    seeded += PAR.deadface(0x4321)
    seeded += PAR.write(2, W + 0x2A, 0xDFDF)
    after = Code(PAR, seeds=seeded.seeds)
    after += PAR.write(1, IW + 0x30, 0xDF)
    case(
        "k-par-deadface-format",
        PAR,
        [("reseed", seeded.text(), "PARv3"), ("after the reseed", after.text(), None)],
        note="the DEADFACE line's seeds decode the file's later cheats",
    )
    # A second CodeBreaker key line
    first = Code(CB)
    first += CB.key(0x9A2B3C4D, 0x5E6F)
    first += CB.write16(W + 0x40, 0x4141)
    second = Code(CB)
    second += CB.key(0x91234567, 0x89AB)
    second += CB.write16(W + 0x42, 0x4242)
    third = Code(CB)
    third.cb = second.cb
    third += CB.write16(W + 0x44, 0x4343)
    case(
        "k-cb-second-key",
        CB,
        [
            ("key 1", first.text(), None),
            ("key 2", second.text(), None),
            ("after key 2", third.text(), None),
        ],
        build(
            "cb",
            [
                (
                    "plain",
                    [
                        CB.write16(W + 0x40, 0x4141),
                        CB.write16(W + 0x42, 0x4242),
                        CB.write16(W + 0x44, 0x4343),
                    ],
                )
            ],
        ),
        note="a key line's cipher decodes the cheats after it",
    )
    # Unaligned ROM writes: a CPU store aligns down, as mGBA's patches do
    case(
        "k-vba-rom-odd",
        VBA,
        build(
            "vba",
            [
                ("w2 odd", [VBA.write(2, t2 + 1, 0x8899)]),
                ("w4 +2", [VBA.write(4, t2 + 10, 0x01234567)]),
            ],
        ),
        note="a halfword or word written at an odd ROM address",
    )
    return out
