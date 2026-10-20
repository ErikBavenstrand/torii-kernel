#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# ///
"""Write the menu's palette (source/ui/palette.c and palette.h) from
docs/design/v0.4/palette.json.

usage: palette.py [--check]

The JSON holds the surfaces of each theme, its pill_mix (how much of an
accent's base the tint has over a card) and one accent per carousel item,
with base, tint, ink and on per theme; its colours are already snapped to
15 bits (#RRGGBB with each channel a 5-bit value scaled up). The C tables
keep them as RGB15. `just palette` runs this; --check compares the committed
files instead of writing them (tests/host.sh fails when they are stale).
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
JSON = ROOT / "docs" / "design" / "v0.4" / "palette.json"
OUT_C = ROOT / "source" / "ui" / "palette.c"
OUT_H = ROOT / "source" / "ui" / "palette.h"

THEMES = ["light", "dark"]
SURFACES = ["bg", "card", "edge", "soft", "ink", "ink2", "ink3", "shadow"]
ACCENT = ["base", "tint", "ink", "on"]


class PaletteError(Exception):
    pass


def rgb15(text: str) -> int:
    """#RRGGBB to RGB15; refuses a colour that isn't snapped to 15 bits."""
    if not re.fullmatch(r"#[0-9A-Fa-f]{6}", text):
        raise PaletteError(f"{text!r} isn't #RRGGBB")
    value = 0
    for shift, pos in ((0, 1), (5, 3), (10, 5)):
        byte = int(text[pos : pos + 2], 16)
        five = byte >> 3
        if (five << 3 | five >> 2) != byte:
            raise PaletteError(f"{text} isn't snapped to 15 bits")
        value |= five << shift
    return value


def q8(fraction: float) -> int:
    if not 0 <= fraction <= 1:
        raise PaletteError(f"pill_mix {fraction} isn't between 0 and 1")
    return round(fraction * 256) if fraction < 1 else 255


def keys(obj: dict, want: list[str], where: str) -> None:
    missing = [k for k in want if k not in obj]
    if missing:
        raise PaletteError(f"{where} has no {', '.join(missing)}")


def load(data: dict) -> dict:
    keys(data, ["surfaces", "pill_mix", "accents", "danger"], "palette.json")
    surfaces = {}
    for theme in THEMES:
        keys(data["surfaces"], [theme], "surfaces")
        keys(data["surfaces"][theme], SURFACES, f"surfaces.{theme}")
        surfaces[theme] = [rgb15(data["surfaces"][theme][s]) for s in SURFACES]
    keys(data["pill_mix"], THEMES, "pill_mix")
    pill_mix = {theme: q8(data["pill_mix"][theme]) for theme in THEMES}
    items = []
    for key, accent in data["accents"].items():
        if not re.fullmatch(r"[a-z][a-z0-9]*", key):
            raise PaletteError(f"accent key {key!r} isn't a C name")
        keys(accent, ["name", *THEMES], f"accents.{key}")
        colours = {}
        for theme in THEMES:
            keys(accent[theme], ACCENT, f"accents.{key}.{theme}")
            colours[theme] = [rgb15(accent[theme][a]) for a in ACCENT]
        items.append((key, accent["name"], colours))
    if data["danger"] not in data["accents"]:
        raise PaletteError(f"danger names {data['danger']!r}, which isn't an accent")
    return {
        "surfaces": surfaces,
        "pill_mix": pill_mix,
        "items": items,
        "danger": data["danger"],
    }


HEAD = "// The menu's palette, written by tools/palette.py from docs/design/v0.4/palette.json:\n// don't edit by hand; edit the JSON and run `just palette`."


def render_h(p: dict) -> str:
    items = "\n".join(f"\tPAL_{key.upper()},  // {name}" for key, name, _ in p["items"])
    pill = "\n".join(
        f"#define PAL_{theme.upper()}_PILL_MIX {p['pill_mix'][theme]}"
        for theme in THEMES
    )
    fields = ", ".join(SURFACES)
    return f"""{HEAD}
#ifndef TORII_UI_PALETTE_H
#define TORII_UI_PALETTE_H

#include <gba_base.h>

enum pal_theme {{
\tPAL_LIGHT,
\tPAL_DARK,
\tPAL_THEMES,
}};

// The items with an accent, in palette.json's order
enum pal_item {{
{items}
\tPAL_ITEMS,
}};

// The item whose ink is the danger colour (Delete, Format, failures)
#define PAL_DANGER PAL_{p["danger"].upper()}

// A theme's surfaces (RGB15)
struct pal_surfaces {{
\tu16 {fields};
}};

// An item's accent in one theme (RGB15): base for icons, bars and solid
// buttons; tint for the selection pill; ink for marks and labels on the
// paper; on for text on a base fill
struct pal_accent {{
\tu16 base, tint, ink, on;
}};

extern const struct pal_surfaces pal_surfaces[PAL_THEMES];
extern const struct pal_accent pal_accents[PAL_THEMES][PAL_ITEMS];

// How much of an accent's base its tint mixes into a card, in 256ths
{pill}

#endif
"""


def colour(value: int) -> str:
    return f"0x{value:04X}"


def render_c(p: dict) -> str:
    lines = [
        HEAD,
        '#include "palette.h"',
        "",
        "const struct pal_surfaces pal_surfaces[PAL_THEMES] = {",
    ]
    for theme in THEMES:
        values = ", ".join(
            f".{s} = {colour(v)}"
            for s, v in zip(SURFACES, p["surfaces"][theme], strict=True)
        )
        lines.append(f"\t[PAL_{theme.upper()}] = {{{values}}},")
    lines += [
        "};",
        "",
        "const struct pal_accent pal_accents[PAL_THEMES][PAL_ITEMS] = {",
    ]
    for theme in THEMES:
        lines.append(f"\t[PAL_{theme.upper()}] =")
        lines.append("\t\t{")
        for key, _, colours in p["items"]:
            values = ", ".join(
                f".{a} = {colour(v)}"
                for a, v in zip(ACCENT, colours[theme], strict=True)
            )
            lines.append(f"\t\t\t[PAL_{key.upper()}] = {{{values}}},")
        lines.append("\t\t},")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main() -> int:
    args = sys.argv[1:]
    if args not in ([], ["--check"]):
        print(__doc__, file=sys.stderr)
        return 2
    try:
        p = load(json.loads(JSON.read_text(encoding="utf-8")))
    except (PaletteError, json.JSONDecodeError) as e:
        print(f"{JSON.name}: {e}", file=sys.stderr)
        return 1
    out = {OUT_C: render_c(p), OUT_H: render_h(p)}
    if args == ["--check"]:
        stale = [
            path.name
            for path, text in out.items()
            if not path.exists() or path.read_text(encoding="utf-8") != text
        ]
        if stale:
            print(f"{', '.join(stale)} stale: run `just palette`", file=sys.stderr)
            return 1
        return 0
    for path, text in out.items():
        path.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
