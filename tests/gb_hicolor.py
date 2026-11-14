#!/usr/bin/env python3
"""Compares Jagoomba's picture of a GBC ROM with mGBA's own GB core's (tests/gb_hicolor.sh).

gb_hicolor.py TORII.png MGBA.png [MAX_OFF] [GAMMA]
    TORII.png is the kernel's screen (240x160, the Game Boy's 160x144 at
    40,8), MGBA.png mGBA's GB core's (256x224, the 160x144 at its top
    left). Colours are compared at 5 bits a channel: Jagoomba's own, the
    GBC's 15-bit colours as they are (gamma 0).
    Jagoomba isn't cycle-accurate: it raises a STAT interrupt as a line
    starts, not in the HBlank before it, and the game's handler can run a
    little early or late, so a colour written there can show a line or two
    before mGBA shows it. So each 8-pixel piece of a row (one tile, one
    palette in tests/gb_hicolor_rom.py's picture) passes when it matches
    the same piece of mGBA's row, or of the next one or two; Jagoomba's
    last row (written at line 143, which mGBA shows from line 144) isn't
    compared. Prints "pieces off N of M" and exits 1 when N is over MAX_OFF
    (default 0).
    GAMMA, the 64 bytes of Jagoomba's HC_GAMMA in hex (32 little-endian
    greys, the setting's conversion of each level), converts mGBA's colours
    first, for a run with the gamma setting on.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from games import png_lines


def rows(path: str, x0: int, y0: int) -> list:
    """The 160x144 picture at X0,Y0, as rows of (r, g, b) at 5 bits."""
    lines = list(png_lines(Path(path)))
    out = []
    for y in range(y0, y0 + 144):
        n, line = lines[y]
        out.append(
            [
                tuple(c >> 3 for c in line[x * n : x * n + 3])
                for x in range(x0, x0 + 160)
            ]
        )
    return out


def main() -> int:
    torii = rows(sys.argv[1], 40, 8)
    mgba = rows(sys.argv[2], 0, 0)
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    if len(sys.argv) > 4:
        raw = bytes.fromhex(sys.argv[4])
        grey = [raw[i] | raw[i + 1] << 8 for i in range(0, 64, 2)]
        conv = lambda c: (grey[c[0]] & 31, grey[c[1]] >> 5 & 31, grey[c[2]] >> 10 & 31)
        mgba = [[conv(c) for c in row] for row in mgba]
    off = []
    for y in range(143):
        for x in range(0, 160, 8):
            piece = torii[y][x : x + 8]
            if all(piece != mgba[v][x : x + 8] for v in range(y, min(y + 3, 144))):
                off.append((x, y))
    print(
        f"pieces off {len(off)} of {143 * 20}"
        + (f" (first x,y {off[:6]})" if off else "")
    )
    return 1 if len(off) > limit else 0


if __name__ == "__main__":
    sys.exit(main())
