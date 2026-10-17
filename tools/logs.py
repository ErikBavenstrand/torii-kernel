#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Pull the kernel's logs off an SD card and name the code addresses in them.

usage: logs.py PATH [SYMFILE] [OUT_DIR]

PATH is the card's mount point (e.g. /Volumes/OMEGA), or a folder holding
torii-N.log files. The kernel writes LOG_FILES of them in turn, each one a
header ("torii log 00000012"), text, and zero bytes to its full 64 KB
(source/logbuf.h). This copies them to OUT_DIR/raw (default dist/logs), joins
their text oldest first into OUT_DIR/torii.log, and appends function names
to code addresses in crash dumps and debug info, as the emulator's crash
reports show them: "08012345 (Launch_now+0x1C)". The names come from
SYMFILE (default dist/ezkernelnew.sym), so they only match the build that
wrote the log: each boot's first line has the build's main address, and a
session from another build keeps its numbers bare.
"""

import re
import shutil
import sys
from pathlib import Path

HEADER = re.compile(rb"^torii log (\d{8}) *\n")
BOOT = re.compile(r"Torii kernel boot: .*build main ([0-9A-F]{8}) end ([0-9A-F]{8})")
WORD = re.compile(r"\b([0-9A-F]{8})\b")
# Kernel crash dumps: their words may be the kernel's code addresses (debug
# info from the in-game menu holds a game's, which the symbols don't cover).
ADDRESS_LINES = re.compile(r" [EID] crash:")


def load_symbols(path: Path) -> list[tuple[int, str]]:
    symbols = []
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) == 2:
            symbols.append((int(parts[0], 16), parts[1]))
    symbols.sort()
    return symbols


def symbolize(symbols: list[tuple[int, str]], address: int) -> str:
    """The emulator's format: " (name+0xOFFSET)", or "" if nothing is near."""
    address &= ~1
    in_kernel = 0x08000000 <= address < 0x08100000 or 0x03000000 <= address < 0x03008000
    if not in_kernel:
        return ""
    best = None
    for sym_address, name in symbols:
        if sym_address > address:
            break
        best = (sym_address, name)
    if best is None or address - best[0] > 0x10000:
        return ""
    return f" ({best[1]}+0x{address - best[0]:X})"


def read_logs(folder: Path) -> list[tuple[int, Path, str]]:
    logs = []
    for path in sorted(folder.glob("torii-*.log")):
        data = path.read_bytes()
        match = HEADER.match(data)
        if not match:
            print(f"skipped {path.name}: no header (never used, or cut off)")
            continue
        text = data[match.end() :].split(b"\0", 1)[0]
        logs.append((int(match.group(1)), path, text.decode("utf-8", "replace")))
    logs.sort()
    return logs


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    source = Path(sys.argv[1])
    sym_path = Path(sys.argv[2] if len(sys.argv) > 2 else "dist/ezkernelnew.sym")
    out_dir = Path(sys.argv[3] if len(sys.argv) > 3 else "dist/logs")
    folder = (
        source / "torii" / "logs" if (source / "torii" / "logs").is_dir() else source
    )
    logs = read_logs(folder)
    if not logs:
        print(f"error: no torii-N.log files in {folder}", file=sys.stderr)
        return 1

    symbols = load_symbols(sym_path) if sym_path.exists() else []
    by_name = {name: address for address, name in symbols}
    our_main = by_name.get("main", 0) & ~1
    if not symbols:
        print(f"warning: no {sym_path}, so addresses stay bare")

    raw = out_dir / "raw"
    shutil.rmtree(raw, ignore_errors=True)
    raw.mkdir(parents=True)
    for _, path, _ in logs:
        shutil.copyfile(path, raw / path.name)

    out = []
    matches = False
    sessions = other_builds = crashes = 0
    for _, _, text in logs:
        for text_line in text.splitlines():
            line = text_line
            boot = BOOT.search(line)
            if boot:
                sessions += 1
                matches = bool(symbols) and int(boot.group(1), 16) & ~1 == our_main
                if symbols and not matches:
                    other_builds += 1
                    line += "  [another build: addresses not named]"
            if " crash: the kernel crashed" in line:
                crashes += 1
            if matches and ADDRESS_LINES.search(line):
                line = WORD.sub(
                    lambda m: m.group(1) + symbolize(symbols, int(m.group(1), 16)), line
                )
            out.append(line)
    joined = out_dir / "torii.log"
    joined.write_text("\n".join(out) + "\n")
    print(
        f"{joined}: {len(out)} lines from {len(logs)} files, {sessions} boots, {crashes} kernel crashes"
    )
    if other_builds:
        print(f"{other_builds} boots came from another build than {sym_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
