"""Before and after sheet of the menu's icons, in both themes, at 4x.

usage: uv run --with pillow python -I tools/mockup/icons_sheet.py REF OUT.png

Draws every 16x16 and 8x8 icon in assets/ui as git revision REF has it
(before) and as the working tree has it (after), on each theme's paper and
card, with the 8x8 icons in the colours the menu draws them in: an item's
ink for its own marker, the neutral ink2 for the rest. Changed icons get a
dot under their "after". C2 drew docs/design/v0.4/icons-c2.png with it.
"""

import io
import subprocess
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gfx

ITEMS16 = [
    "recent",
    "fav",
    "gba",
    "gbc",
    "gb",
    "nes",
    "sd",
    "nor",
    "set",
    "info",
    "folder",
    "file",
    "torii",
]
ACCENT = {
    "recent": "recent",
    "fav": "fav",
    "gba": "gba",
    "gbc": "gbc",
    "gb": "gb",
    "nes": "nes",
    "sd": "sd",
    "nor": "nor",
    "set": "set",
    "info": "info",
    "torii": "torii",
    "star": "fav",
}
STEP16, STEP8 = 22, 14
LEFT = 46


def load(ref: str | None, sub: str, name: str) -> Image.Image | None:
    path = f"assets/ui/{sub}/{name}.png"
    if ref is None:
        p = gfx.ROOT / path
        return Image.open(p).convert("RGBA") if p.exists() else None
    r = subprocess.run(
        ["git", "show", f"{ref}:{path}"], cwd=gfx.ROOT, capture_output=True, check=False
    )
    return (
        Image.open(io.BytesIO(r.stdout)).convert("RGBA") if r.returncode == 0 else None
    )


def paste(img, icon, x, y, colour=None):
    for j in range(icon.height):
        for i in range(icon.width):
            p = icon.getpixel((i, j))
            if p[3] >= 128:
                img.putpixel((x + i, y + j), colour or p[:3])


def ink8(name: str, theme: str) -> tuple:
    acc = ACCENT.get(name)
    if acc:
        return gfx.rgb(gfx.PALETTE["accents"][acc][theme]["ink"])
    return gfx.rgb(gfx.PALETTE["surfaces"][theme]["ink2"])


def panel(ref: str, theme: str, names8: list[str]) -> Image.Image:
    t = gfx.theme(theme)
    w = LEFT + max(len(ITEMS16) * STEP16, len(names8) * STEP8) + 8
    h = 4 + 14 + 2 * 22 + 6 + 2 * 22 + 2 * 14 + 10
    img = Image.new("RGB", (w, h), t.bg)
    gfx.BODY.draw(img, 4, 3, theme, t.ink)
    y = 18

    def row16(y, surface):
        for label, r in (("before", ref), ("after", None)):
            gfx.BODY.draw(img, 4, y + 2, label, t.ink3)
            for i, n in enumerate(ITEMS16):
                old, new = load(ref, "icons16", n), load(None, "icons16", n)
                icon = load(r, "icons16", n)
                x = LEFT + i * STEP16
                if icon:
                    paste(img, icon, x, y)
                if r is None and (
                    old is None or new is None or old.tobytes() != new.tobytes()
                ):
                    img.putpixel((x + 7, y + 18), t.ink)
                    img.putpixel((x + 8, y + 18), t.ink)
            y += 22
        return y

    y = row16(y, t.bg)
    y += 2
    gfx.card(img, t, 2, y, w - 4, h - y - 4)
    y += 4
    y = row16(y, t.card)
    for label, r in (("before", ref), ("after", None)):
        gfx.BODY.draw(img, 4, y, label, t.ink3)
        for i, n in enumerate(names8):
            old, new = load(ref, "icons8", n), load(None, "icons8", n)
            icon = load(r, "icons8", n)
            x = LEFT + i * STEP8
            if icon:
                paste(img, icon, x, y + 2, ink8(n, theme))
            if r is None and (
                old is None or new is None or old.tobytes() != new.tobytes()
            ):
                img.putpixel((x + 3, y + 11), t.ink)
                img.putpixel((x + 4, y + 11), t.ink)
        y += 14
    return img


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__)
        return 2
    ref, out = argv
    names8 = sorted(p.stem for p in (gfx.ASSETS / "icons8").glob("*.png"))
    light, dark = panel(ref, "light", names8), panel(ref, "dark", names8)
    sheet = Image.new("RGB", (light.width, light.height + dark.height))
    sheet.paste(light, (0, 0))
    sheet.paste(dark, (0, light.height))
    sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(out)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
