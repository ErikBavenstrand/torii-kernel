#!/usr/bin/env python3
"""Compares Torii's decoding of a cheat corpus with mGBA's, for
tests/cheat_corpus.sh.

Usage: cheat_corpus.py TORII.tsv MGBA.tsv

Prints how many cheats agree, and each kind of difference with a few examples;
every differing row goes to diff.tsv next to TORII.tsv.
"""

import collections
import sys
from pathlib import Path


def rows(path):
    out = {}
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        cols = line.split("\t")
        out[(cols[0], cols[1])] = cols[2:]
    return out


def main():
    torii, mgba = rows(sys.argv[1]), rows(sys.argv[2])
    kinds = collections.Counter()
    examples = collections.defaultdict(list)
    diffs = []
    for key, t in torii.items():
        status = t[0]
        m = mgba.get(key, ["MISSING"])
        mine = t[3].strip() if len(t) > 3 else ""
        theirs = m[1].strip() if len(m) > 1 else ""
        if len(t) > 2 and t[2].endswith("-alone"):
            kind = "Torii reads it alone, mGBA with the file state"
        elif m[0] == "SKIP":
            kind = (
                "both skip"
                if status in ("PLACEHOLDER", "UNREADABLE")
                else f"mGBA skips, Torii {status}"
            )
        elif m[0] != "REJ0":
            kind = (
                "both reject"
                if status == "UNREADABLE"
                else f"mGBA rejects a line, Torii {status}"
            )
        elif status == "UNREADABLE":
            kind = "Torii rejects only"
        elif status == "MASTER_ONLY":
            kind = (
                "same (master only)"
                if theirs == "|"
                else "Torii master only, mGBA has ops"
            )
        elif status == "OK":
            kind = "same" if mine == theirs else "ops differ"
        else:
            kind = f"Torii {status}"
        kinds[kind] += 1
        if not kind.startswith("same") and kind != "both skip":
            diffs.append(
                f"{key[0]}\t{key[1]}\t{kind}\n  torii: {status} {mine}\n  mgba:  {' '.join(m)}\n"
            )
            if len(examples[kind]) < 3:
                examples[kind].append(f"{key[0]} #{key[1]}")
    print("Torii against mGBA:")
    for kind, count in kinds.most_common():
        print(f"  {kind:40} {count:5}")
        for example in examples[kind]:
            print(f"      {example}")
    Path(sys.argv[1]).with_name("diff.tsv").write_text("".join(diffs), encoding="utf-8")


if __name__ == "__main__":
    main()
