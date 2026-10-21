"""The consoles' placeholder boxes for the card, made from official boxes.

art_scrape.py (`just art-scrape`) calls make() and writes each result to
/torii/art/box-<console>.bin. The kernel shows that file for a game of that
console that has no art, in place of its own placeholder box (source/boxart.h).

Each console's template is a real box from libretro-thumbnails, fetched
like any other picture (dist/art-cache). At 80x80 its artwork, title
included, is replaced by a skeleton: a flat fill under a pale shimmer, a
ground, a torii gate, and two bars where the title was. Everything else of
the box stays: the console's band, logo and seals. The template's regions
are measured on that one picture, so a template is a picture name plus its
rectangles, in the picture's own pixels.

Like all box art, the pictures and the files made from them belong to their
owners. They are made on the user's PC for their own card and are never
committed.
"""

from dataclasses import dataclass

import art
import asset
from PIL import Image, ImageDraw

SIZE = art.SIZE  # 80


def rgb(h: str) -> tuple[int, int, int]:
    return tuple(art.to15(int(h[i : i + 2], 16)) for i in (1, 3, 5))  # type: ignore[return-value]


@dataclass(frozen=True)
class Skeleton:
    """The skeleton picture's colours."""

    fill: str
    shimmer: str
    ground: str
    gate: str
    bars: str

    def colours(self) -> list[tuple[int, int, int]]:
        return sorted(
            {
                rgb(c)
                for c in (self.fill, self.shimmer, self.ground, self.gate, self.bars)
            }
        )


# A light one on the white-banded boxes, a dark one in the NES's black box
LIGHT = Skeleton("#CED0D8", "#DCDEE6", "#C2C5CE", "#A6AAB6", "#B6B9C4")
DARK = Skeleton("#3A3A48", "#444454", "#33333F", "#5E5E70", "#4A4A5A")


@dataclass(frozen=True)
class Template:
    console: str  # its box-<console>.bin
    system: int  # art_scrape.SYSTEMS' index
    name: str  # the picture's name in Named_Boxarts
    art: tuple[int, int, int, int]  # the artwork, replaced (x0, y0, x1, y1)
    keep: tuple = ()  # polygons inside art kept as they are (point lists)
    blank: tuple = ()  # rectangles painted the box's own colour (a title off the art)
    blank_colour: str = "#000000"
    title: tuple[int, int, int, int] | None = None  # the bars' area, if not art's top
    skeleton: Skeleton = LIGHT


TEMPLATES = (
    # The left band reading GAME BOY ADVANCE, with ONLY FOR at its top
    Template("gba", 0, "Metroid Fusion (USA)", art=(100, 0, 512, 512)),
    # The band reading GAME BOY COLOR, and the ONLY FOR corner
    Template(
        "gbc",
        1,
        "Wario Land 3 (World) (En,Ja)",
        art=(108, 0, 512, 512),
        keep=(((384, 512), (512, 384), (512, 512)),),
    ),
    # The band reading GAME BOY, with the Nintendo logo at its top
    Template("gb", 2, "Kirby's Dream Land (USA, Europe)", art=(106, 0, 512, 512)),
    # The black box's top, as the list crops an NES cover: its picture goes,
    # and the title under it
    Template(
        "nes",
        3,
        "Excitebike (Japan, USA) (En)",
        art=(48, 28, 464, 388),
        blank=((40, 340, 474, 512),),
        title=(52, 410, 330, 500),
        skeleton=DARK,
    ),
)


def _masks(size, t: Template) -> tuple[Image.Image, Image.Image]:
    """(the artwork, the blanked parts) in the 80x80 frame, from masks drawn
    at the picture's size and scaled down."""

    def scaled(m):
        return m.resize((SIZE, SIZE), Image.Resampling.BOX).point(
            lambda v: 255 if v >= 128 else 0
        )

    a = Image.new("L", size, 0)
    d = ImageDraw.Draw(a)
    d.rectangle(t.art, fill=255)
    for poly in t.keep:
        d.polygon(list(poly), fill=0)
    b = Image.new("L", size, 0)
    d = ImageDraw.Draw(b)
    for r in t.blank:
        d.rectangle(r, fill=255)
    art_mask, blank_mask = scaled(a), scaled(b)
    blank_mask.paste(0, mask=art_mask)  # the artwork wins where they meet
    return art_mask, blank_mask


def _box(r, scale):
    """A rectangle of the picture's in the 80x80 frame, inclusive."""
    x0, y0, x1, y1 = r
    return (
        round(x0 * scale),
        round(y0 * scale),
        round(x1 * scale) - 1,
        round(y1 * scale) - 1,
    )


def _skeleton(img, art_mask, blank_mask, blank, area, title, sk: Skeleton) -> None:
    """Paints the skeleton into img where art_mask is set, and blank where
    blank_mask is: area is the artwork's box (80x80 frame), title the bars'
    box, drawn over either."""
    px, mk, bk = img.load(), art_mask.load(), blank_mask.load()
    x0, y0, x1, y1 = area
    w, h = x1 - x0 + 1, y1 - y0 + 1
    fill, shimmer, ground, gate, bars = (
        rgb(sk.fill),
        rgb(sk.shimmer),
        rgb(sk.ground),
        rgb(sk.gate),
        rgb(sk.bars),
    )
    gy = y0 + h * 4 // 5  # the ground's top
    for y in range(SIZE):
        for x in range(SIZE):
            if bk[x, y]:
                px[x, y] = blank
            if not mk[x, y]:
                continue
            c = fill
            d = (x - x0) + (y1 - y) - h * 0.35  # along the shimmer's diagonal
            if w * 0.45 <= d < w * 0.45 + max(4, w // 9):
                c = shimmer
            if y >= gy:
                c = ground
            px[x, y] = c

    def put(x, y, c, over_blank=False):
        if 0 <= x < SIZE and 0 <= y < SIZE and (mk[x, y] or (over_blank and bk[x, y])):
            px[x, y] = c

    def rect(ax, ay, bx, by, c):
        for yy in range(ay, by + 1):
            for xx in range(ax, bx + 1):
                put(xx, yy, c)

    # The gate: a kasagi with upturned ends, the nuki, two posts
    tw = max(18, w * 52 // 100) // 2 * 2
    th = h * 56 // 100
    cx = (x0 + x1 + 1) // 2
    lx, rx, top = cx - tw // 2, cx + tw // 2 - 1, gy - th
    rect(lx - 1, top, rx + 1, top, gate)
    put(lx - 2, top - 1, gate)
    put(rx + 2, top - 1, gate)
    rect(lx, top + 1, rx, top + 2, gate)
    nuki = top + 3 + max(3, th // 5)
    rect(cx - 1, top + 3, cx, nuki - 1, gate)
    rect(lx + 1, nuki, rx - 1, nuki + 1, gate)
    pw, inset = (3 if tw >= 24 else 2), max(3, tw // 6)
    rect(lx + inset, top + 3, lx + inset + pw - 1, gy, gate)
    rect(rx - inset - pw + 1, top + 3, rx - inset, gy, gate)
    rect(lx + inset - 1, gy, lx + inset + pw, gy, gate)
    rect(rx - inset - pw, gy, rx - inset + 1, gy, gate)

    # The title's two bars, rounded
    bx0, by0, bx1, by1 = title
    bw = bx1 - bx0 + 1
    for i, (frac, hh) in enumerate(((0.8, 4), (0.5, 3))):
        yy = by0 + 2 + i * 7
        if yy + hh - 1 > by1:
            break
        ww = max(6, int(bw * frac))
        for y in range(yy, yy + hh):
            for x in range(bx0 + 2, bx0 + 2 + ww):
                corner = (x in (bx0 + 2, bx0 + 1 + ww)) and y in (yy, yy + hh - 1)
                if not corner:
                    put(x, y, bars, True)


def make(t: Template, picture) -> bytes:
    """The template's placeholder from its picture (a path): an asset as
    art.convert makes."""
    src = art.square(art.opaque_rgb(picture), "top")  # as art.convert crops covers
    scale = SIZE / src.width
    img = src.resize((SIZE, SIZE), Image.Resampling.LANCZOS).point(art.to15)
    art_mask, blank_mask = _masks(src.size, t)
    area = _box(t.art, scale)
    title = (
        _box(t.title, scale) if t.title else (area[0], area[1], area[2], area[1] + 14)
    )
    blank = rgb(t.blank_colour)
    _skeleton(img, art_mask, blank_mask, blank, area, title, t.skeleton)

    # Colours: the skeleton's exactly, the rest from the box's own pixels
    own = sorted(set(t.skeleton.colours() + ([blank] if t.blank else [])))
    mk, bk = art_mask.load(), blank_mask.load()
    rest = [
        img.getpixel((x, y))
        for y in range(SIZE)
        for x in range(SIZE)
        if not (mk[x, y] or bk[x, y])
    ]
    line = Image.new("RGB", (max(1, len(rest)), 1))
    line.putdata(rest or [own[0]])
    palette = sorted(set(own + art.palette_of(line, art.COLOURS - 1 - len(own))))
    pixels = art.dither(img, palette)
    for y in range(SIZE):
        for x in range(SIZE):
            if mk[x, y] or bk[x, y]:  # its own colour, whatever the dither carried in
                pixels[y][x] = palette.index(img.getpixel((x, y))) + 1
    bgr = [0] + [asset.bgr555(*c) for c in palette]
    bgr += [0] * (art.COLOURS - len(bgr))
    data = asset.make_asset(pixels, bgr, 4)
    assert len(data) == art.FILE_BYTES
    return data
