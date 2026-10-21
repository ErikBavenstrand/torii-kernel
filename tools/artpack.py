#!/usr/bin/env python3
"""The full art pack's format (source/artpack.h): writing and reading it.

Only the standard library, so the emulated cart's tests use it too:

    python3 tools/artpack.py write OUT CRC=COVER.bin [CRC=COVER.bin ...]
    python3 tools/artpack.py find PACK CRC

`write` makes a pack of covers already converted (tools/art.py's output),
each CRC (hex) mapped to its cover's file; CRCs given the same file share
one image. `find` prints the image number of a CRC, or "none".

A file, little-endian: a 24-byte header ("TAPK", version, entries, images,
the record size), the index (CRC32, image number) sorted by CRC, then the
images, each a cover asset zero-padded to RECORD_BYTES. tools/art_pack.py
builds the real pack with it (`just art-pack SD`).
"""

import bisect
import contextlib
import os
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def _defines(header):
    text = header.read_text(encoding="utf-8")
    pattern = r"^#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|\d+)u?\b"
    return {m[1]: int(m[2], 0) for m in re.finditer(pattern, text, re.MULTILINE)}


_H = _defines(ROOT / "source" / "artpack.h")
MAGIC = _H["ARTPACK_MAGIC"]
VERSION = _H["ARTPACK_VERSION"]
MAX = _H["ARTPACK_MAX"]
HEADER = struct.Struct("<IHHIIII")
ENTRY = struct.Struct("<II")
assert HEADER.size == _H["ARTPACK_HEADER_BYTES"]
assert ENTRY.size == _H["ARTPACK_ENTRY_BYTES"]
# A cover (engine/cover.h): the asset header, 16 colours, 10x10 4bpp tiles
RECORD_BYTES = 16 + 16 * 2 + 10 * 10 * 32
ASSET_MAGIC = 0x32534154  # "TAS2"


class PackError(Exception):
    pass


def check_cover(data):
    """Refuses bytes that aren't a cover the kernel reads (cover.c's check)."""
    if len(data) < 16 or len(data) > RECORD_BYTES:
        raise PackError(f"a cover is 16 to {RECORD_BYTES} bytes, not {len(data)}")
    magic, bpp, w, h, _, colours, _, tiles = struct.unpack_from("<IBBBBHHI", data)
    if magic != ASSET_MAGIC or bpp != 4 or (w, h) != (10, 10) or colours > 16:
        raise PackError("not a 10x10 4bpp cover (tools/art.py's output)")
    if len(data) < 16 + ((colours * 2 + 3) & ~3) + tiles or tiles != 3200:
        raise PackError("a cover shorter than its header says")


def build(mapping):
    """The pack's bytes for mapping, {crc: cover bytes}. Equal covers are
    stored once; images are numbered in the order of their first CRC."""
    if not mapping:
        raise PackError("no covers: a pack needs at least one")
    if len(mapping) > MAX:
        raise PackError(f"{len(mapping)} CRCs, more than the kernel's {MAX}")
    images, numbers, index = [], {}, []
    for crc in sorted(mapping):
        if not 0 <= crc <= 0xFFFFFFFF:
            raise PackError(f"{crc:#x} isn't a CRC32")
        data = bytes(mapping[crc])
        n = numbers.get(data)
        if n is None:
            check_cover(data)
            n = numbers[data] = len(images)
            images.append(data)
        index.append((crc, n))
    out = bytearray(
        HEADER.pack(MAGIC, VERSION, 0, len(index), len(images), RECORD_BYTES, 0)
    )
    for crc, n in index:
        out += ENTRY.pack(crc, n)
    for data in images:
        out += data + bytes(RECORD_BYTES - len(data))
    return bytes(out)


def write(path, data):
    """Writes data to path whole or not at all: a temporary file beside it,
    flushed to the disk, then renamed over it."""
    path = Path(path)
    tmp = path.with_name(path.name + ".part")
    try:
        with open(tmp, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    except BaseException:
        with contextlib.suppress(OSError):
            tmp.unlink()
        raise


class Pack:
    """A pack read back: its header checked as the kernel checks it."""

    def __init__(self, data):
        if len(data) < HEADER.size:
            raise PackError("shorter than a header")
        magic, version, _, entries, images, record, _ = HEADER.unpack_from(data)
        if magic != MAGIC:
            raise PackError("not an art pack")
        if version != VERSION:
            raise PackError(f"version {version}, not {VERSION}")
        if record != RECORD_BYTES or not 0 < images <= entries <= MAX:
            raise PackError("a header out of range")
        if len(data) != HEADER.size + entries * ENTRY.size + images * RECORD_BYTES:
            raise PackError("its size isn't what its header gives")
        self.data = data
        self.images = images
        self.index = [
            ENTRY.unpack_from(data, HEADER.size + i * ENTRY.size)
            for i in range(entries)
        ]
        self.crcs = [crc for crc, _ in self.index]
        if self.crcs != sorted(set(self.crcs)):
            raise PackError("its index isn't sorted")

    def find(self, crc):
        """The image number of crc, or None (artpack_find)."""
        i = bisect.bisect_left(self.crcs, crc)
        if i < len(self.crcs) and self.crcs[i] == crc:
            return self.index[i][1]
        return None

    def image(self, n):
        at = HEADER.size + len(self.index) * ENTRY.size + n * RECORD_BYTES
        return self.data[at : at + RECORD_BYTES]


def main(argv):
    try:
        if len(argv) >= 3 and argv[0] == "write":
            mapping = {}
            for arg in argv[2:]:
                crc, _, file = arg.partition("=")
                mapping[int(crc, 16)] = Path(file).read_bytes()
            write(argv[1], build(mapping))
            return 0
        if len(argv) == 3 and argv[0] == "find":
            n = Pack(Path(argv[1]).read_bytes()).find(int(argv[2], 16))
            print("none" if n is None else n)
            return 0
    except (PackError, OSError, ValueError) as e:
        print(f"artpack: {e}", file=sys.stderr)
        return 1
    print(__doc__.split("\n\n")[1], file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
