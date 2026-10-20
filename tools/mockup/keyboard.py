"""The v0.4 rename keyboard mockups (B18, T12).

usage: uv run --with pillow python -I tools/mockup/keyboard.py

Writes docs/design/v0.4/keyboard-light.png and keyboard-dark.png: the
research's 10-column QWERTY grid (docs/research/v0.4/research-v04-ux.md, Q1)
in the Paper clean look. docs/design/v0.4/mockups-b16-b18.md explains every
screen and number.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gfx import (
    BODY,
    HEAD,
    ROOT,
    card,
    chip,
    glyph,
    glyph_size,
    hint_text,
    hints,
    outline,
    page,
    rect,
    rrect,
    sheet,
    theme,
    title_bar,
)

# The keyboard's look table (rename.c), px
KB = {
    "field_x": 4,
    "field_y": 21,
    "field_w": 232,
    "field_h": 18,
    "lchip_x": 8,  # L and R chips at the field's ends: they move the caret
    "rchip_x": 215,
    "chip_y": 25,
    "text_x": 25,  # the name's first pixel
    "text_y": 24,  # its 12 px line's top
    "counter_right": 210,
    "caret_w": 1,
    "caret_gap": 2,  # px the text after the caret moves right
    "caret_h": 11,
    "card_x": 4,
    "card_y": 43,
    "card_w": 232,
    "card_h": 98,
    "cols": 10,
    "col_w": 21,
    "grid_x": 15,  # 4 + (232 - 210) / 2
    "grid_y": 47,  # card top + 4
    "pitch": 18,
    "key_inset": 1,  # the pill is the cell less 1 px each side: 19x16
    "key_radius": 3,
}
MAX_NAME = 60

LOWER = ["1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm._'"]
UPPER = ["1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM._'"]
LATIN_LOWER = ["àáâãäåæçèé", "êëìíîïðñòó", "ôõöøùúûüýþ", "ßÿ!&()+,;@"]
LATIN_UPPER = ["ÀÁÂÃÄÅÆÇÈÉ", "ÊËÌÍÎÏÐÑÒÓ", "ÔÕÖØÙÚÛÜÝÞ", "#$%=[]^~{}"]
# The function row: (key, columns)
FUNCS = [
    ("shift", 2),
    ("latin", 2),
    ("space", 2),
    ("bksp", 1),
    ("cancel", 2),
    ("ok", 1),
]


def func_at(col):
    c = 0
    for k, (_name, span) in enumerate(FUNCS):
        if c <= col < c + span:
            return k
        c += span
    return len(FUNCS) - 1


def cell(row, col, span=1):
    x = KB["grid_x"] + col * KB["col_w"]
    y = KB["grid_y"] + row * KB["pitch"]
    return x, y, span * KB["col_w"], KB["pitch"]


def key_pill(img, t, x, y, w, h, colour):
    i = KB["key_inset"]
    rrect(img, x + i, y + i, w - 2 * i, h - 2 * i, KB["key_radius"], colour)


def centre_text(img, face, x, y, w, h, text, colour, line_top):
    tw = face.width(text)
    face.draw(img, x + (w - tw + 1) // 2, y + line_top, text, colour)


def field(img, t, name, caret, counter_colour=None, scroll_left=False):
    """The name field: a card with the name, the caret and the counter."""
    card(img, t, KB["field_x"], KB["field_y"], KB["field_w"], KB["field_h"])
    chip(img, t, KB["lchip_x"], KB["chip_y"], "L")
    chip(img, t, KB["rchip_x"], KB["chip_y"], "R")
    count = f"{len(name)}/{MAX_NAME}"
    cw = BODY.width(count)
    cx = KB["counter_right"] - cw
    BODY.draw(img, cx, KB["text_y"], count, counter_colour or t.ink3)
    right = cx - 6
    x0 = KB["text_x"]
    start = 0
    if scroll_left:  # keep the caret in view: cut the start, mark it with ...
        dots = BODY.width("...") + 1
        while BODY.width(name[start:caret]) > right - x0 - dots - 2:
            start += 1
        BODY.draw(img, x0, KB["text_y"], "...", t.ink3)
        x0 += dots
    # The caret opens a 2 px gap: the text after it moves right by 2 px, so
    # it never covers a glyph
    clip = (x0, 0, right, 160)
    before, after = name[start:caret], name[caret:]
    x = BODY.draw(img, x0, KB["text_y"], before, t.ink, clip)
    rect(img, x, KB["text_y"] + 1, KB["caret_w"], KB["caret_h"], t.base)
    BODY.draw(img, x + KB["caret_gap"], KB["text_y"], after, t.ink, clip)


def keyboard(img, t, rows, sel, upper=False, latin=False):
    card(img, t, KB["card_x"], KB["card_y"], KB["card_w"], KB["card_h"])
    sr, sc = sel
    for r, keys in enumerate(rows):
        for c, ch in enumerate(keys):
            x, y, w, h = cell(r, c)
            if (r, c) == (sr, sc):
                key_pill(img, t, x, y, w, h, t.tint)
            colour = t.ink if ch.isalpha() else t.ink2
            centre_text(img, HEAD, x, y, w, h, ch, colour, 0)
    # The function row: paper keys, OK solid in the item's colour
    col = 0
    sel_func = func_at(sc) if sr == 4 else -1
    for k, (name, span) in enumerate(FUNCS):
        x, y, w, h = cell(4, col, span)
        col += span
        active = k == sel_func
        i = KB["key_inset"]
        if name == "ok":
            if active:
                outline(img, x, y, w, h, 4, t.ink, t.ink)
            rrect(
                img,
                x + i + (1 if active else 0),
                y + i + (1 if active else 0),
                w - 2 * i - (2 if active else 0),
                h - 2 * i - (2 if active else 0),
                KB["key_radius"],
                t.base,
            )
            centre_text(img, BODY, x, y, w, h, "OK", t.on, 3)
            continue
        on_page = name == "latin" and latin
        key_pill(
            img, t, x, y, w, h, t.tint if active else (t.soft if on_page else t.bg)
        )
        if name == "shift":
            # The shift arrow, filled while upper case is on, and the case
            g = "shift_on" if upper else "shift"
            label = "ABC" if upper else "abc"
            gw, gh = glyph_size(g)
            lw = BODY.width(label)
            gx = x + (w - (gw + 3 + lw) + 1) // 2
            glyph(img, g, gx, y + 5, t.ink if upper else t.ink2)
            BODY.draw(img, gx + gw + 3, y + 3, label, t.ink if upper else t.ink2)
        elif name == "latin":
            centre_text(img, BODY, x, y, w, h, "àé&", t.ink if on_page else t.ink2, 3)
        elif name == "space":
            centre_text(img, BODY, x, y, w, h, "Space", t.ink2, 3)
        elif name == "bksp":
            gw, gh = glyph_size("bksp")
            glyph(img, "bksp", x + (w - gw + 1) // 2, y + (h - gh) // 2 + 1, t.ink2)
        elif name == "cancel":
            centre_text(img, BODY, x, y, w, h, "Cancel", t.ink2, 3)


HINTS = [
    (["A"], "type"),
    (["B"], "delete"),
    (["SELECT"], "Aa"),
    (["START"], "OK"),
]


def screen(
    t,
    name,
    caret,
    sel,
    upper=False,
    latin=False,
    counter=None,
    scroll_left=False,
    message=None,
):
    img = page(t)
    title_bar(img, t, "gba", "Rename")
    field(img, t, name, caret, counter, scroll_left)
    rows = (
        (LATIN_UPPER if upper else LATIN_LOWER)
        if latin
        else (UPPER if upper else LOWER)
    )
    keyboard(img, t, rows, sel, upper, latin)
    if message:
        hint_text(img, t, message, t.danger)
    else:
        hints(img, t, HINTS)
    return img


LONG = "Final Fantasy I & II - Dawn of Souls (fan translation v2.11)"


def screens(name):
    t = theme(name, "gba")
    out = []
    out.append(
        (
            screen(t, "Metroid Fusion", 14, (1, 0)),
            "1 Rename: prefilled, caret at the end",
        )
    )
    if name == "dark":
        return out
    out.append(
        (
            screen(t, "Metroid Fusion J", 16, (1, 6), upper=True),
            "2 Shifted (SELECT): ABC",
        )
    )
    out.append(
        (
            screen(t, "Pokémon Pinball", 4, (0, 9), latin=True),
            "3 Latin-1 page, caret moved with L/R",
        )
    )
    long = LONG
    assert len(long) == MAX_NAME
    out.append(
        (
            screen(
                t,
                long,
                len(long),
                (2, 8),
                counter=t.danger,
                scroll_left=True,
                message="Names stop at 60 letters.",
            ),
            "4 Too long: the counter and the message",
        )
    )
    return out


def main():
    for name in ("light", "dark"):
        out = ROOT / f"docs/design/v0.4/keyboard-{name}.png"
        sheet(screens(name), 2 if name == "light" else 1, out)
        print(out.relative_to(ROOT))


if __name__ == "__main__":
    main()
