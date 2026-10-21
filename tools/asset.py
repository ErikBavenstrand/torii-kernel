#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""Convert pictures to Torii assets (source/engine/asset.h): raw tiles in the
GBA's own format, plus a palette, so the kernel only copies them.

usage: asset.py encode IN.png OUT.tas [--bpp 4|8] [--dither]
       asset.py info FILE.tas...
       asset.py demo [--check]
       asset.py ui [--check]

encode: a PNG whose sides are multiples of 8 (up to 256) becomes an asset.
Colour 0 is transparent: pixels with alpha under 128 get it. Colours are
rounded to the GBA's 15 bits; a 4bpp asset takes up to 15 more, an 8bpp one
up to 255, in order of first appearance. A picture with more is reduced to
that many (median cut; --dither spreads the error, Floyd-Steinberg). An
indexed PNG whose pixels all use the first 16 (or 256) entries is taken as
it is: its indices are kept, and index 0 is transparent.

info: prints each asset's header and checks its size.

demo: writes tests/kernel/asset_data.c, the test asset the kernel's unit
tests use: a generated pattern, no real art. --check compares the committed
file instead.

ui: converts the menu's art in assets/ui (its art.toml says how) into
source/ui/art_data.c and art_ids.h (`just assets`); --check compares the
committed files instead. source/ui/art.h describes the formats.

The limits come from the C headers, so the two never disagree. The output is
the same on every machine: integer maths and a pinned Pillow. tools/art.py
makes box art with make_asset.
"""

import re
import struct
import sys
import tomllib
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEMO_OUT = ROOT / "tests" / "kernel" / "asset_data.c"
MAGIC = b"TAS2"
HEADER = struct.Struct("<4sBBBBHHI")


def c_defines(header: Path) -> dict[str, int]:
    """The numeric #defines of a C header."""
    text = header.read_text(encoding="utf-8")
    pattern = r"^#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|\d+)u?\b"
    return {m[1]: int(m[2], 0) for m in re.finditer(pattern, text, re.MULTILINE)}


_ASSET_H = c_defines(ROOT / "source" / "engine" / "asset.h")
_ART_H = c_defines(ROOT / "source" / "ui" / "art.h")
TILE_BYTES_MAX = _ASSET_H["ASSET_TILE_BYTES_MAX"]
FILE_MAX = _ASSET_H["ASSET_FILE_MAX"]
TILES_PER_SIDE_MAX = _ASSET_H["ASSET_MAX_TILES_PER_SIDE"]
assert _ASSET_H["ASSET_HEADER_BYTES"] == HEADER.size


# --- Assets -------------------------------------------------------------------


def tiles_of(pixels: list[list[int]], bpp: int) -> bytes:
    """Colour indices (rows of pixels) as GBA tiles in row order."""
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
    side = TILES_PER_SIDE_MAX * 8
    if w % 8 or h % 8 or not 8 <= w <= side or not 8 <= h <= side:
        raise ValueError(f"{w}x{h} px: sides must be multiples of 8, up to {side}")
    if bpp not in (4, 8) or len(palette) > (16 if bpp == 4 else 256):
        raise ValueError(f"{len(palette)} colours don't fit {bpp}bpp")
    raw = tiles_of(pixels, bpp)
    if len(raw) > TILE_BYTES_MAX:
        raise ValueError(f"{len(raw)} bytes of tiles, more than {TILE_BYTES_MAX}")
    pal = b"".join(struct.pack("<H", c) for c in palette)
    pal += b"\0" * (-len(pal) % 4)
    head = HEADER.pack(MAGIC, bpp, w // 8, h // 8, 0, len(palette), 0, len(raw))
    asset = head + pal + raw
    if len(asset) > FILE_MAX:
        raise ValueError(f"{len(asset)} bytes, more than {FILE_MAX}")
    return asset


def read_asset(data: bytes) -> tuple[tuple, list[int], bytes]:
    """(header fields, palette, tiles); raises ValueError if broken."""
    if len(data) < HEADER.size:
        raise ValueError("shorter than a header")
    magic, bpp, w, h, flags, colours, reserved, size = HEADER.unpack_from(data)
    if magic != MAGIC or bpp not in (4, 8) or flags or reserved:
        raise ValueError("not a TAS2 asset")
    if size != w * h * bpp * 8:
        raise ValueError("the tile bytes don't match the size")
    pal_end = HEADER.size + (colours * 2 + 3) // 4 * 4
    palette = list(struct.unpack_from(f"<{colours}H", data, HEADER.size))
    tiles = data[pal_end : pal_end + size]
    if len(tiles) != size:
        raise ValueError("shorter than its header says")
    return (bpp, w, h, colours, size), palette, tiles


def bgr555(r: int, g: int, b: int) -> int:
    return r >> 3 | (g >> 3) << 5 | (b >> 3) << 10


def png_pixels(
    path: Path, bpp: int, dither: bool = False
) -> tuple[list[list[int]], list[int], str]:
    """(colour indices, BGR555 palette, a note on what was done) for a PNG."""
    from PIL import Image

    limit = 16 if bpp == 4 else 256
    img = Image.open(path)
    img.load()
    w, h = img.size
    if img.mode == "P":
        data = list(img.get_flattened_data())
        if max(data, default=0) < limit:
            rgb = img.getpalette() or []
            rgb += [0] * (3 * limit - len(rgb))
            count = max(data, default=0) + 1
            palette = [0] + [bgr555(*rgb[3 * i : 3 * i + 3]) for i in range(1, count)]
            pixels = [data[y * w : (y + 1) * w] for y in range(h)]
            return pixels, palette, "indexed, taken as it is"
    rgba = list(img.convert("RGBA").get_flattened_data())
    opaque = [p[3] >= 128 for p in rgba]
    # Rounded to 15 bits first, so colours the GBA can't tell apart are one
    rounded = [(r & 0xF8, g & 0xF8, b & 0xF8) for r, g, b, _ in rgba]
    colours = dict.fromkeys(c for c, o in zip(rounded, opaque, strict=True) if o)
    note = ""
    if len(colours) > limit - 1:
        # Median cut over the opaque pixels; transparent ones borrow the
        # first opaque colour, so they add none
        fill = next(iter(colours))
        flat = Image.new("RGB", (w, h))
        flat.putdata([c if o else fill for c, o in zip(rounded, opaque, strict=True)])
        reduced = flat.quantize(
            colors=limit - 1,
            method=Image.Quantize.MEDIANCUT,
            dither=Image.Dither.NONE,
        )
        if dither:
            # Pillow dithers only when mapping onto a given palette
            reduced = flat.quantize(palette=reduced, dither=Image.Dither.FLOYDSTEINBERG)
        pal = reduced.getpalette() or []
        rounded = [
            (pal[3 * i] & 0xF8, pal[3 * i + 1] & 0xF8, pal[3 * i + 2] & 0xF8)
            for i in reduced.get_flattened_data()
        ]
        note = f"reduced from {len(colours)}" + (", dithered" if dither else "")
    palette = [0]  # colour 0: transparent
    index: dict[int, int] = {}
    flat_pixels = []
    for (r, g, b), o in zip(rounded, opaque, strict=True):
        if not o:
            flat_pixels.append(0)
            continue
        c = bgr555(r, g, b)
        if c not in index:
            index[c] = len(palette)
            palette.append(c)
        flat_pixels.append(index[c])
    pixels = [flat_pixels[y * w : (y + 1) * w] for y in range(h)]
    return pixels, palette, note


def encode_png(path: Path, bpp: int, dither: bool = False) -> bytes:
    pixels, palette, _ = png_pixels(path, bpp, dither)
    return make_asset(pixels, palette, bpp)


# --- The demo and test assets ---------------------------------------------------

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
        f"// {len(data)} bytes; the tiles' CRC-32 is {zlib.crc32(tiles):08X}",
        f"const u32 {name}[{len(words)}] = {{",
    ]
    for i in range(0, len(words), 6):
        lines.append("\t" + ", ".join(f"0x{w:08X}" for w in words[i : i + 6]) + ",")
    lines.append("};")
    return "\n".join(lines)


def demo_source() -> str:
    small = make_asset(*checker(16), 4)
    tiles = read_asset(small)[2]
    out = [
        "// Test assets for tests/kernel, written by `uv run tools/asset.py demo`:",
        "// don't edit by hand. Generated patterns, no real cover art.",
        '#include "tests.h"',
        "",
        "// A 16x16 4bpp checkerboard, for the asset unit tests (in every kernel)",
        c_array("test_asset_small", small, tiles),
        f"const u32 test_asset_small_bytes = {len(small)};",
        f"const u32 test_asset_small_crc = 0x{zlib.crc32(tiles):08X};",
    ]
    return "\n".join(out) + "\n"


# --- The menu's art (source/ui/art.h) ------------------------------------------

ART_DIR = ROOT / "assets" / "ui"
ART_C = ROOT / "source" / "ui" / "art_data.c"
ART_IDS = ROOT / "source" / "ui" / "art_ids.h"
SPRITE_SIZE = _ART_H["ART_SPRITE_SIZE"]
SPRITE_FOOT = _ART_H["ART_SPRITE_FOOT"]
SPRITE_COLOURS = _ART_H["ART_SPRITE_COLOURS"]
SHADOW_INDEX = _ART_H["ART_SHADOW_INDEX"]
BOX_SIZE = _ART_H["ART_BOX_SIZE"]
BOX_COLOURS = max(_ART_H[k] for k in ("ART_BOX_BAND", "ART_BOX_BODY", "ART_BOX_SHAPE"))
assert _ART_H["ART_SPRITE_TILES"] == SPRITE_SIZE * SPRITE_SIZE // 64
assert _ART_H["ART_BOX_TILES"] == BOX_SIZE * BOX_SIZE // 64
assert SPRITE_COLOURS < SHADOW_INDEX <= 15


class ArtError(ValueError):
    """A picture in assets/ui that can't be converted; says which and why."""


def rel(path: Path) -> str:
    """path for a message: from the repo's root when it is inside it."""
    return str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)


def rgba_pixels(path: Path) -> list[list[tuple[int, int, int] | None]]:
    """Rows of colours rounded to 15 bits, None where transparent."""
    from PIL import Image

    img = Image.open(path)
    img.load()
    w, h = img.size
    rgba = list(img.convert("RGBA").get_flattened_data())
    flat = [(r & 0xF8, g & 0xF8, b & 0xF8) if a >= 128 else None for r, g, b, a in rgba]
    return [flat[y * w : (y + 1) * w] for y in range(h)]


def art_name(path: Path) -> str:
    name = path.stem
    if not re.fullmatch(r"[a-z][a-z0-9]*", name):
        raise ArtError(f"{rel(path)}: name it in lower-case letters and digits")
    return name


def art_files(sub: str) -> list[Path]:
    return sorted((ART_DIR / sub).glob("*.png"))


def sized(path: Path, rows: list, w: int, h: int) -> None:
    if len(rows) != h or len(rows[0]) != w:
        raise ArtError(f"{rel(path)}: {len(rows[0])}x{len(rows)} px, not {w}x{h}")


def index_rows(
    rows: list, palette: list[int], path: Path, limit: int
) -> list[list[int]]:
    """Colour indices for rows, adding new colours to palette (BGR555,
    index 0 transparent) up to limit entries."""
    index = {c: i for i, c in enumerate(palette) if i}
    out = []
    for row in rows:
        line = []
        for c in row:
            if c is None:
                line.append(0)
                continue
            v = bgr555(*c)
            if v not in index:
                if len(palette) >= limit:
                    raise ArtError(f"{rel(path)}: more than {limit - 1} colours")
                index[v] = len(palette)
                palette.append(v)
            line.append(index[v])
        out.append(line)
    return out


def one_colour(path: Path, rows: list) -> list[list[int]]:
    """1 where rows are opaque; they must be all one colour."""
    colours = {c for row in rows for c in row if c is not None}
    if len(colours) > 1:
        raise ArtError(f"{rel(path)}: {len(colours)} colours; use one")
    return [[int(c is not None) for c in row] for row in rows]


def art_icons16(banks: list[list[str]]) -> tuple[list, list, list, list[int]]:
    """(names, rows of indices, banks' colours, each icon's bank): the icons
    in name order, in the banks art.toml pairs them into."""
    files = art_files("icons16")
    names = [art_name(p) for p in files]
    listed = [n for bank in banks for n in bank]
    for n in names:
        if listed.count(n) != 1:
            raise ArtError(
                f"assets/ui/art.toml: put icons16/{n}.png in one of icon16_banks"
            )
    for n in listed:
        if n not in names:
            raise ArtError(
                f"assets/ui/art.toml: icon16_banks names {n}, which icons16/ hasn't"
            )
    if any(not 1 <= len(bank) <= 2 for bank in banks):
        raise ArtError("assets/ui/art.toml: icon16_banks takes one or two icons a bank")
    icons: dict[str, list[list[int]]] = {}
    colours, bank_of = [], {}
    for b, bank in enumerate(banks):
        palette = [0]
        for n in bank:
            path = files[names.index(n)]
            rows = rgba_pixels(path)
            sized(path, rows, 16, 16)
            icons[n] = index_rows(rows, palette, path, 16)
            bank_of[n] = b
        colours.append(palette + [0] * (16 - len(palette)))
    return names, [icons[n] for n in names], colours, [bank_of[n] for n in names]


def art_sprite(path: Path) -> tuple[list[list[int]], list[int], int, int]:
    """(64x64 indices, 16 colours, width, height): the drawn part centred
    across, its last row on the foot row."""
    rows = rgba_pixels(path)
    ys = [y for y, row in enumerate(rows) if any(c is not None for c in row)]
    xs = [x for row in rows for x, c in enumerate(row) if c is not None]
    if not ys:
        raise ArtError(f"{rel(path)}: empty")
    rows = [row[min(xs) : max(xs) + 1] for row in rows[min(ys) : max(ys) + 1]]
    w, h = len(rows[0]), len(rows)
    if w > SPRITE_SIZE or h > SPRITE_FOOT + 1:
        raise ArtError(
            f"{rel(path)}: {w}x{h} px drawn, more than {SPRITE_SIZE}x{SPRITE_FOOT + 1}"
        )
    palette = [0]
    cells = index_rows(rows, palette, path, SPRITE_COLOURS + 1)
    x0, y0 = (SPRITE_SIZE - w) // 2, SPRITE_FOOT + 1 - h
    out = [[0] * SPRITE_SIZE for _ in range(SPRITE_SIZE)]
    for y, row in enumerate(cells):
        out[y0 + y][x0 : x0 + w] = row
    return out, palette + [0] * (16 - len(palette)), w, h


def art_shadow(path: Path) -> tuple[list[list[int]], int, int]:
    """(the 32x8 left half in ART_SHADOW_INDEX, width, drawn height)."""
    if not re.fullmatch(r"w\d+", path.stem):
        raise ArtError(f"{rel(path)}: name it wNN.png, NN its width")
    rows = one_colour(path, rgba_pixels(path))
    sized(path, rows, 64, 8)
    if any(row != row[::-1] for row in rows):
        raise ArtError(f"{rel(path)}: not mirrored about the middle")
    xs = [x for row in rows for x, c in enumerate(row) if c]
    ys = [y for y, row in enumerate(rows) if any(row)]
    if not xs:
        raise ArtError(f"{rel(path)}: empty")
    # The width it's named for: a dithered rim may leave its ends out
    width = int(path.stem[1:])
    if max(xs) - min(xs) + 1 > width or width % 2 or width > 64:
        raise ArtError(
            f"{rel(path)}: {max(xs) - min(xs) + 1} px drawn; name it for an even width up to 64 that holds it"
        )
    return (
        [[c * SHADOW_INDEX for c in row[:32]] for row in rows],
        width,
        max(ys) - min(ys) + 1,
    )


def art_box(path: Path) -> tuple[list[list[int]], list[int]]:
    """(80x80 indices, 16 colours) from an indexed PNG, taken as it is."""
    from PIL import Image

    img = Image.open(path)
    img.load()
    if img.mode != "P" or img.size != (BOX_SIZE, BOX_SIZE):
        raise ArtError(f"{rel(path)}: make it an indexed {BOX_SIZE}x{BOX_SIZE} PNG")
    data = list(img.get_flattened_data())
    if max(data) > BOX_COLOURS:
        raise ArtError(f"{rel(path)}: uses index {max(data)}; it has {BOX_COLOURS}")
    rgb = (img.getpalette() or []) + [0] * 48
    palette = [0] + [bgr555(*rgb[3 * i : 3 * i + 3]) for i in range(1, BOX_COLOURS + 1)]
    rows = [data[y * BOX_SIZE : (y + 1) * BOX_SIZE] for y in range(BOX_SIZE)]
    return rows, palette + [0] * (16 - len(palette))


def c_words(data: bytes, indent: str = "\t") -> list[str]:
    words = [int.from_bytes(data[i : i + 4], "little") for i in range(0, len(data), 4)]
    return [
        indent + ", ".join(f"0x{w:08X}" for w in words[i : i + 8]) + ","
        for i in range(0, len(words), 8)
    ]


def c_halves(values: list[int]) -> str:
    return "{" + ", ".join(f"0x{v:04X}" for v in values) + "}"


def c_id(prefix: str, name: str) -> str:
    return f"{prefix}_{name.upper()}"


def art_sources() -> tuple[str, str]:
    """(art_ids.h, art_data.c) for assets/ui."""
    manifest = tomllib.loads((ART_DIR / "art.toml").read_text(encoding="utf-8"))
    icon16_names, icons16, banks16, bank_of16 = art_icons16(manifest["icon16_banks"])
    icon8_names, icons8 = [], []
    for path in art_files("icons8"):
        rows = one_colour(path, rgba_pixels(path))
        sized(path, rows, 8, 8)
        icon8_names.append(art_name(path))
        icons8.append(bytes(sum(bit << x for x, bit in enumerate(row)) for row in rows))
    shadows = sorted((art_shadow(p) for p in art_files("shadows")), key=lambda s: s[1])
    if not shadows:
        raise ArtError("assets/ui/shadows: no shadows")
    sprite_names, sprites = [], []
    for path in art_files("sprites"):
        sprite_names.append(art_name(path))
        sprites.append(art_sprite(path))
    box_rows, box_palette = art_box(ART_DIR / "box.png")

    def shadow_for(width: int) -> int:
        fits = [i for i, s in enumerate(shadows) if s[1] <= width]
        return fits[-1] if fits else 0

    items = []
    for item in manifest["items"]:
        try:
            items.append(
                (
                    item["id"],
                    icon16_names.index(item["icon16"]),
                    icon8_names.index(item["icon8"]),
                    sprite_names.index(item["sprite"]),
                )
            )
        except (KeyError, ValueError) as e:
            raise ArtError(
                f"assets/ui/art.toml: item {item.get('id', '?')}: no such art ({e})"
            ) from e

    def enum(prefix: str, names: list[str], count: str) -> list[str]:
        lines = ["enum {"]
        lines += [f"\t{c_id(prefix, n)}," for n in names]
        return lines + [f"\t{count}", "};", ""]

    ids = [
        "// The menu's art ids (source/ui/art.h), written by `just assets` from",
        "// assets/ui: don't edit by hand.",
        "#ifndef TORII_UI_ART_IDS_H",
        "#define TORII_UI_ART_IDS_H",
        "",
        *enum("ART_ICON16", icon16_names, "ART_ICON16_COUNT"),
        f"#define ART_ICON16_BANKS {len(banks16)}",
        "",
        *enum("ART_ICON8", icon8_names, "ART_ICON8_COUNT"),
        *enum("ART_SPRITE", sprite_names, "ART_SPRITE_COUNT"),
        f"#define ART_SHADOW_COUNT {len(shadows)}",
        "",
        "// The carousel's items, in its order",
        *enum("ART_ITEM", [i[0] for i in items], "ART_ITEM_COUNT"),
        "#endif",
    ]

    data = [
        "// The menu's art (source/ui/art.h), written by `just assets` from the",
        "// PNGs in assets/ui: don't edit by hand.",
        "//",
        "// The Game Boy Advance, Game Boy Color and Game Boy objects are redrawn",
        "// on the outlines of KyleBing's retro game console icons",
        "// (https://github.com/KyleBing/retro-game-console-icons), licensed",
        "// CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/). The rest is",
        "// Torii's own. assets/ui/README.md has the credits.",
        '#include "art.h"',
        "",
        "const u32 art_icon16_tiles[ART_ICON16_COUNT][ART_ICON16_TILES * 8] = {",
    ]
    for name, rows in zip(icon16_names, icons16, strict=True):
        data += (
            [f"\t[{c_id('ART_ICON16', name)}] = {{"]
            + c_words(tiles_of(rows, 4), "\t\t")
            + ["\t},"]
        )
    data += ["};", "", "const u8 art_icon16_bank[ART_ICON16_COUNT] = {"]
    data += [
        f"\t[{c_id('ART_ICON16', n)}] = {b},"
        for n, b in zip(icon16_names, bank_of16, strict=True)
    ]
    data += ["};", "", "const u16 art_icon16_palettes[ART_ICON16_BANKS][16] = {"]
    data += [f"\t{c_halves(bank)}," for bank in banks16]
    data += ["};", "", "const u8 art_icon8_bits[ART_ICON8_COUNT][8] = {"]
    for name, bits in zip(icon8_names, icons8, strict=True):
        data.append(
            f"\t[{c_id('ART_ICON8', name)}] = {{"
            + ", ".join(f"0x{b:02X}" for b in bits)
            + "},"
        )
    data += [
        "};",
        "",
        "const u32 art_sprite_tiles[ART_SPRITE_COUNT][ART_SPRITE_TILES * 8] = {",
    ]
    for name, (rows, _, _, _) in zip(sprite_names, sprites, strict=True):
        data += (
            [f"\t[{c_id('ART_SPRITE', name)}] = {{"]
            + c_words(tiles_of(rows, 4), "\t\t")
            + ["\t},"]
        )
    data += ["};", "", "const u16 art_sprite_palettes[ART_SPRITE_COUNT][16] = {"]
    for name, (_, palette, _, _) in zip(sprite_names, sprites, strict=True):
        data.append(f"\t[{c_id('ART_SPRITE', name)}] = {c_halves(palette)},")
    data += ["};", "", "const struct art_sprite art_sprites[ART_SPRITE_COUNT] = {"]
    for name, (_, _, w, h) in zip(sprite_names, sprites, strict=True):
        data.append(f"\t[{c_id('ART_SPRITE', name)}] = {{{w}, {h}, {shadow_for(w)}}},")
    data += [
        "};",
        "",
        "const u32 art_shadow_tiles[ART_SHADOW_COUNT][ART_SHADOW_HALF_TILES * 8] = {",
    ]
    for rows, _, _ in shadows:
        data += ["\t{"] + c_words(tiles_of(rows, 4), "\t\t") + ["\t},"]
    data += ["};", "", "const struct art_shadow art_shadows[ART_SHADOW_COUNT] = {"]
    data += [f"\t{{{w}, {h}}}," for _, w, h in shadows]
    data += ["};", "", "const u32 art_box_tiles[ART_BOX_TILES * 8] = {"]
    data += c_words(tiles_of(box_rows, 4))
    data += ["};", "", f"const u16 art_box_palette[16] = {c_halves(box_palette)};", ""]
    data += ["const struct art_item art_items[ART_ITEM_COUNT] = {"]
    for item, icon16, icon8, sprite in items:
        data.append(
            f"\t[{c_id('ART_ITEM', item)}] = {{{c_id('ART_ICON16', icon16_names[icon16])}, "
            f"{c_id('ART_ICON8', icon8_names[icon8])}, {c_id('ART_SPRITE', sprite_names[sprite])}}},"
        )
    data += ["};"]
    return "\n".join(ids) + "\n", "\n".join(data) + "\n"


# --- Commands -------------------------------------------------------------------


def main(argv: list[str]) -> int:
    dither = "--dither" in argv
    argv = [a for a in argv if a != "--dither"]
    if len(argv) >= 3 and argv[0] == "encode":
        bpp = 4
        if "--bpp" in argv:
            bpp = int(argv[argv.index("--bpp") + 1])
        try:
            Path(argv[2]).write_bytes(encode_png(Path(argv[1]), bpp, dither))
        except ValueError as e:
            print(f"{argv[1]}: {e}")
            return 1
        except OSError as e:  # missing, unreadable or not a picture; can't write
            print(f"asset.py: {e}")
            return 1
        return 0
    if len(argv) >= 2 and argv[0] == "info":
        failed = 0
        for name in argv[1:]:
            try:
                (bpp, w, h, colours, size), _, _ = read_asset(Path(name).read_bytes())
                print(f"{name}: {w * 8}x{h * 8} {bpp}bpp, {colours} colours, {size} bytes of tiles")
            except (ValueError, IndexError, struct.error) as e:
                print(f"{name}: broken: {e}")
                failed = 1
        return failed
    if argv[:1] == ["ui"]:
        try:
            outputs = dict(zip((ART_IDS, ART_C), art_sources(), strict=True))
        except ArtError as e:
            print(e)
            return 1
        if "--check" in argv:
            stale = [
                p
                for p, t in outputs.items()
                if not p.exists() or p.read_text(encoding="utf-8") != t
            ]
            for p in stale:
                print(f"{p.relative_to(ROOT)} is stale: run `just assets`")
            return 1 if stale else 0
        for p, t in outputs.items():
            p.write_text(t, encoding="utf-8")
            print(f"wrote {p.relative_to(ROOT)}")
        return 0
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
