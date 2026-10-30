#!/usr/bin/env python3
"""mgba-shot key options that type a code on the code keypad
(source/screens/keypad.c).

usage: tests/keypad_keys.py [--at N] [--every N] TOKENS...

Each token is typed in turn: hex digits, ':' and AnyGBA's '[', ']', '+',
'^' and '|' as they are (a token of several types each; the symbols through
the symbol key, which the keypad turns back after '[', '+' and '|'), "\n"
or LINE the Line key, DEL the Del key, SYM the symbol key, and A, B, L, R,
SELECT and START that key alone. The selection starts on 0, as the
keypad opens. Prints -k options, one press and its release per line, the
first at frame @N (default 8) after the last -w, presses --every frames
apart (default 8); the selection it ends on goes to stderr as ROW,COL, for
a later call's --from. tests/cheats.sh types with it, and the keypad's golden
shots (tests/shots/keypad*.opts) were made with it.
"""

import argparse
import sys

COLS = 8
FUNC = {":": 0, "LINE": 1, "SYM": 2, "DEL": 3, "CANCEL": 4, "SAVE": 6}
FUNC_ORDER = [0, 1, 2, 3, 4, 6]
HEX = "0123456789ABCDEF"
SYMBOLS = "[]+^|"  # the symbol row, on the digits' first
BACK = "[+|"  # typing one turns the digits back


def func_index(col):
    return max(i for i, c in enumerate(FUNC_ORDER) if c <= col)


def moves(at, to, symbols=False):
    """The d-pad keys from selection at to to (row, col): straight moves,
    no wrapping; with the symbol row shown, the digits' second row is gone."""
    (r, c), (tr, tc) = at, to
    keys = []
    if r == 2 and tr == 2:
        d = func_index(tc) - func_index(c)
        return ["RIGHT" if d > 0 else "LEFT"] * abs(d)
    if r == 2:
        keys.append("UP")
        r = 0 if symbols else 1
        if symbols:
            c = min(c, len(SYMBOLS) - 1)  # the symbol row's last key
    if symbols and tr == 2:
        d = func_index(tc) - func_index(c)
        return keys + ["DOWN"] + ["RIGHT" if d > 0 else "LEFT"] * abs(d)
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
    symbols = False
    for token in tokens:
        if token in ("A", "B", "L", "R", "SELECT", "START"):
            out.append(token)
            continue
        if token == "SYM":  # the symbol key: the other row of keys
            out += moves(at, (2, FUNC["SYM"]), symbols)
            out.append("A")
            at, symbols = (2, FUNC["SYM"]), not symbols
            continue
        if token in ("\\n", "LINE", "DEL", "CANCEL", "SAVE"):
            targets = [((2, FUNC["LINE" if token == "\\n" else token]), None)]
        else:
            targets = []
            for ch in token.upper():
                if ch == ":":
                    targets.append(((2, FUNC[":"]), None))
                elif ch in HEX:
                    i = HEX.index(ch)
                    targets.append(((i // COLS, i % COLS), False))
                elif ch in SYMBOLS:
                    targets.append(((0, SYMBOLS.index(ch)), True))
                else:
                    sys.exit(f"keypad_keys: {ch!r} isn't on the keypad")
        for t, page in targets:
            if page is not None and page != symbols:
                out += moves(at, (2, FUNC["SYM"]), symbols)
                out.append("A")
                at, symbols = (2, FUNC["SYM"]), page
            out += moves(at, t, symbols)
            out.append("A")
            at = t
            if symbols and SYMBOLS[t[1]] in BACK and t[0] == 0 and page:
                symbols = False
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
