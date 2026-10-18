#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""Convert a pixel font to the kernel's 1bpp proportional font format.

usage: font.py SRC -o OUT --name NAME --line-height H [options]

SRC is a BDF font, or a PNG grid of glyph cells (--png-cell). OUT.c and OUT.h
get a `const struct torii_font font_NAME` covering U+0020-U+00FF, plus a
replacement glyph for everything else. The output depends only on SRC's bytes
and the options, so builds stay reproducible.

options:
  --line-height H     height of a text line in pixels
  --baseline B        rows above the baseline (default: H minus the deepest
                      ASCII descender; ASCII must always fit unchanged)
  --proportional      trim each glyph to its ink and advance by its ink width
                      plus --spacing (for fixed-width fonts)
  --spacing N         pixels after each trimmed glyph (default 1)
  --space N           advance of U+0020 and U+00A0 when trimming (default:
                      the font's own advance minus 2, at least 2)
  --squeeze           fit glyphs that stick out above the line by closing
                      blank rows inside them, the topmost first (the gap
                      between an accent and its capital); ASCII never changes
  --clip              drop ink rows still outside the line instead of failing
                      (the bottom of a cedilla too deep for the line)
  --subset-bdf FILE   also write SRC (a BDF) cut to the glyphs read, to vendor
  --png-cell WxH      PNG only: cell size; cells run left to right, top down
  --png-first CP      PNG only: code point of the first cell (default 0x20)
  --png-baseline B    PNG only: rows above the baseline in a cell
  --png-advance N     PNG only: advance of a glyph (default: the cell width,
                      or the ink width with --proportional)

Format (struct torii_font in OUT.h):
  glyphs[cp - 0x20] for U+0020-U+00FF, glyphs[TORII_FONT_REPLACEMENT] for any
  other code point. Code points the font lacks, and C1 controls, share the
  replacement glyph's entry.
  Each glyph is `rows` bitmap rows starting `top` rows below the line's top;
  rows outside that are blank. A row is `row_bytes` bytes, little-endian, and
  bit 0 is the leftmost pixel, at the pen position.
Why that layout: a GBA 4bpp tile row is one little-endian word whose low
nibble is the leftmost pixel. With bit 0 leftmost, a row expands to that word
by table lookup (bit i to nibble i) with no bit reversal, and drawing at pixel
x of a tile is `word << 4x`, the bits shifted out going to the next tile's
word as `word >> (32 - 4x)` (none when x is 0: a shift by 32 is undefined in
C). Rows of 2 or 4 bytes expand to 2 or 4 words the same way. Storing only the inked rows skips the blank ones
(about a third of a 12 px line), and keeping them contiguous lets the blitter
walk one pointer.
"""

import argparse
import hashlib
import sys
from dataclasses import dataclass, field
from pathlib import Path

FIRST, LAST = 0x20, 0xFF
REPLACEMENT = LAST - FIRST + 1  # index of the replacement glyph
# Drawn as another glyph when the font lacks them
ALIASES = {0xA0: 0x20, 0xAD: 0x2D}


@dataclass
class Glyph:
    """A glyph in line coordinates: pixels are (x, y), y = 0 the line's top."""

    advance: int
    pixels: set[tuple[int, int]] = field(default_factory=set)


@dataclass
class Font:
    name: str
    line_height: int
    baseline: int
    glyphs: list[Glyph]  # REPLACEMENT + 1 entries, index cp - FIRST
    source: str  # comment naming where it came from
    missing: list[int]  # code points drawn with the replacement glyph
    squeezed: list[int]  # code points with blank rows closed by --squeeze
    clipped: list[int]  # code points with ink rows dropped by --clip

    @property
    def max_width(self) -> int:
        return max(
            (max(x for x, _ in g.pixels) + 1 for g in self.glyphs if g.pixels),
            default=0,
        )

    @property
    def row_bytes(self) -> int:
        for size in (1, 2, 4):
            if self.max_width <= 8 * size:
                return size
        raise ValueError(
            f"{self.name}: glyphs {self.max_width} px wide, at most 32 fit"
        )

    def width(self, text: str) -> int:
        return sum(self.glyphs[index(ord(c))].advance for c in text)


def index(cp: int) -> int:
    return cp - FIRST if FIRST <= cp <= LAST else REPLACEMENT


# Source glyphs: code point -> (advance, {(x, height above the baseline)})
Raw = dict[int, tuple[int, set[tuple[int, int]]]]


def encoding(words: list[str]) -> int:
    """The code point of an ENCODING line, or -1 for a non-standard one."""
    return int(words[1]) if len(words) > 1 and int(words[1]) >= 0 else -1


def read_bdf(text: str) -> tuple[Raw, int]:
    """Glyphs and the font's ascent (FONT_ASCENT, else the bounding box top)."""
    glyphs: Raw = {}
    ascent = None
    box_top = None
    lines = iter(text.splitlines())
    for line in lines:
        words = line.split()
        if not words:
            continue
        if words[0] == "FONT_ASCENT":
            ascent = int(words[1])
        elif words[0] == "FONTBOUNDINGBOX":
            box_top = int(words[2]) + int(words[4])
        elif words[0] == "STARTCHAR":
            cp, advance, box, rows = -1, 0, (0, 0, 0, 0), []
            for char_line in lines:
                words = char_line.split()
                if not words:
                    continue
                if words[0] == "ENCODING":
                    cp = encoding(words)
                elif words[0] == "DWIDTH":
                    advance = int(words[1])
                elif words[0] == "BBX":
                    box = tuple(int(w) for w in words[1:5])
                elif words[0] == "BITMAP":
                    for bitmap_line in lines:
                        if bitmap_line.strip() == "ENDCHAR":
                            break
                        rows.append(bitmap_line.strip())
                    break
            w, h, xoff, yoff = box
            pixels = set()
            for r, hexrow in enumerate(rows[:h]):
                bits = int(hexrow, 16) if hexrow else 0
                total = 4 * len(hexrow)
                if total < w:
                    raise ValueError(
                        f"U+{cp:04X}: a bitmap row is narrower than its BBX"
                    )
                for c in range(w):
                    if bits >> (total - 1 - c) & 1:
                        pixels.add((xoff + c, yoff + h - 1 - r))
            if cp >= 0:
                glyphs[cp] = (advance, pixels)
    if ascent is None:
        if box_top is None:
            raise ValueError("BDF has neither FONT_ASCENT nor FONTBOUNDINGBOX")
        ascent = box_top
    return glyphs, ascent


def subset_bdf(text: str) -> str:
    """The BDF with only the glyphs font.py reads: U+0020-U+00FF and U+FFFD."""
    keep = set(range(FIRST, LAST + 1)) | {0xFFFD}
    head, chars, tail = [], [], []
    block: list[str] | None = None
    for line in text.splitlines():
        word = line.split()[0] if line.split() else ""
        if word == "STARTCHAR":
            block = [line]
        elif block is not None:
            block.append(line)
            if word == "ENDCHAR":
                codes = [
                    encoding(b.split()) for b in block if b.startswith("ENCODING ")
                ]
                if codes and codes[0] in keep:
                    chars.append("\n".join(block))
                block = None
        elif chars or word == "ENDFONT":
            tail.append(line)
        else:
            head.append(line)
    head = [f"CHARS {len(chars)}" if h.startswith("CHARS ") else h for h in head]
    return "\n".join(head + chars + tail) + "\n"


def read_png(
    path: Path, cell: tuple[int, int], first: int, baseline: int, advance
) -> Raw:
    """Cells of a PNG grid; a pixel is ink when it is far from the image's
    background (its most common colour) in brightness or opacity."""
    from PIL import Image

    image = Image.open(path).convert("RGBA")
    cw, ch = cell
    cols, rows = image.width // cw, image.height // ch
    glyphs: Raw = {}
    colours = image.getcolors(image.width * image.height)
    *back, back_alpha = max(colours, key=lambda c: (c[0], c[1]))[1]
    for n in range(cols * rows):
        x0, y0 = n % cols * cw, n // cols * ch
        pixels = set()
        for y in range(ch):
            for x in range(cw):
                r, g, b, a = image.getpixel((x0 + x, y0 + y))
                if abs(a - back_alpha) > 127 or (
                    a >= 128 and abs(r + g + b - sum(back)) > 3 * 127
                ):
                    pixels.add((x, baseline - 1 - y))
        glyphs[first + n] = (cw if advance is None else advance, pixels)
    return glyphs


def build(raw: Raw, args, source: str) -> Font:
    """Place the source glyphs on the line and fill U+0020-U+00FF."""
    kept = {cp: raw[cp] for cp in range(FIRST, LAST + 1) if cp in raw}
    for cp, alias in ALIASES.items():
        if cp not in kept and alias in raw:
            kept[cp] = raw[alias]
    for cp in range(0x7F, 0xA0):
        kept.pop(cp, None)  # DEL and C1 controls draw as the replacement glyph
    if 0xFFFD in raw:
        kept[0xFFFD] = raw[0xFFFD]

    if args.proportional:
        default_space = max(2, raw.get(0x20, (4, set()))[0] - 2)
        space = default_space if args.space is None else args.space
        for cp, (_, pixels) in kept.items():
            if pixels:
                left = min(x for x, _ in pixels)
                right = max(x for x, _ in pixels)
                trimmed = {(x - left, y) for x, y in pixels}
                kept[cp] = (right - left + 1 + args.spacing, trimmed)
            else:
                kept[cp] = (space, pixels)

    height = args.line_height
    ascii_ink = [y for cp in range(FIRST, 0x7F) if cp in kept for _, y in kept[cp][1]]
    descent = max(0, -min(ascii_ink, default=0))
    baseline = height - descent if args.baseline is None else args.baseline

    glyphs: dict[int, Glyph] = {}
    squeezed, clipped = [], []
    for cp, (advance, pixels) in kept.items():
        # One that starts left of the pen moves right, and the next glyph too
        left = min((x for x, _ in pixels if x < 0), default=0)
        placed = {(x - left, baseline - 1 - y) for x, y in pixels}
        if args.squeeze:
            fitted = squeeze(placed, height)
            if fitted != placed:
                squeezed.append(cp)
                placed = fitted
        inside = {(x, y) for x, y in placed if 0 <= y < height}
        if inside != placed:
            clipped.append(cp)
        if not 0 <= advance - left <= 255:
            raise ValueError(f"U+{cp:04X}: advance {advance - left} is outside 0-255")
        glyphs[cp] = Glyph(advance - left, inside)
    if clipped and not args.clip:
        names = " ".join(f"U+{cp:04X}" for cp in sorted(clipped))
        raise ValueError(
            f"ink outside the {height} px line (baseline {baseline}): {names}; "
            "change --baseline or --line-height, or pass --squeeze or --clip"
        )
    if any(FIRST <= cp < 0x7F for cp in squeezed + clipped):
        raise ValueError(
            f"ASCII does not fit the {height} px line at baseline {baseline}"
        )

    replacement = glyphs.pop(0xFFFD, None) or box_glyph(glyphs, baseline)
    table, missing = [], []
    for cp in range(FIRST, LAST + 1):
        if cp in glyphs:
            table.append(glyphs[cp])
        else:
            table.append(replacement)
            if not 0x7F <= cp < 0xA0:
                missing.append(cp)
    table.append(replacement)
    return Font(
        args.name,
        height,
        baseline,
        table,
        source,
        missing,
        sorted(squeezed),
        sorted(clipped),
    )


def squeeze(pixels: set[tuple[int, int]], height: int) -> set[tuple[int, int]]:
    """Fit a glyph that sticks out above the line without losing ink or
    moving its baseline: close blank rows inside it, the topmost first (an
    accent moves down onto its capital)."""
    if not pixels:
        return pixels
    while min(y for _, y in pixels) < 0:
        ys = {y for _, y in pixels}
        gaps = [y for y in range(min(ys), max(ys)) if y not in ys]
        if not gaps:
            break
        pixels = {(x, y + 1 if y < gaps[0] else y) for x, y in pixels}
    return pixels


def box_glyph(glyphs: dict[int, Glyph], baseline: int) -> Glyph:
    """A hollow box from the cap height to the baseline, as wide as an 'n'."""
    ref = glyphs.get(ord("H")) or glyphs.get(ord("n"))
    top = min((y for _, y in ref.pixels), default=0) if ref and ref.pixels else 0
    width = max(3, (glyphs[ord("n")].advance - 1) if ord("n") in glyphs else 4)
    bottom = baseline - 1
    pixels = set()
    for y in range(top, bottom + 1):
        for x in range(width):
            if y in (top, bottom) or x in (0, width - 1):
                pixels.add((x, y))
    return Glyph(width + 1, pixels)


def rows_of(glyph: Glyph) -> tuple[int, list[int]]:
    """First inked row and the rows from there, bit 0 the leftmost pixel."""
    if not glyph.pixels:
        return 0, []
    ys = [y for _, y in glyph.pixels]
    top, bottom = min(ys), max(ys)
    rows = [0] * (bottom - top + 1)
    for x, y in glyph.pixels:
        rows[y - top] |= 1 << x
    return top, rows


TYPES = """\
#ifndef TORII_FONT_TYPES
#define TORII_FONT_TYPES
/* Made by tools/font.py; the format is described there. */
#include <stdint.h>

#define TORII_FONT_FIRST 0x20
#define TORII_FONT_LAST 0xFF
/* glyphs[] index for code points outside U+0020-U+00FF */
#define TORII_FONT_REPLACEMENT (TORII_FONT_LAST - TORII_FONT_FIRST + 1)
#define TORII_FONT_GLYPHS (TORII_FONT_REPLACEMENT + 1)

struct torii_glyph {
\tuint16_t offset; /* into bits[], in bytes */
\tuint8_t advance; /* pen advance in pixels */
\tuint8_t top;     /* first bitmap row, from the line's top */
\tuint8_t rows;    /* bitmap rows (0 for blank glyphs) */
\tuint8_t width;   /* inked columns from the pen, at most 8 * row_bytes */
};

struct torii_font {
\tconst struct torii_glyph *glyphs; /* TORII_FONT_GLYPHS entries */
\tconst uint8_t *bits;  /* rows of row_bytes bytes, bit 0 leftmost */
\tuint8_t line_height;  /* pixels from one line's top to the next */
\tuint8_t baseline;     /* rows above the baseline */
\tuint8_t row_bytes;    /* 1, 2 or 4 */
\tuint8_t max_width;    /* widest glyph's inked columns */
};
#endif
"""


def label(cp: int) -> str:
    if cp == REPLACEMENT + FIRST:
        return "replacement"
    ch = chr(cp)
    return (
        f"U+{cp:04X} {ch!r}" if ch.isprintable() and ch not in "*/\\" else f"U+{cp:04X}"
    )


@dataclass
class Packed:
    """The font as the kernel gets it."""

    bits: bytes
    entries: list[tuple[int, int, int, int, int]]  # offset, advance, top, rows, width
    art: list[str]  # a comment picturing each distinct bitmap


def pack(font: Font) -> Packed:
    rb = font.row_bytes
    bits = bytearray()
    offsets: dict[int, int] = {}  # id(glyph) -> offset, so shared glyphs share bits
    entries, art = [], []
    for i, glyph in enumerate(font.glyphs):
        top, rows = rows_of(glyph)
        width = max(x for x, _ in glyph.pixels) + 1 if glyph.pixels else 0
        if id(glyph) not in offsets:
            offsets[id(glyph)] = len(bits)
            for row in rows:
                bits.extend(row >> (8 * b) & 0xFF for b in range(rb))
            art.append(f"{label(i + FIRST)}, offset {offsets[id(glyph)]}")
            art.extend(
                "".join("#" if row >> x & 1 else "." for x in range(width))
                for row in rows
            )
        entries.append((offsets[id(glyph)], glyph.advance, top, len(rows), width))
    if len(bits) > 0xFFFF:
        raise ValueError(
            f"{font.name}: {len(bits)} bytes of bitmaps, offsets are 16-bit"
        )
    return Packed(bytes(bits), entries, art)


def emit(font: Font) -> tuple[str, str]:
    """The .h and .c text."""
    packed = pack(font)
    name = font.name
    guard = f"TORII_FONT_{name.upper()}_H"
    header = (
        f"/* Made by tools/font.py from {font.source}. Do not edit. */\n"
        f"#ifndef {guard}\n#define {guard}\n\n{TYPES}\n"
        f"extern const struct torii_font font_{name};\n\n#endif\n"
    )
    data = [
        "\t" + " ".join(f"0x{b:02X}," for b in packed.bits[start : start + 12])
        for start in range(0, len(packed.bits), 12)
    ]
    entries = [
        f"\t{{{o}, {a}, {t}, {r}, {w}}}, /* {label(i + FIRST)} */"
        for i, (o, a, t, r, w) in enumerate(packed.entries)
    ]
    source = (
        f"/* Made by tools/font.py from {font.source}. Do not edit.\n"
        f" * {len(packed.bits)} bytes of bitmaps in {font.row_bytes} byte rows. */\n"
        f'#include "font_{name}.h"\n\n'
        f"static const uint8_t font_{name}_bits[] __attribute__((aligned(4))) = {{\n"
        + "\n".join(data)
        + "\n};\n\n/* The bitmaps, '#' inked:\n"
        + "\n".join(f" * {line}" for line in packed.art)
        + "\n */\n\n"
        f"static const struct torii_glyph font_{name}_glyphs[TORII_FONT_GLYPHS] = {{\n"
        + "\n".join(entries)
        + "\n};\n\n"
        f"const struct torii_font font_{name} = {{\n"
        f"\tfont_{name}_glyphs,\n\tfont_{name}_bits,\n"
        f"\t{font.line_height},\n\t{font.baseline},\n\t{font.row_bytes},\n\t{font.max_width},\n"
        "};\n"
    )
    return header, source


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Convert a pixel font (see the module docstring)."
    )
    p.add_argument("src", type=Path)
    p.add_argument("-o", "--out", type=Path, help="OUT.c and OUT.h; omit to only check")
    p.add_argument("--name", required=True)
    p.add_argument("--line-height", type=int, required=True)
    p.add_argument("--baseline", type=int)
    p.add_argument("--proportional", action="store_true")
    p.add_argument("--spacing", type=int, default=1)
    p.add_argument("--space", type=int)
    p.add_argument("--squeeze", action="store_true")
    p.add_argument("--clip", action="store_true")
    p.add_argument("--subset-bdf", type=Path)
    p.add_argument("--png-cell")
    p.add_argument("--png-first", type=lambda s: int(s, 0), default=FIRST)
    p.add_argument("--png-baseline", type=int)
    p.add_argument("--png-advance", type=int)
    args = p.parse_args(argv)
    if not args.name.isidentifier():
        p.error("--name must be a C identifier")
    return args


def load(args: argparse.Namespace) -> Font:
    data = args.src.read_bytes()
    digest = hashlib.sha256(data).hexdigest()[:16]
    opts = [f"--line-height {args.line_height}"]
    if args.baseline is not None:
        opts.append(f"--baseline {args.baseline}")
    if args.proportional:
        opts.append(f"--proportional --spacing {args.spacing}")
        if args.space is not None:
            opts.append(f"--space {args.space}")
    if args.squeeze:
        opts.append("--squeeze")
    if args.clip:
        opts.append("--clip")
    if args.png_cell:
        w, h = (int(n) for n in args.png_cell.lower().split("x"))
        if args.png_baseline is None:
            raise ValueError("a PNG needs --png-baseline")
        raw = read_png(
            args.src, (w, h), args.png_first, args.png_baseline, args.png_advance
        )
        opts.append(f"--png-cell {w}x{h} --png-first 0x{args.png_first:X}")
        opts.append(f"--png-baseline {args.png_baseline}")
        if args.png_advance is not None:
            opts.append(f"--png-advance {args.png_advance}")
    else:
        raw, _ = read_bdf(data.decode("latin-1"))
    source = f"{args.src.name} (sha256 {digest}), {' '.join(opts)}"
    return build(raw, args, source)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        font = load(args)
    except ValueError as e:
        print(f"font.py: {args.src}: {e}", file=sys.stderr)
        return 1
    if font.missing:
        names = " ".join(f"U+{cp:04X}" for cp in font.missing)
        print(f"font.py: {args.name}: replacement glyph for {names}", file=sys.stderr)
    if font.squeezed:
        names = " ".join(f"U+{cp:04X}" for cp in font.squeezed)
        print(f"font.py: {args.name}: squeezed {names}", file=sys.stderr)
    if font.clipped:
        names = " ".join(f"U+{cp:04X}" for cp in font.clipped)
        print(f"font.py: {args.name}: clipped {names}", file=sys.stderr)
    if args.subset_bdf:
        text = subset_bdf(args.src.read_bytes().decode("latin-1"))
        args.subset_bdf.write_bytes(text.encode("latin-1"))
    if args.out:
        header, source = emit(font)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.with_suffix(".h").write_text(header, newline="\n")
        args.out.with_suffix(".c").write_text(source, newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
