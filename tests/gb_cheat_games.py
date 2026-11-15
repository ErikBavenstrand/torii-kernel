#!/usr/bin/env python3
"""libretro's GB and GBC cheats on your games (roms/), through Torii and mGBA's GB core.

usage: gb_cheat_games.py CARTS CORPUS [GAME...]   (inside the torii-emu container)

For each game in GAMES whose ROM and libretro files are here (the corpus is
tools/cheat_emu_stats.py's, never committed; a missing one is skipped), the
cheats Torii runs (GameShark 01/90-97 to WRAM or HRAM, Game Genie) are picked,
up to a budget, and the game runs twice: in mGBA's own GB core with mGBA's cheat
device (mgba-shot -E) and through Torii on the emulated cart. Then:

- GameShark: at frames FRAMES (of the game) each poke's byte is read where each
  emulator keeps it (Jagoomba: XGB_RAM, GBC_EXRAM by the WRAM bank, XGB_HRAM,
  and for type 01 at D000 the bank mapped then, g_memmap_tbl). A poke holds when
  its byte is the code's value at one of those frames. Every poke that holds in
  mGBA must hold in Torii (one that holds in neither is the game's own writing
  between the vertical blank and the read: reported, not failed). For 90-97 codes
  mGBA writes the mapped bank (it ignores the type byte), so Torii alone counts.
- Game Genie: the patches a device makes on the bus (every bank holding the
  compare, none on the bytes the emulators' checksum samples, each line on
  what the earlier ones left) are worked out from the ROM, and Torii's log must
  count as many; up to SPOT of them, and of the banks left alone, are read back
  from the game's copy in PSRAM.
- Neither run may crash, and Torii's may log no error.
"""

import os
import re
import subprocess
import sys
from pathlib import Path

SHOT = "emu/build-linux/mgba-shot"
FRAMES = (900, 901, 902, 903)
POKES = 40  # of the 63 the table holds
GENIE = 8  # Game Genie cheats a game
SPOT = 48
GB = "Nintendo - Game Boy"
GBC = "Nintendo - Game Boy Color"

# ROM in roms/, libretro files (GameShark first)
GAMES = {
    "Pokemon - Red Version.gb": [
        f"{GB}/Pokemon - Red Version (USA, Europe) (SGB Enhanced).cht",
    ],
    "Pokemon - Yellow Version.gb": [
        f"{GB}/Pokemon - Yellow Version - Special Pikachu Edition (USA, Europe) (GameShark).cht",
        f"{GB}/Pokemon - Yellow Version - Special Pikachu Edition (USA, Europe) (Game Genie).cht",
    ],
    "Pokemon - Crystal Version.gbc": [
        f"{GBC}/Pokemon - Crystal Version (USA, Europe).cht",
        f"{GBC}/Pokemon - Crystal Version (USA, Europe) (Game Genie).cht",
    ],
    "The Legend of Zelda - Link's Awakening DX.gbc": [
        f"{GBC}/Legend of Zelda, The - Link's Awakening DX (USA, Europe) (Rev 2) (SGB Enhanced) (GB Compatible).cht",
        f"{GBC}/Legend of Zelda, The - Link's Awakening DX (USA, Europe) (Game Genie).cht",
    ],
    "Wario Land 3.gbc": [
        f"{GBC}/Wario Land 3 (World) (GameShark).cht",
        f"{GBC}/Wario Land 3 (World) (En,Ja).cht",
    ],
    "Kirby's Dream Land 1.gb": [
        f"{GB}/Kirby's Dream Land (USA, Europe) (GameShark).cht",
        f"{GB}/Kirby's Dream Land (USA, Europe) (Game Genie).cht",
    ],
    "Kirby's Dream Land 2.gb": [
        f"{GB}/Kirby's Dream Land 2 (USA, Europe) (GameShark).cht",
        f"{GB}/Kirby's Dream Land 2 (USA, Europe) (Game Genie).cht",
    ],
    "Tetris DX.gbc": [
        f"{GBC}/Tetris DX (World) (SGB Enhanced) (GB Compatible).cht",
    ],
    "Super Mario Land 1.gb": [
        f"{GB}/Super Mario Land (World) (GameShark).cht",
        f"{GB}/Super Mario Land (World) (Game Genie).cht",
    ],
    "Wario Land 1 - Super Mario Land 3.gb": [
        f"{GB}/Wario Land - Super Mario Land 3 (World) (GameShark).cht",
        f"{GB}/Wario Land - Super Mario Land 3 (World) (Game Genie).cht",
    ],
    "Metroid 2 - Return of Samus.gb": [
        f"{GB}/Metroid II - Return of Samus (World).cht",
        f"{GB}/Metroid II - Return of Samus (World) (Game Genie).cht",
    ],
    "Final Fantasy Adventure.gb": [
        f"{GB}/Final Fantasy Adventure (USA) (GameShark).cht",
        f"{GB}/Final Fantasy Adventure (USA) (Game Genie).cht",
    ],
    "Mega Man 2.gb": [
        f"{GB}/Mega Man II (USA, Europe) (GameShark).cht",
        f"{GB}/Mega Man II (USA, Europe) (Game Genie).cht",
    ],
    "Super Mario Bros. Deluxe.gbc": [
        f"{GBC}/Super Mario Bros. Deluxe (World) (Game Genie).cht",
    ],
    "Donkey Kong Country.gbc": [
        f"{GBC}/Donkey Kong Country (USA, Europe) (GameShark).cht",
    ],
}

CODE = re.compile(r'^cheat(\d+)_code\s*=\s*"(.*)"\s*$')
DESC = re.compile(r'^cheat(\d+)_desc\s*=\s*"(.*)"\s*$')
GS = re.compile(r"^[0-9A-Fa-f]{8}$")
GG = re.compile(r"^[0-9A-Fa-f]{3}-[0-9A-Fa-f]{3}(-[0-9A-Fa-f]{3})?$")


def sh(
    *args: str, check: bool = False, env: dict | None = None
) -> subprocess.CompletedProcess:
    return subprocess.run(args, capture_output=True, text=True, check=check, env=env)


def read_cht(path: Path) -> list[tuple[str, str]]:
    descs, codes = {}, {}
    for line in path.read_text(errors="replace").splitlines():
        if m := CODE.match(line.strip()):
            codes[int(m[1])] = m[2]
        elif m := DESC.match(line.strip()):
            descs[int(m[1])] = m[2]
    return [(descs.get(i, f"Cheat {i}"), codes[i]) for i in sorted(codes)]


def gs_line(line: str):
    """(kind, value, addr) of a GameShark line Torii runs, else None."""
    if not GS.match(line):
        return None
    op = int(line, 16)
    kind, value, addr = op >> 24, op >> 16 & 0xFF, (op & 0xFF) << 8 | op >> 8 & 0xFF
    if kind != 0x01 and not 0x90 <= kind <= 0x97:
        return None
    if not (0xC000 <= addr < 0xFE00 or 0xFF80 <= addr < 0xFFFF):
        return None
    return kind, value, addr


def gg_line(line: str):
    """(addr, value, compare|None) of a Game Genie line, else None."""
    if not GG.match(line):
        return None
    p = line.split("-")
    op1, op2 = int(p[0], 16), int(p[1], 16)
    addr = (op1 & 0xF) << 8 | (op2 >> 4) & 0xFF | ((op2 & 0xF) ^ 0xF) << 12
    if addr >= 0x8000:
        return None
    compare = None
    if len(p) == 3:
        op3 = int(p[2], 16)
        v = (op3 & 0xF00) << 20 | (op3 & 0xF)
        v = (v >> 2 | v << 30) & 0xFFFFFFFF
        v |= v >> 24
        compare = (v ^ 0xBA) & 0xFF
    return addr, op1 >> 4, compare


def pick(files: list[Path]) -> tuple[list[tuple[str, str]], list, list]:
    """The cheats to pick: (desc, code), their pokes and their genie lines."""
    picked, pokes, genies = [], [], []
    seen = set()
    n_genie = 0
    for f in files:
        for desc, code in read_cht(f):
            lines = [x for x in code.split("+") if x]
            if not lines or code in seen:
                continue
            g = [gs_line(x) for x in lines]
            if all(g):
                if len(pokes) + len(g) > POKES:
                    continue
                pokes += g
            else:
                gg = [gg_line(x) for x in lines]
                if not all(gg) or n_genie >= GENIE:
                    continue
                n_genie += 1
                genies += gg
            seen.add(code)
            picked.append((desc, code))
    return picked, pokes, genies


def plan_genie(rom: bytes, genies: list) -> tuple[int, list, list, int]:
    """A device's patches: (count, [(offset, value)], [(offset, value) left alone], lines patching nothing)."""
    image = bytearray(rom)
    banks = (len(rom) + 0x3FFF) // 0x4000
    patched, alone = [], []
    idle = 0
    for addr, value, compare in genies:
        before = len(patched)
        places = (
            [addr]
            if addr < 0x4000
            else [b * 0x4000 + addr - 0x4000 for b in range(1, banks)]
        )
        for at in places:
            if compare is not None and image[at] != compare:
                alone.append((at, image[at]))
                continue
            if at < 0x4000 and (at & 127) < 4:
                alone.append((at, image[at]))
                continue
            image[at] = value
            patched.append((at, value))
        idle += len(patched) == before
    final = [(at, image[at]) for at, _ in patched]
    return len(patched), final, [(at, image[at]) for at, _ in alone], idle


def mem(text: str) -> dict[int, dict[int, int]]:
    """{frame: {address: byte}} from mgba-shot's -m lines."""
    out: dict[int, dict[int, int]] = {}
    for line in text.splitlines():
        m = re.match(r"mem frame (\d+) ([0-9A-F]+):(.*)", line)
        if m:
            frame, base = int(m[1]), int(m[2], 16)
            d = out.setdefault(frame, {})
            for i, x in enumerate(m[3].split()):
                d[base + i] = int(x, 16)
    return out


def cht_text(picked: list[tuple[str, str]]) -> str:
    out = [f"cheats = {len(picked)}", ""]
    for i, (desc, code) in enumerate(picked):
        out += [
            f'cheat{i}_desc = "{desc}"',
            f'cheat{i}_code = "{code}"',
            f"cheat{i}_enable = true",
            "",
        ]
    return "\n".join(out)


def run(
    name: str, files: list[Path], carts: Path, syms: dict[str, int], emu_size: int
) -> tuple[list, list]:
    rom = Path("roms", name).read_bytes()
    gbc = name.endswith(".gbc")
    picked, pokes, genies = pick(files)
    d = carts / re.sub(r"[^A-Za-z0-9]+", "-", name)
    d.mkdir(parents=True)
    (d / "game.cht").write_text(cht_text(picked))
    ext = ".gbc" if gbc else ".gb"
    notes, fails = (
        [f"{len(picked)} cheats, {len(pokes)} pokes, {len(genies)} genie lines"],
        [],
    )
    # mGBA
    dumps = []
    for f in FRAMES:
        dumps += ["-m", f"{f}:0xC000:0x2000", "-m", f"{f}:0xFF80:0x80"]
    m = sh(
        SHOT, f"roms/{name}", "-E", str(d / "game.cht"), *dumps, "-f", str(FRAMES[-1])
    )
    (d / "mgba.txt").write_text(m.stdout)
    if m.returncode != 0:
        fails.append(f"mGBA's run failed ({m.returncode}): {m.stderr[-200:]}")
    # Torii
    sd = d / "sd.img"
    sh("mkfs.fat", "-C", "-F", "32", "-n", "GAMES", str(sd), "131072", check=True)
    sh("mcopy", "-i", str(sd), f"roms/{name}", "::/G" + ext, check=True)
    sh("mmd", "-i", str(sd), "::/torii", "::/torii/cheats", check=True)
    sh("mcopy", "-i", str(sd), str(d / "game.cht"), "::/torii/cheats/G.cht", check=True)
    nav = sh(
        "bash",
        "-c",
        '. tests/nav.sh; nav_launch "$1" at=100',
        "_",
        "/G" + ext,
        check=True,
    ).stdout.split()
    n_patch, final, alone, idle = plan_genie(rom, genies)
    tdumps = []
    for f in FRAMES:
        tdumps += [
            "-m",
            f"@{f}:{syms['XGB_RAM']:#x}:0x2000",
            "-m",
            f"@{f}:{syms['XGB_HRAM']:#x}:0x80",
            "-m",
            f"@{f}:{syms['GBC_EXRAM']:#x}:0x6000",
            "-m",
            f"@{f}:{syms['g_memmap_tbl']:#x}:0x40",
        ]
    psram = 0x08000000 + emu_size
    for at, _ in final[:SPOT] + alone[:SPOT]:
        tdumps += ["-m", f"@{FRAMES[0]}:{psram + at:#x}:1"]
    # A dialog after the load says how many lines patched nothing: A closes it
    ack = ["-w", "screen ready: dialog", "-k", "@10:A", "-k", "@14:-"] if idle else []
    if idle:
        notes.append(
            f"{idle} genie lines patch nothing in this ROM (another version's)"
        )
    t = sh(
        SHOT,
        "dist/ezkernelnew.bin",
        *nav,
        *ack,
        "-w",
        "launch: starting the game",
        *tdumps,
        env={**os.environ, "OMEGA_DIR": str(d)},
    )
    (d / "torii.txt").write_text(t.stdout)
    (d / "torii.log").write_text(t.stderr)
    if t.returncode != 0:
        return notes, fails + [f"Torii's run failed ({t.returncode})"]
    if "crash at frame" in t.stderr or "crash at frame" in m.stderr:
        fails.append("a crash")
    errors = [x for x in t.stderr.splitlines() if x.startswith("kernel error:")]
    if errors:
        fails.append(f"Torii logged {errors[0]}")
    # Game Genie
    if genies:
        made = re.search(r"kernel: cheats: (\d+) lines made (\d+) patches", t.stderr)
        got = int(made[2]) if made else 0
        if got != n_patch:
            fails.append(f"Torii made {got} genie patches, the device {n_patch}")
        allmem = {}
        for v in mem(t.stdout).values():
            allmem.update(v)
        bad = [
            (at, v, allmem.get(psram + at))
            for at, v in final[:SPOT] + alone[:SPOT]
            if allmem.get(psram + at) != v
        ]
        if bad:
            fails.append(
                f"{len(bad)} ROM bytes in PSRAM unlike the device's, first at {bad[0][0]:X}: {bad[0][2]} not {bad[0][1]:02X}"
            )
        notes.append(
            f"genie: {n_patch} patches as the device, {min(SPOT, len(final))}+{min(SPOT, len(alone))} read back"
        )
    # GameShark
    if pokes:
        mm, tt = mem(m.stdout), mem(t.stdout)
        tframes = sorted(tt)
        held = {"both": 0, "mgba only": 0, "torii only": 0, "neither": 0}
        for kind, value, addr in pokes:
            a = addr - 0x2000 if 0xE000 <= addr < 0xFE00 else addr
            mg = any(mm.get(f, {}).get(a) == value for f in FRAMES)
            th = False
            for f in tframes:
                fm = tt[f]
                if a >= 0xFF80:
                    at = syms["XGB_HRAM"] + a - 0xFF80
                elif a < 0xD000:
                    at = syms["XGB_RAM"] + a - 0xC000
                elif kind >= 0x90 and gbc and (kind & 7) >= 2:
                    at = syms["GBC_EXRAM"] + ((kind & 7) - 2) * 0x1000 + a - 0xD000
                elif kind >= 0x90 and gbc:
                    at = syms["XGB_RAM"] + 0x1000 + a - 0xD000
                else:
                    base = syms["g_memmap_tbl"] + 4 * 0xD
                    ptr = sum(fm.get(base + i, 0) << (8 * i) for i in range(4))
                    at = (ptr + a) & 0xFFFFFFFF
                th |= fm.get(at) == value
            if kind >= 0x90:
                mg = th  # mGBA ignores the bank: Torii's own read stands
            key = (
                "both"
                if mg and th
                else "mgba only"
                if mg
                else "torii only"
                if th
                else "neither"
            )
            held[key] += 1
            if key == "mgba only":
                fails.append(
                    f"poke {kind:02X} {addr:04X}={value:02X} holds in mGBA, not in Torii"
                )
        notes.append(
            "pokes held: " + ", ".join(f"{k} {v}" for k, v in held.items() if v)
        )
    return notes, fails


def main() -> int:
    carts, corpus = Path(sys.argv[1]), Path(sys.argv[2])
    only = sys.argv[3:]
    names = ["XGB_RAM", "XGB_HRAM", "GBC_EXRAM", "g_memmap_tbl"]
    out = sh(
        "python3",
        "tests/elf_sym.py",
        "emulators/out/jagoombacolor.elf",
        *names,
        check=True,
    ).stdout.split()
    syms = dict(zip(names, (int(x, 16) for x in out), strict=True))
    emu_size = Path("emulators/out/jagoombacolor.gba").stat().st_size
    status, ran = 0, 0
    for name, chts in GAMES.items():
        if only and name not in only:
            continue
        files = [corpus / "cht" / c for c in chts]
        if not Path("roms", name).is_file() or not all(f.is_file() for f in files):
            print(f"  {name}: skipped (no ROM or no libretro file)", file=sys.stderr)
            continue
        ran += 1
        notes, fails = run(name, files, carts, syms, emu_size)
        if fails:
            status = 1
            print(f"FAIL: gb cheat games: {name}: " + "; ".join(fails[:4]))
        print(f"  {name}: " + "; ".join(notes), file=sys.stderr)
    print(f"{ran} games run", file=sys.stderr)
    return status


if __name__ == "__main__":
    sys.exit(main())
