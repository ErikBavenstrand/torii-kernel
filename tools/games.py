#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Add games to roms/games.toml, the list of games the compatibility test runs.

usage: games.py add PATH...   (ROM files, or folders searched for .gba/.gbc/.gb)

The list is git-ignored; it starts as a copy of roms/games.example.toml.
ROMs stay where they are: a folder outside roms/ is remembered in the
git-ignored roms/library.txt, which `just compat` mounts read-only. For each
ROM the list records the file name, SHA-1, system, title and game code, the
save chip the game needs, whether it uses the clock, and any other cartridge
hardware. For GBA games the save chip and clock come from the library strings
Nintendo's SDK puts in every ROM; the EEPROM size, which the strings don't
give, comes from the kernel's table in source/saveMODE.h; other hardware
comes from mGBA's table of games (emu/mgba/src/gba/overrides.c). For GB and
GBC games they come from the cartridge header. A game already in the list
(same SHA-1) is left alone.
"""

import hashlib
import json
import re
import sys
import tomllib
from pathlib import Path

MANIFEST = Path("roms/games.toml")
LIBRARY = Path("roms/library.txt")
KERNEL_TABLE = Path("source/saveMODE.h")
MGBA_TABLE = Path("emu/mgba/src/gba/overrides.c")
SUFFIXES = {".gba": "gba", ".gbc": "gbc", ".gb": "gb"}

# Kernel save codes (saveMODE.h, Get_saveMODE) by the names used in the list.
KERNEL_SAVE = {
    0x11: "sram32k",
    0x21: "eeprom512",
    0x22: "eeprom8k",
    0x23: "eeprom8k",  # EEPROM 8K for games over 16 MB
    0x31: "flash128k",
    0x32: "flash64k",
    0x33: "flash64k",  # FLASH512
}
MGBA_HARDWARE = {
    "HW_LIGHT_SENSOR": "light sensor",
    "HW_RUMBLE": "rumble",
    "HW_TILT": "tilt",
    "HW_GYRO": "gyro",
    "HW_EREADER": "e-reader",
}
# GB cartridge types (header byte 0x147) with a battery, and with a clock (MBC3)
GB_BATTERY = {0x03, 0x06, 0x09, 0x0D, 0x0F, 0x10, 0x13, 0x1B, 0x1E, 0x22, 0xFF}
GB_CLOCK = {0x0F, 0x10}


def kernel_table() -> dict[str, int]:
    text = KERNEL_TABLE.read_bytes().decode("gbk")
    return {
        code: int(mode, 16)
        for code, mode in re.findall(r'\{"(\w{4})",(0x[0-9a-fA-F]+)\}', text)
    }


def mgba_hardware() -> dict[str, list[str]]:
    if not MGBA_TABLE.exists():
        return {}
    found = {}
    for code, flags in re.findall(
        r'\{ "([A-Z0-9]{4})", [A-Z0-9_]+, ([A-Z_| ]+)', MGBA_TABLE.read_text()
    ):
        names = [
            MGBA_HARDWARE[f.strip()]
            for f in flags.split("|")
            if f.strip() in MGBA_HARDWARE
        ]
        if names:
            found[code] = names
    return found


def gba_save(rom: bytes, kernel_mode: int | None) -> str:
    """The save chip the game needs, from its SDK library strings."""
    if b"FLASH1M_V" in rom:
        return "flash128k"
    if b"FLASH512_V" in rom or b"FLASH_V" in rom:
        return "flash64k"
    if b"SRAM_V" in rom or b"SRAM_F_V" in rom:
        return "sram32k"
    if b"EEPROM_V" in rom:
        kernel = KERNEL_SAVE.get(kernel_mode)
        return kernel if kernel and kernel.startswith("eeprom") else "eeprom8k"
    return "none"


def describe(path: Path, rom: bytes, table: dict, hardware: dict) -> dict:
    system = SUFFIXES[path.suffix.lower()]
    if system == "gba":
        code = rom[0xAC:0xB0].decode("ascii", "replace")
        return {
            "system": "gba",
            "title": rom[0xA0:0xAC].rstrip(b"\0").decode("ascii", "replace"),
            "code": code,
            "save": gba_save(rom, table.get(code)),
            "rtc": b"SIIRTC_V" in rom,
            "hardware": hardware.get(code, []),
        }
    cart = rom[0x147] if len(rom) > 0x147 else 0
    if len(rom) > 0x143 and rom[0x143] in (0x80, 0xC0):
        system = "gbc"
    title = rom[0x134:0x143].split(b"\0")[0].decode("ascii", "replace").strip()
    return {
        "system": system,
        "title": title,
        "code": f"{system.upper()}{rom[0x14D]:02X}"
        if len(rom) > 0x14D
        else system.upper(),
        "save": "battery" if cart in GB_BATTERY else "none",
        "rtc": cart in GB_CLOCK,
        "hardware": [],
    }


def library_roots() -> list[Path]:
    if not LIBRARY.exists():
        return []
    return [
        Path(line.strip()) for line in LIBRARY.read_text().splitlines() if line.strip()
    ]


def remember(root: Path) -> None:
    """Add a folder to roms/library.txt unless a listed folder holds it."""
    roots = library_roots()
    if any(root == r or r in root.parents for r in roots):
        return
    roots = [r for r in roots if root not in r.parents] + [root]
    LIBRARY.write_text("".join(f"{r}\n" for r in roots))
    print(f"library: {root}")


def toml_entry(path: Path, sha1: str, info: dict) -> str:
    q = json.dumps  # JSON strings are valid TOML basic strings
    # Plugins the kernel loads for other file types (e.g. System/PLUG) are .gba
    # files too, but not games.
    skip = 'skip = "kernel plugin, not a game"\n' if "PLUG" in path.parts else ""
    return (
        "\n[[game]]\n"
        f"file = {q(path.name)}\n"
        f"system = {q(info['system'])}\n"
        f"title = {q(info['title'])}\n"
        f"code = {q(info['code'])}\n"
        f"sha1 = {q(sha1)}\n"
        f"{skip}"
        f"save = {q(info['save'])}\n"
        f"rtc = {'true' if info['rtc'] else 'false'}\n"
        f"hardware = {json.dumps(info['hardware'])}\n"
    )


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[1] != "add":
        print(__doc__, file=sys.stderr)
        return 2
    table, hardware = kernel_table(), mgba_hardware()
    if not MANIFEST.exists():
        MANIFEST.write_text(Path("roms/games.example.toml").read_text())
    games = tomllib.loads(MANIFEST.read_text()).get("game", [])
    known = {g["sha1"] for g in games}
    roms_dir = Path("roms").resolve()
    added = 0
    entries = []
    for arg in sys.argv[2:]:
        path = Path(arg).expanduser().resolve()
        files = (
            [path]
            if path.is_file()
            else sorted(p for p in path.rglob("*") if p.is_file())
        )
        files = [f for f in files if f.suffix.lower() in SUFFIXES]
        if not files:
            print(f"no games in {arg}", file=sys.stderr)
            continue
        if roms_dir not in path.parents and path != roms_dir:
            remember(path if path.is_dir() else path.parent)
        for rom_path in files:
            rom = rom_path.read_bytes()
            sha1 = hashlib.sha1(rom).hexdigest()
            if sha1 in known:
                continue
            known.add(sha1)
            info = describe(rom_path, rom, table, hardware)
            entries.append(toml_entry(rom_path, sha1, info))
            added += 1
    with MANIFEST.open("a") as f:
        f.writelines(entries)
    print(f"added {added} game(s); {len(known)} listed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
