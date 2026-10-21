"""The v0.4 palette sheet: docs/design/v0.4/palette.png from palette.json.

usage: uv run --with pillow python -I tools/mockup/palette_sheet.py

Draws both themes side by side: each carousel item's accent (base, pill
tint, ink and on), a sample pill on a card with the item's 16x16 icon, and
the surfaces. Run it after changing palette.json (`just palette` writes the
C tables from the same file).
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gfx import BODY, HEAD, PALETTE, ROOT, icon, icon16, rect, rgb, rrect

from PIL import Image

OUT = ROOT / "docs/design/v0.4/palette.png"
SCALE = 2
PANEL_W, PANEL_H = 368, 326
SW_X = 116  # the first swatch column
SAMPLE_X = 226
GAP = 8
ROW = 22
SWATCH = 20
SURFACES = ["bg", "card", "edge", "soft", "shadow", "ink", "ink2", "ink3"]


def swatch(img, x, y, w, h, colour, edge):
    rect(img, x, y, w, h, edge)
    rect(img, x + 1, y + 1, w - 2, h - 2, colour)


def panel(name: str) -> Image.Image:
    s = {k: rgb(v) for k, v in PALETTE["surfaces"][name].items()}
    img = Image.new("RGB", (PANEL_W, PANEL_H), s["bg"])
    HEAD.draw(img, 8, 2, name, s["ink"])
    for i, label in enumerate(("base", "pill", "ink", "on")):
        BODY.draw(img, SW_X + i * 26 + (SWATCH - BODY.width(label)) // 2, 6, label, s["ink2"])
    BODY.draw(img, SAMPLE_X, 6, "sample (card / pill)", s["ink2"])
    y = 22
    for item, accent in PALETTE["accents"].items():
        a = {k: rgb(v) for k, v in accent[name].items() if k != "contrast"}
        BODY.draw(img, 8, y + 4, accent["name"], s["ink"])
        for i, key in enumerate(("base", "tint", "ink", "on")):
            swatch(img, SW_X + i * 26, y, SWATCH, SWATCH, a[key], s["edge"])
        x = SAMPLE_X
        rect(img, x, y - 1, 134, ROW, s["card"])
        rrect(img, x + 2, y + 2, 130, 16, 3, a["tint"])
        path = icon16(item)
        if path.exists():
            icon(img, path, x + 2, y + 2)
        BODY.draw(img, x + 21, y + 4, "Play", s["ink"])
        BODY.draw(img, x + 64, y + 4, "Label", a["ink"])
        y += ROW
    y += 8
    for i, key in enumerate(SURFACES):
        x = 8 + i * 44
        swatch(img, x, y, 38, 16, s[key], s["edge"])
        BODY.draw(img, x + (38 - BODY.width(key)) // 2, y + 20, key, s["ink2"])
        hexa = PALETTE["surfaces"][name][key].lstrip("#")
        BODY.draw(img, x + (38 - BODY.width(hexa)) // 2, y + 32, hexa, s["ink3"])
    return img


def main() -> int:
    light, dark = panel("light"), panel("dark")
    sheet = Image.new("RGB", (2 * PANEL_W + 3 * GAP, PANEL_H + 2 * GAP), (40, 40, 40))
    sheet.paste(light, (GAP, GAP))
    sheet.paste(dark, (2 * GAP + PANEL_W, GAP))
    sheet.resize((sheet.width * SCALE, sheet.height * SCALE), Image.NEAREST).save(OUT)
    print(f"wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
