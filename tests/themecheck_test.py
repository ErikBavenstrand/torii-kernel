#!/usr/bin/env python3
"""Tests for the theme checker (tools/themecheck.c, through
tools/theme_check.py's build), run by tests/host.sh in the torii-emu
container: it prints what the kernel logs for good and broken themes, finds
files ignoring case as FAT does, and `new` writes themes that load cleanly."""

import shutil
import struct
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import asset
import theme_check

CASES = Path("dist/host/themecheck-cases")
failures = []
# The checker's runs, in parallel: each takes about 0.5 s, nearly all of it
# LeakSanitizer's check at exit. (name, want, status, the run's future)
POOL = ThreadPoolExecutor(8)
runs = []


def check(name: str, files: dict[str, bytes | str], want: list[str], status: int):
    """Writes a theme folder of files, checks it, and compares every line."""
    folder = CASES / name
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    for file, data in files.items():
        (folder / file).write_bytes(data.encode() if isinstance(data, str) else data)
    runs.append(
        (name, want, status, POOL.submit(theme_check.cli, "check", str(folder)))
    )


def finish(name: str, want: list[str], status: int, result) -> None:
    """Compares a checker run's lines and exit status with check()'s want."""
    got = [x for x in result.stdout.splitlines() if not x.startswith("theme-check:")]
    if got != want or result.returncode != status or result.stderr:
        failures.append(
            f"{name}: exit {result.returncode} (want {status}), printed {got} {result.stderr!r}, want {want}"
        )


def wallpaper(bpp: int = 4, tiles: int = 4) -> bytes:
    pixels = [[(x + y) % 3 + 1 for x in range(8 * tiles)] for y in range(8)]
    return asset.make_asset(pixels, [0, 0x1F, 0x3E0, 0x7C00], bpp)


INI = "version = 1\nname = Case\n"
WALL = INI + "[wallpaper]\nfile = wall.tas\n"


def main() -> int:
    theme_check.build()
    selftest = POOL.submit(theme_check.cli, "selftest")
    example = POOL.submit(theme_check.cli, "check", "themes/example")
    news = {
        b: POOL.submit(theme_check.cli, "new", f"New {b}", b) for b in ("dark", "light")
    }
    check(
        "good",
        {"theme.ini": WALL, "wall.tas": wallpaper()},
        ["kernel: theme: loaded good (Case, 0 warnings, a wallpaper)"],
        0,
    )
    check(
        "warned",
        {"theme.ini": INI + "colour = 1\n[layout]\npad = 99\n"},
        [
            "kernel: theme: line 3: unknown key colour; ignored",
            "kernel: theme: line 5: pad = 99 is out of range 0 to 16; clamped",
            "kernel: theme: loaded warned (Case, 2 warnings)",
        ],
        0,
    )
    check(
        "nowall",
        {"theme.ini": WALL},
        [
            "kernel: theme: nowall: no wallpaper wall.tas; none shown",
            "kernel: theme: loaded nowall (Case, 1 warnings)",
        ],
        0,
    )
    # FAT ignores case: the card would find both
    check(
        "Cased",
        {"THEME.INI": WALL.replace("name = Case\n", ""), "Wall.TAS": wallpaper()},
        ["kernel: theme: loaded Cased (Cased, 0 warnings, a wallpaper)"],
        0,
    )
    check(
        "noini",
        {"readme.txt": "hi"},
        ["kernel error: theme: noini has no theme.ini"],
        1,
    )
    check(
        "v2",
        {"theme.ini": "version = 2\nbogus\n"},
        ["kernel error: theme: v2: theme.ini is version 2; this kernel reads 1"],
        1,
    )
    check(
        "bigini",
        {"theme.ini": INI + ";" * 0x2000},
        ["kernel error: theme: bigini: theme.ini is over 8 KB"],
        1,
    )
    check(
        "wall8",
        {"theme.ini": WALL, "wall.tas": wallpaper(8)},
        ["kernel error: theme: wall8: wallpaper wall.tas is 8bpp; it must be 4bpp"],
        1,
    )
    check(
        "wallcut",
        {"theme.ini": WALL, "wall.tas": wallpaper()[:-4]},
        [
            "kernel error: theme: wallcut: wallpaper wall.tas: the file is shorter than its header says"
        ],
        1,
    )
    check(
        "wallbig",
        {"theme.ini": WALL, "wall.tas": wallpaper() + b"\0" * 0x4000},
        ["kernel error: theme: wallbig: wallpaper wall.tas is over 16 KB"],
        1,
    )
    check(
        "wallmagic",
        {"theme.ini": WALL, "wall.tas": b"PNG!" + wallpaper()[4:]},
        [
            "kernel error: theme: wallmagic: wallpaper wall.tas: not an asset (no TAS1 magic)"
        ],
        1,
    )
    # The header says 33 tiles wide: out of the format's range
    bad = bytearray(wallpaper())
    struct.pack_into("<B", bad, 5, 33)
    check(
        "wallwide",
        {"theme.ini": WALL, "wall.tas": bytes(bad)},
        [
            "kernel error: theme: wallwide: wallpaper wall.tas: bpp, size, flags or colours out of range"
        ],
        1,
    )
    check(
        ".hidden",
        {"theme.ini": INI},
        ['kernel error: theme: ".hidden" isn\'t a theme folder name'],
        1,
    )

    for base in ("dark", "light"):
        text = news[base].result().stdout
        check(
            f"new-{base}",
            {"theme.ini": text},
            [f"kernel: theme: loaded new-{base} (New {base}, 0 warnings)"],
            0,
        )

    if selftest.result().returncode:
        failures.append(f"selftest: {selftest.result().stdout.strip()}")
    example = example.result()
    if example.returncode or not example.stdout.startswith(
        "kernel: theme: loaded example (Example, 0 warnings, a wallpaper)\n"
    ):
        failures.append(f"example: {example.stdout!r}")
    for name, want, status, run in runs:
        finish(name, want, status, run.result())

    for f in failures:
        print(f"FAIL: themecheck: {f}")
    if failures:
        return 1
    print("PASS: theme checker prints the kernel's lines for good and broken themes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
