#!/usr/bin/env python3
"""The cheat test ROM's cases, for tests/cheat_engine.sh: every code type of
every format, encrypted and raw, as a small libretro .cht each, with the keys
each needs held, run on tests/roms/cheattest in mGBA's own cheat engine and
through Torii, whose snapshots have to match. The hand-made cases below come
first; tests/cheat_matrix.py adds the type matrix (each code type at every
width and target, many cheats to a case) and the cases where mGBA reads a code
unlike the device, which mGBA runs as equivalent codes (NAME.equiv.cht);
tests/cheat_fuzz.py runs random cases the same way.

Usage:
  cheat_engine.py gen DIR [N/M]   write DIR/cases.txt (this shard's case
                                  names), DIR/NAME.cht and DIR/NAME.opts
                                  (mgba-shot options: the checkpoints' waits,
                                  dumps and keys)
  cheat_engine.py report DIR      compare the runs' dumps, print a line per
                                  case; exits 1 on a failure

The ROM (tests/roms/cheattest/main.c) snapshots every watched region on each
8th frame, right after the frame's cheats ran in both engines, and holds the
snapshot still for 8 frames; each run dumps it 3 frames after the ROM logs
the checkpoint. So the runs compare at the same ROM frames whatever Torii's
boot took, and wherever in the frame each engine ran the cheats (mGBA as the
frame ends, Torii in the VBlank interrupt, both before the ROM's main loop
wakes). Keys change 4 frames after a checkpoint, in both runs alike.

Results per case:
  same       Torii's snapshots match mGBA's at every checkpoint
  differs    they don't: a finding, with both snapshots' bytes where they
             differ (fails the test, unless the case is pending)
  pending    Torii can't run it yet (it needs the per-frame interpreter, or
             is a 16- or 32-bit write to palette, VRAM or OAM, which the
             byte table can't make): a failure since the interpreter
             landed; CHEAT_ENGINE_STRICT=0 only reports it
  mgba-off   mGBA's engine can't run it (says why); Torii's run is shown
  differs (allowed), xfail, xpass
             tests/cheat_expected.toml's [[engine]] entry for the case: an
             intended difference, or a known bug (xpass: it no longer shows)
Every case also checks its codes decode the same in mGBA's decoders and in
Torii's (tools/cheat_oracle.c, tools/cheat_corpus.c: DIR/*.tsv), or as a
rule of tests/cheat_expected.toml explains, an encrypted case the same as its
raw twin, and that mGBA's run differs from the ROM without cheats (the code
does something). A difference names the cheats whose slots it is in.
"""

import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import cheat_diff
import cheat_matrix
from cheat_encode import CB, GSA, PAR, VBA, Code, GSARaw, PARRaw, cht

# Watched regions (main.c)
E = 0x02030000
W, CNT, C, A, P, F, R = (
    E,
    E + 0x100,
    E + 0x200,
    E + 0x300,
    E + 0x400,
    E + 0x500,
    E + 0x600,
)
IW = 0x03004000
PAL, VRAM, OAM = 0x05000000, 0x06000000, 0x07000000
BG2CNT, BG3CNT, WININ = 0x0400000C, 0x0400000E, 0x04000048
KEYINPUT = 0x04000130
SNAP, SNAP_LEN = 0x02020000, 0x878
CHECKPOINTS = list(
    range(40, 112, 8)
)  # phase p = cp // 8 - 1 in the counters the cheats see there

KEYS = {
    "A": 0,
    "B": 1,
    "SELECT": 2,
    "START": 3,
    "RIGHT": 4,
    "LEFT": 5,
    "UP": 6,
    "DOWN": 7,
    "R": 8,
    "L": 9,
}


def keyinput(keys):
    """KEYINPUT while keys ("A+B") are held"""
    held = 0
    for k in keys.split("+") if keys else []:
        held |= 1 << KEYS[k]
    return 0x3FF & ~held


def phase(cp):
    return cp // 8 - 1


def symbols():
    out = {}
    for line in (ROOT / "tests/roms/cheattest/cheattest.sym").read_text().splitlines():
        parts = line.split()
        if len(parts) == 2 and re.fullmatch(r"[0-9A-Fa-f]{8}", parts[0]):
            out[parts[1]] = int(parts[0], 16)
    return out


class Case:
    def __init__(
        self,
        name,
        fmt,
        lines,
        keys=None,
        button=None,
        expect=None,
        note="",
        twin=None,
        run=True,
        effect=True,
        cheats=None,
        slots=None,
        equiv=None,
    ):
        self.name, self.fmt, self.note, self.twin, self.run = name, fmt, note, twin, run
        self.effect = effect  # False: the code rightly changes nothing (a byte store to OAM, a never-true test)
        self.keys = keys or {}  # checkpoint: keys held from 4 frames after it
        self.button = button or {}  # checkpoint: 1 or 0, the device's button
        self.expect = expect  # None: decided from Torii's decoding; "mgba-off: why"; "differs: why"
        # Where each cheat writes, [(start, end, what)], to name the cheats a
        # difference is in
        self.slots = slots or []
        # Where mGBA reads the codes unlike the device: codes mGBA reads as
        # the device reads these, [(desc, code, None)], for its run
        self.equiv = equiv
        if cheats is not None:
            # Several cheats: [(desc, code text, cheatN_format or None)]
            self.cheats = cheats
            return
        code = Code(fmt)
        if fmt in (GSA, GSARaw, PAR, PARRaw):
            code += fmt.marker()
        for chunk in lines:
            code += chunk
        self.cheats = [(name, code.text(), None)]

    def opts(self):
        out = []
        keys = "-"
        button = 0
        for cp in CHECKPOINTS:
            out.append(
                f'-w "cheattest: checkpoint {cp:04d}" -m @3:0x{SNAP:08X}:0x{SNAP_LEN:X}'
            )
            keys = self.keys.get(cp, keys)
            out.append(f"-k @4:{keys}")
            if cp in self.button:
                button = self.button[cp]
                out.append(f"-B @4:{button}")
        return "\n".join(out) + "\n"


def cases():
    sym = symbols()
    magic, magic2, table, table2, hook = (
        sym["get_magic"],
        sym["magic2_value"],
        sym["rom_table"],
        sym["rom_table2"],
        sym["frame_hook"],
    )
    out = []

    def both(name, enc, raw, build, **kw):
        out.append(Case(f"{name}-raw", raw, build(raw), **kw))
        out.append(Case(name, enc, build(enc), twin=f"{name}-raw", **kw))

    a_held = {56: "A", 72: "-"}  # A held for the snapshots of checkpoints 64 and 72
    b_held = {72: "B", 88: "-"}

    # --- GameShark v1/v2 ---
    for enc, raw in [(GSA, GSARaw)]:
        g = "gsa"
        both(f"{g}-w8", enc, raw, lambda f: [f.write(1, W + 0x11, 0x5A)])
        both(f"{g}-w16", enc, raw, lambda f: [f.write(2, W + 0x22, 0xBEEF)])
        both(f"{g}-w32", enc, raw, lambda f: [f.write(4, W + 0x34, 0xDEADBEEF)])
        both(f"{g}-w8-iwram", enc, raw, lambda f: [f.write(1, IW + 0x11, 0x77)])
        both(f"{g}-w16-iwram", enc, raw, lambda f: [f.write(2, IW + 0x20, 0x1234)])
        both(f"{g}-w8-palette", enc, raw, lambda f: [f.write(1, PAL + 7, 0x3E)])
        both(f"{g}-w16-palette", enc, raw, lambda f: [f.write(2, PAL + 4, 0x7C1F)])
        both(f"{g}-w32-palette", enc, raw, lambda f: [f.write(4, PAL + 8, 0x03E07C1F)])
        both(f"{g}-w8-vram", enc, raw, lambda f: [f.write(1, VRAM + 9, 0x5C)])
        both(f"{g}-w16-vram", enc, raw, lambda f: [f.write(2, VRAM + 6, 0xABCD)])
        both(
            f"{g}-w8-oam",
            enc,
            raw,
            lambda f: [f.write(1, OAM + 13, 0x99)],
            effect=False,
            note="a byte store to OAM does nothing, on the GBA as in mGBA",
        )
        both(f"{g}-w16-oam", enc, raw, lambda f: [f.write(2, OAM + 10, 0x4321)])
        both(f"{g}-w16-io", enc, raw, lambda f: [f.write(2, BG2CNT, 0x1F83)])
        both(
            f"{g}-list",
            enc,
            raw,
            lambda f: [f.write_list(0x0203F800, [W + 0x10, W + 0x14, W + 0x18])],
            expect="differs: mGBA's list takes its value for the first address and writes only the first count - 1 listed (Torii writes all three)",
        )
        both(
            f"{g}-list-self",
            enc,
            raw,
            lambda f: [f.write_list(W + 0x40, [W + 0x10, W + 0x14])],
            expect="differs: mGBA's list also writes its value at the value (an address), and counts it as one of the listed",
        )
        both(
            f"{g}-rom-patch",
            enc,
            raw,
            lambda f: [f.rom_patch(magic, 0x20A5), f.rom_patch(table + 4, 0x1111)],
        )
        both(
            f"{g}-button",
            enc,
            raw,
            lambda f: [f.button(1, W + 0x12, 0x42), f.button(2, W + 0x24, 0x4242)],
            button={56: 1, 72: 0},
            note="mGBA's -B holds the device's button for checkpoints 64 and 72",
        )
        both(
            f"{g}-if-eq",
            enc,
            raw,
            lambda f: [f.if_eq(CNT + 2, 6 * 0x123), f.write(2, C + 0x10, 0x600D)],
        )
        both(
            f"{g}-if-eq-keys",
            enc,
            raw,
            lambda f: [f.if_eq(KEYINPUT, keyinput("A")), f.write(2, W + 0x26, 0x0A0A)],
            keys=a_held,
        )
        both(
            f"{g}-if-eq-range",
            enc,
            raw,
            lambda f: [
                f.if_eq_range(CNT + 0, 7, 2),
                f.write(1, C + 0x20, 0x71),
                f.write(2, C + 0x22, 0x7272),
                f.write(1, C + 0x24, 0x73),
            ],
        )
        both(
            f"{g}-hook",
            enc,
            raw,
            lambda f: [f.hook(hook), f.write(2, W + 0x28, 0x4B4B)],
            note="mGBA runs it at the hook (frame_hook), Torii in its VBlank hook",
        )
    out.append(
        Case(
            "gsa-deadface",
            GSA,
            [
                GSA.deadface(0x1234),
                GSA.write(2, W + 0x2A, 0xDFDF),
                GSA.write(1, IW + 0x30, 0xDF),
            ],
            note="the lines after DEADFACE are encrypted with its seeds",
        )
    )

    # --- Action Replay v3 ---
    enc, raw = PAR, PARRaw
    g = "par"
    both(f"{g}-w8", enc, raw, lambda f: [f.write(1, W + 0x11, 0x5A)])
    both(f"{g}-w16", enc, raw, lambda f: [f.write(2, W + 0x22, 0xBEEF)])
    both(f"{g}-w32", enc, raw, lambda f: [f.write(4, W + 0x34, 0xDEADBEEF)])
    both(f"{g}-w8-iwram", enc, raw, lambda f: [f.write(1, IW + 0x11, 0x77)])
    both(f"{g}-w16-palette", enc, raw, lambda f: [f.write(2, PAL + 4, 0x7C1F)])
    both(f"{g}-w16-vram", enc, raw, lambda f: [f.write(2, VRAM + 6, 0xABCD)])
    both(
        f"{g}-w8-oam",
        enc,
        raw,
        lambda f: [f.write(1, OAM + 13, 0x99)],
        effect=False,
        note="a byte store to OAM does nothing, on the GBA as in mGBA",
    )
    both(f"{g}-fill8", enc, raw, lambda f: [f.write(1, F + 0x10, 0xF1, count=8)])
    both(f"{g}-fill16", enc, raw, lambda f: [f.write(2, F + 0x20, 0xF2F2, count=4)])
    both(f"{g}-slide1", enc, raw, lambda f: [f.slide(1, F + 0x40, 0x10, 6, 2, 3)])
    both(f"{g}-slide2", enc, raw, lambda f: [f.slide(2, F + 0x60, 0x2000, 5, 2, 0x11)])
    both(
        f"{g}-slide4",
        enc,
        raw,
        lambda f: [f.slide(4, F + 0x80, 0x40000000, 4, 1, 0x21)],
    )
    both(f"{g}-ptr8", enc, raw, lambda f: [f.write_ptr(1, P, 0x81, 3)])
    both(f"{g}-ptr16", enc, raw, lambda f: [f.write_ptr(2, P, 0x8282, 4)])
    both(f"{g}-ptr32", enc, raw, lambda f: [f.write_ptr(4, P, 0x84848484)])
    both(f"{g}-ptr-iwram", enc, raw, lambda f: [f.write_ptr(2, P + 4, 0x9292, 2)])
    both(f"{g}-add8", enc, raw, lambda f: [f.add(1, A + 0x01, 0x10)])
    both(f"{g}-add16", enc, raw, lambda f: [f.add(2, A + 0x10, 0x1234)])
    both(f"{g}-add32", enc, raw, lambda f: [f.add(4, A + 0x20, 0xF0000001)])
    both(
        f"{g}-io8",
        enc,
        raw,
        lambda f: [f.io(1, BG3CNT - 0x04000000, 0x45)],
        note="C6: mGBA's decoder (and Torii's) writes 8 bits",
    )
    both(
        f"{g}-io16",
        enc,
        raw,
        lambda f: [f.io(2, WININ - 0x04000000, 0x1B2D)],
        note="C7: 16 bits as mGBA reads it",
    )
    # Conditionals: what the counters hold at the checkpoints makes each true at some, false at others
    conds = [
        ("eq8", "EQ", 1, CNT + 0, 6),
        ("ne16", "NE", 2, CNT + 2, 6 * 0x123),
        (
            "lt32",
            "LT",
            4,
            CNT + 0x10,
            0x7FFFFFFE,
        ),  # signed: the counter goes negative at p 8
        ("gt32", "GT", 4, CNT + 0x10, 0x7FFFFFFD),
        ("ult32", "ULT", 4, CNT + 0x10, 0x7FFFFFFE),
        ("ugt32", "UGT", 4, CNT + 0x10, 0x7FFFFFFD),
        (
            "lt8",
            "LT",
            1,
            CNT + 0x0A,
            5,
        ),  # read zero-extended: (p & 15) - 8 is 0xFC.. or 0..
        ("gt16", "GT", 2, CNT + 0x08, 0x0002),
        ("and16", "AND", 2, CNT + 0x0C, 0x00C0),
        ("never", "EQ", 8, CNT + 0, 0),
    ]
    for tag, kind, width, addr, value in conds:
        both(
            f"{g}-if-{tag}",
            enc,
            raw,
            lambda f, k=kind, w=width, a=addr, v=value: [
                f.cond(k, w, a, v),
                f.write(2, C + 0x10, 0x600D),
            ],
            effect=tag != "never",
        )
    both(
        f"{g}-if-next2",
        enc,
        raw,
        lambda f: [
            f.cond("EQ", 1, CNT, 7, "next2"),
            f.write(1, C + 0x30, 0x31),
            f.write(2, C + 0x32, 0x3232),
            f.write(1, C + 0x34, 0x33),
        ],
    )
    both(
        f"{g}-if-block-else",
        enc,
        raw,
        lambda f: [
            f.cond("ULT", 1, CNT, 7, "block"),
            f.write(1, C + 0x40, 0x41),
            f.write(2, C + 0x42, 0x4242),
            f.else_(),
            f.write(2, C + 0x44, 0x4444),
            f.endif(),
            f.write(1, C + 0x46, 0x46),
        ],
    )
    both(
        f"{g}-if-nested",
        enc,
        raw,
        lambda f: [
            f.cond("UGT", 1, CNT, 5, "block"),
            f.cond("AND", 2, CNT + 0x0C, 0x0180),
            f.write(2, C + 0x50, 0x5050),
            f.write(1, C + 0x52, 0x52),
            f.else_(),
            f.cond("EQ", 1, CNT, 4),
            f.write(2, C + 0x54, 0x5454),
            f.endif(),
        ],
        note="a one-line conditional inside a block and its ELSE (blocks themselves don't nest in mGBA)",
    )
    both(
        f"{g}-if-keys",
        enc,
        raw,
        lambda f: [
            f.cond("EQ", 2, KEYINPUT, keyinput("B")),
            f.write(2, W + 0x26, 0x0B0B),
        ],
        keys=b_held,
    )
    both(
        f"{g}-if-keys-and",
        enc,
        raw,
        lambda f: [f.cond("AND", 2, KEYINPUT, 0x0001), f.write(2, W + 0x2C, 0x0C0C)],
        keys=a_held,
        note="true while A is up",
    )
    both(
        f"{g}-button",
        enc,
        raw,
        lambda f: [f.button(2, W + 0x24, 0x4242)],
        button={56: 1, 72: 0},
    )
    for slot, (addr, value) in enumerate(
        [(magic, 0x20A5), (table + 0, 0x1111), (table + 6, 0x2222), (magic2, 0xF00D)], 1
    ):
        both(
            f"{g}-rom-patch{slot}",
            enc,
            raw,
            lambda f, s=slot, a=addr, v=value: [f.rom_patch(s, a, v)],
        )
    both(f"{g}-hook", enc, raw, lambda f: [f.hook(hook), f.write(2, W + 0x28, 0x4B4B)])
    both(
        f"{g}-slowdown",
        enc,
        raw,
        lambda f: [f.slowdown(0x100), f.write(2, W + 0x2E, 0x5D5D)],
        expect="mgba-off: mGBA has no slowdown code; Torii decodes it and runs nothing yet",
        run=False,
    )
    out.append(
        Case(
            "par-deadface",
            PAR,
            [
                PAR.deadface(0x4321),
                PAR.write(2, W + 0x2A, 0xDFDF),
                PAR.write(1, IW + 0x30, 0xDF),
            ],
        )
    )

    # --- CodeBreaker ---
    def cbboth(name, build, **kw):
        out.append(Case(name, CB, build(), **kw))
        out.append(
            Case(f"{name}-enc", CB, [CB.key(0x9A2B3C4D, 0x5E6F), *build()], **kw)
        )

    cbboth("cb-w8", lambda: [CB.write8(W + 0x11, 0x5A)])
    cbboth("cb-w16", lambda: [CB.write16(W + 0x22, 0xBEEF)])
    cbboth("cb-w8-iwram", lambda: [CB.write8(IW + 0x11, 0x77)])
    cbboth("cb-w16-palette", lambda: [CB.write16(PAL + 4, 0x7C1F)])
    cbboth("cb-w8-vram", lambda: [CB.write8(VRAM + 9, 0x5C)])
    cbboth("cb-w16-vram", lambda: [CB.write16(VRAM + 6, 0xABCD)])
    cbboth("cb-w16-oam", lambda: [CB.write16(OAM + 10, 0x4321)])
    cbboth("cb-w16-io", lambda: [CB.write16(BG2CNT, 0x1F83)])
    cbboth("cb-slide", lambda: [CB.slide(F + 0x10, 0x1000, 5, 2, 0x0101)])
    cbboth("cb-slide-step4", lambda: [CB.slide(F + 0x40, 0xAAAA, 4, 4, 0)])
    cbboth(
        "cb-super",
        lambda: [CB.super(F + 0x80, bytes(range(0x51, 0x5B)))],
        expect="mgba-off: mGBA doesn't run type 5 (it rejects the line, then reads the data lines as codes)",
    )
    cbboth("cb-or", lambda: [CB.op("OR", A + 0x10, 0x8001)])
    cbboth("cb-and", lambda: [CB.op("AND", A + 0x12, 0x0FF0)])
    cbboth("cb-add", lambda: [CB.op("ADD", A + 0x14, 0x1111)])
    for tag, kind, value in [
        ("eq", "EQ", 6 * 0x123),
        ("ne", "NE", 6 * 0x123),
        ("gt", "GT", 8 * 0x123),
        ("lt", "LT", 8 * 0x123),
        ("and", "IFAND", 0x0410),
    ]:
        addr = CNT + 0x0C if kind == "IFAND" else CNT + 2
        cbboth(
            f"cb-if-{tag}",
            lambda k=kind, a=addr, v=value: [
                CB.op(k, a, v),
                CB.write16(C + 0x10, 0x600D),
            ],
        )
    cbboth(
        "cb-if-chain",
        lambda: [
            CB.op("GT", CNT + 2, 5 * 0x123),
            CB.op("LT", CNT + 2, 10 * 0x123),
            CB.write16(C + 0x12, 0x1212),
        ],
    )
    cbboth(
        "cb-keys", lambda: [CB.keys(0x0001), CB.write16(W + 0x26, 0x0A0A)], keys=a_held
    )
    cbboth(
        "cb-keys-eq",
        lambda: [CB.op("EQ", KEYINPUT, keyinput("B")), CB.write8(W + 0x27, 0x0B)],
        keys=b_held,
    )
    cbboth(
        "cb-rom",
        lambda: [CB.write8(table2 + 1, 0xAB), CB.write16(table2 + 4, 0xCDEF)],
        expect="mgba-off: mGBA's engine stores into the ROM as a CPU would, which does nothing; Torii patches the ROM",
    )
    cbboth(
        "cb-hook", lambda: [CB.game_id(), CB.hook(hook), CB.write16(W + 0x28, 0x4B4B)]
    )

    # --- VBA raw ---
    out.append(Case("vba-w8", VBA, [VBA.write(1, W + 0x11, 0x5A)]))
    out.append(Case("vba-w16", VBA, [VBA.write(2, W + 0x22, 0xBEEF)]))
    out.append(Case("vba-w32", VBA, [VBA.write(4, W + 0x34, 0xDEADBEEF)]))
    out.append(Case("vba-w16-palette", VBA, [VBA.write(2, PAL + 4, 0x7C1F)]))
    out.append(
        Case(
            "vba-w8-oam",
            VBA,
            [VBA.write(1, OAM + 13, 0x99)],
            effect=False,
            note="a byte store to OAM does nothing, on the GBA as in mGBA",
        )
    )
    out.append(
        Case(
            "vba-rom",
            VBA,
            [
                VBA.write(1, table2 + 1, 0xAB),
                VBA.write(2, table2 + 4, 0xCDEF),
                VBA.write(4, magic2, 0xCAFEF00D),
            ],
        )
    )
    out += cheat_matrix.cases(Case, sym)
    from cheat_fuzz import saved_cases

    out += saved_cases()
    return out


# --- gen ------------------------------------------------------------------


# mGBA's libretro reader (src/core/cheats.c) reads lines of up to 511
# characters, and a longer one fails the whole file: a cheatN_code line's
# code stays under this
CODE_MAX = 480


def cht_file(cheats):
    """A libretro .cht file of [(desc, code, cheatN_format or None)], every
    cheat picked; a format line is Torii's own (mGBA's reader ignores it)"""
    for d, c, _ in cheats:
        assert len(c) <= CODE_MAX, f"{d}: {len(c)} characters, past mGBA's line length"
    text = cht([(d, c) for d, c, _ in cheats])
    for i, (_, _, fmt) in enumerate(cheats):
        if fmt:
            text = text.replace(
                f"cheat{i}_enable = true",
                f'cheat{i}_enable = true\ncheat{i}_format = "{fmt}"',
            )
    return text


def shard(items, spec):
    if not spec:
        return items
    n, m = (int(x) for x in spec.split("/"))
    return [c for i, c in enumerate(items) if i % m == n - 1]


def gen(out, spec=None, every=None):
    """Writes the cases (all of cases(), or every) of this shard"""
    out.mkdir(parents=True, exist_ok=True)
    every = cases() if every is None else every
    names = [c.name for c in every]
    assert len(set(names)) == len(names), "case names repeat"
    mine = shard(every, spec)
    for c in mine:
        (out / f"{c.name}.cht").write_text(cht_file(c.cheats), encoding="utf-8")
        (out / f"{c.name}.opts").write_text(c.opts(), encoding="utf-8")
        if c.equiv:
            (out / f"{c.name}.equiv.cht").write_text(
                cht_file(c.equiv), encoding="utf-8"
            )
        if c.twin:
            twin = next(t for t in every if t.name == c.twin)
            (out / f"{twin.name}.cht").write_text(
                cht_file(twin.cheats), encoding="utf-8"
            )
    (out / "cases.txt").write_text(
        "".join(f"{c.name}\t{int(c.run)}\n" for c in mine), encoding="utf-8"
    )
    (out / "meta.json").write_text(
        json.dumps({c.name: meta(c) for c in mine}), encoding="utf-8"
    )


# --- report ---------------------------------------------------------------


def snapshots(path):
    """{checkpoint: bytes} from a run's -m dumps of the snapshot"""
    data = {}
    if not path.exists():
        return {}
    for line in path.read_text(errors="replace").splitlines():
        m = re.match(r"mem frame (\d+) ([0-9A-F]{8}):((?: [0-9A-F]{2})+)$", line)
        if m:
            frame, addr = int(m[1]), int(m[2], 16)
            data.setdefault(frame, bytearray(SNAP_LEN))
            chunk = bytes(int(b, 16) for b in m[3].split())
            data[frame][addr - SNAP : addr - SNAP + len(chunk)] = chunk
    out = {}
    for snap in data.values():
        out[int.from_bytes(snap[0x870:0x874], "little")] = bytes(snap)
    return out


def where(off):
    for base, start, end in [
        (E, 0, 0x700),
        (IW, 0x700, 0x800),
        (PAL, 0x800, 0x820),
        (VRAM, 0x820, 0x840),
        (OAM, 0x840, 0x860),
        (BG2CNT, 0x860, 0x864),
        (WININ, 0x864, 0x868),
    ]:
        if start <= off < end:
            return base + off - start
    return SNAP + off


def diff(a, b, limit=6):
    """Where two snapshots differ: 'ADDR: aa bb / cc dd' runs"""
    runs, i = [], 0
    while i < SNAP_LEN:
        if a[i] != b[i]:
            j = i
            while j < SNAP_LEN and a[j] != b[j] and j - i < 8:
                j += 1
            runs.append(f"{where(i):08X}: {a[i:j].hex(' ')} / {b[i:j].hex(' ')}")
            i = j
        else:
            i += 1
    more = f" (+{len(runs) - limit} more)" if len(runs) > limit else ""
    return "; ".join(runs[:limit]) + more


def without_keys(snap):
    """A snapshot less the ROM's two copies of KEYINPUT (SNAP + 0x874, EWRAM
    + 0x10E): a button case's Torii run holds the keys the device's button is
    (cheatcode.h's CHEAT_BUTTON_KEYS), where mGBA's holds its own button"""
    b = bytearray(snap)
    b[0x874:0x876] = b"\0\0"
    b[0x10E:0x110] = b"\0\0"
    return bytes(b)


def pending_reason(t_lower):
    """Why Torii can't run a cheat yet, from its lowering"""
    if t_lower == "TOO_BIG":
        return "over the interpreter's budget"
    return None


def slot_names(c, addrs):
    """The cheats whose slots hold addrs"""
    out = []
    for a in addrs:
        for start, end, what in c.slots:
            if start <= a < end and what not in out:
                out.append(what)
    return out


def differing_addrs(a, b):
    return [where(i) for i in range(SNAP_LEN) if a[i] != b[i]]


def decode_notes(c, decoded, rules):
    """(bad, notes) from each cheat's decodings: Torii's against mGBA's
    (tools/cheat_diff.py's rules), and an encrypted case against its raw twin"""
    bad, notes = [], []
    mgba_off = c.expect and c.expect.startswith("mgba-off")
    for n, (desc, _, _) in enumerate(c.cheats):
        d = decoded.get((f"{c.name}.cht", str(n)))
        if d is None:
            bad.append(f"{desc}: not decoded")
            continue
        if d.t_status != "OK":
            bad.append(f"{desc}: Torii decodes it {d.t_status}")
        if mgba_off or c.equiv:
            continue  # mGBA decodes other codes
        v, rid = cheat_diff.verdict(d, rules)
        if v == "mismatch":
            bad.append(
                f"{desc}: decoders differ: torii {cheat_diff.text(d.t_ops)} | {' '.join(d.t_patches)}"
                f" / mgba {d.m_status} {cheat_diff.text(d.m_ops)} | {' '.join(d.m_patches)}"
            )
        elif v in ("allow", "xfail"):
            notes.append(f"{desc}: decodes unlike mGBA's as {v} rule {rid} says")
        if c.twin:
            tw = decoded.get((f"{c.twin}.cht", str(n)))
            if tw is None or (tw.t_ops, tw.t_patches) != (d.t_ops, d.t_patches):
                bad.append(f"{desc}: decodes unlike its raw twin {c.twin}")
    return bad, notes


def report(out):
    """Compares each case's runs (out/meta.json lists the cases gen wrote);
    exits 1 on a failure"""
    strict = os.environ.get("CHEAT_ENGINE_STRICT", "1") != "0"
    meta = json.loads((out / "meta.json").read_text(encoding="utf-8"))
    expected = cheat_diff.load_expected()
    rules, engine = expected["decode"], expected["engine"]
    chts = sorted(out.glob("*.cht"))
    decoded = {
        (d.file, d.n): d
        for d in cheat_diff.cheats(chts, out / "torii.tsv", out / "mgba.tsv")
    }
    counts, failed = {}, 0
    for line in (out / "cases.txt").read_text().splitlines():
        name, run = line.split("\t")
        c = Meta(meta[name])
        bad, notes = decode_notes(c, decoded, rules)
        reason = None
        for n in range(len(c.cheats)):
            d = decoded.get((f"{name}.cht", str(n)))
            reason = reason or (d and pending_reason(d.t_lower))
        mgba_off = c.expect and c.expect.startswith("mgba-off")
        differ = []
        if run == "0":
            result = "not run"
            notes.append(c.expect.split(": ", 1)[1] if c.expect else "")
        else:
            base = snapshots(out / f"{name}.base.txt")
            mg = snapshots(out / f"{name}.mgba.txt")
            to = snapshots(out / f"{name}.torii.txt")
            if c.button:
                mg = {cp: without_keys(v) for cp, v in mg.items()}
                to = {cp: without_keys(v) for cp, v in to.items()}
                base = {cp: without_keys(v) for cp, v in base.items()}
            log_path = out / f"{name}.torii.log"
            torii_log = (
                log_path.read_text(errors="replace") if log_path.exists() else ""
            )
            missing = [
                cp
                for cp in CHECKPOINTS
                if cp not in mg or cp not in to or cp not in base
            ]
            if missing:
                bad.append(
                    f"no snapshot at checkpoints {missing} (mgba {sorted(mg)}, torii {sorted(to)})"
                )
                result = "differs"
            else:
                if (
                    not mgba_off
                    and c.effect
                    and all(mg[cp] == base[cp] for cp in CHECKPOINTS)
                ):
                    bad.append(
                        "mGBA's run shows no effect: the code does nothing there"
                    )
                differ = [cp for cp in CHECKPOINTS if mg[cp] != to[cp]]
                left_out = re.search(
                    r"cheats: cheat \d+ \(.*?\): (.*?), left out", torii_log
                )
                if left_out:
                    reason = reason or f"Torii leaves it out ({left_out[1]})"
                if not differ:
                    result = "same"
                elif mgba_off:
                    result = "mgba-off"
                elif c.expect and c.expect.startswith("differs"):
                    result = "differs (known)"
                    notes.append(c.expect.split(": ", 1)[1])
                elif reason and not strict:
                    result = "pending"
                else:
                    result = "differs"
                if differ and result != "same":
                    cp = differ[0]
                    who = slot_names(c, differing_addrs(mg[cp], to[cp]))
                    notes.append(
                        f"at checkpoints {differ}; at {cp}, mgba / torii: {diff(mg[cp], to[cp])}"
                        + (f"; in {', '.join(who)}" if who else "")
                    )
                    if mgba_off:
                        notes.insert(0, c.expect.split(": ", 1)[1])
                        tb = [cp for cp in CHECKPOINTS if to[cp] != base[cp]]
                        notes.append(
                            f"Torii's run changes {(tb and diff(base[tb[0]], to[tb[0]])) or 'nothing'}"
                        )
                if reason and result == "pending":
                    notes.insert(0, reason)
        if bad:
            notes = bad + notes
            if result != "differs (known)":
                result = "differs"
        # tests/cheat_expected.toml's word on the case
        if name in engine:
            kind, why = engine[name]
            if kind == "allow" and result == "differs":
                result = "differs (allowed)"
                notes.insert(0, why)
            elif kind == "xfail":
                result = "xfail" if result == "differs" else "xpass"
                notes.insert(0, f"{why}: {expected['bugs'][why]}")
        counts[result] = counts.get(result, 0) + 1
        failed += result == "differs"
        tag = (
            "FAIL" if result == "differs" else "SKIP" if result == "pending" else "PASS"
        )
        detail = "; ".join(n for n in notes if n)
        print(
            f"{tag}: cheat_engine: {name} ({c.fmt}): {result}{': ' + detail if detail else ''}"
        )
    print("cheat_engine: " + ", ".join(f"{n} {k}" for k, n in sorted(counts.items())))
    return 1 if failed else 0


class Meta:
    """What report needs of a case, as gen saved it"""

    def __init__(self, d):
        self.__dict__.update(d)
        self.slots = [tuple(s) for s in self.slots]
        self.button = {int(k): v for k, v in self.button.items()}


def meta(c):
    return {
        "name": c.name,
        "fmt": c.fmt.name,
        "expect": c.expect,
        "effect": c.effect,
        "button": c.button,
        "twin": c.twin,
        "slots": c.slots,
        "cheats": c.cheats,
        "equiv": bool(c.equiv),
    }


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "gen":
        gen(Path(sys.argv[2]), sys.argv[3] if len(sys.argv) > 3 else None)
    elif len(sys.argv) == 3 and sys.argv[1] == "report":
        sys.exit(report(Path(sys.argv[2])))
    else:
        sys.exit(__doc__)
