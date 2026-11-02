#!/usr/bin/env python3
"""How many of libretro's GB, GBC and NES cheats Torii's decoders run, and why not.

Usage: uv run tools/cheat_emu_coverage.py [DIR]   (DIR holds the `cht` folders, default
dist/cheat-emu-corpus, as tools/cheat_emu_stats.py fetches it; never committed).

Builds a host helper (tools/cheat_emu_coverage.c, source/cheatemu.c as cheatfile.c's
decode_emu calls it), feeds it every non-empty cheat once per console (a duplicate
code string counts once) and counts the answers, refined by the line's shape.
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
CODE = re.compile(r'^cheat(\d+)_code\s*=\s*"(.*)"\s*$')
HELPER = Path("dist/cheat_emu_coverage")


def build():
    subprocess.run(
        [
            "cc",
            "-O1",
            "-I",
            "tests/host",
            "-I",
            "source",
            "-o",
            str(HELPER),
            "tools/cheat_emu_coverage.c",
            "source/cheatemu.c",
        ],
        check=True,
    )


def shape(console, line):
    s = line.strip()
    if console == "NES":
        return re.sub(r"[A-Za-z]", "L", re.sub(r"[0-9]", "9", s))
    m = re.fullmatch(
        r"([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})", s
    )
    if m:
        t = m.group(1).upper()
        addr = int(m.group(4) + m.group(3), 16)
        region = (
            "ROM"
            if addr < 0x8000
            else "VRAM"
            if addr < 0xA000
            else "cartRAM"
            if addr < 0xC000
            else "WRAM"
            if addr < 0xE000
            else "echo"
            if addr < 0xFE00
            else "OAM/IO"
            if addr < 0xFF80
            else "HRAM/IE"
        )
        return f"gs:{t} -> {region}"
    if s.startswith("$"):
        return "xploder"
    return re.sub(r"[0-9A-Fa-f]", "h", s)


def reason(console, cheat, ans, parts, strict=False):
    if ans == "ok":
        return "runs"
    if ans == "big":
        return "too big"
    lines = [p for p in cheat.split("+") if p.strip()]
    if ans == "unreadable":
        if any(l.strip().startswith("$") for l in lines):
            return "unreadable: Xploder"
        if any(
            re.search(r"[?*]|[XYxy]{1}", l)
            and console != "NES"
            and not re.fullmatch(r"[0-9A-Fa-f-]+", l.strip())
            for l in lines
        ):
            return "unreadable: wildcards or text"
        if console == "NES" and any(re.search(r"[?*]", l) for l in lines):
            return "unreadable: wildcards"
        if console == "NES" and any(
            re.fullmatch(r"[0-9A-Fa-f]{4}:[0-9A-Fa-f]{2}", l.strip()) for l in lines
        ):
            return "unreadable: raw write to ROM/PPU/register"
        return "unreadable: other"
    # cantrun
    why = set()
    for l, p in zip(lines, parts, strict=False):
        if p != "cantrun":
            continue
        m = re.fullmatch(r"([0-9A-Fa-f]{2})[0-9A-Fa-f]{6}", l.strip())
        t = int(m.group(1), 16) if m else -1
        why.add(
            "unknown GameShark type"
            if t != 1 and not 0x90 <= t <= 0x97
            else "write to ROM/VRAM/cart RAM/OAM/IO"
        )
    return "can't run: " + " + ".join(sorted(why))


def main():
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "dist/cheat-emu-corpus")
    build()
    for console, folder in FOLDERS.items():
        seen, total, empty = set(), 0, 0
        for f in sorted((root / "cht" / folder).glob("*.cht")):
            for raw in f.read_text(encoding="utf-8", errors="replace").splitlines():
                m = CODE.match(raw.strip())
                if not m:
                    continue
                if not m.group(2).strip():
                    empty += 1
                    continue
                total += 1
                seen.add(m.group(2).strip())
        codes = sorted(seen)
        con = "NES" if console == "NES" else "GB"
        inp = "".join(f"{con} {c}\n" for c in codes)
        out = subprocess.run(
            [str(HELPER)], input=inp, capture_output=True, text=True, check=True
        ).stdout.splitlines()
        assert len(out) == len(codes), (len(out), len(codes))
        reasons = collections.Counter()
        shapes = collections.Counter()
        mixed_ok = 0
        for c, o in zip(codes, out, strict=False):
            w = o.split()
            ans, parts = w[1], w[2:]
            r = reason(console, c, ans, parts)
            reasons[r] += 1
            if (
                ans == "ok"
                and len(
                    {
                        "genie" if "-" in p or re.search("[G-Z]", p) else "x"
                        for p in c.split("+")
                    }
                )
                > 1
            ):
                mixed_ok += 1
            if ans != "ok":
                for l, p in zip(
                    [x for x in c.split("+") if x.strip()], parts, strict=False
                ):
                    if p != "ok":
                        shapes[shape(console, l)] += 1
        n = len(codes)
        print(
            f"{console}: {total} cheats with a code ({empty} empty), {n} distinct; runs {reasons['runs']} ({100 * reasons['runs'] / n:.1f}%)"
        )
        for r, k in reasons.most_common():
            print(f"  {k:6} {100 * k / n:5.1f}%  {r}")
        print("  top unsupported line shapes:")
        for s, k in shapes.most_common(10):
            print(f"  {k:6}  {s}")


main()
