#!/usr/bin/env python3
"""Screenshot dumps for the software renderer's tests (tests/ppu_render.sh).

ppu_compare.py dump OUT FRAME [--readable] < mgba-shot's stdout
    A dump in the in-game menu's layout (source/ingame_abi.h, struct
    ingame_shot and what follows it) from the -m lines mgba-shot printed
    after FRAME for VRAM, the palettes, OAM and I/O 0x000-0x05F. With
    --readable, the write-only registers are 0, as the overlay records
    them.
ppu_compare.py diff BMP PNG
    Compares the renderer's BMP with mGBA's PNG at 5 bits a channel (mGBA
    blends at 8, so a channel may be 1 off); prints "DIFF N" for the
    pixels further off and the first few, and exits 1 if any.
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from games import png_lines

SHOT = 0xC0000
PAL = 0xC0100 - SHOT
OAM = 0xC0500 - SHOT
VRAM = 0xC1000 - SHOT
BYTES = VRAM + 0x18000
IO_AT = 16  # struct ingame_shot's io
# The display registers the hardware reads back (halfword indexes): DISPCNT,
# DISPSTAT, VCOUNT, BG0-3CNT, WININ, WINOUT, BLDCNT, BLDALPHA
READABLE = {0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x24, 0x25, 0x28, 0x29}


def memory(frame: int) -> dict:
    mem = {}
    for line in sys.stdin:
        parts = line.split()
        if len(parts) < 4 or parts[0] != "mem" or parts[2] != str(frame):
            continue
        addr = int(parts[3].rstrip(":"), 16)
        for i, b in enumerate(parts[4:]):
            mem[addr + i] = int(b, 16)
    return mem


def dump(out: str, frame: int, readable: bool) -> None:
    mem = memory(frame)

    def take(addr: int, n: int) -> bytes:
        missing = [a for a in range(addr, addr + n) if a not in mem]
        if missing:
            sys.exit(f"ppu_compare: frame {frame}: no dump of {missing[0]:08X}")
        return bytes(mem[a] for a in range(addr, addr + n))

    data = bytearray(BYTES)
    io = bytearray(take(0x04000000, 0x60))
    if readable:
        for h in range(0x30):
            if h not in READABLE:
                io[h * 2 : h * 2 + 2] = b"\0\0"
    struct.pack_into("<IHH", data, 0, 0x31485354, 1, 224)
    data[IO_AT : IO_AT + 0x60] = io
    data[PAL : PAL + 0x400] = take(0x05000000, 0x400)
    data[OAM : OAM + 0x400] = take(0x07000000, 0x400)
    data[VRAM : VRAM + 0x18000] = take(0x06000000, 0x18000)
    Path(out).write_bytes(data)


def bmp_pixels(path: Path):
    data = path.read_bytes()
    (off,) = struct.unpack_from("<I", data, 10)
    w, h = struct.unpack_from("<ii", data, 18)
    rows = []
    stride = (w * 3 + 3) & ~3
    for y in range(abs(h)):
        row = y if h < 0 else abs(h) - 1 - y
        line = data[off + row * stride : off + row * stride + w * 3]
        rows.append([(line[x * 3 + 2], line[x * 3 + 1], line[x * 3]) for x in range(w)])
    return rows


def diff(bmp: str, png: str) -> int:
    ours = bmp_pixels(Path(bmp))
    bad = []
    for y, (channels, line) in enumerate(png_lines(Path(png))):
        for x in range(240):
            theirs = line[x * channels : x * channels + 3]
            mine = ours[y][x]
            if any(
                abs((a >> 3) - (b >> 3)) > 1 for a, b in zip(mine, theirs, strict=True)
            ):
                bad.append((x, y, mine, tuple(theirs)))
    print(f"DIFF {len(bad)}")
    for x, y, mine, theirs in bad[:5]:
        print(f"  ({x},{y}) ours {mine} mGBA's {theirs}")
    return 1 if bad else 0


def main() -> int:
    if len(sys.argv) >= 4 and sys.argv[1] == "dump":
        dump(sys.argv[2], int(sys.argv[3]), "--readable" in sys.argv[4:])
        return 0
    if len(sys.argv) == 4 and sys.argv[1] == "diff":
        return diff(sys.argv[2], sys.argv[3])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
