"""The placeholder boxes list with art shows for a game without art.

usage: uv run --with pillow python -I tools/mockup/boxes.py [SHEET.png [REF]]

Draws assets/ui/boxes/*.png (then run `just assets`): each console's box
as its real (US) boxes are laid out, redrawn at 80x80: the Game Boys' silver
band down the left with the console's name reading up it, the rating box
and the Nintendo oval or ONLY FOR corner at the bottom; the NES black box
with its sprite drawn on a grid; a slate band for the rest. Where the game's
art and name would be is a skeleton, so it reads as "no art yet": title bars
over a muted torii gate before a pale sun (on the NES, a pixel torii before
a red sun). The PNGs are drawn in the light theme's colours; art.toml's
box_dark replaces some in the dark theme. SHEET, if given, shows the boxes on both themes' cards at 1x and 4x,
under the boxes at git revision REF when that is given.
"""

import io
import subprocess
import sys
import tomllib
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "assets/ui/boxes"
CLEAR = (0, 0, 0, 0)


def C(h):
    return (int(h[1:3], 16) & 0xF8, int(h[3:5], 16) & 0xF8, int(h[5:7], 16) & 0xF8, 255)


# The skeleton's tones, warm like the paper (art.toml turns them graphite)
SKY = C("#E8E0D8")
SUN = C("#F8F0E8")
GROUND = C("#E0D8D0")
GATE = C("#D0B0A8")  # the gate, the title bars and the GBA seal: a faded vermilion
EDGE = C("#A8A098")  # the box's outline
SHADOW = C("#D8D0C8")
LIGHT_TONES = (SKY, SUN, GROUND, GATE)

# 3x5 letters (the GBA band, GBC's COLOR) and bold italic 6x7 ones (GAME BOY)
FONT = {
    "A": [".#.", "#.#", "###", "#.#", "#.#"],
    "B": ["##.", "#.#", "##.", "#.#", "##."],
    "C": [".##", "#..", "#..", "#..", ".##"],
    "D": ["##.", "#.#", "#.#", "#.#", "##."],
    "E": ["###", "#..", "##.", "#..", "###"],
    "G": [".##", "#..", "#.#", "#.#", ".##"],
    "L": ["#..", "#..", "#..", "#..", "###"],
    "M": ["#...#", "##.##", "#.#.#", "#...#", "#...#"],
    "N": ["#..#", "##.#", "#.##", "#..#", "#..#"],
    "O": [".#.", "#.#", "#.#", "#.#", ".#."],
    "R": ["##.", "#.#", "##.", "#.#", "#.#"],
    "V": ["#.#", "#.#", "#.#", "#.#", ".#."],
    "Y": ["#.#", "#.#", ".#.", ".#.", ".#."],
    " ": ["..", "..", "..", "..", ".."],
}
# 4x5 letters, wider for the bands' bold words
WIDE = {
    "A": [".##.", "#..#", "####", "#..#", "#..#"],
    "B": ["###.", "#..#", "###.", "#..#", "###."],
    "C": [".###", "#...", "#...", "#...", ".###"],
    "D": ["###.", "#..#", "#..#", "#..#", "###."],
    "E": ["####", "#...", "###.", "#...", "####"],
    "G": [".###", "#...", "#.##", "#..#", ".###"],
    "L": ["#...", "#...", "#...", "#...", "####"],
    "M": ["#...#", "##.##", "#.#.#", "#...#", "#...#"],
    "N": ["#..#", "##.#", "#.##", "#..#", "#..#"],
    "O": [".##.", "#..#", "#..#", "#..#", ".##."],
    "R": ["###.", "#..#", "###.", "#.#.", "#..#"],
    "V": ["#...#", "#...#", ".#.#.", ".#.#.", "..#.."],
    "Y": ["#...#", ".#.#.", "..#..", "..#..", "..#.."],
    " ": ["..", "..", "..", "..", ".."],
}
BOLD = {
    "G": [".####.", "##....", "##....", "##.###", "##..##", "##..##", ".####."],
    "A": [".####.", "##..##", "##..##", "######", "##..##", "##..##", "##..##"],
    "M": ["##...##", "###.###", "##.#.##", "##.#.##", "##...##", "##...##", "##...##"],
    "E": ["######", "##....", "##....", "#####.", "##....", "##....", "######"],
    "B": ["#####.", "##..##", "##..##", "#####.", "##..##", "##..##", "#####."],
    "O": [".####.", "##..##", "##..##", "##..##", "##..##", "##..##", ".####."],
    "Y": ["##..##", "##..##", "##..##", ".####.", "..##..", "..##..", "..##.."],
    " ": ["..", "..", "..", "..", "..", "..", ".."],
}


def torii(w, small=False):
    """A torii gate w pixels wide, as rows ("#" drawn): the kasagi with
    upturned ends, the shimaki, the gakuzuka, the nuki through the pillars,
    then the pillars (the row ending "|" repeats down to the ground) and
    their footings."""
    pw = 2 if small else 3
    inset = round(w * 0.2)
    mid = w // 2

    def row(*spans):
        r = ["."] * w
        for a, b in spans:
            for x in range(a, b + 1):
                r[x] = "#"
        return "".join(r)

    left = (inset, inset + pw - 1)
    right = (w - 1 - inset - pw + 1, w - 1 - inset)
    rows = [row((0, 0), (w - 1, w - 1)), row((0, w - 1)), row((1, w - 2))]
    if not small:
        rows.append(row())
    rows.append(row((2, w - 3)))
    gak = row(left, (mid - 1, mid), right)
    rows += [gak] * (1 if small else 3)
    nuki = row((inset - 3, w - 1 - inset + 3))
    rows += [nuki] * (1 if small else 2)
    rows.append(row(left, right) + "|")
    rows.append(row((left[0] - 1, left[1] + 1), (right[0] - 1, right[1] + 1)))
    return rows


class Pic:
    def __init__(s):
        s.im = Image.new("RGBA", (80, 80), CLEAR)
        s.px = s.im.load()

    def p(s, x, y, c):
        if 0 <= x < 80 and 0 <= y < 80:
            s.px[x, y] = c

    def get(s, x, y):
        return s.px[x, y]

    def rect(s, x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                s.p(x, y, c)

    def pill(s, x, y, w, h, c):
        """A rounded bar: its corners keep what was under them."""
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                corner = xx in (x, x + w - 1) and yy in (y, y + h - 1)
                if not corner or h < 3:
                    s.p(xx, yy, c)

    def disc(s, cx, cy, r, c, x0=0, y0=0, x1=79, y1=79):
        """A filled circle, clipped to (x0, y0)-(x1, y1)."""
        for y in range(max(y0, int(cy - r - 1)), min(y1, int(cy + r + 1)) + 1):
            for x in range(max(x0, int(cx - r - 1)), min(x1, int(cx + r + 1)) + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r + r * 0.6:
                    s.p(x, y, c)

    def sprite(s, rows, x, y, c, height):
        """A gate (TORII), its "|" row repeated so it stands height rows."""
        fixed = len(rows) - 1
        yy = y
        for row in rows:
            n = height - fixed if row.endswith("|") else 1
            for _ in range(n):
                for gx, b in enumerate(row.rstrip("|")):
                    if b == "#":
                        s.p(x + gx, yy, c)
                yy += 1
        return yy

    def text(s, txt, x, y, colours):
        """txt in the 3x5 font, each letter in its colour (spaces take none)."""
        i = 0
        for ch in txt:
            g = FONT[ch]
            for gy, row in enumerate(g):
                for gx, b in enumerate(row):
                    if b == "#":
                        s.p(x + gx, y + gy, colours[i])
            x += len(g[0]) + 1
            if ch != " ":
                i += 1

    def vtext(s, txt, x, ybottom, colour, font=FONT, gap=1):
        """txt turned to read upwards from ybottom, its glyphs' rows going
        right from column x; colour(i) gives letter i's colour. Returns the
        row above the last letter."""
        yy = ybottom
        for i, ch in enumerate(txt):
            g = font[ch]
            for gy, row in enumerate(g):
                for gx, b in enumerate(row):
                    if b == "#":
                        s.p(x + gy, yy - gx, colour(i))
            yy -= len(g[0]) + gap
        return yy

    @staticmethod
    def vlength(txt, font=FONT, gap=1):
        return sum(len(font[ch][0]) for ch in txt) + gap * (len(txt) - 1)


def frame(pic, body):
    """The box: 79x79 with one-pixel rounded corners, a 1 px shadow right
    and below."""
    pic.rect(1, 1, 77, 77, body)
    for i in range(1, 78):
        pic.p(i, 0, EDGE)
        pic.p(i, 78, EDGE)
        pic.p(0, i, EDGE)
        pic.p(78, i, EDGE)
        pic.p(i + 1, 79, SHADOW)
        pic.p(79, i + 1, SHADOW)
    for x, y in ((1, 1), (77, 1), (1, 77), (77, 77)):
        pic.p(x, y, EDGE)
    pic.p(79, 79, CLEAR)


def corners(pic):
    for x, y in ((1, 1), (77, 1), (1, 77), (77, 77)):
        pic.p(x, y, EDGE)


def skeleton(pic, x0, y0, x1, y1, plate, tones=LIGHT_TONES, small=False, above=0):
    """The "no art" picture in (x0, y0)-(x1, y1): sky, a sun behind a torii
    gate standing on a ground plate `plate` rows tall. Returns the plate's
    top row."""
    sky, sun, ground, gate = tones
    pic.rect(x0, y0, x1, y1, sky)
    gy = y1 - plate + 1
    # The ground: the plate, rising into two low hills behind the gate
    W = x1 - x0 + 1
    for x in range(x0, x1 + 1):
        h = 0.0
        for c, r, top in ((0.12, 0.42, 0.13), (0.9, 0.5, 0.2)):
            d = (x - x0 + 0.5 - c * W) / (r * W)
            if abs(d) < 1:
                h = max(h, top * (gy - y0) * (1 - d * d) ** 0.5)
        pic.rect(x, gy - round(h), x, y1, ground)
    w = (x1 - x0 + 1) * (44 if small else 62) // 100 // 2 * 2
    rows = torii(w, small)
    room = gy - y0 - above
    height = max(
        len(rows) + 2, min(int(room * 0.84), int(w * (0.78 if small else 0.9)))
    )
    gx = (x0 + x1 + 1) // 2 - w // 2
    ty = gy - height
    # The sun, framed by the gate under its beams
    r = w * 0.28
    pic.disc(gx + w / 2 - 0.5, ty + height * 0.45, r, sun, x0, y0, x1, gy - 1)
    pic.sprite(rows, gx, ty, gate, height)
    return gy


SILVER = C("#F0F0F0")  # the Game Boys' band
SILVERD = C("#C0C0C8")  # its edge
BAND_X = 13  # the band's last column, its edge
ART_X = BAND_X + 1


def band(pic):
    """The Game Boys' silver band down the left edge."""
    pic.rect(1, 1, BAND_X - 1, 77, SILVER)
    pic.rect(BAND_X, 1, BAND_X, 77, SILVERD)


def title_bars(pic, x0, x1, y, colour):
    """Two skeleton title bars centred in x0..x1 from row y, as the games'
    logos sit at the top of their art."""
    w = x1 - x0 + 1
    cx = x0 + w // 2
    for share, height, dy in ((70, 5, 0), (44, 3, 7)):
        width = w * share // 100 // 2 * 2
        pic.pill(cx - width // 2, y + dy, width, height, colour)


def rating(pic, x, y):
    """The rating box at the art's bottom left, as an outline."""
    pic.rect(x, y, x + 6, y + 8, GATE)
    pic.rect(x + 1, y + 1, x + 5, y + 7, GROUND)
    pic.rect(x + 2, y + 2, x + 4, y + 4, GATE)
    pic.rect(x + 2, y + 6, x + 4, y + 6, GATE)


def nintendo(pic, x, y, ring, inside):
    """The Nintendo oval, 14x7: a ring round a word of dashes."""
    pic.pill(x, y, 14, 7, ring)
    pic.pill(x + 1, y + 1, 12, 5, inside)
    for dx in (3, 4, 6, 7, 8, 10):
        pic.p(x + dx, y + 3, ring)


boxes = {}
TONES_TOP = 20  # the art's rows above the gate's room: the title bars

# --- GBA: the silver band with GAME BOY ADVANCE reading up it in grey, the
# word ADVANCE bold
p = Pic()
frame(p, SKY)
band(p)
GREY = C("#383838")
y = p.vtext("GAME BOY", 5, 74, lambda i: GREY)
p.vtext("ADVANCE", 4, y - 3, lambda i: GREY, WIDE)
gy = skeleton(p, ART_X, 1, 77, 77, 12, above=TONES_TOP)
title_bars(p, ART_X, 77, 6, GATE)
rating(p, ART_X + 4, 67)
p.pill(62, 69, 12, 6, GATE)  # the publisher
corners(p)
boxes["gba"] = p

# --- GB: the band with a big indigo GAME BOY and the red Nintendo mark
# above it; the Nintendo oval at the bottom right
p = Pic()
frame(p, SKY)
band(p)
INDIGO = C("#3038A0")
RED = C("#D02828")
WHITE = C("#F8F8F0")
y0 = 74
p.vtext("GAME BOY", 3, y0, lambda i: INDIGO, BOLD)
p.pill(2, 3, 10, 5, RED)  # Nintendo
for x in (4, 6, 8, 9):
    p.p(x, 5, WHITE)
gy = skeleton(p, ART_X, 1, 77, 77, 12, above=TONES_TOP)
title_bars(p, ART_X, 77, 6, GATE)
rating(p, ART_X + 4, 67)
nintendo(p, 61, 69, RED, WHITE)
corners(p)
boxes["gb"] = p

# --- GBC: the band with GAME BOY in indigo and COLOR in five colours, and
# the black ONLY FOR corner at the bottom right
p = Pic()
frame(p, SKY)
band(p)
NAVY = C("#283898")
RAINBOW = [C("#D82830"), C("#7838B8"), C("#48A030"), C("#E8B010"), C("#2860D0")]
y = p.vtext("GAME BOY", 4, 74, lambda i: NAVY, WIDE)
p.vtext("COLOR", 4, y - 3, lambda i: RAINBOW[i], WIDE)
gy = skeleton(p, ART_X, 1, 77, 77, 12, above=TONES_TOP)
title_bars(p, ART_X, 77, 6, GATE)
rating(p, ART_X + 4, 67)
CORNER = C("#101018")
for i in range(19):
    p.rect(77 - i, 59 + i, 77, 59 + i, CORNER)
for i, c in enumerate(RAINBOW):  # COLOR, along its edge
    x, y = 66 + i * 2, 74 - i * 2
    p.rect(x, y, x + 1, y + 1, c)
corners(p)
boxes["gbc"] = p

# --- NES: the black box: the title on black over the grid, the game's
# sprite drawn on the grid (a torii before the sun), the seal
p = Pic()
BLACK = C("#181820")
frame(p, BLACK)
DIM = C("#505060")
GRID = C("#282838")
SEAL = C("#C82830")
SEALW = C("#E0E0E0")
# The sprite's tones, the same in both themes on the black box
CELL_SUN = C("#985060")
CELL_GROUND = C("#383848")
CELL_GATE = C("#C8C8D8")
p.pill(6, 5, 44, 5, DIM)
p.pill(6, 12, 28, 3, DIM)
for y in range(18, 65, 3):
    p.rect(1, y, 77, y, GRID)
for x in range(3, 78, 4):
    p.rect(x, 18, x, 64, GRID)
SPRITE = [
    "...................",
    "...................",
    "...#...........#...",
    "...#############...",
    "....###########....",
    "......#..#..#......",
    ".....#########.....",
    "......#.....#......",
    "......#.....#......",
    "......#.....#......",
    "......#.....#......",
    ".....##.....##.....",
    "ggggggggggggggggggg",
    "ggggggggggggggggggg",
    "ggggggggggggggggggg",
]


def cell(cx, cy, c):
    x, y = 4 + cx * 4, 19 + cy * 3
    p.rect(x, y, min(x + 2, 77), y + 1, c)


for cy in range(15):
    for cx in range(19):
        if (cx - 9) ** 2 + (cy - 7) ** 2 <= 4.2**2:
            cell(cx, cy, CELL_SUN)
for cy, row in enumerate(SPRITE):
    for cx, b in enumerate(row):
        if b == "#":
            cell(cx, cy, CELL_GATE)
        elif b == "g":
            cell(cx, cy, CELL_GROUND)
nintendo(p, 6, 68, SEAL, SEALW)  # the seal
p.pill(44, 70, 28, 3, DIM)  # Nintendo Entertainment System
corners(p)
boxes["nes"] = p

# --- Anything else: a plain slate band holding the title bars
p = Pic()
frame(p, SKY)
SLATE = C("#586878")
SLATEL = C("#8898A8")
p.rect(1, 1, 77, 15, SLATE)
p.pill(6, 4, 34, 4, SLATEL)
p.pill(6, 10, 20, 3, SLATEL)
p.rect(1, 16, 77, 16, SLATEL)
gy = skeleton(p, 1, 17, 77, 77, 12, above=4)
rating(p, 5, 67)
corners(p)
boxes["other"] = p


def dark_map(toml_text):
    return {C(k)[:3]: C(v) for k, v in tomllib.loads(toml_text)["box_dark"].items()}


def darken(im, dark):
    im = im.copy()
    px = im.load()
    for y in range(80):
        for x in range(80):
            c = px[x, y]
            if c[3] and c[:3] in dark:
                px[x, y] = dark[c[:3]]
    return im


def git_show(ref, path):
    return subprocess.run(
        ["git", "show", f"{ref}:{path}"], cwd=ROOT, check=True, capture_output=True
    ).stdout


def sheet(path, ref=None):
    """Before (the boxes and dark map at git revision ref, if given) and
    after, on the light theme's paper and card, then the dark theme's; at
    1x above, then 4x."""
    sets = [
        (
            [pic.im for pic in boxes.values()],
            dark_map((ROOT / "assets/ui/art.toml").read_text()),
        )
    ]
    if ref:
        ims = [
            Image.open(io.BytesIO(git_show(ref, f"assets/ui/boxes/{n}.png"))).convert(
                "RGBA"
            )
            for n in boxes
        ]
        sets.insert(0, (ims, dark_map(git_show(ref, "assets/ui/art.toml").decode())))
    themes = [
        ((0xF7, 0xEF, 0xE7), (255, 255, 255), False),
        ((0x18, 0x18, 0x21), (0x29, 0x29, 0x31), True),
    ]
    W, H = len(boxes) * 92 + 8, len(sets) * len(themes) * 92 + 8
    small = Image.new("RGB", (W, H))
    for t, (bg, card, dark) in enumerate(themes):
        for k, (ims, dmap) in enumerate(sets):
            y = (t * len(sets) + k) * 92
            small.paste(bg, (0, y, W, y + 100))
            for i, im in enumerate(ims):
                x = 8 + i * 92
                small.paste(card, (x, y + 4, x + 84, y + 88))
                shown = darken(im, dmap) if dark else im
                small.paste(shown, (x + 2, y + 6), shown)
    out = Image.new("RGB", (W * 4, H * 5), (0xF7, 0xEF, 0xE7))
    out.paste(small, (0, 0))
    out.paste(small.resize((W * 4, H * 4), Image.NEAREST), (0, H))
    out.save(path)


if __name__ == "__main__":
    for n, pic in boxes.items():
        cols = {pic.im.getpixel((x, y)) for x in range(80) for y in range(80)} - {CLEAR}
        print(n, len(cols), "colours")
        assert len(cols) <= 15, n
        pic.im.save(OUT / f"{n}.png")
    if len(sys.argv) > 1:
        sheet(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
