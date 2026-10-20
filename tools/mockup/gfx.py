"""Drawing kit for the v0.4 mockups: the Paper clean look at 240x160.

Used by tools/mockup/settings.py and keyboard.py. Colours come from
docs/design/v0.4/palette.json and the shared sizes from source/ui/ui_theme.c
(LOOK), so a mockup drawn here matches what the kernel's theme says. Text is
drawn from the kernel's own font conversion (tools/font.py, the same options
as `just font`), so every glyph and advance is the one the GBA draws.
"""

import json
import sys
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import font as fontlib

W, H = 240, 160
PALETTE = json.loads((ROOT / "docs/design/v0.4/palette.json").read_text())
ASSETS = ROOT / "assets/ui"

# ui_theme.c LOOK, px
LOOK = {
    "margin": 4,
    "title_icon": (5, 1),
    "title_text_x": 25,
    "title_right": 235,
    "content_top": 20,
    "content_bottom": 143,
    "settings_pitch": 12,
    "pill_h": 12,
    "pill_radius": 3,
    "card_radius": 4,
    "hint_y": 147,
    "hint_chip_h": 10,
    "hint_chip_pad": 3,
    "hint_label_gap": 2,
    "hint_gap": 8,
}


def rgb(h: str) -> tuple[int, int, int]:
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))


def snap(c: tuple[int, int, int]) -> tuple[int, int, int]:
    """A colour as the GBA's 15-bit palette shows it."""
    return tuple((v >> 3) * 255 // 31 for v in c)


def mix(a, b, amount: float):
    return snap(
        tuple(round(x * (1 - amount) + y * amount) for x, y in zip(a, b, strict=False))
    )


@dataclass
class Theme:
    name: str
    bg: tuple
    card: tuple
    edge: tuple
    soft: tuple
    ink: tuple
    ink2: tuple
    ink3: tuple
    shadow: tuple
    float: tuple  # a floating card (popup, field editor)
    base: tuple  # the item's accent: Settings
    tint: tuple
    aink: tuple
    on: tuple
    danger: tuple
    danger_tint: tuple
    dark: bool


def theme(name: str, item: str = "set") -> Theme:
    s = {k: rgb(v) for k, v in PALETTE["surfaces"][name].items()}
    a = {
        k: rgb(v) for k, v in PALETTE["accents"][item][name].items() if k != "contrast"
    }
    d = {
        k: rgb(v)
        for k, v in PALETTE["accents"][PALETTE["danger"]][name].items()
        if k != "contrast"
    }
    dark = name == "dark"
    return Theme(
        name=name,
        bg=s["bg"],
        card=s["card"],
        edge=s["edge"],
        soft=s["soft"],
        ink=s["ink"],
        ink2=s["ink2"],
        ink3=s["ink3"],
        shadow=s["shadow"],
        float=mix(s["card"], s["ink"], 13 / 256) if dark else s["card"],
        base=a["base"],
        tint=a["tint"],
        aink=a["ink"],
        on=a["on"],
        danger=d["ink"],
        danger_tint=d["tint"],
        dark=dark,
    )


# --- Fonts ------------------------------------------------------------------


class Face:
    def __init__(self, bdf: str, name: str, line: int):
        args = fontlib.parse_args(
            [
                str(ROOT / "assets/fonts/galmuri" / bdf),
                "--name",
                name,
                "--line-height",
                str(line),
                "--squeeze",
                "--clip",
            ]
        )
        self.font = fontlib.load(args)
        self.line = line

    def glyph(self, c: str):
        return self.font.glyphs[fontlib.index(ord(c))]

    def width(self, text: str) -> int:
        """Advance width; the last glyph's trailing gap is dropped."""
        if not text:
            return 0
        w = sum(self.glyph(c).advance for c in text)
        last = self.glyph(text[-1])
        ink = max((x for x, _ in last.pixels), default=-1) + 1
        return w - last.advance + max(ink, 1 if text[-1] != " " else last.advance)

    def draw(self, img, x: int, y: int, text: str, colour, clip=None):
        """Draws text with its line's top at y. clip: (x0, y0, x1, y1)."""
        cx0, cy0, cx1, cy1 = clip or (0, 0, img.width, img.height)
        for c in text:
            g = self.glyph(c)
            for gx, gy in g.pixels:
                px, py = x + gx, y + gy
                if cx0 <= px < cx1 and cy0 <= py < cy1:
                    img.putpixel((px, py), colour)
            x += g.advance
        return x

    def fit(self, text: str, width: int) -> str:
        if self.width(text) <= width:
            return text
        while text and self.width(text + "...") > width:
            text = text[:-1]
        return text.rstrip() + "..."


BODY = Face("Galmuri9.bdf", "body", 12)
HEAD = Face("Galmuri11-Bold.bdf", "heading", 16)


# --- Shapes -----------------------------------------------------------------

# Rows of a rounded corner: how far each row from the edge is inset. Read off
# the existing mockups (pages-*.png): r3 pills and chips, r4 cards.
CORNERS = {
    0: [],
    1: [1],
    2: [1],
    3: [2, 1],
    4: [3, 1, 1],
    5: [3, 2, 1, 1],
    6: [4, 2, 1, 1],
    7: [5, 3, 2, 1, 1],
}


def rrect(img, x, y, w, h, r, colour, clip=None):
    cx0, cy0, cx1, cy1 = clip or (0, 0, img.width, img.height)
    ins = CORNERS[min(r, 7)]
    for j in range(h):
        k = min(j, h - 1 - j)
        i = ins[k] if k < len(ins) else 0
        py = y + j
        if not cy0 <= py < cy1:
            continue
        for px in range(max(x + i, cx0), min(x + w - i, cx1)):
            img.putpixel((px, py), colour)


def rect(img, x, y, w, h, colour, clip=None):
    rrect(img, x, y, w, h, 0, colour, clip)


def outline(img, x, y, w, h, r, colour, inner, clip=None):
    """A 1 px ring: rrect in colour, then the inside in inner."""
    rrect(img, x, y, w, h, r, colour, clip)
    rrect(img, x + 1, y + 1, w - 2, h - 2, max(r - 1, 0), inner, clip)


def card(img, t: Theme, x, y, w, h, clip=None, floating=False, fill=None):
    """A page card (shadow 1 px below) or a floating one (1 px edge, 2 px shadow)."""
    r = LOOK["card_radius"]
    if floating:
        rrect(img, x + 1, y + 2, w, h, r, t.shadow, clip)
        rrect(img, x, y + 1, w, h, r, t.shadow, clip)
        outline(img, x, y, w, h, r, t.edge, fill or t.float, clip)
    else:
        rrect(img, x, y + 1, w, h, r, t.shadow, clip)
        rrect(img, x, y, w, h, r, fill or t.card, clip)


def hline(img, x0, x1, y, colour):
    for x in range(x0, x1):
        img.putpixel((x, y), colour)


def vline(img, x, y0, y1, colour):
    for y in range(y0, y1):
        img.putpixel((x, y), colour)


# --- Small pixel glyphs the fonts lack ------------------------------------------

GLYPHS = {
    "up": ["..#..", ".###.", "#####"],
    "down": ["#####", ".###.", "..#.."],
    "left": ["..#", ".##", "###", ".##", "..#"],
    "right": ["#..", "##.", "###", "##.", "#.."],
    "updown": ["..#..", ".###.", ".....", ".###.", "..#.."],
    "leftright": ["..#...#..", ".##...##.", "###...###", ".##...##.", "..#...#.."],
    "chev": ["#..", ".#.", "..#", ".#.", "#.."],
    "bksp": [
        "...########",
        "..#........#",
        ".#..#...#..#",
        "#....#.#...#",
        "#.....#....#",
        "#....#.#...#",
        ".#..#...#..#",
        "..#........#",
        "...########",
    ],
    "space": ["#.......#", "#########"],
    "shift": [
        "...#...",
        "..#.#..",
        ".#...#.",
        "##...##",
        ".#...#.",
        ".#...#.",
        ".#####.",
    ],
    "shift_on": [
        "...#...",
        "..###..",
        ".#####.",
        "#######",
        ".#####.",
        ".#####.",
        ".#####.",
    ],
    "check": ["......#", ".....##", "#...##.", "##.##..", ".###...", "..#...."],
}


def glyph(img, name, x, y, colour):
    rows = GLYPHS[name]
    for j, row in enumerate(rows):
        for i, ch in enumerate(row):
            if ch == "#":
                img.putpixel((x + i, y + j), colour)
    return max(len(r) for r in rows), len(rows)


def glyph_size(name):
    rows = GLYPHS[name]
    return max(len(r) for r in rows), len(rows)


def icon(img, path: Path, x, y, colour=None):
    """Pastes an icon PNG; a one-colour 8x8 icon is drawn in colour."""
    ic = Image.open(path).convert("RGBA")
    for j in range(ic.height):
        for i in range(ic.width):
            p = ic.getpixel((i, j))
            if p[3] >= 128:
                img.putpixel((x + i, y + j), colour or p[:3])


def icon8(name):
    return ASSETS / "icons8" / f"{name}.png"


def icon16(name):
    return ASSETS / "icons16" / f"{name}.png"


# --- Page furniture ---------------------------------------------------------------


def page(t: Theme) -> Image.Image:
    return Image.new("RGB", (W, H), t.bg)


def title_bar(img, t: Theme, item_icon: str, title: str, right: str = ""):
    icon(img, icon16(item_icon), *LOOK["title_icon"])
    HEAD.draw(img, LOOK["title_text_x"], 1, title, t.ink)
    if right:
        BODY.draw(img, LOOK["title_right"] - BODY.width(right), 4, right, t.ink2)


def chip_width(face_text) -> int:
    """A hint chip: a letter (13 px), a word, or a pixel glyph."""
    if isinstance(face_text, tuple):  # ("glyph", name)
        gw, _ = glyph_size(face_text[1])
        return max(13, gw + 2 * LOOK["hint_chip_pad"] + 2)
    return max(13, BODY.width(face_text) + 2 * LOOK["hint_chip_pad"] + 1)


def chip(img, t: Theme, x, y, face_text, fill=None, ink=None, h=None, clip=None):
    """Draws a key chip with its top at y; returns its width."""
    h = h or LOOK["hint_chip_h"]
    w = chip_width(face_text)
    rrect(img, x, y, w, h, 3, fill or t.soft, clip)
    if isinstance(face_text, tuple):
        gw, gh = glyph_size(face_text[1])
        glyph(img, face_text[1], x + (w - gw) // 2, y + (h - gh) // 2, ink or t.ink2)
    else:
        tw = BODY.width(face_text)
        # Body caps sit 2 rows into the 12 px line; a 10 px chip shows them
        # from its 1st row
        BODY.draw(img, x + (w - tw + 1) // 2, y - 1, face_text, ink or t.ink2, clip)
    return w


def hints(img, t: Theme, items, y=None, colour=None):
    """Key chips at the bottom, centred: items = [([chip, ...], label), ...]."""
    y = LOOK["hint_y"] if y is None else y
    gap, lgap = LOOK["hint_gap"], LOOK["hint_label_gap"]

    def item_w(chips, label):
        w = sum(chip_width(c) for c in chips) + 2 * (len(chips) - 1)
        return w + (lgap + BODY.width(label) if label else 0)

    total = sum(item_w(c, lab) for c, lab in items) + gap * (len(items) - 1)
    x = (W - total) // 2
    for chips, label in items:
        for _i, c in enumerate(chips):
            x += chip(img, t, x, y + 1, c) + 2
        x -= 2
        if label:
            x += lgap
            BODY.draw(img, x, y, label, colour or t.ink3)
            x += BODY.width(label)
        x += gap
    return total


def hint_text(img, t: Theme, text, colour=None, y=None):
    """The one-line explanation in the hint area, centred."""
    y = LOOK["hint_y"] if y is None else y
    BODY.draw(img, (W - BODY.width(text)) // 2, y, text, colour or t.ink2)


def scrollbar(img, t: Theme, top, bottom, frac0, frac1, x=231):
    """The thin scrollbar on the paper: a 1 px track and a 3 px thumb."""
    vline(img, x, top, bottom, t.edge)
    span = bottom - top
    a = top + round(span * frac0)
    b = max(a + 6, top + round(span * frac1))
    rrect(img, x - 1, a, 3, b - a, 1, t.base)


# --- Sheets -------------------------------------------------------------------

SHEET_BG = (30, 31, 35)
CAPTION = (198, 189, 181)


def sheet(screens, cols, out: Path, scale=2, pad=16):
    """screens = [(image, caption)]: a grid at scale with captions under each."""
    rows = (len(screens) + cols - 1) // cols
    cw, ch = W * scale, H * scale
    cap_h = 12 * scale + 6
    sw = pad + cols * (cw + pad)
    sh = pad + rows * (ch + cap_h + pad)
    out_img = Image.new("RGB", (sw, sh), SHEET_BG)
    for n, (im, caption) in enumerate(screens):
        r, c = divmod(n, cols)
        x = pad + c * (cw + pad)
        y = pad + r * (ch + cap_h + pad)
        out_img.paste(im.resize((cw, ch), Image.NEAREST), (x, y))
        cap = Image.new("RGB", (cw // scale, 12), SHEET_BG)
        BODY.draw(cap, 0, 0, caption, CAPTION)
        out_img.paste(cap.resize((cw, 12 * scale), Image.NEAREST), (x, y + ch + 4))
    out.parent.mkdir(parents=True, exist_ok=True)
    out_img.save(out, optimize=True)
    return out_img
