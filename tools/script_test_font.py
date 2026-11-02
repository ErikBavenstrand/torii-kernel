#!/usr/bin/env python3
"""The script font the tests use (the pseudo-locale en-XA's): BDFs whose glyphs
are numbered boxes, made from the code point alone, so a character from a script
the menu fonts lack draws something recognisable (a box with a pattern that
differs from every other character's) and the tests need no real font.

usage: script_test_font.py [--check]   writes assets/fonts/test/*.bdf, or checks
                                       they are what this makes

Greek (U+0391-03C9) and Cyrillic (U+0410-044F) letters are 6 px boxes, kana
(U+3041-3096) 10 px ones (2 byte rows), and U+0141 a Latin Extended-A letter
the menu fonts fold to L: the script font's own glyph wins. The body font's
glyphs are 8 rows tall, the heading font's 11, so both draw in their lines.
"""

import sys
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "assets" / "fonts" / "test"
RANGES = [(0x141, 0x141), (0x391, 0x3C9), (0x410, 0x44F), (0x3041, 0x3096)]
# (file, glyph height, narrow width, wide width)
FONTS = [("script-test-body.bdf", 8, 6, 10), ("script-test-heading.bdf", 11, 8, 12)]


def glyph(cp: int, height: int, narrow: int, wide: int) -> list[int]:
    """Rows of a box with a pattern inside, bit 0 the left; a different one
    for each code point."""
    width = wide if cp >= 0x3000 else narrow
    state = (cp * 2654435761) & 0xFFFFFFFF or 1
    rows = []
    for y in range(height):
        row = 0
        for x in range(width):
            edge = y in (0, height - 1) or x in (0, width - 1)
            state ^= (state << 13) & 0xFFFFFFFF
            state ^= state >> 17
            state ^= (state << 5) & 0xFFFFFFFF
            if edge or (state & 3 == 0 and 0 < y < height - 1 and 0 < x < width - 1):
                row |= 1 << x
        rows.append(row)
    return rows


def make(height: int, narrow: int, wide: int) -> str:
    cps = [cp for a, b in RANGES for cp in range(a, b + 1)]
    out = [
        "STARTFONT 2.1",
        f"FONT -torii-script-test-{height}",
        f"SIZE {height} 75 75",
        f"FONTBOUNDINGBOX {wide} {height} 0 0",
        "STARTPROPERTIES 2",
        f"FONT_ASCENT {height}",
        "FONT_DESCENT 0",
        "ENDPROPERTIES",
        f"CHARS {len(cps)}",
    ]
    for cp in cps:
        width = wide if cp >= 0x3000 else narrow
        digits = 2 * ((width + 7) // 8)
        out += [
            f"STARTCHAR U+{cp:04X}",
            f"ENCODING {cp}",
            "SWIDTH 500 0",
            f"DWIDTH {width + 1} 0",
            f"BBX {width} {height} 0 0",
            "BITMAP",
        ]
        for row in reversed(glyph(cp, height, narrow, wide)):
            # BDF rows run top down, the leftmost pixel the highest bit
            bits = sum(1 << (digits * 4 - 1 - x) for x in range(width) if row >> x & 1)
            out.append(f"{bits:0{digits}X}")
        out.append("ENDCHAR")
    out.append("ENDFONT")
    return "\n".join(out) + "\n"


def main(argv: list[str]) -> int:
    check = "--check" in argv
    bad = False
    for name, height, narrow, wide in FONTS:
        text = make(height, narrow, wide)
        path = OUT / name
        if check:
            if not path.exists() or path.read_text() != text:
                print(f"{path} is stale: run `just script-test-font`", file=sys.stderr)
                bad = True
        else:
            OUT.mkdir(parents=True, exist_ok=True)
            path.write_text(text, newline="\n")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
