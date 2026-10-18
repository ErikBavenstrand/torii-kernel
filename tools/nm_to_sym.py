#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Turn `arm-none-eabi-nm -n` output into an armips symbol file for mGBA.

usage: nm_to_sym.py NM_OUTPUT OUT.sym [KERNEL.gba]

Each line of the result is ADDRESS NAME, which mGBA's debugger and the
headless runner's crash reports read. Thumb functions have bit 0 set in the
ELF, so it is cleared. Given the kernel, the first line is a comment with its
build ID, "; torii build id 1A2B3C4D", which tools/logs.py checks logs
against; mGBA and mgba-shot skip it, as it doesn't start with an address.
"""

import sys
from pathlib import Path

from build_id import read_id

KEEP = set("TtDdBbRr")


def main() -> int:
    lines = []
    if len(sys.argv) > 3:
        lines.append(f"; torii build id {read_id(Path(sys.argv[3])):08X}\n")
    with open(sys.argv[1]) as f:
        for line in f:
            parts = line.split()
            if (
                len(parts) != 3
                or parts[1] not in KEEP
                or parts[2].startswith(("$", "."))
            ):
                continue
            address = int(parts[0], 16)
            if parts[1] in "Tt":
                address &= ~1
            lines.append(f"{address:08X} {parts[2]}\n")
    with open(sys.argv[2], "w") as f:
        f.writelines(lines)
    return 0


if __name__ == "__main__":
    sys.exit(main())
