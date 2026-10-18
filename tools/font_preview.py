#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""Render the font candidates as mock Torii screens (docs/research/fonts.md).

usage: font_preview.py [--out DIR] [--cache DIR] [KEY...]

Downloads each candidate's font files once into the cache (default
dist/fonts/candidates, checked against the SHA-256 below), converts them with
tools/font.py and draws, at 240x160, an SD list and a heading sample, in a
light and a dark palette. The text is drawn from the packed bitmaps the kernel
would get, so the sheet shows exactly what the blitter will draw. Writes
OUT/sheet.png (every candidate at 1x) and OUT/KEY@3x.png (one, 3x), and
prints a Markdown table of each pair's metrics.
"""

import argparse
import hashlib
import io
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import font as fontlib

# name: (url, sha256 of the download, member inside the archive or None)
TERMINUS = (
    "https://downloads.sourceforge.net/project/terminus-font/terminus-font-4.49/"
    "terminus-font-4.49.1.tar.gz"
)
ARK = "https://github.com/TakWolf/ark-pixel-font/releases/download/2026.10.16/"
FILES = {
    "ark-pixel-10px-proportional-latin.bdf": (
        ARK + "ark-pixel-font-10px-proportional-bdf-v2026.10.16.zip",
        "4958febd30d1622fc154f80e4354a1c87ecfbcf29aa94d5e10ebdbd28b59e77e",
        "ark-pixel-10px-proportional-latin.bdf",
    ),
    "ark-pixel-12px-proportional-latin.bdf": (
        ARK + "ark-pixel-font-12px-proportional-bdf-v2026.10.16.zip",
        "cbf4356bb72ae5aa7d99e2ab349faae3a730e238586cc29a7d9ef6ce268e478d",
        "ark-pixel-12px-proportional-latin.bdf",
    ),
    "Galmuri9.bdf": (
        "https://github.com/quiple/galmuri/releases/download/v2.40.4/Galmuri-v2.40.4.zip",
        "c8b3d9861a62ae73c8b1178091401cd79994812437ef386413f6dd54856e60e7",
        "Galmuri9.bdf",
    ),
    "Galmuri11-Bold.bdf": (
        "https://github.com/quiple/galmuri/releases/download/v2.40.4/Galmuri-v2.40.4.zip",
        "c8b3d9861a62ae73c8b1178091401cd79994812437ef386413f6dd54856e60e7",
        "Galmuri11-Bold.bdf",
    ),
    "spleen-6x12.bdf": (
        "https://github.com/fcambus/spleen/releases/download/2.2.0/spleen-2.2.0.tar.gz",
        "ec42925c6b56d2138c862b2f97147c872e472f674bf03423417d827a08d69a89",
        "spleen-2.2.0/spleen-6x12.bdf",
    ),
    "spleen-8x16.bdf": (
        "https://github.com/fcambus/spleen/releases/download/2.2.0/spleen-2.2.0.tar.gz",
        "ec42925c6b56d2138c862b2f97147c872e472f674bf03423417d827a08d69a89",
        "spleen-2.2.0/spleen-8x16.bdf",
    ),
    "ter-u12n.bdf": (
        TERMINUS,
        "d961c1b781627bf417f9b340693d64fc219e0113ad3a3af1a3424c7aa373ef79",
        "terminus-font-4.49.1/ter-u12n.bdf",
    ),
    "ter-u16b.bdf": (
        TERMINUS,
        "d961c1b781627bf417f9b340693d64fc219e0113ad3a3af1a3424c7aa373ef79",
        "terminus-font-4.49.1/ter-u16b.bdf",
    ),
    "cozette.bdf": (
        "https://github.com/slavfox/Cozette/releases/download/v.1.30.0/cozette.bdf",
        "8d740166af3a14053773ac7a8846bf288c8818a44f73ab28d4725d91b03a5639",
        None,
    ),
}

# key: (title, body file and font.py options, heading file and options)
BODY, HEADING = ["--line-height", "12"], ["--line-height", "16"]
CANDIDATES = {
    "ark": (
        "Ark Pixel 10px / 12px (OFL-1.1)",
        ("ark-pixel-10px-proportional-latin.bdf", [*BODY, "--squeeze", "--clip"]),
        ("ark-pixel-12px-proportional-latin.bdf", [*HEADING, "--squeeze", "--clip"]),
    ),
    "galmuri": (
        "Galmuri9 / Galmuri11 Bold (OFL-1.1)",
        ("Galmuri9.bdf", [*BODY, "--squeeze", "--clip"]),
        ("Galmuri11-Bold.bdf", [*HEADING, "--squeeze", "--clip"]),
    ),
    "spleen": (
        "Spleen 6x12 / 8x16, trimmed (BSD-2-Clause)",
        ("spleen-6x12.bdf", [*BODY, "--proportional"]),
        ("spleen-8x16.bdf", [*HEADING, "--proportional", "--squeeze", "--clip"]),
    ),
    "terminus": (
        "Terminus 12 / 16 bold, trimmed (OFL-1.1)",
        ("ter-u12n.bdf", [*BODY, "--proportional"]),
        ("ter-u16b.bdf", [*HEADING, "--proportional"]),
    ),
    "cozette": (
        "Cozette, trimmed, 13 px line / Spleen 8x16 (MIT, BSD-2-Clause)",
        ("cozette.bdf", ["--line-height", "13", "--proportional"]),
        ("spleen-8x16.bdf", [*HEADING, "--proportional", "--squeeze", "--clip"]),
    ),
}

W, H = 240, 160
BG, TEXT, DIM, BAR, BAR_TEXT, SEL, SEL_TEXT, RULE = range(8)


def gba(rgb: int) -> tuple[int, int, int]:
    """An RGB colour as the GBA shows it: 5 bits a channel."""
    channels = (rgb >> 16 & 0xFF, rgb >> 8 & 0xFF, rgb & 0xFF)
    return tuple((c >> 3) * 255 // 31 for c in channels)


PALETTES = {
    "light": [
        0xF4F1EA,
        0x1E1E22,
        0x6E6A64,
        0x2E5E8C,
        0xFFFFFF,
        0xC9DAEE,
        0x10243A,
        0xC8C2B8,
    ],
    "dark": [
        0x15171C,
        0xE4E4E6,
        0x8A8F98,
        0x262B35,
        0xF2F2F2,
        0x2F4F7A,
        0xFFFFFF,
        0x3A3F48,
    ],
}

GAMES = [
    ("Hacks/", ""),
    ("Advance Wars 2 - Black Hole Rising (USA, Australia).gba", "8M"),
    ("Castlevania - Aria of Sorrow (USA).gba", "8M"),
    ("Final Fantasy VI Advance (Europe) (En,Fr,De,Es,It).gba", "16M"),
    ("Golden Sun - L'Âge Perdu (France).gba", "16M"),
    ("Kirby & The Amazing Mirror (Europe).gba", "16M"),
    ("Mother 3 (Japan) [T-En by Tomato v1.2].gba", "32M"),
    ("Pokémon - Version Émeraude (France).gba", "16M"),
    ("Pokémon Mystery Dungeon - Red Rescue Team.gba", "32M"),
    ("WarioWare, Inc. - Mega Microgame$!.gba", "8M"),
    ("Zelda - The Minish Cap (Europe) {Rev 1}.gba", "16M"),
]


class Face:
    """A converted font, drawn from its packed bitmaps."""

    def __init__(self, path: Path, options: list[str], name: str):
        args = fontlib.parse_args([str(path), "--name", name, *options])
        self.font = fontlib.load(args)
        self.packed = fontlib.pack(self.font)
        self.line = self.font.line_height

    def width(self, text: str) -> int:
        return sum(self.packed.entries[fontlib.index(ord(c))][1] for c in text)

    def fit(self, text: str, width: int) -> str:
        """text, cut with "..." to fit width."""
        if self.width(text) <= width:
            return text
        while text and self.width(text + "...") > width:
            text = text[:-1]
        return text.rstrip() + "..."

    def draw(
        self, image: Image.Image, x: int, y: int, text: str, colour, right: int = W
    ):
        rb = self.font.row_bytes
        for c in text:
            offset, advance, top, rows, width = self.packed.entries[
                fontlib.index(ord(c))
            ]
            for k in range(rows):
                start = offset + k * rb
                row = int.from_bytes(self.packed.bits[start : start + rb], "little")
                for col in range(width):
                    px, py = x + col, y + top + k
                    if row >> col & 1 and 0 <= px < right and 0 <= py < H:
                        image.putpixel((px, py), colour)
            x += advance


def screen(palette: list[int]) -> Image.Image:
    image = Image.new("P", (W, H), BG)
    image.putpalette([c for rgb in palette for c in gba(rgb)])
    return image


def fill(image: Image.Image, box: tuple[int, int, int, int], colour: int):
    image.paste(colour, box)


def sd_list(body: Face, heading: Face, palette: list[int]) -> Image.Image:
    image = screen(palette)
    bar = heading.line + 2
    fill(image, (0, 0, W, bar), BAR)
    heading.draw(image, 6, 1, "SD  /GBA", BAR_TEXT)
    count = "3/248"
    heading.draw(image, W - 6 - heading.width(count), 1, count, BAR_TEXT)
    status_top = H - body.line - 3
    rows = (status_top - bar - 2) // body.line
    y = bar + 1
    for i, (name, size) in enumerate(GAMES[:rows]):
        colour, dim = TEXT, DIM
        if i == 3:
            fill(image, (0, y, W, y + body.line), SEL)
            colour = dim = SEL_TEXT
        body.draw(image, W - 6 - body.width(size), y, size, dim)
        body.draw(image, 6, y, body.fit(name, W - 12 - body.width("32M") - 6), colour)
        y += body.line
    fill(image, (0, status_top, W, status_top + 1), RULE)
    body.draw(image, 6, status_top + 2, "FLASH 128K  RTC  [Hacks]", DIM)
    clock = "Mon 14:32"
    body.draw(image, W - 6 - body.width(clock), status_top + 2, clock, TEXT)
    return image


def latin1_lines(face: Face, width: int) -> list[str]:
    chars = [chr(c) for c in range(0x21, 0x100) if not 0x7F <= c < 0xA1 and c != 0xAD]
    lines, line = [], ""
    for c in chars:
        if face.width(line + c) > width:
            lines.append(line)
            line = ""
        line += c
    return [*lines, line]


def sample(body: Face, heading: Face, palette: list[int]) -> Image.Image:
    image = screen(palette)
    y = 3
    heading.draw(image, 6, y, "Pokémon Émeraude", TEXT)
    y += heading.line
    for text, colour in (
        ("Ça marche: Français, ½ ¾ © ®, µs, ¿Qué? ¡Sí!", DIM),
        ("Zoë's Café: crème brûlée, jalapeño, Straße, Øresund.", TEXT),
    ):
        body.draw(image, 6, y, body.fit(text, W - 12), colour)
        y += body.line
    fill(image, (6, y + 1, W - 6, y + 2), RULE)
    y += 4
    for line in latin1_lines(body, W - 12):
        if y + body.line > H - heading.line - 2:
            break
        body.draw(image, 6, y, line, TEXT)
        y += body.line
    heading.draw(
        image,
        6,
        H - heading.line - 2,
        heading.fit("ÀÉÎÕÜ Çß ñ 0123 [v1.2]", W - 12),
        DIM,
    )
    return image


def fetch(name: str, cache: Path) -> Path:
    path = cache / name
    if path.exists():
        return path
    url, digest, member = FILES[name]
    archive = cache / Path(url).name
    if not archive.exists():
        print(f"font_preview.py: fetching {url}", file=sys.stderr)
        with urllib.request.urlopen(url) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != digest:
            sys.exit(f"font_preview.py: {url} does not match its SHA-256")
        cache.mkdir(parents=True, exist_ok=True)
        archive.write_bytes(data)
    data = archive.read_bytes()
    if member is None:
        content = data
    elif archive.name.endswith(".zip"):
        content = zipfile.ZipFile(io.BytesIO(data)).read(member)
    else:
        with tarfile.open(fileobj=io.BytesIO(data)) as tar:
            content = tar.extractfile(member).read()
    path.write_bytes(content)
    return path


def save(image: Image.Image, path: Path):
    """Save as a palette PNG; the images have far fewer than 256 colours."""
    paletted = image.convert("P", palette=Image.Palette.ADAPTIVE, colors=256)
    assert paletted.convert("RGB").tobytes() == image.tobytes(), "colours lost"
    paletted.save(path, optimize=True)


def metrics(key: str, title: str, body: Face, heading: Face) -> str:
    lower = "abcdefghijklmnopqrstuvwxyz"
    name = "Pokémon - Version Émeraude (France).gba"

    def chars(cps: list[int]) -> str:
        return "".join(chr(cp) for cp in sorted(set(cps))) or "none"

    return (
        f"| {key} | {body.line} / {heading.line} "
        f"| {body.width(lower) / 26:.1f} / {heading.width(lower) / 26:.1f} "
        f"| {body.width(name)} | {len(body.packed.bits) + 6 * fontlib.REPLACEMENT + 6} "
        f"/ {len(heading.packed.bits) + 6 * fontlib.REPLACEMENT + 6} "
        f"| {' '.join(f'U+{cp:04X}' for cp in sorted(set(body.font.missing))) or 'none'} "
        f"| {chars(body.font.squeezed + heading.font.squeezed)} "
        f"| {chars(body.font.clipped + heading.font.clipped)} |"
    )


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--out", type=Path, default=Path("docs/research/fonts"))
    p.add_argument("--cache", type=Path, default=Path("dist/fonts/candidates"))
    p.add_argument("keys", nargs="*", default=list(CANDIDATES))
    args = p.parse_args(argv)
    args.out.mkdir(parents=True, exist_ok=True)

    gap = 4
    rows = []
    print(
        "| pair | line px | mean a-z advance | Pokémon name px | bytes | missing | squeezed | clipped |"
    )
    print("|---|---|---|---|---|---|---|---|")
    for key in args.keys:
        title, (body_file, body_opts), (heading_file, heading_opts) = CANDIDATES[key]
        body = Face(fetch(body_file, args.cache), body_opts, "body")
        heading = Face(fetch(heading_file, args.cache), heading_opts, "heading")
        print(metrics(key, title, body, heading))
        screens = [
            make(body, heading, PALETTES[pal])
            for make in (sd_list, sample)
            for pal in ("light", "dark")
        ]
        top = body.line + gap
        row = Image.new("RGB", (4 * W + 5 * gap, top + H + gap), gba(0x9A9A9A))
        body.draw(row, gap, gap // 2, title, gba(0x101010), right=row.width)
        for i, s in enumerate(screens):
            row.paste(s.convert("RGB"), (gap + i * (W + gap), top))
        rows.append(row)
        save(
            row.resize((row.width * 3, row.height * 3), Image.NEAREST),
            args.out / f"{key}@3x.png",
        )
    sheet = Image.new("RGB", (rows[0].width, sum(r.height for r in rows)))
    y = 0
    for r in rows:
        sheet.paste(r, (0, y))
        y += r.height
    save(sheet, args.out / "sheet.png")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
