"""The v0.4 Settings page mockups (B16, T25).

usage: uv run --with pillow python -I tools/mockup/settings.py

Writes docs/design/v0.4/settings-light.png and settings-dark.png. The rows,
labels, explanations and values are B6's (source/settings_model.c and the
en_set_* strings in source/lang.c); docs/design/v0.4/mockups-b16-b18.md
explains every screen and number.
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
    chip,
    chip_width,
    glyph,
    hint_text,
    hints,
    hline,
    icon,
    icon8,
    mix,
    outline,
    page,
    rect,
    rrect,
    scrollbar,
    sheet,
    snap,
    theme,
    title_bar,
)

# The page's own look (settings_page.c's look table), px
SET = {
    "card_x": 4,
    "card_w": 223,  # 4..226; the scrollbar sits on the paper at x 231
    "card_pad": 2,  # above the first row and below the last
    "pitch": 12,
    "pill_x": 7,
    "pill_w": 216,
    "label_x": 13,
    "value_right": 218,  # values end here (exclusive)
    "head_icon_x": 8,
    "head_text_x": 20,
    "head_h": 12,  # the section label's line, above its card
    "gap": 2,  # paper between a card's shadow and the next label
    "track_top": 24,
    "track_bottom": 142,
    "track_x": 231,
    "swatch_w": 12,
    "swatch_h": 10,
    "swatch_gap": 2,
    "keychip_gap": 2,
    "editor_x": 126,
    "editor_w": 104,
}
CLIP = (0, LOOK["content_top"], W, LOOK["content_bottom"] + 1)

# LED colours by value (red 4, green 2, blue 1), as en_set_colours names them,
# drawn as the LED looks rather than as pure RGB
LED = {
    0: ("Off", None),
    1: ("Blue", (57, 115, 231)),
    2: ("Green", (57, 181, 74)),
    3: ("Cyan", (41, 189, 214)),
    4: ("Red", (231, 57, 57)),
    5: ("Magenta", (214, 66, 198)),
    6: ("Yellow", (239, 198, 33)),
    7: ("White", (255, 255, 255)),
}

# B6's table: (label, explanation, value). A value is a string, ("keys",
# [...]), ("colour", n) or ("action",).
SECTIONS = [
    (
        "look",
        "Look",
        [
            ("Theme", "Light paper or dark graphite.", "Light"),
            ("Game view", "Game lists: names alone, or box art.", "List"),
            ("Start on", "Where the menu opens at power-on.", "Last place"),
            ("Dim screen", "Dims the screen after idle minutes.", "10 min"),
        ],
    ),
    (
        "clock",
        "Clock",
        [
            ("Time", "The cart's clock. A sets it.", "14:32"),
            ("Date", "The cart's date. A sets it.", "2026-10-20"),
            ("24-hour clock", "14:32 when on, 2:32 PM when off.", "On"),
            ("Clock for games", "Lets games read the cart's clock.", "On"),
        ],
    ),
    (
        "games",
        "Games",
        [
            ("Fast patching", "Patches from a built-in table first.", "On"),
            ("Auto save", "Copies saves to the card, no question.", "Off"),
            ("Save states", "Save and load a game's state in game.", "Off"),
            ("Cheats", "Applies a game's cheat file.", "Off"),
        ],
    ),
    (
        "cart",
        "Cart",
        [
            ("Rumble for games", "Lets games use the cart's rumble.", "On"),
            ("Mode B", "What the cart is in a DS (mode B).", "Link"),
            ("LED", "The cart's light.", "On"),
            ("LED colour", "The light's colour while idle.", ("colour", 7)),
            ("SD card light", "The light while the SD card works.", ("colour", 0)),
        ],
    ),
    (
        "keys",
        "Hotkeys",
        [
            ("Back to menu", "Hold the keys to leave a game.", "On"),
            (
                "Menu keys",
                "Held together in game. A sets them.",
                ("keys", ["L", "R", "START"]),
            ),
            ("Sleep", "Screen off. START+SELECT wakes it.", "On"),
            (
                "Sleep keys",
                "Held together in game. A sets them.",
                ("keys", ["L", "R", "SELECT"]),
            ),
        ],
    ),
    (
        "library",
        "Library",
        [
            ("Rescan library", "Looks for games on the card again.", ("action",)),
            ("Show all files", "The SD card page shows hidden files.", "Off"),
        ],
    ),
    ("chip", "System", [("Logging", "Writes a log to /torii/logs.", "Off")]),
]
ROWS = [(s, i) for s, sec in enumerate(SECTIONS) for i in range(len(sec[2]))]


def block_h(n):
    return SET["head_h"] + 2 * SET["card_pad"] + SET["pitch"] * n + 1 + SET["gap"]


def layout():
    """Each section's label y and each row's y, in page coordinates (0 = the
    content top)."""
    y, out = 0, []
    for _, _, rows in SECTIONS:
        out.append(y)
        y += block_h(len(rows))
    return out, y


SEC_Y, PAGE_H = layout()


def row_y(s, i, scroll):
    return (
        LOOK["content_top"]
        + SEC_Y[s]
        - scroll
        + SET["head_h"]
        + SET["card_pad"]
        + SET["pitch"] * i
    )


def scroll_for_section(s):
    return SEC_Y[s]


def swatch(img, t, x, y, n, selected=False):
    """A 12x10 LED swatch with a 1 px edge; Off is the card with a slash."""
    w, h = SET["swatch_w"], SET["swatch_h"]
    _name, c = LED[n]
    if selected:
        outline(img, x - 1, y - 1, w + 2, h + 2, 3, t.ink, t.ink, CLIP)
    if c is None:
        edge = t.ink3
        outline(img, x, y, w, h, 2, edge, t.card, CLIP)
        for k in range(1, w - 1):  # a slash, bottom left to top right
            yy = y + h - 2 - (k - 1) * (h - 3) // (w - 3)
            img.putpixel((x + k, yy), edge)
    else:
        edge = snap(mix(c, (0, 0, 0), 0.28)) if c != (255, 255, 255) else t.ink3
        outline(img, x, y, w, h, 2, edge, snap(c), CLIP)


def keychips(t, keys):
    return sum(chip_width(k) for k in keys) + SET["keychip_gap"] * (len(keys) - 1)


def draw_keychips(img, t, right, y, keys, fill=None, ink=None):
    x = right - keychips(t, keys)
    for k in keys:
        x += (
            chip(img, t, x, y + 1, k, fill=fill, ink=ink, clip=CLIP)
            + SET["keychip_gap"]
        )


def draw_value(img, t, right, y, value, sel, editing=None):
    """A row's value, right-aligned to right. editing: what « » holds."""
    ink = t.ink2 if sel else t.ink3
    if editing is not None:
        mark_l, mark_r = "«", "»"
        rw = BODY.width(mark_r)
        BODY.draw(img, right - rw, y, mark_r, t.aink, CLIP)
        inner_r = right - rw - 3
        if isinstance(editing, str):
            iw = BODY.width(editing)
            BODY.draw(img, inner_r - iw, y, editing, t.ink, CLIP)
        else:
            iw = editing(img, inner_r, y)
        lx = inner_r - iw - 3 - BODY.width(mark_l)
        BODY.draw(img, lx, y, mark_l, t.aink, CLIP)
        return
    if isinstance(value, str):
        BODY.draw(img, right - BODY.width(value), y, value, ink, CLIP)
    elif value[0] == "keys":
        draw_keychips(img, t, right, y, value[1])
    elif value[0] == "colour":
        name, _ = LED[value[1]]
        tw = BODY.width(name)
        BODY.draw(img, right - tw, y, name, ink, CLIP)
        swatch(img, t, right - tw - 4 - SET["swatch_w"], y + 1, value[1])
    elif value[0] == "action":
        glyph(img, "chev", right - 4, y + 4, ink)


def settings_page(t, scroll, sel, edit=None, overrides=None, position=True):
    """The page scrolled to scroll px with row sel = (section, row) selected.
    edit: (« » content) for the selected row; overrides: {(s, i): value}."""
    img = page(t)
    overrides = overrides or {}
    for s, (ic, name, rows) in enumerate(SECTIONS):
        top = LOOK["content_top"] + SEC_Y[s] - scroll
        if top > CLIP[3] or top + block_h(len(rows)) < CLIP[1]:
            continue
        # The section label on the paper: 8x8 icon and name in the item's ink
        # Only whole lines show: a label or row cut by the view's edge is
        # left out, while cards clip by the pixel
        if CLIP[1] <= top and top + SET["head_h"] <= CLIP[3]:
            icon(img, icon8(ic), SET["head_icon_x"], top + 2, t.aink)
            BODY.draw(img, SET["head_text_x"], top, name, t.aink, CLIP)
        cy = top + SET["head_h"]
        ch = 2 * SET["card_pad"] + SET["pitch"] * len(rows)
        card(img, t, SET["card_x"], cy, SET["card_w"], ch, clip=CLIP)
        for i, (label, _, value) in enumerate(rows):
            ry = cy + SET["card_pad"] + SET["pitch"] * i
            if ry < CLIP[1] or ry + SET["pitch"] > CLIP[3]:
                continue
            is_sel = (s, i) == sel
            if is_sel:
                rrect(
                    img,
                    SET["pill_x"],
                    ry,
                    SET["pill_w"],
                    LOOK["pill_h"],
                    LOOK["pill_radius"],
                    t.tint,
                    CLIP,
                )
            BODY.draw(img, SET["label_x"], ry, label, t.ink if is_sel else t.ink2, CLIP)
            v = overrides.get((s, i), value)
            draw_value(
                img, t, SET["value_right"], ry, v, is_sel, edit if is_sel else None
            )
    # The title bar draws over anything scrolled under it
    rect(img, 0, 0, W, LOOK["content_top"], t.bg)
    n = ROWS.index(sel) + 1
    title_bar(img, t, "set", "Settings", f"{n}/{len(ROWS)}" if position else "")
    view = LOOK["content_bottom"] + 1 - LOOK["content_top"]
    scrollbar(
        img,
        t,
        SET["track_top"],
        SET["track_bottom"],
        scroll / PAGE_H,
        (scroll + view) / PAGE_H,
        SET["track_x"],
    )
    return img


def explain(sel):
    s, i = sel
    return SECTIONS[s][2][i][1]


def browse(t, scroll, sel, **kw):
    img = settings_page(t, scroll, sel, **kw)
    hint_text(img, t, explain(sel))
    return img


EDIT_HINTS = [([("glyph", "leftright")], "change"), (["A"], "done"), (["B"], "cancel")]


def editing(t, scroll, sel, inner, hint_items=None, **kw):
    img = settings_page(t, scroll, sel, edit=inner, **kw)
    hints(img, t, hint_items or EDIT_HINTS)
    return img


def time_editor(t, scroll, sel, fields, active, title="Time"):
    """The field card beside the Time row (#20): fields = ["14", "32"]."""
    img = settings_page(t, scroll, sel)
    ry = row_y(sel[0], sel[1], scroll)
    x, w = SET["editor_x"], SET["editor_w"]
    h = 60
    top = min(max(ry - 20, LOOK["content_top"] + 1), LOOK["content_bottom"] + 1 - h)
    card(img, t, x, top, w, h, floating=True)
    # The notch: 3 px pointing at the row's middle, over the card's edge
    my = ry + 6
    for k in range(3):
        for j in range(-k, k + 1):
            img.putpixel((x - 3 + k, my + j), t.edge if abs(j) == k else t.float)
    img.putpixel((x, my), t.float)
    for j in (-1, 1):
        img.putpixel((x, my + j), t.float)
    # Header: 8x8 icon and the title in ink, then a hairline
    icon(img, icon8("clock"), x + 6, top + 4, t.aink)
    BODY.draw(img, x + 17, top + 2, title, t.ink)
    hline(img, x + 4, x + w - 4, top + 15, t.edge)
    # The fields in the heading font: the active one in a tint box with
    # arrows above and below
    sep = " : " if title == "Time" else "-"
    parts = []
    for k, f in enumerate(fields):
        parts.append(("f", f, k))
        if k < len(fields) - 1:
            parts.append(("s", sep if not (title == "Time" and k == 1) else " ", k))
    box_pad = 3
    widths = [HEAD.width(p[1]) + (2 * box_pad if p[0] == "f" else 0) for p in parts]
    total = sum(widths)
    fx = x + (w - total) // 2
    fy = top + 30
    for (kind, text, k), pw in zip(parts, widths, strict=False):
        if kind == "f":
            if k == active:
                rrect(img, fx, fy - 1, pw, 17, 3, t.tint)
                gw = 5
                glyph(img, "up", fx + (pw - gw) // 2, fy - 6, t.aink)
                glyph(img, "down", fx + (pw - gw) // 2, fy + 18, t.aink)
            HEAD.draw(img, fx + box_pad, fy - 1, text, t.ink if k == active else t.ink2)
        else:
            HEAD.draw(img, fx, fy - 1, text, t.ink3)
        fx += pw
    hints(
        img,
        t,
        [
            ([("glyph", "updown")], "change"),
            ([("glyph", "leftright")], "field"),
            (["A"], "set"),
            (["B"], "cancel"),
        ],
    )
    return img


def swatch_strip(t, selected):
    def draw(img, right, y):
        n = 8
        w = n * SET["swatch_w"] + (n - 1) * SET["swatch_gap"]
        x = right - w
        for k in range(n):
            swatch(
                img,
                t,
                x + k * (SET["swatch_w"] + SET["swatch_gap"]),
                y + 1,
                k,
                k == selected,
            )
        return w

    return draw


def capture_chips(t, keys):
    def draw(img, right, y):
        w = keychips(t, keys)
        draw_keychips(img, t, right, y, keys, fill=t.base, ink=t.on)
        return w

    return draw


def countdown_hint(img, t, text, frac):
    """The capture's hint: a 5 s bar that empties, then the text."""
    bar_w, gap = 40, 6
    tw = BODY.width(text)
    x = (W - (bar_w + gap + tw)) // 2
    y = LOOK["hint_y"] + 4
    rrect(img, x, y, bar_w, 3, 1, t.soft)
    rrect(img, x, y, max(3, round(bar_w * frac)), 3, 1, t.base)
    BODY.draw(img, x + bar_w + gap, LOOK["hint_y"], text, t.ink2)


def screens(name):
    t = theme(name)
    view = LOOK["content_bottom"] + 1 - LOOK["content_top"]
    bottom = PAGE_H - view
    out = []
    if name == "light":
        out.append((browse(t, 0, (0, 0)), "1 Settings: the top, Theme selected"))
        out.append((editing(t, 0, (0, 2), "Last place"), "2 Start on, edited in place"))
        dark = theme("dark")
        out.append(
            (
                editing(dark, 0, (0, 0), "Dark"),
                "3 Theme previews live (LEFT/RIGHT)",
            )
        )
        out.append(
            (
                time_editor(t, SEC_Y[1], (1, 0), ["14", "32"], 0),
                "4 Time: the field card",
            )
        )
        cart = scroll_for_section(3)
        img = settings_page(t, cart, (3, 3), edit=swatch_strip(t, 5))
        hints(
            img,
            t,
            [([("glyph", "leftright")], "Magenta"), (["A"], "done"), (["B"], "cancel")],
        )
        out.append((img, "5 LED colour: swatches, name in the hints"))
        keys = scroll_for_section(4)
        img = settings_page(t, keys, (4, 3), edit=capture_chips(t, ["L", "R"]))
        countdown_hint(img, t, "Press keys... let go to set.", 0.7)
        out.append((img, "6 Sleep keys: press the combination"))
        img = settings_page(t, keys, (4, 3))
        hint_text(img, t, "Already used by Menu keys.", t.danger)
        out.append((img, "7 Refused: the old keys stay (90 f)"))
        out.append((browse(t, bottom, (5, 0)), "8 The bottom: Library and System"))
    else:
        dark = {(0, 0): "Dark"}
        out.append(
            (
                editing(t, 0, (0, 3), "10 min", overrides=dark),
                "1 Dark: Dim screen, edited in place",
            )
        )
        cart = scroll_for_section(3)
        out.append((browse(t, cart, (3, 3)), "2 Dark: Cart and Hotkeys"))
    return out


def main():
    for name in ("light", "dark"):
        out = ROOT / f"docs/design/v0.4/settings-{name}.png"
        sheet(screens(name), 4 if name == "light" else 2, out)
        print(out.relative_to(ROOT))


if __name__ == "__main__":
    main()
