#!/usr/bin/env python3
"""Counts the code kinds in libretro-database's GB, GBC and NES cheat files.

Usage: uv run tools/cheat_emu_stats.py [DIR]   (DIR holds the `cht` folders,
default: a shallow sparse clone of libretro-database made in dist/cheat-emu-corpus,
never committed; CC-BY-SA-4.0).

A cheat is one `cheatN_code` string; `+` joins lines. The tool classifies each
line by its shape and counts cheats and lines per console and kind.
"""
import collections
import re
import subprocess
import sys
from pathlib import Path

FOLDERS = {
    "GB": "Nintendo - Game Boy",
    "GBC": "Nintendo - Game Boy Color",
    "NES": "Nintendo - Nintendo Entertainment System",
}
NES_LETTERS = "APZLGITYEOXUKSVN"
CODE = re.compile(r'^cheat(\d+)_code\s*=\s*"(.*)"\s*$')


def classify(console, line):
    s = line.strip()
    if console != "NES" and re.search(r"[^0-9A-Fa-f$-]", s):
        return "bad-chars"  # wildcards ('?', 'X', 'Y') or text
    if console == "NES":
        if re.fullmatch(f"[{NES_LETTERS}]{{6}}", s, re.IGNORECASE):
            return "gg6"
        if re.fullmatch(f"[{NES_LETTERS}]{{8}}", s, re.IGNORECASE):
            return "gg8"
        if re.fullmatch(r"[0-9A-Fa-f]{4}:[0-9A-Fa-f]{2}", s):
            return "raw"
        if re.fullmatch(r"[0-9A-Fa-f]{4}\?[0-9A-Fa-f]{2}:[0-9A-Fa-f]{2}", s):
            return "raw-compare"
        return "bad-chars" if re.search(r"[?*]", s) else "other"
    if re.fullmatch(r"[0-9A-Fa-f]{3}-[0-9A-Fa-f]{3}-[0-9A-Fa-f]{3}", s):
        return "gg9"
    if re.fullmatch(r"[0-9A-Fa-f]{3}-[0-9A-Fa-f]{3}", s):
        return "gg6"
    if re.fullmatch(r"[0-9A-Fa-f]{8}", s):
        return "gs:" + s[:2].upper()
    if re.fullmatch(r"\$[0-9A-Fa-f]{8}", s):
        return "dollar-hex8"
    return "other"


def main():
    if len(sys.argv) > 1:
        root = Path(sys.argv[1])
    else:
        root = Path("dist/cheat-emu-corpus")
        if not root.exists():
            root.parent.mkdir(exist_ok=True)
            subprocess.run(["git", "clone", "-q", "--depth", "1", "--filter=blob:none", "--sparse",
                            "https://github.com/libretro/libretro-database.git", str(root)], check=True)
            subprocess.run(["git", "-C", str(root), "sparse-checkout", "set"] +
                           ["cht/" + f for f in FOLDERS.values()], check=True)
    for console, folder in FOLDERS.items():
        files = sorted((root / "cht" / folder).glob("*.cht"))
        cheats = lines = empty = 0
        kinds = collections.Counter()
        mixed = 0
        others = []
        for f in files:
            for raw in f.read_text(encoding="utf-8", errors="replace").splitlines():
                m = CODE.match(raw.strip())
                if not m:
                    continue
                if not m.group(2).strip():
                    empty += 1
                    continue
                cheats += 1
                parts = [p for p in m.group(2).split("+") if p.strip()]
                ks = set()
                for p in parts:
                    k = classify(console, p)
                    kinds[k] += 1
                    lines += 1
                    ks.add(k.split(":")[0])
                    if k == "other" and len(others) < 8:
                        others.append(p)
                if len(ks) > 1:
                    mixed += 1
        print(f"{console}: {len(files)} files, {cheats} cheats with a code ({empty} empty), {lines} lines, {mixed} cheats mixing kinds")
        for k, n in sorted(kinds.items(), key=lambda kv: -kv[1]):
            print(f"  {k:14} {n}")
        if others:
            print("  other examples:", others)


main()
