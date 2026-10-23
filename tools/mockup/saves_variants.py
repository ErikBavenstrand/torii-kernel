"""Three directions for the v0.5 save screens, drawn side by side.

usage: uv run --with pillow python -I tools/mockup/saves_variants.py

Writes docs/design/v0.5/saves-variant-a.png, -b.png and -c.png (dark, with
the main screen in light last). Each sheet walks the same five jobs: a game's
saves, what a slot can do, backups and restoring one, choosing a slot at
launch, and Settings > Saves. docs/design/v0.5/saves-variants.md compares them.

  A  Pages and popups: the Settings page's section cards, actions in a popup
     beside the row (saves.py's screens).
  B  List and detail: the slots on the left, the chosen slot's facts and its
     backups on the right, as the list with art shows a game's facts.
  C  Slot tabs: one slot at a time, its save now and its backups as a
     timeline; L and R switch slots as path pills, the way the SD page shows
     folders.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import saves as A
from gfx import (
    BODY,
    HEAD,
    LOOK,
    ROOT,
    W,
    card,
    chip,
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

GAME = A.GAME
SLOTS = A.SLOTS  # (name, last saved, active)
BACKUPS = A.BACKUPS
CHOOSE = [([("glyph", "updown")], "choose"), (["A"], "ok"), (["B"], "close")]


def pill_row(img, t, x, y, w, text, sel, value="", ink=None, vink=None):
    if sel:
        rrect(img, x + 3, y, w - 6, 12, 3, t.tint)
    BODY.draw(img, x + 9, y, text, ink or (t.ink if sel else t.ink2))
    if value:
        BODY.draw(
            img,
            x + w - 9 - BODY.width(value),
            y,
            value,
            vink or (t.ink2 if sel else t.ink3),
        )


def dot(img, t, x, y):
    rrect(img, x, y + 4, 4, 4, 1, t.aink)


def tag(img, t, x, y, text):
    """A small solid tag (the accent's base, its on colour): "Active"."""
    w = BODY.width(text) + 7
    rrect(img, x, y + 1, w, 11, 3, t.base)
    BODY.draw(img, x + 4, y, text, t.on)
    return w


# --- B: list and detail -----------------------------------------------------------

B_LIST_W = 104  # the slots' card: x 4..108
B_PANE_X = 112  # the detail card: to x 236


def b_page(t, sel, focus="list", pane_sel=None):
    """Slots left; right, the chosen slot: its name, Active, when it was
    saved, then its backups, which RIGHT moves into."""
    img = page(t)
    title_bar(img, t, "gba", "Saves", BODY.fit(GAME, 110))
    top, bottom = LOOK["content_top"], LOOK["content_bottom"] + 1
    card(img, t, 4, top, B_LIST_W, bottom - top - 1)
    rows = [(n, a) for n, _, a in SLOTS] + [("New slot", False)]
    ys = []
    for i, (name, active) in enumerate(rows):
        y = top + 4 + 13 * i
        ys.append(y)
        on = i == sel and focus == "list"
        if i == sel:
            rrect(
                img,
                7,
                y,
                B_LIST_W - 6,
                12,
                3,
                t.tint if on else mix(t.tint, t.card, 0.55),
            )
        if active:
            dot(img, t, 10, y)
        if name == "New slot":
            BODY.draw(img, 9, y, "+", t.aink)
        BODY.draw(img, 17, y, name, t.ink if i == sel else t.ink2)
    # The detail card
    px, pw = B_PANE_X, W - 4 - B_PANE_X
    card(img, t, px, top, pw, bottom - top - 1)
    if sel < len(SLOTS):
        name, when, active = SLOTS[sel]
        HEAD.draw(img, px + 7, top + 1, BODY.fit(name, pw - 60), t.ink)
        if active:
            tag(img, t, px + pw - 7 - BODY.width("Active") - 7, top + 3, "Active")
        BODY.draw(img, px + 7, top + 18, "Saved", t.ink3)
        BODY.draw(img, px + pw - 7 - BODY.width(when), top + 18, when, t.ink2)
        hline(img, px + 5, px + pw - 5, top + 33, t.edge)
        icon(img, icon8("clock"), px + 7, top + 37, t.aink)
        BODY.draw(img, px + 18, top + 35, "Backups", t.aink)
        n = 3 if active else 1
        short = ["5 h", "1 d", "3 d"]  # how long ago, short: the pane is narrow
        for k in range(n):
            y = top + 49 + 12 * k
            when_k, _ = BACKUPS[k]
            pill_row(
                img, t, px, y, pw, when_k, focus == "pane" and pane_sel == k, short[k]
            )
    else:
        HEAD.draw(img, px + 7, top + 1, "New slot", t.ink)
        for k, line in enumerate(
            ["Empty, a copy of", "Main, or a .sav file", "from the card."]
        ):
            BODY.draw(img, px + 7, top + 20 + 12 * k, line, t.ink2)
    return img, ys


def b_saves(t):
    img, _ = b_page(t, 0)
    hints(
        img,
        t,
        [
            (["A"], "play"),
            (["START"], "options"),
            ([("glyph", "right")], "backups"),
            (["B"], "back"),
        ],
    )
    return img


def b_options(t):
    img, ys = b_page(t, 1)
    A.popup(
        img,
        t,
        ys[1],
        "Boss Rush",
        "save",
        [
            ("Make active", "", ""),
            ("Rename", "", ""),
            ("Copy to new slot", "", ""),
            ("Export .sav", "", ""),
            ("Delete", "", "danger"),
        ],
    )
    hints(img, t, CHOOSE)
    return img


def b_backups(t):
    img, _ = b_page(t, 0, focus="pane", pane_sel=1)
    hints(img, t, [(["A"], "restore"), ([("glyph", "left")], "slots"), (["B"], "back")])
    return img


def b_restore(t):
    img = b_backups(t)
    return A.confirm(
        img,
        t,
        "Restore backup?",
        "Main goes back to Oct 22 18:30.\nIts save now is kept as a backup.",
        "Restore",
    )


def b_launch(t):
    """Play with two or more slots: the same page, A plays the chosen slot."""
    img, _ = b_page(t, 0)
    rect(img, 0, 0, W, LOOK["content_top"], t.bg)
    title_bar(img, t, "gba", "Play with", BODY.fit(GAME, 110))
    hints(img, t, [([("glyph", "updown")], "slot"), (["A"], "play"), (["B"], "cancel")])
    return img


def b_storage(t):
    """Settings > Saves as the NOR page is: storage first, then the list."""
    st = theme(t.name, "set")
    img = page(st)
    title_bar(img, st, "set", "Saves", "129 KB")
    top = LOOK["content_top"]
    card(img, st, 4, top, 232, 30)
    A.bar(
        img,
        st,
        top + 6,
        [(81, st.base), (32, mix(st.base, st.card, 0.45)), (16, st.ink3)],
        129,
    )
    x = 13
    for label, colour in [
        ("Saves 81 KB", st.base),
        ("Backups 32", mix(st.base, st.card, 0.45)),
        ("Trash 16", st.ink3),
    ]:
        rrect(img, x, top + 18, 5, 5, 1, colour)
        BODY.draw(img, x + 8, top + 14, label, st.ink2)
        x += BODY.width(label) + 20
    y = top + 34
    card(img, st, 4, y, 232, 4 + 12 * 3)
    BODY.draw(img, 13, y + 2, "Without a game", st.aink)
    BODY.draw(img, 227 - BODY.width("2"), y + 2, "2", st.ink3)
    pill_row(img, st, 4, y + 14, 232, "/Old/Hello.gba", True, "2 slots")
    pill_row(img, st, 4, y + 26, 232, "/Gone/Pocket.gb", False, "1 slot")
    y += 4 + 36 + 4
    card(img, st, 4, y, 232, 16)
    pill_row(img, st, 4, y + 2, 232, "Empty trash", False, "16 KB", ink=st.danger)
    hint_text(img, st, "Its ROM moved or is gone. A relinks it.")
    return img


# --- C: slot tabs and a timeline ---------------------------------------------------


def c_tabs(img, t, y, active_tab, names, active_slot=0):
    """The slots as pills across the top, the shown one tinted, L and R at
    the ends (the rename keyboard's L and R chips)."""
    chip(img, t, 4, y + 1, "L")
    x = 21
    for i, name in enumerate(names):
        w = BODY.width(name) + 10 + (6 if i == active_slot else 0)
        if i == active_tab:
            rrect(img, x, y, w, 12, 3, t.tint)
        tx = x + 5
        if i == active_slot:
            dot(img, t, x + 4, y)
            tx += 6
        BODY.draw(img, tx, y, name, t.ink if i == active_tab else t.ink3)
        x += w + 3
    BODY.draw(img, x + 2, y, "+", t.aink)
    chip(img, t, W - 4 - 13, y + 1, "R")


def c_page(t, tab, sel):
    img = page(t)
    title_bar(img, t, "gba", "Saves", BODY.fit(GAME, 110))
    top = LOOK["content_top"]
    c_tabs(img, t, top + 1, tab, [n for n, _, _ in SLOTS])
    _, when, active = SLOTS[tab]
    y = top + 16
    # The save now: one card, its own line
    card(img, t, 4, y, 232, 30)
    if sel == 0:
        rrect(img, 7, y + 3, 226, 24, 4, t.tint)
    HEAD.draw(img, 13, y + 4, "Current save", t.ink if sel == 0 else t.ink2)
    BODY.draw(img, 227 - BODY.width(when), y + 3, when, t.ink2)
    sub = "Active: Play loads it" if active else "Not active"
    BODY.draw(img, 227 - BODY.width(sub), y + 14, sub, t.aink if active else t.ink3)
    # Its backups as a timeline: a line down the left, a tick per backup
    y += 34
    BODY.draw(img, 20, y, "Backups", t.aink)
    icon(img, icon8("clock"), 8, y + 2, t.aink)
    y += 12
    n = len(BACKUPS) if active else 1
    card(img, t, 4, y, 232, 4 + 12 * n)
    for k in range(n):
        ry = y + 2 + 12 * k
        when_k, ago = BACKUPS[k]
        if sel == k + 1:
            rrect(img, 7, ry, 226, 12, 3, t.tint)
        rect(img, 14, ry + (0 if k else 6), 1, 12 if k < n - 1 else 6, t.edge)
        rrect(img, 12, ry + 4, 5, 5, 2, t.aink if sel == k + 1 else t.ink3)
        BODY.draw(img, 22, ry, when_k, t.ink if sel == k + 1 else t.ink2)
        BODY.draw(
            img, 227 - BODY.width(ago), ry, ago, t.ink2 if sel == k + 1 else t.ink3
        )
    return img


C_HINTS = [
    (["L", "R"], "slot"),
    (["A"], "play"),
    (["START"], "slot menu"),
    (["B"], "back"),
]


def c_saves(t):
    img = c_page(t, 0, 0)
    hints(img, t, C_HINTS)
    return img


def c_options(t):
    img = c_page(t, 1, 0)
    A.popup(
        img,
        t,
        LOOK["content_top"] + 16 + 9,
        "Boss Rush",
        "save",
        [
            ("Make active", "", ""),
            ("Rename", "", ""),
            ("Copy to new slot", "", ""),
            ("Export .sav", "", ""),
            ("Delete", "", "danger"),
        ],
    )
    hints(img, t, CHOOSE)
    return img


def c_backups(t):
    img = c_page(t, 0, 2)
    hints(img, t, [(["L", "R"], "slot"), (["A"], "restore"), (["B"], "back")])
    return img


def c_restore(t):
    img = c_backups(t)
    return A.confirm(
        img,
        t,
        "Restore backup?",
        "Main goes back to Oct 22 18:30.\nIts save now is kept as a backup.",
        "Restore",
    )


def c_launch(t):
    """Play with two or more slots: a small card over the list, the slots as
    the page's pills; LEFT and RIGHT choose, A plays."""
    gt = theme(t.name, "gba")
    img2 = page(gt)
    title_bar(img2, gt, "gba", "Game Boy Advance", "12/161")
    card(img2, gt, 4, 20, 232, 124)
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
    for i, n in enumerate(names):
        y = 22 + 15 * i
        if i == 2:
            rrect(img2, 7, y, 226, 13, 3, gt.tint)
        BODY.draw(img2, 13, y + 1, n, gt.ink if i == 2 else gt.ink2)
    x, y, w, h = 24, 54, 192, 52
    card(img2, gt, x, y, w, h, floating=True)
    HEAD.draw(img2, x + (w - HEAD.width("Play with")) // 2, y + 1, "Play with", gt.ink)
    names = [n for n, _, _ in SLOTS]
    widths = [BODY.width(n) + 10 + (6 if i == 0 else 0) for i, n in enumerate(names)]
    total = sum(widths) + 4 * (len(names) - 1)
    px = x + (w - total) // 2
    for i, n in enumerate(names):
        if i == 0:
            rrect(img2, px, y + 20, widths[i], 13, 3, gt.base)
            rrect(img2, px + 4, y + 25, 4, 4, 1, gt.on)
            BODY.draw(img2, px + 11, y + 21, n, gt.on)
        else:
            rrect(img2, px, y + 20, widths[i], 13, 3, gt.soft)
            BODY.draw(img2, px + 5, y + 21, n, gt.ink2)
        px += widths[i] + 4
    BODY.draw(
        img2,
        x + (w - BODY.width("Saved today 14:32")) // 2,
        y + 37,
        "Saved today 14:32",
        gt.ink3,
    )
    hints(
        img2,
        gt,
        [([("glyph", "leftright")], "slot"), (["A"], "play"), (["B"], "cancel")],
    )
    return img2


def c_storage(t):
    """Settings > Saves: each part of the card is a row that opens what it
    holds; Trash opens the deleted slots, which A brings back."""
    st = theme(t.name, "set")
    img = page(st)
    title_bar(img, st, "set", "Saves", "129 KB")
    top = A.section(img, st, LOOK["content_top"], "chip", "On the card")
    y, after = A.card_rows(img, st, top, 3)
    A.row(img, st, y, "Saves · 3 games", "81 KB", False)
    A.row(img, st, y + 12, "Without a game · 2", "", True, chev=True)
    A.row(img, st, y + 24, "Trash · 1 slot", "16 KB", False, chev=True)
    top = A.section(img, st, after, "clock", "Backups", "32 KB")
    y, _ = A.card_rows(img, st, top, 1)
    A.row(img, st, y, "Backups kept a slot", "5", False)
    hint_text(img, st, "Saves whose ROM moved or is gone.")
    return img


def c_trash(t):
    st = theme(t.name, "set")
    img = page(st)
    title_bar(img, st, "set", "Trash", "16 KB")
    top = A.section(img, st, LOOK["content_top"], "save", "Deleted slots", "30 days")
    y, after = A.card_rows(img, st, top, 2)
    A.row(img, st, y, "Pokémon Emerald · Old", "4 days left", True)
    A.row(img, st, y + 12, "Kirby · Main", "29 days left", False)
    y, _ = A.card_rows(img, st, after, 1)
    A.row(img, st, y, "Empty trash", "16 KB", False, "danger")
    hints(img, st, [(["A"], "bring back"), (["B"], "back")])
    return img


# --- Sheets -------------------------------------------------------------------

VARIANTS = {
    "a": (
        "A  Pages and popups",
        [
            (A.screen_saves, "A1 A game's saves"),
            (lambda t: A.screen_slot_popup(t, 1), "A2 A on a slot: its popup"),
            (A.screen_backups, "A3 Backups (from the popup)"),
            (A.screen_restore, "A4 Restore asks"),
            (A.screen_launch, "A5 Play: a popup beside the game"),
            (A.screen_storage, "A6 Settings > Saves"),
        ],
        A.screen_saves,
    ),
    "b": (
        "B  List and detail",
        [
            (b_saves, "B1 Slots left, the chosen one right"),
            (b_options, "B2 START: the slot's popup"),
            (b_backups, "B3 RIGHT: into its backups"),
            (b_restore, "B4 Restore asks"),
            (b_launch, "B5 Play: the same page"),
            (b_storage, "B6 Settings > Saves, as NOR's"),
        ],
        b_saves,
    ),
    "c": (
        "C  Slot tabs and a timeline",
        [
            (c_saves, "C1 One slot at a time, L R"),
            (c_options, "C2 START: the slot's popup"),
            (c_backups, "C3 DOWN: its backups"),
            (c_restore, "C4 Restore asks"),
            (c_launch, "C5 Play: the slots as pills"),
            (c_storage, "C6 Settings > Saves: rows that open"),
            (c_trash, "C7 Trash: deleted slots come back"),
        ],
        c_saves,
    ),
}


def main():
    out = ROOT / "docs/design/v0.5"
    for key, (_, screens, light) in VARIANTS.items():
        dark = theme("dark", "gba")
        shots = [(f(dark), cap) for f, cap in screens]
        shots.append((light(theme("light", "gba")), f"{key.upper()}1 in light"))
        sheet(shots, 2, out / f"saves-variant-{key}.png")


if __name__ == "__main__":
    main()
