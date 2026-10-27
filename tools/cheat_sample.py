#!/usr/bin/env python3
"""Writes tests/cheats/sample/, the committed cheat corpus test-host decodes
with Torii's decoders and mGBA's (tests/cheat_corpus.sh, the rules in
tests/cheat_expected.toml): seeded random cheats of every format and code
type (tools/cheat_gen.py), and the edges-*.cht files' hand-made codes for
the shapes where Torii means to differ or has a known bug. All of it is Torii's
own, made by tools/cheat_encode.py; none comes from a cheat database.

Usage: cheat_sample.py [--check]  (--check: fail if the files are stale)
"""

import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cheat_encode import CB, GSA, PAR, Code, GSARaw, PARRaw, cht
from cheat_gen import FORMATS, Space, program

OUT = Path(__file__).resolve().parent.parent / "tests" / "cheats" / "sample"
PER_FORMAT = 40


def code(fmt, *chunks, marker=True):
    c = Code(fmt)
    if marker and fmt in (GSA, GSARaw, PAR, PARRaw):
        c += fmt.marker()
    for ch in chunks:
        c += ch
    return c.text()


def edges():
    """Each a file of its own: {name: [(desc, code)]}"""
    w, iw = 0x02001000, 0x03001000
    files = {}
    files["edges-par"] = [
        (
            "open block",
            code(
                PAR,
                PAR.cond("EQ", 2, w, 5, "block"),
                PAR.write(2, w + 2, 1),
                PAR.write(1, w + 4, 2),
            ),
        ),
        (
            "empty then, else",
            code(
                PAR,
                PAR.cond("NE", 1, w, 5, "block"),
                PAR.else_(),
                PAR.write(2, w + 2, 1),
                PAR.endif(),
            ),
        ),
        (
            "C6",
            code(PAR, PAR.io(2, 0x0C, 0x1F83)),
            r"OK ASSIGN/2/0400000C/1F83/1/0/0 \|",
            None,
        ),
        (
            "C7",
            code(PAR, PAR.io(4, 0x48, 0x1B2D3F00)),
            r"OK ASSIGN/4/04000048/1B2D3F00/1/0/0 \|",
            None,
        ),
        ("slowdown", code(PAR, PAR.slowdown(0x200), PAR.write(2, w, 3))),
        (
            "disable",
            code(PAR, [(0xC8000000 | PAR.addr(w), 1)], PAR.write(2, w, 3)),
            r"OK ",
            None,
        ),
        (
            "button covered",
            code(PAR, PAR.cond("EQ", 2, w, 1), PAR.button(2, w + 6, 0x1234)),
        ),
        (
            "width-8 never",
            code(
                PAR, PAR.cond("EQ", 8, w, 1, "block"), PAR.write(4, iw, 9), PAR.endif()
            ),
        ),
        (
            "nested",
            code(
                PAR,
                PAR.cond("UGT", 1, w, 2, "block"),
                PAR.cond("AND", 2, w, 0x10),
                PAR.write(2, w + 8, 1),
                PAR.else_(),
                PAR.write(4, w + 12, 2),
                PAR.endif(),
            ),
        ),
    ]
    files["edges-par-raw"] = [
        (
            "padding",
            code(PARRaw, PAR.write(2, w, 7), [(0x80000000, 0)], PAR.write(2, w + 2, 8)),
        ),
        (
            "padding covered",
            code(
                PARRaw,
                PAR.cond("EQ", 2, w, 1, "next2"),
                PAR.write(2, w, 7),
                [(0x80000000, 0)],
                PAR.write(2, w + 2, 8),
            ),
            # next-two covers two code lines, the padding one of them
            r"OK EQ/2/02001000/1/1/0 ASSIGN/2/02001000/7/1/0/0 ASSIGN/2/02001002/8/1/0/0 \|",
            None,
        ),
    ]
    files["edges-gsa"] = [
        ("list", code(GSA, GSA.write_list(0x0203F800, [w, w + 4, w + 8]))),
        ("button covered", code(GSA, GSA.if_eq(w, 1), GSA.button(2, w + 2, 0x55))),
        (
            "slowdown",
            code(GSA, [(0x80F00000, 0x10)], GSA.write(2, w, 1)),
            r"OK .*ASSIGN/2/02001000/1/1/0/0 \|",
            None,
        ),
        ("deadface", code(GSA, GSA.deadface(0x1234), GSA.write(2, w, 0xDFDF))),
    ]
    files["edges-cb"] = [
        ("super", code(CB, CB.super(w, bytes(range(1, 9))))),
        (
            "super covered",
            code(CB, CB.op("EQ", w, 1), CB.super(w + 0x10, bytes(range(1, 5)))),
        ),
        ("super cut short", code(CB, CB.super(w, bytes(range(1, 9)))[:2])),
        ("keys", code(CB, CB.keys(0x0001), CB.write16(w, 1))),
        ("D other", code(CB, [(0xD0000010, 0x0001)], CB.write16(w, 1))),
        ("two hooks", code(CB, CB.hook(0x08000100), CB.game_id(), CB.hook(0x08000200))),
        ("placeholder", "8200100? 00XX"),
        ("vba high", "820001A8:FFFF+33000024:0002"),
    ]
    first = Code(CB)
    first += CB.key(0x9A2B3C4D, 0x5E6F)
    first += CB.write16(w, 0x1111)
    second = Code(CB)
    second += CB.key(0x91234567, 0x89AB)
    second += CB.write16(w + 2, 0x2222)
    third = Code(CB)
    third.cb = second.cb
    third += CB.write16(w + 4, 0x3333)
    files["edges-cb-keys"] = [
        ("key 1", first.text(), r"OK ASSIGN/2/02001000/1111/1/0/0 \|", None),
        ("key 2", second.text(), r"OK ASSIGN/2/02001002/2222/1/0/0 \|", None),
        (
            "after key 2",
            third.text(),
            r"OK ASSIGN/2/02001004/3333/1/0/0 \|",
            None,
        ),
    ]
    return files


def files():
    """{file name: text}, expect.tsv among them: per edge cheat, a regex
    Torii's decoding ("STATUS ops | patches") must match, as the device
    reads the code, and the known bug (tests/cheat_expected.toml) that
    keeps it from matching yet, if any"""
    out = {}
    rng = random.Random(20261026)
    for fmt in FORMATS:
        # Only what both mean alike: an intended difference (a CodeBreaker
        # super code mGBA rejects, say) can change how mGBA reads the
        # file's later cheats; edges-*.cht hold those
        out[f"random-{fmt}"] = program(rng, fmt, Space.anywhere(), PER_FORMAT, 6)
    expect = []
    for name, cheats in edges().items():
        out[name] = [c[:2] for c in cheats]
        for n, c in enumerate(cheats):
            if len(c) > 2:
                expect.append(f"{name}.cht\t{n}\t{c[2]}\t{c[3] or '-'}\n")
    texts = {f"{name}.cht": cht(cheats) + "\n" for name, cheats in out.items()}
    texts["expect.tsv"] = "".join(expect)
    return texts


def main():
    want = files()
    if sys.argv[1:] == ["--check"]:
        have = {p.name: p.read_text(encoding="utf-8") for p in OUT.iterdir()}
        if have != want:
            print("FAIL: tests/cheats/sample is stale: run tools/cheat_sample.py")
            return 1
        n = sum(t.count("_code") for t in want.values())
        print(f"PASS: tests/cheats/sample is tools/cheat_sample.py's ({n} cheats)")
        return 0
    OUT.mkdir(parents=True, exist_ok=True)
    for p in OUT.iterdir():
        p.unlink()
    for name, text in want.items():
        (OUT / name).write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
