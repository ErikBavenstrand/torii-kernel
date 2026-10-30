#!/usr/bin/env python3
"""The screenshot viewer's goldens' pictures (tests/shots/shots-*.opts).

usage: tests/shots/data/bmp_fixtures.py   writes them beside this file

shot-scene.bmp  240x160, 24 bits, top-down with Torii's own 54-byte header
                (ppu_bmp_head), in fewer than 176 colours: kept exactly
shot-many.bmp   the same, in thousands of colours: cut by median cut
small.bmp       64x48, 4 bits, bottom-up: centred on black
rle.bmp         an 8-bit RLE header: refused
"""

import math
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent


def head(w, h, bpp, comp=0, pal=b"", data_bytes=0):
    off = 54 + len(pal)
    info = struct.pack(
        "<IiiHHIIiiII", 40, w, h, 1, bpp, comp, data_bytes, 2835, 2835, len(pal) // 4, 0
    )
    return b"BM" + struct.pack("<IHHI", off + data_bytes, 0, 0, off) + info + pal


def rgb24(rows):
    """Rows of (r, g, b) as BMP 24-bit rows, top first."""
    out = bytearray()
    for row in rows:
        line = bytearray()
        for r, g, b in row:
            line += bytes((b, g, r))
        line += bytes(-len(line) % 4)
        out += line
    return bytes(out)


def five(v):
    """8 bits as the GBA shows them: 5 bits, expanded as mGBA (ppu_bmp_row)."""
    c = v >> 3
    return c << 3 | c >> 2


def scene():
    rows = []
    for y in range(160):
        row = []
        for x in range(240):
            if (x - 180) ** 2 + (y - 40) ** 2 < 18**2:
                c = (255, 214, 90)  # the sun
            elif y < 100:
                c = (40 + y // 10 * 8, 90 + y // 10 * 10, 200)  # sky in 10 bands
            elif y < 104:
                c = (60, 140, 60)
            else:
                stripe = (x // 16 + y // 8) % 2
                c = (90 + stripe * 30, 70 + stripe * 20, 40)  # a checked ground
            if 20 <= x < 60 and 70 <= y < 100:  # a house
                c = (200, 60, 50) if y < 80 else (230, 230, 220)
            row.append(tuple(five(v) for v in c))
        rows.append(row)
    return rows


def many():
    rows = []
    for y in range(160):
        row = []
        for x in range(240):
            r = 128 + 127 * math.sin(x / 23.0)
            g = 128 + 127 * math.sin(y / 17.0 + x / 41.0)
            b = 128 + 127 * math.sin((x + y) / 29.0)
            row.append((int(r), int(g), int(b)))
        rows.append(row)
    return rows


def main():
    data = rgb24(scene())
    (HERE / "shot-scene.bmp").write_bytes(
        head(240, -160, 24, data_bytes=len(data)) + data
    )
    data = rgb24(many())
    (HERE / "shot-many.bmp").write_bytes(
        head(240, -160, 24, data_bytes=len(data)) + data
    )
    pal = b"".join(
        bytes((b, g, r, 0))
        for r, g, b in [(i * 16, 255 - i * 16, (i * 40) % 256) for i in range(16)]
    )
    px = bytearray()
    for y in reversed(range(48)):  # bottom-up
        for x in range(0, 64, 2):  # two pixels a byte, the left one high
            px.append((x // 8 + y // 6) % 16 << 4 | ((x + 1) // 8 + y // 6) % 16)
    (HERE / "small.bmp").write_bytes(
        head(64, 48, 4, pal=pal, data_bytes=len(px)) + bytes(px)
    )
    pal = bytes(4 * 2)
    (HERE / "rle.bmp").write_bytes(
        head(16, 16, 8, comp=1, pal=pal, data_bytes=4) + bytes(4)
    )


if __name__ == "__main__":
    main()
