#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Add games to roms/games.toml, the list of games the compatibility test runs.

usage: games.py add ROM...    (each ROM already copied into roms/)

For each ROM it records the file name, SHA-1, game code, title, the save chip
the game needs and whether it uses the clock. Both come from the library
strings Nintendo's SDK puts in every ROM; the EEPROM size, which the strings
don't give, comes from the kernel's table in source/saveMODE.h. A game already
in the list (same SHA-1) is left alone. Runs inside the torii-emu container.
"""

import hashlib
import re
import sys
import tomllib
from pathlib import Path

MANIFEST = Path("roms/games.toml")
KERNEL_TABLE = Path("source/saveMODE.h")

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


def kernel_table() -> dict[str, int]:
    text = KERNEL_TABLE.read_bytes().decode("gbk")
    return {
        code: int(mode, 16)
        for code, mode in re.findall(r'\{"(\w{4})",(0x[0-9a-fA-F]+)\}', text)
    }


def save_from_rom(rom: bytes, kernel_mode: int | None) -> tuple[str, str | None]:
    """Return the save chip the game needs, and a warning if unsure."""
    if b"FLASH1M_V" in rom:
        return "flash128k", None
    if b"FLASH512_V" in rom or b"FLASH_V" in rom:
        return "flash64k", None
    if b"SRAM_V" in rom or b"SRAM_F_V" in rom:
        return "sram32k", None
    if b"EEPROM_V" in rom:
        kernel = KERNEL_SAVE.get(kernel_mode)
        if kernel and kernel.startswith("eeprom"):
            return kernel, None
        return (
            "eeprom8k",
            "EEPROM size unknown (not in the kernel's table); set it by hand",
        )
    return "none", None


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[1] != "add":
        print(__doc__, file=sys.stderr)
        return 2
    table = kernel_table()
    games = (
        tomllib.loads(MANIFEST.read_text()).get("game", []) if MANIFEST.exists() else []
    )
    known = {g["sha1"] for g in games}
    entries = []
    for path in map(Path, sys.argv[2:]):
        rom = path.read_bytes()
        sha1 = hashlib.sha1(rom).hexdigest()
        if sha1 in known:
            print(f"already listed: {path.name}")
            continue
        code = rom[0xAC:0xB0].decode("ascii", "replace")
        title = rom[0xA0:0xAC].rstrip(b"\0").decode("ascii", "replace")
        kernel_mode = table.get(code)
        save, warning = save_from_rom(rom, kernel_mode)
        rtc = b"SIIRTC_V" in rom
        if warning:
            print(f"warning: {path.name}: {warning}", file=sys.stderr)
        if kernel_mode is None:
            print(
                f"note: {path.name}: {code} is not in the kernel's table; the menu must pick its save type",
                file=sys.stderr,
            )
        elif KERNEL_SAVE.get(kernel_mode) != save:
            print(
                f"note: {path.name}: the kernel's table says {kernel_mode:#x}, the ROM needs {save}",
                file=sys.stderr,
            )
        known.add(sha1)
        entries.append(
            "\n[[game]]\n"
            f'file = "{path.name}"\n'
            f'title = "{title}"\n'
            f'code = "{code}"\n'
            f'sha1 = "{sha1}"\n'
            f'save = "{save}"\n'
            f"rtc = {'true' if rtc else 'false'}\n"
        )
        print(f"added: {path.name} ({code}, {save}{', clock' if rtc else ''})")
    with MANIFEST.open("a") as f:
        f.writelines(entries)
    return 0


if __name__ == "__main__":
    sys.exit(main())
