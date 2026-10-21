#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""Make box art for the kernel's game lists from any picture.

usage: art.py add ROM_OR_FILE_NAME IMAGE SD_PATH [--align A] [--preview PNG]
       art.py convert IMAGE OUT.bin [--align A] [--preview PNG]

add (`just art-add`): converts IMAGE (a PNG, JPEG or anything else Pillow
reads) into custom art for one game and writes it to the card at SD_PATH (a
mounted card, say /Volumes/OMEGA), as /torii/art/custom/<file name>.bin.
ROM_OR_FILE_NAME is the game's file on the card, or only its file name
("Pokemon Prism.gbc"): custom art goes by file name, extension included, so
copies of a game in different folders share it, and a romhack never gets
its base game's art. The list with art shows it in place of the matched art
or the placeholder.

convert: the same conversion into OUT.bin, anywhere (B23's scraper uses it).

The conversion, made to look good at 80x80 on the GBA's screen: the picture
is cropped to a square (--align picks which part of a tall or wide picture
stays: top, centre or bottom, left, centre or right; by default the top of
a tall picture, where box art has its title, and the centre of a wide one),
scaled down with Lanczos, and reduced to 15 colours of the GBA's 15-bit
range (median cut refined by k-means), with a gentle serpentine Floyd-
Steinberg dither at three quarters strength, so gradients don't band and
flat areas stay clean. Colour 0 stays unused: on a BG it is transparent.
The result is an asset of raw 4bpp tiles (source/engine/asset.h) that the
kernel copies to VRAM as it is. --preview also writes the result as a PNG,
three times the size, to look at on the PC.

Box art is the publishers' work: use pictures you may use, keep the files
on your card, and don't publish them.
"""

import argparse
import os
import sys
import unicodedata
from pathlib import Path

from PIL import Image, ImageOps

sys.path.insert(0, str(Path(__file__).resolve().parent))
import asset

_COVER_H = asset.c_defines(asset.ROOT / "source" / "engine" / "cover.h")
TILES = _COVER_H["COVER_TILES_W"]
assert _COVER_H["COVER_TILES_H"] == TILES
SIZE = TILES * 8  # 80 px
COLOURS = _COVER_H["COVER_COLOURS"]  # 16 entries, 0 left unused
FILE_BYTES = asset.HEADER.size + COLOURS * 2 + SIZE * SIZE // 2
CUSTOM_DIR = Path("torii") / "art" / "custom"
EXT = ".bin"
NAME_MAX = 255  # UTF-16 units of a FAT long name
# Bytes of a game's name the kernel starts
GAME_NAME_MAX = asset.c_defines(asset.ROOT / "source" / "userdata.h")[
    "USERDATA_NAME_MAX"
]
GAME_EXTS = (".gba", ".gbc", ".gb", ".nes")
DITHER = (3, 4)  # the share of each pixel's error passed on
ALIGNS = ("centre", "center", "top", "bottom", "left", "right")


class ArtError(ValueError):
    """A picture or a name that can't be made into box art; says why."""


def square(img: Image.Image, align: str) -> Image.Image:
    """The largest square of img, from the part align names (centred
    across the other way)."""
    w, h = img.size
    side = min(w, h)
    x, y = (w - side) // 2, (h - side) // 2
    if h > w and align in ("top", "bottom"):
        y = 0 if align == "top" else h - side
    elif w > h and align in ("left", "right"):
        x = 0 if align == "left" else w - side
    return img.crop((x, y, x + side, y + side))


def opaque_rgb(path: Path) -> Image.Image:
    """The picture upright (EXIF), on white where it is transparent."""
    try:
        img = Image.open(path)
        img.load()
    except OSError as e:
        raise ArtError(f"{path}: not a picture Pillow can read ({e})") from e
    img = ImageOps.exif_transpose(img)
    rgba = img.convert("RGBA")
    flat = Image.new("RGBA", rgba.size, (255, 255, 255, 255))
    flat.alpha_composite(rgba)
    return flat.convert("RGB")


def expand5(c: int) -> int:
    """A 5-bit channel as the screen shows it, in 8 bits."""
    return c << 3 | c >> 2


def to15(v: int) -> int:
    """An 8-bit channel at the nearest of the GBA's 32 levels, in 8 bits."""
    return expand5((v * 31 + 127) // 255)


def distance(a: tuple[int, int, int], r: int, g: int, b: int) -> int:
    """How far apart two colours look ("redmean", integer)."""
    dr, dg, db = a[0] - r, a[1] - g, a[2] - b
    mean = (a[0] + r) // 2
    return ((512 + mean) * dr * dr >> 8) + 4 * dg * dg + ((767 - mean) * db * db >> 8)


def palette_of(img: Image.Image) -> list[tuple[int, int, int]]:
    """Up to 15 colours of the GBA's for img, darkest first: median cut,
    then each colour moved to its pixels' (refined twice). A colour most of
    whose pixels are one colour becomes that colour exactly, so flat areas
    match it and dither nothing; a slot freed so goes to the colour worst
    served."""
    reduced = img.quantize(
        colors=COLOURS - 1,
        method=Image.Quantize.MEDIANCUT,
        kmeans=8,
        dither=Image.Dither.NONE,
    )
    pal = reduced.getpalette() or []
    used = sorted(set(reduced.get_flattened_data()))
    palette = sorted({tuple(to15(pal[3 * i + k]) for k in range(3)) for i in used})
    counts: dict[tuple[int, int, int], int] = {}
    for c in img.get_flattened_data():
        counts[c] = counts.get(c, 0) + 1
    for _ in range(2):
        clusters: list[dict[tuple[int, int, int], int]] = [{} for _ in palette]
        for c, n in sorted(counts.items()):
            best = min(range(len(palette)), key=lambda i: (distance(palette[i], *c), i))
            clusters[best][c] = n
        moved = set()
        for cluster in clusters:
            total = sum(cluster.values())
            if not total:
                continue
            top, top_n = max(sorted(cluster.items()), key=lambda kv: kv[1])
            if 2 * top_n >= total:
                moved.add(top)
            else:
                moved.add(
                    tuple(
                        to15(sum(c[k] * n for c, n in cluster.items()) // total)
                        for k in range(3)
                    )
                )
        palette = sorted(moved)
        while len(palette) < COLOURS - 1 and len(palette) < len(counts):
            worst = max(
                sorted(counts.items()),
                key=lambda kv: kv[1] * min(distance(p, *kv[0]) for p in palette),
            )[0]
            if worst in palette:
                break
            palette = sorted(palette + [worst])
    return sorted(palette, key=lambda c: (c[0] * 299 + c[1] * 587 + c[2] * 114, c))


def scaled(e: int, num: int, den: int) -> int:
    """e * num / den, rounded toward 0 (the same on every machine)."""
    v = abs(e) * num // den
    return v if e >= 0 else -v


def dither(img: Image.Image, palette: list[tuple[int, int, int]]) -> list[list[int]]:
    """Colour indices (1 up) of img on palette: serpentine Floyd-Steinberg
    at DITHER strength, in integers."""
    w, h = img.size
    px = [[list(img.getpixel((x, y))) for x in range(w)] for y in range(h)]
    out = [[0] * w for _ in range(h)]
    num, den = DITHER
    for y in range(h):
        step = 1 if y % 2 == 0 else -1
        xs = range(w) if step == 1 else range(w - 1, -1, -1)
        for x in xs:
            r, g, b = (min(255, max(0, v)) for v in px[y][x])
            best = min(
                range(len(palette)), key=lambda i: (distance(palette[i], r, g, b), i)
            )
            out[y][x] = best + 1
            err = [v - c for v, c in zip((r, g, b), palette[best], strict=True)]
            for dx, dy, share in ((step, 0, 7), (-step, 1, 3), (0, 1, 5), (step, 1, 1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and ny < h:
                    for k in range(3):
                        px[ny][nx][k] += scaled(err[k], share * num, 16 * den)
    return out


def convert(path: Path, align: str = "top") -> bytes:
    """IMAGE as box art: an asset of SIZE x SIZE 4bpp tiles."""
    if align not in ALIGNS:
        raise ArtError(f"--align is one of {', '.join(ALIGNS)}, not {align!r}")
    img = square(opaque_rgb(path), align)
    if img.size[0] < 8:
        raise ArtError(f"{path}: too small ({img.size[0]} px)")
    img = img.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    # At the GBA's levels first: a flat area then matches its colour exactly
    # and passes no error on
    img = img.point(to15)
    palette = palette_of(img)
    pixels = dither(img, palette)
    bgr = [0] + [
        asset.bgr555(r, g, b) for r, g, b in palette
    ]  # bgr555 keeps the top 5 bits
    bgr += [0] * (COLOURS - len(bgr))
    data = asset.make_asset(pixels, bgr, 4)
    assert len(data) == FILE_BYTES
    return data


def preview(data: bytes, scale: int = 3) -> Image.Image:
    """Box art as the GBA shows it, scale times the size."""
    (_, w, h, _, _), palette, tiles = asset.read_asset(data)
    img = Image.new("RGB", (w * 8, h * 8))
    for y in range(h * 8):
        for x in range(w * 8):
            b = tiles[((y // 8) * w + x // 8) * 32 + (y % 8) * 4 + (x % 8) // 2]
            c = palette[b >> 4 if x % 2 else b & 15]
            img.putpixel(
                (x, y), (expand5(c & 31), expand5(c >> 5 & 31), expand5(c >> 10))
            )
    return img.resize((w * 8 * scale, h * 8 * scale), Image.Resampling.NEAREST)


def game_name(rom: str) -> str:
    """The game's file name: as the card lists it when rom is a file there
    (byte for byte, as the kernel looks it up), else as typed, in NFC (as
    cards and most PCs write names; macOS may hand a typed name over NFD)."""
    path = Path(rom)
    if path.is_file():
        want = unicodedata.normalize("NFC", path.name)
        for entry in sorted(os.listdir(path.parent)):
            if unicodedata.normalize("NFC", entry) == want:
                return entry
        return path.name
    return unicodedata.normalize("NFC", path.name)


def custom_name(rom: str) -> str:
    """The art file's name for a game: its file name (game_name) and EXT."""
    name = game_name(rom)
    if not name or name in (".", ".."):
        raise ArtError(f"{rom!r} names no file")
    art = name + EXT
    if len(name.encode("utf-8")) > GAME_NAME_MAX:
        raise ArtError(
            f"{name}: longer than the {GAME_NAME_MAX} bytes a game's name may have"
        )
    if len(art.encode("utf-16-le")) // 2 > NAME_MAX:
        raise ArtError(
            f"{name}: too long a name for its art file (FAT names hold {NAME_MAX} characters with {EXT})"
        )
    return art


def add(rom: str, image: Path, sd: Path, align: str) -> tuple[Path, bytes]:
    """Writes IMAGE as rom's custom art on the card at sd; (its path, data)."""
    if not sd.is_dir():
        raise ArtError(f"{sd}: not a folder (the card's, such as /Volumes/OMEGA)")
    name = custom_name(rom)
    data = convert(image, align)
    folder = sd / CUSTOM_DIR
    folder.mkdir(parents=True, exist_ok=True)
    out = folder / name
    out.write_bytes(data)
    return out, data


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="art.py",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = parser.add_subparsers(dest="command", required=True)
    p_add = sub.add_parser("add", help="custom art for a game, onto the card")
    p_add.add_argument("rom", metavar="ROM_OR_FILE_NAME")
    p_add.add_argument("image", metavar="IMAGE", type=Path)
    p_add.add_argument("sd", metavar="SD_PATH", type=Path)
    p_conv = sub.add_parser("convert", help="a picture into box art, anywhere")
    p_conv.add_argument("image", metavar="IMAGE", type=Path)
    p_conv.add_argument("out", metavar="OUT.bin", type=Path)
    for p in (p_add, p_conv):
        p.add_argument(
            "--align",
            default="top",
            choices=ALIGNS,
            help="the part of the picture kept",
        )
        p.add_argument(
            "--preview", type=Path, metavar="PNG", help="also write the result as a PNG"
        )
    args = parser.parse_args(argv)
    try:
        if args.command == "add":
            out, data = add(args.rom, args.image, args.sd, args.align)
            if Path(args.rom).suffix.lower() not in GAME_EXTS:
                print(
                    f"note: {Path(args.rom).name} has no game extension ({', '.join(GAME_EXTS)})"
                )
            print(f"wrote {out}")
        else:
            data = convert(args.image, args.align)
            args.out.write_bytes(data)
            print(f"wrote {args.out}")
        if args.preview:
            preview(data).save(args.preview)
            print(f"wrote {args.preview}")
    except ArtError as e:
        print(f"art.py: {e}")
        return 1
    except OSError as e:
        print(f"art.py: {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
