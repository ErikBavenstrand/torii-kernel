#!/usr/bin/env python3
"""The fixture dats' games (tests/names/*.dat) and the files that match them.

The dats are small stand-ins for libretro-database's No-Intro dats, so the
names tests never use the network: tests/names_db_test.py builds names.db
from them, and tests/names_db.sh puts the matching files on an emulated card.

    tests/names/fixture.py roms DIR    writes the files into DIR (by card path)
    tests/names/fixture.py dats        writes the dats again (after editing GAMES)
"""

import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent


def rom(seed, size, code=None):
    """A file's bytes: a pattern from seed, with a GBA game code at 0xAC."""
    data = bytearray((i * seed + seed * 7 + (i >> 8)) & 0xFF for i in range(size))
    if code:
        data[0xAC:0xB0] = code.encode("ascii")
    return bytes(data)


# (dat file, game name, serial or None, (seed, size, code) or None for a game no file matches)
GAMES = [
    ("Nintendo - Game Boy Advance.dat", "Advance Wars (USA) (Beta)", "AWRE", (2, 4096, "AWRE")),
    ("Nintendo - Game Boy Advance.dat", "Advance Wars (USA)", "AWRE", (1, 4096, "AWRE")),
    ("Nintendo - Game Boy Advance.dat", "Legend of Zelda, The - The Minish Cap (USA)", "BZME", (3, 8192, "BZME")),
    ("Nintendo - Game Boy Advance.dat", 'Quote \\"Test\\" (World)', None, (10, 1024, None)),
    ("Nintendo - Game Boy Advance.dat", "Unseen (Europe)", "UNSP", None),
    ("Nintendo - Game Boy Color.dat", "Colour Game (USA)", None, (4, 2048, None)),
    ("Nintendo - Game Boy Color.dat", "Colour Named Gb (World)", None, (6, 2048, None)),
    ("Nintendo - Game Boy.dat", "Mono Game (Japan)", None, (5, 2048, None)),
    ("Nintendo - Nintendo Entertainment System.dat", "Famicom Game (Japan)", None, (7, 16400, None)),
]

# The card's files: (card path, (seed, size, code)). A hack keeps its base
# game's code (named by it), and one file is in no dat.
FILES = [
    ("/GBA/Wars.gba", (1, 4096, "AWRE")),
    ("/GBA/zz Minish.gba", (3, 8192, "BZME")),
    ("/GBA/Hack of Minish.gba", (9, 4096, "BZME")),
    ("/GBA/Homebrew.gba", (11, 4096, None)),
    ("/Colour.gbc", (4, 2048, None)),
    ("/Colour named GB.gb", (6, 2048, None)),
    ("/GB/Mono.gb", (5, 2048, None)),
    ("/NES/Famicom.nes", (7, 16400, None)),
]

DATS = [
    "Nintendo - Game Boy Advance.dat",
    "Nintendo - Game Boy Color.dat",
    "Nintendo - Game Boy.dat",
    "Nintendo - Nintendo Entertainment System.dat",
]


def write_dats(out=HERE):
    for dat in DATS:
        system = dat[:-4]
        lines = [
            "clrmamepro (",
            f'\tname "{system}"',
            f'\tdescription "{system} (Torii test fixture)"',
            '\tversion "2026.01.02"',
            ")",
            "",
        ]
        for i, (d, name, serial, spec) in enumerate(GAMES):
            if d != dat:
                continue
            data = rom(*spec) if spec else bytes([i]) * 64
            crc = zlib.crc32(data)
            ext = {"Nintendo - Game Boy Advance.dat": "gba", "Nintendo - Game Boy Color.dat": "gbc"}.get(dat, "gb")
            if dat.endswith("Entertainment System.dat"):
                ext = "nes"
            lines.append("game (")
            lines.append(f'\tname "{name}"')
            # A serial at the game's level, or (the Minish Cap's) only on its rom
            on_rom = serial == "BZME"
            if serial and not on_rom:
                lines.append(f'\tserial "{serial}"')
            rom_serial = f' serial "{serial}"' if on_rom else ""
            lines.append(f'\trom ( name "{name}.{ext}" size {len(data)} crc {crc:08X}{rom_serial} )')
            lines.append(")")
            lines.append("")
        (out / dat).write_text("\n".join(lines))


def write_roms(out):
    for path, spec in FILES:
        p = Path(out) / path.lstrip("/")
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(rom(*spec))


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "roms":
        write_roms(sys.argv[2])
    elif len(sys.argv) == 2 and sys.argv[1] == "dats":
        write_dats()
    else:
        sys.exit(__doc__)
