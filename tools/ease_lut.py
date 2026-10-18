#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# ///
"""Write the easing tables for the motion engine (source/engine/ease_lut.c).

usage: ease_lut.py [--check]

Each table has 256 entries: entry i is the curve at t = i/255, as Q15
(32768 is 1.0), rounded half up. The maths is exact integer arithmetic, so
the output is the same on every machine; tests/host/test_motion.c recomputes
it in C and fails on any difference. --check compares the committed file
instead of writing it.
"""

import sys
from itertools import pairwise
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "source" / "engine" / "ease_lut.c"
ONE = 32768
N = 255
CUBE = N**3

# Name, and the numerator over 255^3 of the curve at t = i/255. The order
# matches the Ease enum in source/engine/motion.h (after EASE_LINEAR, which
# needs no table).
CURVES = [
    ("in cubic: t^3", lambda i: i**3),
    ("out cubic: 1 - (1 - t)^3", lambda i: CUBE - (N - i) ** 3),
    (
        "in-out cubic: 4t^3, then 1 - 4(1 - t)^3",
        lambda i: 4 * i**3 if 2 * i < N else CUBE - 4 * (N - i) ** 3,
    ),
]


def entry(numerator: int) -> int:
    return (ONE * numerator + CUBE // 2) // CUBE


def render() -> str:
    lines = [
        "// Easing tables for source/engine/motion.c, written by tools/ease_lut.py:",
        "// don't edit by hand. Entry i is the curve at t = i/255 in Q15.",
        '#include "motion.h"',
        "",
        "const u16 ease_lut[EASE_TABLES][EASE_LUT_SIZE] = {",
    ]
    for name, curve in CURVES:
        values = [entry(curve(i)) for i in range(N + 1)]
        assert values[0] == 0 and values[-1] == ONE
        assert all(a <= b for a, b in pairwise(values))
        lines.append(f"\t// {name}")
        lines.append("\t{")
        for row in range(0, len(values), 8):
            chunk = ", ".join(str(v) for v in values[row : row + 8])
            lines.append(f"\t\t{chunk},")
        lines.append("\t},")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main() -> int:
    text = render()
    if sys.argv[1:] == ["--check"]:
        if OUT.read_text(encoding="utf-8") != text:
            print(f"{OUT.name} is stale: run tools/ease_lut.py", file=sys.stderr)
            return 1
        return 0
    if sys.argv[1:]:
        print(__doc__, file=sys.stderr)
        return 2
    OUT.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
