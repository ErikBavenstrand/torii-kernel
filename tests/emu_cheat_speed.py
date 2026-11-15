#!/usr/bin/env python3
"""The GB cheats' cost in GB frames (tests/emu_cheat_speed.sh).

usage: emu_cheat_speed.py CARTS [--measure]

Jagoomba's frame counter (`frame`, from GLOBAL_PTR_BASE) is read 600 GBA frames
apart, from 1,300 frames after the launch, as docs/research/gbc-hicolor.md
measured hi-colour: with VSync on, 600 means full speed. Each game runs without
cheats and with 63 pokes (the most the table takes: type 97, WRAM bank 7 at
DF00-DF3E, a bank DMG games never see and these GBC games leave alone in the
measured stretch, so the game runs the same and only the writes cost).

The check (always): the hi-colour test ROM (tests/gb_hicolor_rom.py, 576
colours a frame) with 63 pokes runs at full speed with VSync on. --measure also
runs the owned games in roms/ that are here, with VSync off too (novblankwait_
poked to 1 at @1200), and prints the table.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

SHOT = "emu/build-linux/mgba-shot"
ELF = "emulators/out/jagoombacolor.elf"
GAMES = [
    "Pokemon - Crystal Version.gbc",
    "The Legend of Zelda - Link's Awakening DX.gbc",
    "Donkey Kong Country.gbc",
    "Wario Land 3.gbc",
    "Dr. Mario.gb",
]


def sh(
    *args: str, check: bool = False, env: dict | None = None
) -> subprocess.CompletedProcess:
    return subprocess.run(args, capture_output=True, text=True, check=check, env=env)


def pokes() -> str:
    lines = [f"97{i:02X}{i:02X}DF" for i in range(63)]
    out = ["cheats = 3", ""]
    for n, part in enumerate((lines[:21], lines[21:42], lines[42:])):
        out += [
            f'cheat{n}_desc = "Pokes {n}"',
            f'cheat{n}_code = "{"+".join(part)}"',
            f"cheat{n}_enable = true",
            "",
        ]
    return "\n".join(out)


def words(text: str) -> dict[int, int]:
    """{frame: first word} of each -m line."""
    out = {}
    for line in text.splitlines():
        if line.startswith("mem frame "):
            head, data = line.split(":", 1)
            b = [int(x, 16) for x in data.split()]
            out[int(head.split()[2])] = b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24
    return out


def run(carts: Path, game: Path, cheats: bool, vsync: bool, glob: int) -> int:
    """GB frames in 600 GBA frames."""
    d = carts / f"{game.stem}-{int(cheats)}-{int(vsync)}".replace(" ", "_").replace(
        "'", ""
    )
    ext = game.suffix

    def fresh() -> None:  # a new cart: a played game leaves Recent and a save to copy
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
        sd = d / "sd.img"
        sh("mkfs.fat", "-C", "-F", "32", "-n", "SPEED", str(sd), "131072", check=True)
        sh("mcopy", "-i", str(sd), str(game), "::/S" + ext, check=True)
        if cheats:
            (d / "S.cht").write_text(pokes())
            sh("mmd", "-i", str(sd), "::/torii", "::/torii/cheats", check=True)
            sh(
                "mcopy",
                "-i",
                str(sd),
                str(d / "S.cht"),
                "::/torii/cheats/S.cht",
                check=True,
            )

    fresh()
    nav = sh(
        "bash",
        "-c",
        '. tests/nav.sh; nav_launch "$1" at=100',
        "_",
        "/S" + ext,
        check=True,
    ).stdout.split()
    frame = glob + 0x4B4
    flags = (
        glob + 0x4D4
    ) & ~3  # doubletimer, gbamode, request_gb_type, novblankwait (top byte)
    env = {**os.environ, "OMEGA_DIR": str(d)}
    base = [SHOT, "dist/ezkernelnew.bin", *nav, "-w", "launch: starting the game"]
    extra = []
    if not vsync:
        r = sh(*base, "-m", f"@1199:{flags:#x}:4", env=env)
        word = words(r.stdout)
        if not word:
            raise RuntimeError(f"{game.name}: no flags word")
        fresh()
        extra = [
            "-p",
            f"@1200:{flags:#x}:{(next(iter(word.values())) & 0xFFFFFF) | 1 << 24:#x}",
        ]
    r = sh(
        *base, *extra, "-m", f"@1300:{frame:#x}:4", "-m", f"@1900:{frame:#x}:4", env=env
    )
    w = words(r.stdout)
    if r.returncode != 0 or len(w) != 2:
        raise RuntimeError(f"{game.name}: the run failed ({r.returncode})")
    if cheats and "63 pokes written" not in r.stderr:
        raise RuntimeError(f"{game.name}: the 63 pokes weren't written")
    a, b = (w[k] for k in sorted(w))
    return b - a


def main() -> int:
    carts = Path(sys.argv[1])
    measure = "--measure" in sys.argv[2:]
    glob = int(
        sh("python3", "tests/elf_sym.py", ELF, "GLOBAL_PTR_BASE", check=True).stdout, 16
    )
    test_rom = carts / "HiColour.gbc"
    sh("python3", "tests/gb_hicolor_rom.py", str(test_rom), check=True)
    status = 0
    plain = run(carts, test_rom, False, True, glob)
    full = run(carts, test_rom, True, True, glob)
    print(
        f"  hi-colour ROM, VSync on: {plain} GB frames in 600 without cheats, {full} with 63 pokes",
        file=sys.stderr,
    )
    if full < plain * 97 // 100:
        print(
            f"FAIL: emu cheat speed: the hi-colour ROM with 63 pokes ran {full} GB frames in 600, {plain} without"
        )
        status = 1
    if not measure:
        return status
    games = [test_rom] + [Path("roms", g) for g in GAMES if Path("roms", g).is_file()]
    print(
        "| Game | VSync on, none | VSync on, 63 pokes | VSync off, none | VSync off, 63 pokes |"
    )
    print("|---|---|---|---|---|")
    for g in games:
        row = [run(carts, g, c, v, glob) for v in (True, False) for c in (False, True)]
        print(f"| {g.stem} | " + " | ".join(str(x) for x in row) + " |", flush=True)
    return status


if __name__ == "__main__":
    sys.exit(main())
