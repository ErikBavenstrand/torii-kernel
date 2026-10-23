"""The game's Start menu (the START popup) with Saves: today's and a proposal.

usage: uv run --with pillow python -I tools/mockup/saves_popup.py

Writes docs/design/v0.5/saves-popup.png. The "today" screens are the kernel's
own golden screenshots (tests/shots/png); the proposals are drawn over the
same pages' goldens with the popup's look (saves.py's popup), so the list
under them is the real one. The proposal is the same for the three save
screen directions: only the page Saves opens differs.
"""

import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import saves as A
from gfx import BODY, ROOT, glyph, hints, sheet, theme

SHOTS = ROOT / "tests/shots/png"
ROW0 = 22  # a list's first row, px
PITCH = 14


def golden(name):
    return Image.open(SHOTS / f"{name}.png").convert("RGB")


def move_pill(img, t, from_row, to_row, x0=7, x1=233):
    """Moves a list's selection pill from one row to another on a golden:
    the tint's pixels become the card's in one, the card's the tint's in the
    other (the text stays)."""
    # The golden's own colours: the pill's middle, and the card beside it
    tint = img.getpixel((x0 + 3, ROW0 + PITCH * from_row + 6))
    cardc = img.getpixel((x0 + 3, ROW0 + PITCH * to_row + 6))
    for row, (a, b) in ((from_row, (tint, cardc)), (to_row, (cardc, tint))):
        y0 = ROW0 + PITCH * row - 1
        for y in range(y0, y0 + 15):
            for x in range(x0 - 3, x1 + 3):
                if img.getpixel((x, y)) == a:
                    img.putpixel((x, y), b)


def popup_top(row_y, n, titled=True):
    h = (A.POP["head_h"] if titled else 4) + A.POP["pitch"] * n + A.POP["pad_bottom"]
    top = min(row_y - A.POP["above"], A.POP["bottom_max"] - h)
    return max(top, A.POP["top_min"])


def stepper(img, t, row_y, n, i, value, sel):
    """An item whose value LEFT/RIGHT step, shown « value » while selected."""
    top = popup_top(row_y, n)
    y = top + A.POP["head_h"] + A.POP["pitch"] * i
    right = A.POP["x"] + A.POP["w"] - A.POP["value_r"]
    if sel:
        rw = BODY.width("»")
        BODY.draw(img, right - rw, y, "»", t.aink)
        vx = right - rw - 3 - BODY.width(value)
        BODY.draw(img, vx, y, value, t.ink)
        BODY.draw(img, vx - 3 - BODY.width("«"), y, "«", t.aink)
    else:
        BODY.draw(img, right - BODY.width(value), y, value, t.ink3)


def chev(img, t, row_y, n, i):
    top = popup_top(row_y, n)
    y = top + A.POP["head_h"] + A.POP["pitch"] * i
    glyph(img, "chev", A.POP["x"] + A.POP["w"] - A.POP["value_r"] - 3, y + 4, t.ink3)


GBA_ITEMS = [
    ("Play", "", ""),
    ("Saves", "", ""),
    ("Favourite", "off", ""),
    ("Rename", "", ""),
    ("Game info", "", ""),
    ("Delete", "", "danger"),
]


def gba_popup(img, t, cursor, boot="addons", title="A name too long to fit on its row"):
    """Play steps its boot (with addons or clean) with LEFT/RIGHT; Saves
    shows the active slot and opens the game's save page."""
    A.popup(img, t, ROW0, title, "gba", GBA_ITEMS, cursor)
    n = len(GBA_ITEMS)
    stepper(img, t, ROW0, n, 0, boot, cursor == 0)
    top = popup_top(ROW0, n)
    y = top + A.POP["head_h"] + A.POP["pitch"]
    right = A.POP["x"] + A.POP["w"] - A.POP["value_r"] - 7
    BODY.draw(
        img, right - BODY.width("Main"), y, "Main", t.ink2 if cursor == 1 else t.ink3
    )
    chev(img, t, ROW0, n, 1)
    if cursor == 0:
        hints(
            img,
            t,
            [
                ([("glyph", "updown")], "choose"),
                ([("glyph", "leftright")], "boot"),
                (["A"], "ok"),
                (["B"], "close"),
            ],
        )
    else:
        hints(
            img, t, [([("glyph", "updown")], "choose"), (["A"], "ok"), (["B"], "close")]
        )
    return img


def clear_hints(img, t):
    for y in range(145, 160):
        for x in range(240):
            img.putpixel((x, y), t.bg)


def proposed_list(t, cursor=0, boot="addons"):
    img = golden("games-gba.screen")
    clear_hints(img, t)
    return gba_popup(img, t, cursor, boot)


def proposed_art(t):
    img = golden("games-art.gba")
    clear_hints(img, t)
    return gba_popup(img, t, 0)


SD_ITEMS = [
    ("Launch", "", ""),
    ("Write to NOR", "", ""),
    ("Saves", "", ""),
    ("Add to favourites", "", ""),
    ("Rename", "", ""),
    ("File info", "", ""),
    ("Delete", "", "danger"),
]


def proposed_sd(t):
    img = golden("sd-root.root")
    move_pill(img, t, 0, 2)
    clear_hints(img, t)
    ry = ROW0 + PITCH * 2
    A.popup(img, t, ry, "Hello.gba", "file", SD_ITEMS, 2)
    n = len(SD_ITEMS)
    stepper(img, t, ry, n, 0, "addons", False)
    top = popup_top(ry, n)
    y = top + A.POP["head_h"] + A.POP["pitch"] * 2
    right = A.POP["x"] + A.POP["w"] - A.POP["value_r"] - 7
    BODY.draw(img, right - BODY.width("Main"), y, "Main", t.ink2)
    chev(img, t, ry, n, 2)
    hints(img, t, [([("glyph", "updown")], "choose"), (["A"], "ok"), (["B"], "close")])
    return img


def main():
    dark = theme("dark", "gba")
    light = theme("light", "gba")
    sd = theme("light", "sd")
    screens = [
        (golden("games-gba.popup"), "Today: 8 items, Saves among the boots"),
        (golden("games-art-popup.popup"), "Today, with art: no room for the title"),
        (golden("sd-confirm-tall.popup"), "Today, SD: 9 items, no room for Saves"),
        (proposed_list(dark), "New: 6 items; Play's boot is « »"),
        (proposed_list(dark, 0, "clean"), "New: LEFT/RIGHT, a clean boot"),
        (proposed_list(dark, 1), "New: Saves shows the active slot"),
        (proposed_art(light), "New, with art: the title fits"),
        (proposed_sd(sd), "New, SD: 7 items, Saves always there"),
    ]
    sheet(screens, 2, ROOT / "docs/design/v0.5/saves-popup.png")


if __name__ == "__main__":
    main()
