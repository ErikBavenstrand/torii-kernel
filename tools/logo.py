#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""Torii's logo: a pixel torii gate and the name in the menu's Galmuri font.

usage: logo.py [OUT_DIR]   (`just logo`; docs/media by default)

Writes logo-dark.png and logo-light.png (transparent, for the README on
GitHub's dark and light themes), icon.png (512x512, the gate on the menu's
dark background) and social-preview.png (1280x640, for the repository's
social preview, which GitHub only takes as an upload in Settings).
"""

import io
import sys
import tempfile
from pathlib import Path

from PIL import BdfFontFile, Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent

BG = (24, 24, 33)  # the dark theme's bg (assets/ui/palette.json)
INK_DARK = (239, 239, 231)  # dark theme ink
INK_LIGHT = (41, 41, 49)  # light theme ink
SUB_DARK = (181, 173, 165)
SUB_LIGHT = (107, 99, 90)
RED = (222, 66, 49)
DARKRED = (165, 41, 33)
BLACK = (16, 16, 20)
RIM = (90, 86, 96)

GATE = [
    "KK........................KK",
    "KKKKKKKKKKKKKKKKKKKKKKKKKKKK",
    ".KKKKKKKKKKKKKKKKKKKKKKKKKK.",
    "..RRRRRRRRRRRRRRRRRRRRRRRR..",
    "....RRR.....RRRR.....RRR....",
    "....RRR.....RRRR.....RRR....",
    ".RRRRRRRRRRRRRRRRRRRRRRRRRR.",
    ".DDDDDDDDDDDDDDDDDDDDDDDDDD.",
    *["....RRR..............RRR...."] * 10,
    "....RRD..............RRD....",
    "....RRD..............RRD....",
    "...KKKKK............KKKKK...",
    "...KKKKK............KKKKK...",
]
COLOURS = {"K": BLACK, "R": RED, "D": DARKRED}


def font(name: str) -> ImageFont.ImageFont:
    # Pillow's BDF reader takes ASCII properties only: the copyright line
    # has a non-ASCII sign, which no glyph needs
    data = (ROOT / f"assets/fonts/galmuri/{name}.bdf").read_bytes()
    data = bytes(b if b < 128 else ord("?") for b in data)
    pil = BdfFontFile.BdfFontFile(io.BytesIO(data))
    # Pillow writes a font as NAME.pil and NAME.pbm and loads it from there
    with tempfile.TemporaryDirectory() as tmp:
        pil.save(str(Path(tmp) / name))
        return ImageFont.load(str(Path(tmp) / f"{name}.pil"))


def text(s: str, f: ImageFont.ImageFont, colour, scale: int) -> Image.Image:
    left, top, right, bottom = f.getbbox(s)
    im = Image.new("RGBA", (right - left + 1, bottom - top + 1), (0, 0, 0, 0))
    ImageDraw.Draw(im).text((-left, -top), s, font=f, fill=colour)
    return im.resize((im.width * scale, im.height * scale), Image.Resampling.NEAREST)


def gate(scale: int, rim: bool) -> Image.Image:
    h, w = len(GATE), len(GATE[0])
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for y, row in enumerate(GATE):
        for x, c in enumerate(row):
            if c in COLOURS:
                im.putpixel((x, y), (*COLOURS[c], 255))
    im = im.resize((w * scale, h * scale), Image.Resampling.NEAREST)
    if not rim:
        return im
    # A rim one gate pixel wide, so the black beam shows on a dark background
    out = Image.new("RGBA", (im.width + 2 * scale, im.height + 2 * scale), (0, 0, 0, 0))
    solid = Image.new("RGBA", im.size, (*RIM, 255))
    for dx in (-scale, 0, scale):
        for dy in (-scale, 0, scale):
            out.paste(solid, (scale + dx, scale + dy), im.split()[3])
    out.paste(im, (scale, scale), im)
    return out


def lockup(scale: int, ink, sub_ink, rim: bool, subtitle: bool) -> Image.Image:
    """The gate beside the name (and the tagline under it), transparent."""
    bold, body = font("Galmuri11-Bold"), font("Galmuri9")
    g = gate(scale, rim)
    word = text("Torii", bold, ink, scale + 2)
    sub = text(
        "Custom kernel for the EZ-Flash Omega DE", body, sub_ink, max(2, scale // 3)
    )
    gap = scale * 6
    right = max(word.width, sub.width + scale // 2 if subtitle else 0)
    block_h = word.height + (scale * 3 + sub.height if subtitle else 0)
    im = Image.new(
        "RGBA", (g.width + gap + right, max(g.height, block_h)), (0, 0, 0, 0)
    )
    im.paste(g, (0, (im.height - g.height) // 2), g)
    y = (im.height - block_h) // 2
    im.paste(word, (g.width + gap, y), word)
    if subtitle:
        im.paste(sub, (g.width + gap + scale // 2, y + word.height + scale * 3), sub)
    return im


def main(argv: list[str]) -> int:
    out = Path(argv[0]) if argv else ROOT / "docs/media"
    out.mkdir(parents=True, exist_ok=True)
    lockup(8, INK_DARK, SUB_DARK, True, True).save(out / "logo-dark.png", optimize=True)
    lockup(8, INK_LIGHT, SUB_LIGHT, False, True).save(
        out / "logo-light.png", optimize=True
    )

    icon = Image.new("RGB", (512, 512), BG)
    g = gate(14, True)
    icon.paste(g, ((512 - g.width) // 2, (512 - g.height) // 2), g)
    icon.save(out / "icon.png", optimize=True)

    social = Image.new("RGB", (1280, 640), BG)
    lk = lockup(10, INK_DARK, SUB_DARK, True, True)
    social.paste(lk, ((1280 - lk.width) // 2, (640 - lk.height) // 2), lk)
    social.save(out / "social-preview.png", optimize=True)
    print(f"logo: logo-dark.png, logo-light.png, icon.png, social-preview.png in {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
