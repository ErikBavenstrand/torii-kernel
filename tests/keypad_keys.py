#!/usr/bin/env python3
"""mgba-shot key options that type a code on the code keypad
(source/screens/keypad.c).

usage: tests/keypad_keys.py [--at N] [--every N] TOKENS...

Each token is typed in turn: hex digits and ':' as they are (a token of
several types each), "\\n" or LINE the Line key, DEL the Del key, and A, B,
L, R, SELECT and START that key alone. The selection starts on 0, as the
keypad opens. Prints -k options, one press and its release per line, the
first at frame @N (default 8) after the last -w, presses --every frames
apart (default 8); the selection it ends on goes to stderr as ROW,COL, for
a later call's --from. tests/cheats.sh types with it, and the keypad's golden
shots (tests/shots/keypad*.opts) were made with it.
"""

import argparse
import sys

COLS = 8
FUNC = {":": 0, "LINE": 1, "DEL": 3, "CANCEL": 4, "SAVE": 6}
FUNC_ORDER = [0, 1, 3, 4, 6]
HEX = "0123456789ABCDEF"


def func_index(col):
    return max(i for i, c in enumerate(FUNC_ORDER) if c <= col)


def moves(at, to):
    """The d-pad keys from selection at to to (row, col): straight moves,
    no wrapping."""
    (r, c), (tr, tc) = at, to
    keys = []
    if r == 2 and tr == 2:
        d = func_index(tc) - func_index(c)
        return ["RIGHT" if d > 0 else "LEFT"] * abs(d)
    if r == 2:
        keys.append("UP")
        r = 1
    if tr < 2:
        keys += ["DOWN" if tr > r else "UP"] * abs(tr - r)
        keys += ["RIGHT" if tc > c else "LEFT"] * abs(tc - c)
    else:
        keys += ["RIGHT" if tc > c else "LEFT"] * abs(tc - c)
        keys += ["DOWN"] * (2 - r)
    return keys


def presses(tokens, at=(0, 0)):
    """The keys for tokens from selection at, and the selection after."""
    out = []
    for token in tokens:
        if token in ("A", "B", "L", "R", "SELECT", "START"):
            out.append(token)
            continue
        if token in ("\\n", "LINE", "DEL", "CANCEL", "SAVE"):
            targets = [(2, FUNC["LINE" if token == "\\n" else token])]
        else:
            targets = []
            for ch in token.upper():
                if ch == ":":
                    targets.append((2, FUNC[":"]))
                elif ch in HEX:
                    i = HEX.index(ch)
                    targets.append((i // COLS, i % COLS))
                else:
                    sys.exit(f"keypad_keys: {ch!r} isn't on the keypad")
        for t in targets:
            out += moves(at, t)
            out.append("A")
            at = t
    return out, at


def main(argv):
    p = argparse.ArgumentParser()
    p.add_argument("--at", type=int, default=8)
    p.add_argument("--every", type=int, default=8)
    p.add_argument(
        "--from",
        dest="start",
        default="0,0",
        help="the selection to start from: ROW,COL",
    )
    p.add_argument("tokens", nargs="+")
    args = p.parse_args(argv)
    frame = args.at
    keys, end = presses(args.tokens, tuple(int(x) for x in args.start.split(",")))
    for key in keys:
        print(f"-k @{frame}:{key} -k @{frame + 4}:-")
        frame += args.every
    # Where the selection ends, for a later call's --from
    print(f"{end[0]},{end[1]}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
