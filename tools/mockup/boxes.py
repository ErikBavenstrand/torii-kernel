"""The placeholder boxes list with art shows for a game without art.

usage: uv run --with pillow python -I tools/mockup/boxes.py [SHEET.png [REF]]

Draws assets/ui/boxes/*.png (then run `just assets`): each console's box
as its real boxes are laid out (GBA's band down the left, the Game Boys'
band across the top, NES's black box), with a skeleton where the game's art
and name would be: a muted torii gate before a pale sun, on a ground plate
that holds two title bars, so it reads as "no art yet". The PNGs are drawn
in the light theme's colours; art.toml's box_dark replaces some in the dark
theme. SHEET, if given, shows the boxes on both themes' cards at 1x and 4x,
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
# The NES picture window's own tones, cooler, lifted in the dark theme
NES_TONES = (C("#E0E0E8"), C("#F0F0F8"), C("#D8D8E0"), C("#B8B8C8"))
INK = C("#283028")  # the Game Boys' logo

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

    def vtext(s, txt, x, ybottom, colour):
        """txt in the 3x5 font turned to read upwards from ybottom; colour(i)
        gives letter i's colour."""
        yy = ybottom
        for i, ch in enumerate(txt):
            g = FONT[ch]
            for gy, row in enumerate(g):
                for gx, b in enumerate(row):
                    if b == "#":
                        s.p(x + gy, yy - gx, colour(i))
            yy -= len(g[0]) + 1

    def logo(s, txt, x, y, c):
        """txt in the bold italic letters (the Game Boys' logo)."""
        for ch in txt:
            g = BOLD[ch]
            for gy, row in enumerate(g):
                for gx, b in enumerate(row):
                    if b == "#":
                        s.p(x + gx + (1 if gy < 3 else 0), y + gy, c)
            x += len(g[0]) + 1


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


def skeleton(pic, x0, y0, x1, y1, plate, tones=LIGHT_TONES, small=False):
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
    room = gy - y0
    height = max(
        len(rows) + 2, min(int(room * 0.84), int(w * (0.78 if small else 0.9)))
    )
    gx = (x0 + x1 + 1) // 2 - w // 2
    top = gy - height
    # The sun, framed by the gate under its beams
    r = w * 0.28
    pic.disc(gx + w / 2 - 0.5, top + height * 0.45, r, sun, x0, y0, x1, gy - 1)
    pic.sprite(rows, gx, top, gate, height)
    return gy


def title_bars(pic, x, gy, colour, widths=(34, 20)):
    """Two skeleton title bars on the ground plate from its row gy."""
    pic.pill(x, gy + 3, widths[0], 4, colour)
    pic.pill(x, gy + 9, widths[1], 3, colour)


boxes = {}

# --- GBA: a dark band down the left edge, GAME BOY ADVANCE reading up it
p = Pic()
frame(p, SKY)
BAND = C("#202038")
LOGO = C("#F0F0F0")
ADV = C("#A098F0")
LINE = C("#5848C8")
p.rect(1, 1, 11, 77, BAND)
p.rect(12, 1, 12, 77, LINE)
p.vtext("GAME BOY ADVANCE", 4, 73, lambda i: LOGO if i < 8 else ADV)
gy = skeleton(p, 13, 1, 77, 77, 15)
title_bars(p, 18, gy, GATE, (30, 18))
p.pill(61, gy + 3, 12, 7, GATE)  # the seal
p.pill(62, gy + 4, 10, 5, GROUND)
p.rect(64, gy + 6, 69, gy + 6, GATE)
corners(p)
boxes["gba"] = p

# --- GB: a grey band with GAME BOY over the DMG's navy and magenta lines,
# and the Nintendo oval
p = Pic()
frame(p, SKY)
BANDG = C("#C8C8B8")
NAVY = C("#283890")
MAG = C("#A02870")
RED = C("#C83030")
WHITE = C("#F8F8F0")
p.rect(1, 1, 77, 15, BANDG)
p.logo("GAME BOY", 5, 4, INK)
p.pill(61, 4, 14, 7, RED)
p.pill(62, 5, 12, 5, WHITE)
for x in (64, 65, 67, 68, 69, 71):
    p.p(x, 7, RED)
p.rect(1, 16, 77, 16, NAVY)
p.rect(1, 17, 77, 17, MAG)
gy = skeleton(p, 1, 18, 77, 77, 15)
title_bars(p, 6, gy, GATE)
corners(p)
boxes["gb"] = p

# --- GBC: a white band, GAME BOY over a five-colour COLOR, a teal corner
p = Pic()
frame(p, SKY)
TEAL = C("#089C94")
TEALD = C("#087068")
p.rect(1, 1, 77, 19, WHITE)
p.logo("GAME BOY", 5, 4, INK)
p.text(
    "COLOR",
    39,
    13,
    [C("#D82830"), C("#7838B8"), C("#48A030"), C("#E8B010"), C("#2860D0")],
)
for i in range(14):
    for x in range(64 + i, 78):
        p.p(x, 1 + i, TEAL)
    p.p(64 + i, 1 + i, TEALD)
p.rect(1, 20, 77, 20, TEAL)
gy = skeleton(p, 1, 21, 77, 77, 15)
title_bars(p, 6, gy, GATE)
corners(p)
boxes["gbc"] = p

# --- NES: the black box: the title on black, the picture in a window, the
# grid with a game's sprite on it (a torii), the red seal
p = Pic()
BLACK = C("#181820")
frame(p, BLACK)
DIM = C("#505060")
GRID = C("#282838")
SEAL = C("#C82830")
SEALW = C("#E0E0E0")
p.pill(6, 5, 44, 5, DIM)
p.pill(6, 12, 28, 3, DIM)
p.rect(8, 18, 70, 53, DIM)
skeleton(p, 9, 19, 69, 52, 7, NES_TONES, small=True)
# The grid, and a sprite on it: each cell 4x3, the lines between
for y in range(57, 78, 3):
    p.rect(1, y, 77, y, GRID)
for x in range(3, 78, 4):
    p.rect(x, 57, x, 77, GRID)
SPRITE = [
    "#########",
    ".#######.",
    "..#####..",
    "..#...#..",
    ".#######.",
    "..#...#..",
]
PIXEL = C("#686878")
for cy, row in enumerate(SPRITE):
    for cx, b in enumerate(row):
        if b == "#":
            x, y = 40 + cx * 4, 58 + cy * 3
            p.rect(x, y, x + 2, y + 1, PIXEL)
p.pill(6, 64, 20, 9, SEAL)
p.pill(7, 65, 18, 7, SEALW)
p.pill(8, 66, 16, 5, SEAL)
for x in (10, 11, 13, 14, 15, 17, 18, 20, 21):
    p.p(x, 68, SEALW)
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
gy = skeleton(p, 1, 17, 77, 77, 15)
title_bars(p, 6, gy, GATE)
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
