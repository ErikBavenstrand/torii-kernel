#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Write the kernel's build ID into a linked kernel (source/build_id.h).

usage: build_id.py KERNEL.gba [KERNEL.elf...]

The ID is the CRC32 of KERNEL.gba with the ID's own word read as 0, so
writing it changes nothing it covers, and running this again gives the same
ID. The word follows the text "TORII BUILD ID:\\0" (source/build_id_word.s); it is
written in the .gba and in each .elf, so the symbols and gdb see it too.
read_id() reads it back from a .gba, .elf or upgrade file (ezkernelnew.bin).
"""

import struct
import sys
import zlib
from pathlib import Path

MARKER = b"TORII BUILD ID:\0"


def id_offset(data: bytes, name: str) -> int:
    at = data.find(MARKER)
    if at < 0 or data.find(MARKER, at + 1) >= 0:
        raise ValueError(f"{name}: needs the build ID's marker exactly once")
    return at + len(MARKER)


def compute_id(gba: bytes) -> int:
    at = id_offset(gba, "kernel")
    return zlib.crc32(gba[:at] + bytes(4) + gba[at + 4 :])


def read_id(path: Path) -> int:
    """The build ID in a kernel file; 0 if it was never written."""
    data = path.read_bytes()
    return struct.unpack_from("<I", data, id_offset(data, str(path)))[0]


def write_id(path: Path, build_id: int) -> None:
    data = bytearray(path.read_bytes())
    struct.pack_into("<I", data, id_offset(data, str(path)), build_id)
    path.write_bytes(data)


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    paths = [Path(p) for p in sys.argv[1:]]
    try:
        build_id = compute_id(paths[0].read_bytes())
        if build_id == 0:  # the kernel reads 0 as "no ID"
            raise ValueError("the CRC is 0; change anything to get another")
        for path in paths:
            write_id(path, build_id)
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"{paths[0]}: build id {build_id:08X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
