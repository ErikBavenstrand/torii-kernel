"""The placeholder boxes list with art shows for a game without art.

usage: uv run --with pillow python -I tools/mockup/boxes.py [PREVIEW.png]

Draws assets/ui/boxes/*.png (then run `just assets`): each console's box
as its real boxes are laid out, with a muted skeleton picture (a torii gate
under a shimmer) and skeleton title bars where the game's art and name
would be, so it reads as "no art yet". The PNGs are drawn in the light
theme's colours; art.toml's box_dark replaces some in the dark theme, and
PREVIEW, if given, shows both at 3x.
"""

import sys
import tomllib
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "assets/ui/boxes"
PREV = sys.argv[1] if len(sys.argv) > 1 else None


def C(h):
    return (int(h[1:3], 16) & 0xF8, int(h[3:5], 16) & 0xF8, int(h[5:7], 16) & 0xF8, 255)


# skeleton tones (light; dark map in art.toml)
S0 = C("#E8E0D8")  # art fill
S1 = C("#E0D8D0")  # ground
S2 = C("#D0C8C0")  # torii, title bars
S3 = C("#F0E8E0")  # shine
# The NES picture window's own tones, which art.toml lifts in the dark theme
NES_TONES = (C("#E0E0E8"), C("#D8D8E0"), C("#C8C8D0"), C("#F0F0F8"))
EDGE = C("#A8A098")  # box outline
SHADOW = C("#D8D0C8")
DARK = tomllib.loads((ROOT / "assets/ui/art.toml").read_text())["box_dark"]

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
    "S": [".##", "#..", ".#.", "..#", "##."],
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


def logo(pic, txt, x, y, c):
    for ch in txt:
        g = BOLD[ch]
        for gy, row in enumerate(g):
            for gx, b in enumerate(row):
                if b == "#":
                    pic.p(x + gx + (1 if gy < 3 else 0), y + gy, c)
        x += len(g[0]) + 1


class Pic:
    def __init__(s):
        s.im = Image.new("RGBA", (80, 80), (0, 0, 0, 0))
        s.px = s.im.load()

    def p(s, x, y, c):
        if 0 <= x < 80 and 0 <= y < 80:
            s.px[x, y] = c

    def rect(s, x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                s.p(x, y, c)

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

    def vtext(s, txt, x, ybottom, colfn):
        # rotated 90 CCW: reads bottom to top; glyph row r -> column x+r
        yy = ybottom
        for i, ch in enumerate(txt):
            g = FONT[ch]
            for gy, row in enumerate(g):
                for gx, b in enumerate(row):
                    if b == "#":
                        s.p(x + gy, yy - gx, colfn(i, ch))
            yy -= len(g[0]) + 1


def frame(pic, body):
    # box x0..78, y0..78, rounded by one pixel, a 1 px shadow right and below
    pic.rect(1, 1, 77, 77, body)
    for x in range(1, 78):
        pic.p(x, 0, EDGE)
        pic.p(x, 78, EDGE)
        pic.p(x + 1, 79, SHADOW)
    for y in range(1, 78):
        pic.p(0, y, EDGE)
        pic.p(78, y, EDGE)
        pic.p(79, y + 1, SHADOW)
    pic.p(79, 79, (0, 0, 0, 0))
    pic.p(79, 1, (0, 0, 0, 0))
    pic.p(1, 79, (0, 0, 0, 0))
    for x, y in ((1, 1), (77, 1), (1, 77), (77, 77)):
        pic.p(x, y, EDGE)


def bar(pic, x, y, w, h, c):
    pic.rect(x, y, x + w - 1, y + h - 1, c)
    if h > 2:
        for xx, yy in ((x, y), (x + w - 1, y), (x, y + h - 1), (x + w - 1, y + h - 1)):
            pic.p(xx, yy, pic_bg)


def art(pic, x0, y0, x1, y1, torii_w=None, tones=None):
    # the skeleton artwork: fill, a shimmer stripe, ground, a torii gate
    fill, ground, gate, shine = tones or (S0, S1, S2, S3)
    w = x1 - x0 + 1
    h = y1 - y0 + 1
    pic.rect(x0, y0, x1, y1, fill)
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            d = (x - x0) + (y1 - y)  # diagonal position
            if w * 0.45 <= d - h * 0.35 < w * 0.45 + 7:
                pic.p(x, y, shine)
    gy = y0 + int(h * 0.80)
    pic.rect(x0, gy, x1, y1, ground)
    tw = torii_w or max(18, int(w * 0.52)) // 2 * 2
    th = int(h * 0.56)
    cx = (x0 + x1 + 1) // 2
    lx = cx - tw // 2
    rx = cx + tw // 2 - 1
    top = gy - th
    # kasagi: upturned ends
    pic.rect(lx - 1, top, rx + 1, top, gate)
    pic.p(lx - 2, top - 1, gate)
    pic.p(rx + 2, top - 1, gate)
    pic.rect(lx, top + 1, rx, top + 2, gate)
    # gakuzuka and nuki
    nuki = top + 3 + max(3, th // 5)
    pic.rect(cx - 1, top + 3, cx, nuki - 1, gate)
    pic.rect(lx + 1, nuki, rx - 1, nuki + 1, gate)
    pw = 3 if tw >= 24 else 2
    inset = max(3, tw // 6)
    pic.rect(lx + inset, top + 3, lx + inset + pw - 1, gy, gate)
    pic.rect(rx - inset - pw + 1, top + 3, rx - inset, gy, gate)
    # feet
    pic.rect(lx + inset - 1, gy, lx + inset + pw, gy, gate)
    pic.rect(rx - inset - pw, gy, rx - inset + 1, gy, gate)


boxes = {}

# --- GBA: a dark band down the left edge, GAME BOY ADVANCE reading up it
p = Pic()
frame(p, S0)
BAND = C("#202030")
LOGO = C("#E0E0F0")
ADV = C("#9890D8")
LINE = C("#4A39B5")
p.rect(1, 1, 11, 77, BAND)
for y in (1, 77):
    p.p(1, y, EDGE)
p.rect(12, 1, 12, 77, LINE)
p.vtext("GAME BOY ADVANCE", 4, 74, lambda i, ch: LOGO if i < 8 else ADV)
pic_bg = S0
art(p, 13, 1, 77, 77)
bar(p, 19, 6, 38, 4, S2)
bar(p, 19, 12, 24, 3, S2)
bar(p, 65, 67, 8, 7, S2)  # the seal
p.p(77, 1, EDGE)
p.p(77, 77, EDGE)
boxes["gba"] = p

# --- GB: a grey top band with GAME BOY, the DMG's two bezel lines, a seal badge
p = Pic()
frame(p, S0)
BANDG = C("#C0C8B0")
INK = C("#283028")
NAVY = C("#283890")
MAG = C("#A02870")
RED = C("#C83030")
p.rect(1, 1, 77, 15, BANDG)
p.rect(1, 16, 77, 16, NAVY)
p.rect(1, 17, 77, 17, MAG)
logo(p, "GAME BOY", 5, 4, INK)
# Nintendo badge: a red rounded oval at the band's right
p.rect(64, 4, 75, 10, RED)
for x in (66, 67, 69, 70, 71, 73):
    p.p(x, 7, C("#F0E0D8"))
for xy in ((64, 4), (75, 4), (64, 10), (75, 10)):
    p.p(*xy, BANDG)
pic_bg = S0
art(p, 1, 18, 77, 77)
bar(p, 6, 66, 36, 4, S2)
bar(p, 6, 72, 22, 3, S2)
for xy in ((1, 1), (77, 1), (1, 77), (77, 77)):
    p.p(*xy, EDGE)
boxes["gb"] = p

# --- GBC: a white band, GAME BOY and a five-colour COLOR, a teal corner badge
p = Pic()
frame(p, S0)
WHITE = C("#F8F8F0")
TEAL = C("#089C94")
TEALD = C("#087068")
p.rect(1, 1, 77, 19, WHITE)
logo(p, "GAME BOY", 5, 4, INK)
p.text(
    "COLOR",
    38,
    13,
    [C("#D82830"), C("#7838B8"), C("#48A030"), C("#E8B010"), C("#2860D0")],
)
for i in range(13):
    for x in range(77 - 12 + i, 78):
        p.p(x, 1 + i, TEAL)
for i in range(13):
    p.p(65 + i, 1 + i, TEALD)
p.rect(1, 20, 77, 20, TEAL)
pic_bg = S0
art(p, 1, 21, 77, 77)
bar(p, 6, 66, 36, 4, S2)
bar(p, 6, 72, 22, 3, S2)
for xy in ((1, 1), (77, 1), (1, 77), (77, 77)):
    p.p(*xy, EDGE)
boxes["gbc"] = p

# --- NES: the black box: title on black, a framed picture, a grid, the red seal
p = Pic()
frame(p, C("#181820"))
DIM = C("#505060")
GRID = C("#282838")
SEAL = C("#C82830")
SEALW = C("#E0E0E0")
pic_bg = C("#181820")
bar(p, 6, 5, 46, 5, DIM)
bar(p, 6, 12, 30, 3, DIM)
for y in range(58, 78, 3):
    p.rect(1, y, 77, y, GRID)
for x in range(3, 78, 4):
    p.rect(x, 58, x, 77, GRID)
p.rect(9, 18, 69, 54, DIM)
art(p, 10, 19, 68, 53, tones=NES_TONES)
p.rect(5, 66, 22, 74, SEAL)
for x in (8, 9, 11, 12, 13, 15, 16, 17, 19):
    p.p(x, 69, SEALW)
for x in (8, 9, 10, 12, 13, 15, 16, 17, 18, 19):
    p.p(x, 71, SEALW)
for xy in ((5, 66), (22, 66), (5, 74), (22, 74)):
    p.p(*xy, C("#181820"))
for xy in ((1, 1), (77, 1), (1, 77), (77, 77)):
    p.p(*xy, EDGE)
boxes["nes"] = p

# --- Anything else: a plain slate band with a title bar over the artwork
p = Pic()
frame(p, S0)
SLATE = C("#586878")
SLATEL = C("#8898A8")
p.rect(1, 1, 77, 13, SLATE)
pic_bg = SLATE
bar(p, 6, 4, 34, 4, SLATEL)
bar(p, 44, 5, 14, 3, SLATEL)
pic_bg = S0
art(p, 1, 14, 77, 77)
bar(p, 6, 66, 36, 4, S2)
bar(p, 6, 72, 22, 3, S2)
for xy in ((1, 1), (77, 1), (1, 77), (77, 77)):
    p.p(*xy, EDGE)
boxes["other"] = p

for n, pic in boxes.items():
    cols = {pic.im.getpixel((x, y)) for x in range(80) for y in range(80)} - {
        (0, 0, 0, 0)
    }
    print(n, len(cols), "colours")
    pic.im.save(OUT / f"{n}.png")
if not PREV:
    sys.exit(0)


# preview: light card row and dark card row, at 3x
def darken(im):
    im = im.copy()
    px = im.load()
    m = {C(k)[:3]: C(v) for k, v in DARK.items()}
    for y in range(80):
        for x in range(80):
            c = px[x, y]
            if c[3] and c[:3] in m:
                px[x, y] = m[c[:3]]
    return im


W = len(boxes) * 96 + 16
sheet = Image.new("RGB", (W, 2 * 96 + 16), (0xF7, 0xEF, 0xE7))
for row, (bg, card, fn) in enumerate(
    [
        ((0xF7, 0xEF, 0xE7), (255, 255, 255), lambda i: i),
        ((0x18, 0x18, 0x21), (0x29, 0x29, 0x31), darken),
    ]
):
    sheet.paste(bg, (0, row * 104, W, row * 104 + 104))
    for i, pic in enumerate(boxes.values()):
        x = 12 + i * 96
        y = row * 104 + 12
        sheet.paste(card, (x - 4, y - 4, x + 84, y + 84))
        im = fn(pic.im)
        sheet.paste(im, (x, y), im)
sheet = sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST)
sheet.save(PREV)
