#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow==13.0.0"]
# ///
"""Convert pictures to Torii assets (source/engine/asset.h): tiles compressed
with the GBA BIOS's LZ77, plus a palette.

usage: asset.py encode IN.png OUT.tas [--bpp 4|8]
       asset.py info FILE.tas...
       asset.py demo [--check]

encode: a PNG whose sides are multiples of 8 (up to 256) becomes an asset.
Colours are rounded to the GBA's 15 bits; pixels with alpha under 128 are
transparent (colour 0). A 4bpp asset takes up to 15 colours, an 8bpp one
up to 255, in order of first appearance; there is no colour reduction yet
(T15's PNG converter adds it).

info: prints each asset's header and checks that its tiles decode.

demo: writes tests/kernel/asset_data.c, the test assets the kernel tests
and the test kernel's cover demo use: generated patterns, no real art.
--check compares the committed file instead.

The output is the same on every machine: integer maths and a deterministic
compressor, whose matches never point at the byte just before (distance 1),
so the BIOS's VRAM decoder (SWI 0x12) reads them as well as the WRAM one.
"""

import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEMO_OUT = ROOT / "tests" / "kernel" / "asset_data.c"
MAGIC = b"TAS1"
HEADER = struct.Struct("<4sBBBBHHI")
TILE_BYTES_MAX = 0x2000  # ASSET_TILE_BYTES_MAX
FILE_MAX = 0x4000  # ASSET_FILE_MAX


# --- LZ77, as the BIOS reads it ----------------------------------------------


def lz77_compress(data: bytes) -> bytes:
    """Greedy LZ77 in the BIOS's format (type 0x10), padded to 4 bytes.

    Matches are 3-18 bytes at distances 2-4096: never 1, which the VRAM
    decoder can't read back (it writes halfwords)."""
    out = bytearray(struct.pack("<I", 0x10 | len(data) << 8))
    chains: dict[bytes, list[int]] = {}
    i = 0
    while i < len(data):
        flag_at = len(out)
        out.append(0)
        flags = 0
        for bit in range(8):
            if i >= len(data):
                break
            best_len, best_dist = 0, 0
            key = data[i : i + 3]
            if len(key) == 3:
                # The nearest 256 candidates: enough, and bounded
                for j in reversed(chains.get(key, [])[-256:]):
                    dist = i - j
                    if dist > 4096:
                        break
                    if dist < 2:
                        continue
                    n = 3
                    while n < 18 and i + n < len(data) and data[j + n] == data[i + n]:
                        n += 1
                    if n > best_len:
                        best_len, best_dist = n, dist
                        if n == 18:
                            break
            if best_len >= 3:
                flags |= 0x80 >> bit
                v = (best_len - 3) << 12 | (best_dist - 1)
                out += bytes((v >> 8, v & 0xFF))
                step = best_len
            else:
                out.append(data[i])
                step = 1
            for k in range(i, min(i + step, len(data) - 2)):
                chains.setdefault(data[k : k + 3], []).append(k)
            i += step
        out[flag_at] = flags
    while len(out) % 4:
        out.append(0)
    return bytes(out)


def lz77_decompress(packed: bytes) -> bytes:
    """Decodes as the BIOS's LZ77UnCompWram does."""
    if len(packed) < 4 or packed[0] != 0x10:
        raise ValueError("not LZ77 type 0x10")
    size = int.from_bytes(packed[1:4], "little")
    out = bytearray()
    i = 4
    while len(out) < size:
        flags = packed[i]
        i += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                v = packed[i] << 8 | packed[i + 1]
                i += 2
                n, dist = (v >> 12) + 3, (v & 0xFFF) + 1
                if dist > len(out):
                    raise ValueError("match before the start")
                for _ in range(n):
                    out.append(out[-dist])
            else:
                out.append(packed[i])
                i += 1
    return bytes(out[:size])


# --- Assets -------------------------------------------------------------------


def tiles_of(pixels: list[list[int]], bpp: int) -> bytes:
    """Colour indices (rows of pixels) as GBA tiles in 1D sprite order."""
    h, w = len(pixels), len(pixels[0])
    out = bytearray()
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            for y in range(ty, ty + 8):
                row = pixels[y][tx : tx + 8]
                if bpp == 8:
                    out += bytes(row)
                else:
                    out += bytes(row[x] | row[x + 1] << 4 for x in range(0, 8, 2))
    return bytes(out)


def make_asset(pixels: list[list[int]], palette: list[int], bpp: int) -> bytes:
    """An asset from colour indices and BGR555 colours."""
    h, w = len(pixels), len(pixels[0])
    if w % 8 or h % 8 or not 8 <= w <= 256 or not 8 <= h <= 256:
        raise ValueError(f"{w}x{h}: sides must be multiples of 8, up to 256")
    if bpp not in (4, 8) or len(palette) > (16 if bpp == 4 else 256):
        raise ValueError(f"{len(palette)} colours don't fit {bpp}bpp")
    raw = tiles_of(pixels, bpp)
    if len(raw) > TILE_BYTES_MAX:
        raise ValueError(f"{len(raw)} bytes of tiles, more than {TILE_BYTES_MAX}")
    packed = lz77_compress(raw)
    assert lz77_decompress(packed) == raw
    pal = b"".join(struct.pack("<H", c) for c in palette)
    pal += b"\0" * (-len(pal) % 4)
    head = HEADER.pack(MAGIC, bpp, w // 8, h // 8, 0, len(palette), 0, len(packed))
    asset = head + pal + packed
    if len(asset) > FILE_MAX:
        raise ValueError(f"{len(asset)} bytes, more than {FILE_MAX}")
    return asset


def read_asset(data: bytes) -> tuple[tuple, list[int], bytes]:
    """(header fields, palette, decoded tiles); raises ValueError if broken."""
    if len(data) < HEADER.size:
        raise ValueError("shorter than a header")
    magic, bpp, w, h, flags, colours, reserved, lz = HEADER.unpack_from(data)
    if magic != MAGIC or bpp not in (4, 8) or flags or reserved:
        raise ValueError("not a TAS1 asset")
    pal_end = HEADER.size + (colours * 2 + 3) // 4 * 4
    palette = list(struct.unpack_from(f"<{colours}H", data, HEADER.size))
    tiles = lz77_decompress(data[pal_end : pal_end + lz])
    if len(tiles) != w * h * bpp * 8:
        raise ValueError("tiles don't match the size")
    return (bpp, w, h, colours, lz), palette, tiles


def bgr555(r: int, g: int, b: int) -> int:
    return r >> 3 | (g >> 3) << 5 | (b >> 3) << 10


def encode_png(path: Path, bpp: int) -> bytes:
    from PIL import Image

    img = Image.open(path).convert("RGBA")
    w, h = img.size
    data = list(img.getdata())
    palette = [0]  # colour 0: transparent
    index: dict[int, int] = {}
    pixels = []
    for y in range(h):
        row = []
        for x in range(w):
            r, g, b, a = data[y * w + x]
            if a < 128:
                row.append(0)
                continue
            c = bgr555(r, g, b)
            if c not in index:
                index[c] = len(palette)
                palette.append(c)
            row.append(index[c])
        pixels.append(row)
    limit = 16 if bpp == 4 else 256
    if len(palette) > limit:
        raise ValueError(
            f"{path}: {len(palette) - 1} colours, {bpp}bpp takes {limit - 1}"
        )
    return make_asset(pixels, palette, bpp)


# --- The demo and test assets ---------------------------------------------------

# 3x5 digits, a row a string
DIGITS = {
    "1": ["010", "110", "010", "010", "111"],
    "2": ["111", "001", "111", "100", "111"],
    "3": ["111", "001", "011", "001", "111"],
    "4": ["101", "101", "111", "001", "001"],
    "5": ["111", "100", "111", "001", "111"],
    "6": ["111", "100", "111", "101", "111"],
    "7": ["111", "001", "010", "010", "010"],
    "8": ["111", "101", "111", "101", "111"],
}

# Each demo cover's hue, as an RGB at full brightness
HUES = [
    (248, 64, 64),
    (248, 160, 40),
    (176, 224, 48),
    (40, 200, 168),
    (64, 120, 248),
    (168, 88, 240),
    (240, 96, 184),
    (224, 224, 224),
]


def demo_cover(n: int, size: int = 64) -> tuple[list[list[int]], list[int]]:
    """Cover n: a gradient in its hue, a frame, and the digit n + 1."""
    r, g, b = HUES[n % len(HUES)]
    # 1-6: dark to bright; 7: frame; 8: digit; 9: shadow
    shades = [bgr555(r * k // 8, g * k // 8, b * k // 8) for k in range(2, 8)]
    palette = (
        [0]
        + shades
        + [bgr555(248, 248, 248), bgr555(255, 255, 255), bgr555(16, 16, 24)]
    )
    glyph = DIGITS[str(n % 8 + 1)]
    scale = size // 8
    gx, gy = (size - 3 * scale) // 2, (size - 5 * scale) // 2
    pixels = []
    for y in range(size):
        row = []
        for x in range(size):
            c = 1 + min(5, (y * 6 // size + (x * 2 // size)) % 6)
            if x < 2 or y < 2 or x >= size - 2 or y >= size - 2:
                c = 7
            elif x < 4 or y < 4 or x >= size - 4 or y >= size - 4:
                c = 9
            for dx, dy, colour in ((2, 2, 9), (0, 0, 8)):
                u, v = x - gx - dx, y - gy - dy
                if (
                    0 <= u < 3 * scale
                    and 0 <= v < 5 * scale
                    and glyph[v // scale][u // scale] == "1"
                ):
                    c = colour
            row.append(c)
        pixels.append(row)
    return pixels, palette


def checker(size: int) -> tuple[list[list[int]], list[int]]:
    """A small 4bpp test picture: a checkerboard with a diagonal."""
    pixels = [
        [3 if x == y else 1 + ((x // 4 + y // 4) & 1) for x in range(size)]
        for y in range(size)
    ]
    return pixels, [0, bgr555(0, 0, 128), bgr555(255, 255, 0), bgr555(255, 0, 0)]


def c_array(name: str, data: bytes, tiles: bytes) -> str:
    words = [int.from_bytes(data[i : i + 4], "little") for i in range(0, len(data), 4)]
    lines = [
        f"// {len(data)} bytes; the decoded tiles' CRC-32 is {zlib.crc32(tiles):08X}",
        f"const u32 {name}[{len(words)}] = {{",
    ]
    for i in range(0, len(words), 6):
        lines.append("\t" + ", ".join(f"0x{w:08X}" for w in words[i : i + 6]) + ",")
    lines.append("};")
    return "\n".join(lines)


DEMO_COVERS = 8


def demo_source() -> str:
    small = make_asset(*checker(16), 4)
    out = [
        "// Test assets for tests/kernel, written by `uv run tools/asset.py demo`:",
        "// don't edit by hand. Generated patterns, no real cover art.",
        '#include "tests.h"',
        "",
        "// A 16x16 4bpp checkerboard, for the asset unit tests (in every kernel)",
        c_array("test_asset_small", small, read_asset(small)[2]),
        f"const u32 test_asset_small_bytes = {len(small)};",
        f"const u32 test_asset_small_crc = 0x{zlib.crc32(read_asset(small)[2]):08X};",
        "",
        "#ifdef TORII_TEST_KERNEL",
        f"// The cover demo's {DEMO_COVERS} covers, 64x64 4bpp, each a hue and a digit",
    ]
    names = []
    for n in range(DEMO_COVERS):
        asset = make_asset(*demo_cover(n), 4)
        name = f"demo_cover_{n}"
        names.append((name, len(asset)))
        out.append(c_array(name, asset, read_asset(asset)[2]))
    out.append("")
    out.append(f"const struct test_asset demo_covers[{DEMO_COVERS}] = {{")
    for name, size in names:
        out.append(f"\t{{{name}, {size}}},")
    out.append("};")
    out.append("#endif")
    return "\n".join(out) + "\n"


# --- Commands -------------------------------------------------------------------


def main(argv: list[str]) -> int:
    if len(argv) >= 3 and argv[0] == "encode":
        bpp = 4
        if "--bpp" in argv:
            bpp = int(argv[argv.index("--bpp") + 1])
        Path(argv[2]).write_bytes(encode_png(Path(argv[1]), bpp))
        return 0
    if len(argv) >= 2 and argv[0] == "info":
        failed = 0
        for name in argv[1:]:
            try:
                (bpp, w, h, colours, lz), _, tiles = read_asset(Path(name).read_bytes())
                print(
                    f"{name}: {w * 8}x{h * 8} {bpp}bpp, {colours} colours, {lz} bytes of LZ77 for {len(tiles)}"
                )
            except (ValueError, IndexError, struct.error) as e:
                print(f"{name}: broken: {e}")
                failed = 1
        return failed
    if argv[:1] == ["demo"]:
        text = demo_source()
        if "--check" in argv:
            if DEMO_OUT.read_text(encoding="utf-8") != text:
                print(f"{DEMO_OUT} is stale: run `uv run tools/asset.py demo`")
                return 1
            return 0
        DEMO_OUT.write_text(text, encoding="utf-8")
        print(f"wrote {DEMO_OUT.relative_to(ROOT)}")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
