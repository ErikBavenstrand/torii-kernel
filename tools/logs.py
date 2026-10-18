#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Pull the kernel's logs off an SD card and name the code addresses in them.

usage: logs.py PATH [SYMFILE] [OUT_DIR] [--sym SYMFILE] [--tag TAG] [--out OUT_DIR]

PATH is the card's mount point (e.g. /Volumes/OMEGA), or a folder holding
torii-N.log files. The kernel writes LOG_FILES of them in turn, each one a
header ("torii log 00000012"), text, and zero bytes to its full 64 KB
(source/logbuf.h). This copies them to OUT_DIR/raw (default dist/logs), joins
their text oldest first into OUT_DIR/torii.log, and appends function names
to code addresses in crash dumps, as the emulator's crash reports show them:
"08012345 (Launch_now+0x1C)".

The names come from SYMFILE (default dist/ezkernelnew.sym), so they only fit
the build that wrote the log. Each boot's first line has the build ID
(source/build_id.h), and so does each crash dump; SYMFILE's first line has
its own. Addresses from another build stay bare, and the summary says which
builds those were. --tag v0.2.1 uses that release's ezkernelnew.sym instead,
downloaded with the GitHub CLI (gh). Logs from before build IDs (v0.2.0) are
matched by main's address only, which other builds can share.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

HEADER = re.compile(rb"^torii log (\d{8}) *\n")
BOOT = re.compile(
    r"Torii kernel boot: .*build main ([0-9A-F]{8}) end ([0-9A-F]{8})(?: id ([0-9A-F]{8}))?"
)
CRASH_BUILD = re.compile(r" crash: build id ([0-9A-F]{8})")
SYM_BUILD = re.compile(r"^; torii build id ([0-9A-F]{8})$", re.MULTILINE)
REPO = "ErikBavenstrand/torii-kernel"
WORD = re.compile(r"\b([0-9A-F]{8})\b")
# Kernel crash dumps: their words may be the kernel's code addresses (debug
# info from the in-game menu holds a game's, which the symbols don't cover).
ADDRESS_LINES = re.compile(r" [EID] crash:")


def load_symbols(path: Path) -> tuple[list[tuple[int, str]], int | None]:
    """The symbols, sorted, and the build ID the file names (None if none)."""
    text = path.read_text()
    symbols = []
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 2:
            symbols.append((int(parts[0], 16), parts[1]))
    symbols.sort()
    build = SYM_BUILD.search(text)
    return symbols, int(build.group(1), 16) if build else None


def fetch_release_symbols(tag: str, out_dir: Path) -> Path:
    """Download a release's ezkernelnew.sym with gh into OUT_DIR/release-TAG."""
    folder = out_dir / f"release-{tag}"
    folder.mkdir(parents=True, exist_ok=True)
    command = ["gh", "release", "download", tag, "--repo", REPO]
    command += ["--pattern", "ezkernelnew.sym", "--dir", str(folder), "--clobber"]
    try:
        subprocess.run(command, check=True)
    except FileNotFoundError:
        sys.exit(
            f"error: --tag needs the GitHub CLI (gh); or download ezkernelnew.sym from "
            f"https://github.com/{REPO}/releases/tag/{tag} and pass --sym"
        )
    except subprocess.CalledProcessError:
        sys.exit(
            f"error: couldn't download {tag}'s ezkernelnew.sym with gh "
            "(releases before v0.2.1 have none)"
        )
    return folder / "ezkernelnew.sym"


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
        if len(data) != 0x10000:
            print(
                f"skipped {path.name}: {len(data)} bytes, not a log file the kernel uses"
            )
            continue
        # The zeros after the text, and any a torn write left inside it
        text = data[match.end() :].rstrip(b"\0").replace(b"\0", b"")
        logs.append((int(match.group(1)), path, text.decode("utf-8", "replace")))
    logs.sort()
    return logs


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        usage=__doc__.splitlines()[2].removeprefix("usage: "),
        description=__doc__.split("\n\n")[0],
    )
    parser.add_argument(
        "path", type=Path, help="SD card or folder of torii-N.log files"
    )
    parser.add_argument("symfile", nargs="?", type=Path, help="the same as --sym")
    parser.add_argument("outdir", nargs="?", type=Path, help="the same as --out")
    parser.add_argument(
        "--sym", type=Path, help="symbols (default dist/ezkernelnew.sym)"
    )
    parser.add_argument("--tag", help="use this GitHub release's symbols, e.g. v0.2.1")
    parser.add_argument("--out", type=Path, help="output folder (default dist/logs)")
    args = parser.parse_args()
    args.sym = args.sym or args.symfile
    args.out = args.out or args.outdir or Path("dist/logs")
    if args.sym and args.tag:
        parser.error("give --sym or --tag, not both")
    return args


def main() -> int:
    args = parse_args()
    source, out_dir = args.path, args.out
    folder = (
        source / "torii" / "logs" if (source / "torii" / "logs").is_dir() else source
    )
    logs = read_logs(folder)
    if not logs:
        print(f"error: no torii-N.log files in {folder}", file=sys.stderr)
        return 1

    if args.tag:
        sym_path = fetch_release_symbols(args.tag, out_dir)
    else:
        sym_path = args.sym or Path("dist/ezkernelnew.sym")
    symbols, sym_id = load_symbols(sym_path) if sym_path.exists() else ([], None)
    by_name = {name: address for address, name in symbols}
    our_main = by_name.get("main", 0) & ~1
    if not symbols:
        print(f"warning: no {sym_path}, so addresses stay bare")
    elif sym_id is None:
        print(
            f"warning: {sym_path} has no build id (built before v0.2.1?), "
            "so boots are matched by main's address only"
        )

    def same_build(build_id: int | None, main_address: int | None) -> bool:
        if not symbols:
            return False
        if build_id is not None and sym_id is not None:
            return build_id == sym_id
        return main_address is not None and main_address & ~1 == our_main

    raw = out_dir / "raw"
    shutil.rmtree(raw, ignore_errors=True)
    raw.mkdir(parents=True)
    for _, path, _ in logs:
        shutil.copyfile(path, raw / path.name)

    out = []
    matches = False
    sessions = crashes = 0
    other_builds: dict[str, int] = {}  # build ID (or "main X") -> boots or crashes
    for _, _, text in logs:
        for text_line in text.splitlines():
            line = text_line
            boot = BOOT.search(line)
            crash_build = CRASH_BUILD.search(line)
            if boot:
                sessions += 1
                build_id = int(boot.group(3), 16) if boot.group(3) else None
                matches = same_build(build_id, int(boot.group(1), 16))
                if symbols and not matches:
                    name = boot.group(3) or f"main {boot.group(1)}, no id"
                    other_builds[name] = other_builds.get(name, 0) + 1
                    line += "  [another build: addresses not named]"
            elif crash_build and sym_id is not None:
                # The dump is the crashed kernel's, which may not be the one reporting it.
                matches = same_build(int(crash_build.group(1), 16), None)
                if symbols and not matches:
                    name = crash_build.group(1)
                    other_builds[name] = other_builds.get(name, 0) + 1
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
        mine = f"build id {sym_id:08X}" if sym_id is not None else "no build id"
        print(f"warning: these came from other builds than {sym_path} ({mine}):")
        for name, count in other_builds.items():
            print(f"  build id {name}: {count} boots or crash dumps")
        print("  name their addresses with --tag (the release they came from) or --sym")
    return 0


if __name__ == "__main__":
    sys.exit(main())
