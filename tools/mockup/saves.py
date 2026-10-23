"""The v0.5 save screens, redesigned: mockups for review before they are built.

usage: uv run --with pillow python -I tools/mockup/saves.py

Writes docs/design/v0.5/saves-dark.png and saves-light.png. The screens and
the reasoning are in docs/design/v0.5/saves.md. Everything is drawn with the
v0.4 kit (gfx.py): the menu's fonts, palette.json, ui_theme.c's sizes, and
the popup's and Settings page's own look tables, so nothing here is new
furniture: a save screen is a Settings-style page of section cards, actions
are popups beside their row, and a destructive step is the centred
confirmation with Cancel preselected.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gfx import (
    BODY,
    HEAD,
    LOOK,
    ROOT,
    W,
    card,
    glyph,
    hint_text,
    hints,
    hline,
    icon,
    icon8,
    mix,
    page,
    rect,
    rrect,
    sheet,
    theme,
    title_bar,
)

# The Settings page's look (settings_page.c), which every save page shares
PG = {
    "card_x": 4,
    "card_w": 232,  # no scrollbar: these pages fit
    "card_pad": 2,
    "pitch": 12,
    "pill_x": 7,
    "pill_w": 226,
    "label_x": 13,
    "value_right": 227,
    "head_icon_x": 8,
    "head_text_x": 20,
    "head_h": 12,
    "gap": 2,
    "mark_x": 13,  # the in-use dot
    "name_x": 21,  # a slot's name, after the dot's room
}
# The popup's look (ui_popup.c)
POP = {
    "x": 126,
    "w": 104,
    "above": 20,
    "top_min": 21,
    "bottom_max": 144,
    "notch": 3,
    "head_h": 19,
    "title_y": 2,
    "rule_y": 16,
    "rule_in": 5,
    "icon_x": 6,
    "title_x": 16,
    "pad_bottom": 3,
    "pitch": 12,
    "text_x": 8,
    "value_r": 8,
    "pill_in": 3,
}
# The confirmation's (ui_popup.c)
CONF = {"x": 32, "y": 48, "w": 175, "h": 62, "pad": 8, "title_y": 4, "text_y": 18}
CONF_BUTTON = {"y": 39, "w": 66, "h": 15, "gap": 8}

CONTENT = (0, LOOK["content_top"], W, LOOK["content_bottom"] + 1)


# --- Pieces -------------------------------------------------------------------


def section(img, t, y, ic, name, right=None):
    """A section's label on the paper (8x8 icon and name in the item's ink),
    with an optional ink3 note on the right; returns the card's top."""
    icon(img, icon8(ic), PG["head_icon_x"], y + 2, t.aink)
    BODY.draw(img, PG["head_text_x"], y, name, t.aink)
    if right:
        BODY.draw(img, PG["value_right"] - BODY.width(right), y, right, t.ink3)
    return y + PG["head_h"]


def card_rows(img, t, top, n):
    h = 2 * PG["card_pad"] + PG["pitch"] * n
    card(img, t, PG["card_x"], top, PG["card_w"], h)
    return top + PG["card_pad"], top + h + 1 + PG["gap"]


def row(img, t, y, label, value="", sel=False, kind="plain", mark=False, chev=False):
    """A selectable row: label on the left (danger ink for a destructive
    action), value right-aligned; mark draws the in-use dot."""
    if sel:
        rrect(img, PG["pill_x"], y, PG["pill_w"], LOOK["pill_h"], 3, t.tint)
    x = PG["name_x"] if kind == "slot" else PG["label_x"]
    ink = t.danger if kind == "danger" else (t.ink if sel else t.ink2)
    BODY.draw(img, x, y, label, ink)
    if mark:
        rrect(img, PG["mark_x"] + 1, y + 4, 4, 4, 1, t.aink)
    right = PG["value_right"]
    if chev:
        glyph(img, "chev", right - 4, y + 4, t.ink2 if sel else t.ink3)
        right -= 9
    if value:
        BODY.draw(img, right - BODY.width(value), y, value, t.ink2 if sel else t.ink3)


def fact(img, t, y, label, value):
    """A read-only line: no pill ever lands on it, so it is drawn quieter
    (label ink3, value ink2) than a row the cursor can reach."""
    BODY.draw(img, PG["label_x"], y, label, t.ink3)
    BODY.draw(img, PG["value_right"] - BODY.width(value), y, value, t.ink2)


def bar(img, t, y, parts, total):
    """A storage bar across the card: parts = [(bytes, colour)]."""
    x0, w = PG["label_x"], PG["value_right"] - PG["label_x"]
    rrect(img, x0, y, w, 4, 1, t.soft)
    x = x0
    for n, c in parts:
        pw = max(2, round(w * n / total)) if n else 0
        rect(img, x, y, pw, 4, c)
        x += pw


def popup(img, t, row_y, title, ic, items, cursor=0):
    """A popup beside the row at row_y: items = [(text, value, kind)], kind
    "", "danger" or "dim"."""
    x, w = POP["x"], POP["w"]
    h = POP["head_h"] + POP["pitch"] * len(items) + POP["pad_bottom"]
    top = row_y - POP["above"]
    top = min(top, POP["bottom_max"] - h)
    top = max(top, POP["top_min"])
    card(img, t, x, top, w, h, floating=True)
    my = row_y + 6
    for k in range(3):
        for j in range(-k, k + 1):
            img.putpixel((x - 3 + k, my + j), t.edge if abs(j) == k else t.float)
    for j in (-1, 0, 1):
        img.putpixel((x, my + j), t.float)
    icon(img, icon8(ic), x + POP["icon_x"], top + POP["title_y"] + 2, t.aink)
    BODY.draw(
        img,
        x + POP["title_x"],
        top + POP["title_y"],
        BODY.fit(title, w - POP["title_x"] - 6),
        t.ink,
    )
    hline(img, x + POP["rule_in"], x + w - POP["rule_in"], top + POP["rule_y"], t.edge)
    for i, (text, value, kind) in enumerate(items):
        iy = top + POP["head_h"] + POP["pitch"] * i
        if i == cursor:
            rrect(img, x + POP["pill_in"], iy, w - 2 * POP["pill_in"], 12, 3, t.tint)
        ink = (
            t.danger
            if kind == "danger"
            else t.ink3
            if kind == "dim"
            else (t.ink if i == cursor else t.ink2)
        )
        BODY.draw(img, x + POP["text_x"], iy, text, ink)
        if value:
            vink = t.aink if kind == "mark" else t.ink3
            BODY.draw(img, x + w - POP["value_r"] - BODY.width(value), iy, value, vink)
    return img


def confirm(img, t, title, text, yes, danger=False):
    """The centred confirmation over the dimmed page, Cancel preselected."""
    dim = t.shadow if t.dark else (255, 255, 255)
    for yy in range(LOOK["content_top"], H_ALL):
        for xx in range(W):
            img.putpixel((xx, yy), mix(img.getpixel((xx, yy)), dim, 0.5))
    x, y, w, h = CONF["x"], CONF["y"], CONF["w"], CONF["h"]
    card(img, t, x, y, w, h, floating=True)
    HEAD.draw(
        img, x + (w - HEAD.width(title)) // 2, y + CONF["title_y"] - 3, title, t.ink
    )
    for k, line in enumerate(text.split("\n")):
        BODY.draw(
            img,
            x + (w - BODY.width(line)) // 2,
            y + CONF["text_y"] + 12 * k - 2,
            line,
            t.ink2,
        )
    bw, bh, gap = CONF_BUTTON["w"], CONF_BUTTON["h"], CONF_BUTTON["gap"]
    bx = x + (w - 2 * bw - gap) // 2
    by = y + h - bh - 6
    rrect(img, bx, by, bw, bh, 7, t.base)
    BODY.draw(img, bx + (bw - BODY.width("Cancel")) // 2, by + 1, "Cancel", t.on)
    rrect(img, bx + bw + gap, by, bw, bh, 7, t.soft)
    BODY.draw(
        img,
        bx + bw + gap + (bw - BODY.width(yes)) // 2,
        by + 1,
        yes,
        t.danger if danger else t.ink,
    )
    return img


H_ALL = 160


# --- The game's Saves page ------------------------------------------------------

GAME = "Pokémon Emerald"
SLOTS = [
    ("Main", "Today 14:32", True),
    ("Boss Rush", "Oct 21 18:30", False),
    ("Nuzlocke", "Never saved", False),
]


def saves_page(t, sel, new_sel=False):
    """Slots first (the in-use one marked), New slot last as an action row."""
    img = page(t)
    title_bar(img, t, "gba", "Saves", BODY.fit(GAME, 110))
    top = section(img, t, LOOK["content_top"], "save", "Slots", "Flash 128 KB")
    y, _ = card_rows(img, t, top, len(SLOTS) + 1)
    rows_y = []
    for i, (name, when, used) in enumerate(SLOTS):
        rows_y.append(y)
        row(img, t, y, name, when, sel == i, "slot", mark=used)
        y += PG["pitch"]
    rows_y.append(y)
    # New slot lines up with the names, a + where the dot goes
    row(img, t, y, "New slot", "", sel == len(SLOTS), "slot", chev=True)
    BODY.draw(img, PG["mark_x"], y, "+", t.aink)
    return img, rows_y


def saves_hints(img, t, sel):
    if sel < len(SLOTS):
        hints(img, t, [(["A"], "options"), (["START"], "play"), (["B"], "back")])
    else:
        hints(img, t, [(["A"], "new slot"), (["B"], "back")])


def screen_saves(t):
    img, _ = saves_page(t, 0)
    saves_hints(img, t, 0)
    return img


def screen_slot_popup(t, i):
    img, ys = saves_page(t, i)
    name, _, used = SLOTS[i]
    items = [("Play", "", "")]
    if not used:
        items.append(("Make active", "", ""))
    items += [
        ("Backups", "3", ""),
        ("Rename", "", ""),
        ("Copy to new slot", "", ""),
        ("Export .sav", "", ""),
    ]
    items.append(("Delete", "", "danger"))
    popup(img, t, ys[i], name, "save", items)
    hints(img, t, [([("glyph", "updown")], "choose"), (["A"], "ok"), (["B"], "close")])
    return img


def screen_new_popup(t):
    img, ys = saves_page(t, len(SLOTS))
    popup(
        img,
        t,
        ys[-1],
        "New slot",
        "save",
        [("Empty", "", ""), ("Copy of Main", "", ""), ("From a .sav file", "", "")],
    )
    hints(img, t, [([("glyph", "updown")], "choose"), (["A"], "ok"), (["B"], "close")])
    return img


# --- Backups ------------------------------------------------------------------

BACKUPS = [
    ("Today 09:15", "5 hours ago"),
    ("Oct 22 18:30", "yesterday"),
    ("Oct 20 21:04", "3 days ago"),
]


def backups_page(t, sel):
    img = page(t)
    title_bar(img, t, "gba", "Backups", "Main")
    top = section(img, t, LOOK["content_top"], "clock", "Main", "keeps 5")
    y, _ = card_rows(img, t, top, len(BACKUPS))
    for i, (when, ago) in enumerate(BACKUPS):
        row(img, t, y, when, ago, sel == i)
        y += PG["pitch"]
    return img


def screen_backups(t):
    img = backups_page(t, 1)
    hint_text(img, t, "Each save keeps the one before it.")
    return img


def screen_restore(t):
    img = backups_page(t, 1)
    hint_text(img, t, "Each save keeps the one before it.")
    return confirm(
        img,
        t,
        "Restore backup?",
        "Main goes back to Oct 22 18:30.\nIts save now is kept as a backup.",
        "Restore",
    )


# --- The launch's slot choice ---------------------------------------------------


def screen_launch(t):
    """A on a game with two or more slots: which one, in a popup beside the
    row (the list stays), the active slot first and chosen."""
    img = page(t)
    title_bar(img, t, "gba", "Game Boy Advance", "12/161")
    card(img, t, 4, 20, 232, 124)
    names = [
        "Metroid Fusion",
        "Mother 3",
        "Pokémon Emerald",
        "Pokémon FireRed",
        "Golden Sun",
        "Kirby",
        "Mario Kart",
        "Wario Land 4",
    ]
    gt = theme(t.name, "gba")
    for i, n in enumerate(names):
        y = 22 + 15 * i
        if i == 2:
            rrect(img, 7, y, 226, 13, 3, gt.tint)
        BODY.draw(img, 13, y + 1, n, gt.ink if i == 2 else gt.ink2)
    popup(
        img,
        gt,
        22 + 15 * 2,
        "Play with",
        "save",
        [
            ("Main", "today", "mark"),
            ("Boss Rush", "Oct 21", ""),
            ("Nuzlocke", "new", ""),
        ],
    )
    hints(
        img, gt, [([("glyph", "updown")], "slot"), (["A"], "play"), (["B"], "cancel")]
    )
    return img


# --- Settings > Saves: the card's saves as a whole -------------------------------


def storage_page(t, sel, orphans=True):
    st = theme(t.name, "set")
    img = page(st)
    title_bar(img, st, "set", "Saves", "129 KB")
    # Storage: read-only, so facts, not rows: the cursor never lands here
    top = section(img, st, LOOK["content_top"], "chip", "On the card")
    rect_h = 2 * PG["card_pad"] + 3 * PG["pitch"] + 8
    card(img, st, PG["card_x"], top, PG["card_w"], rect_h)
    y = top + PG["card_pad"] + 2
    bar(
        img,
        st,
        y,
        [(81, st.base), (32, mix(st.base, st.card, 0.45)), (16, st.ink3)],
        129,
    )
    y += 7
    fact(img, st, y, "Saves · 3 games", "81 KB")
    fact(img, st, y + 12, "Backups · 4", "32 KB")
    fact(img, st, y + 24, "Trash · 1 slot, 30 days", "16 KB")
    y = top + rect_h + 1 + PG["gap"]
    # Actions
    rows_y = []
    if orphans:
        y2 = section(img, st, y, "folder", "Without a game", "2")
        ry, _ = card_rows(img, st, y2, 2)
        for i, (p, n) in enumerate(
            [("/Old/Hello.gba", "2 slots"), ("/Gone/Pocket.gb", "1 slot")]
        ):
            rows_y.append(ry)
            row(img, st, ry, BODY.fit(p, 150), n, sel == i)
            ry += PG["pitch"]
        y = ry + PG["card_pad"] + 1 + PG["gap"]
    else:
        y2 = section(img, st, y, "folder", "Without a game")
        card(img, st, PG["card_x"], y2, PG["card_w"], 16)
        BODY.draw(img, PG["label_x"], y2 + 2, "Every save has its game.", st.ink3)
        y = y2 + 16 + 1 + PG["gap"]
    ry, _ = card_rows(img, st, y, 1)
    rows_y.append(ry)
    row(img, st, ry, "Empty trash", "16 KB", sel == (2 if orphans else 0), "danger")
    return img, st, rows_y


def screen_storage(t):
    img, st, _ = storage_page(t, 0)
    hint_text(img, st, "Its ROM moved or is gone. A relinks it.")
    return img


def screen_orphan_popup(t):
    img, st, ys = storage_page(t, 0)
    popup(
        img,
        st,
        ys[0],
        "Hello.gba",
        "folder",
        [("Relink to a game", "", ""), ("Delete", "", "danger")],
    )
    hints(img, st, [([("glyph", "updown")], "choose"), (["A"], "ok"), (["B"], "close")])
    return img


def screen_storage_clean(t):
    img, st, _ = storage_page(t, 0, orphans=False)
    hint_text(img, st, "Deleted slots wait here for 30 days.")
    return img


# --- Sheets -------------------------------------------------------------------

SCREENS = [
    (screen_saves, "1 A game's Saves: the active slot dotted"),
    (lambda t: screen_slot_popup(t, 0), "2 A on a slot: what it can do"),
    (lambda t: screen_slot_popup(t, 1), "3 A slot not in use: Make active"),
    (screen_new_popup, "4 New slot: empty, a copy, or a file"),
    (screen_backups, "5 A slot's backups, newest first"),
    (screen_restore, "6 Restore asks; nothing is lost"),
    (screen_launch, "7 Play, two or more slots: which"),
    (screen_storage, "8 Settings > Saves: facts, then actions"),
    (screen_orphan_popup, "9 A save without a game"),
    (screen_storage_clean, "10 Nothing to fix"),
]


def main():
    out = ROOT / "docs/design/v0.5"
    for name in ("dark", "light"):
        t = theme(name, "gba")
        sheet([(f(t), cap) for f, cap in SCREENS], 2, out / f"saves-{name}.png")


if __name__ == "__main__":
    main()
