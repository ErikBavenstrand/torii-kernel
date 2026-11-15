#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""Convert a pixel font to the kernel's 1bpp proportional font format.

usage: font.py SRC -o OUT --name NAME --line-height H [options]

SRC is a BDF font, or a PNG grid of glyph cells (--png-cell). OUT.c gets a
`const struct torii_font font_NAME` covering U+0020-U+00FF, plus a
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
                      between an accent and its capital), and those below it
                      by closing the lowest or, with none, rising; ASCII
                      never changes
  --clip              drop ink rows still outside the line instead of failing
                      (the bottom of a cedilla too deep for the line)
  --subset-bdf FILE   also write SRC (a BDF) cut to the glyphs read, to vendor
  --extra [RANGES]    instead, the glyphs beyond Latin-1 in RANGES (hex, as
                      "0100-024F, 0370-03FF"; default EXTRA_RANGES), placed on the line of the
                      built-in font NAME (body: assets/fonts/galmuri/Galmuri9.bdf
                      on 12 px) and written to OUT.c as a script blob
                      (source/engine/script.h) in ROM, `font_NAME_extra`:
                      the kernel's own glyphs beyond Latin-1, drawn
                      whenever no language pack's script font is in use
  --extra-heading F   with --extra: the heading font's glyphs from BDF F, a
                      regular font made bold (each glyph smeared a pixel
                      right); without it, headings draw the body's glyphs
  --png-cell WxH      PNG only: cell size; cells run left to right, top down
  --png-first CP      PNG only: code point of the first cell (default 0x20)
  --png-baseline B    PNG only: rows above the baseline in a cell
  --png-advance N     PNG only: advance of a glyph (default: the cell width,
                      or the ink width with --proportional)

Format (struct torii_font in source/engine/font.h, which OUT.c includes and
which declares the menu's fonts):
  glyphs[cp - 0x20] for U+0020-U+00FF, glyphs[TORII_FONT_REPLACEMENT] for any
  other code point. Code points the font lacks, and C1 controls, share the
  replacement glyph's entry.
  Each glyph is `rows` bitmap rows starting `top` rows below the line's top;
  rows outside that are blank. A row is 1 byte for a glyph 8 px wide or less
  (`width`), else 2 bytes starting at an even offset, little-endian, and bit 0
  is the leftmost pixel, at the pen position, so glyphs are at most 16 px wide.
  The font's `row_bytes` is the widest glyph's (1 or 2).
  Kerning: after the glyphs, one entry for each left glyph that kerns (A F L
  P T V W Y f r v w y, as far as the pairs below leave any): the same bitmap
  with an advance one less, at glyphs[TORII_FONT_GLYPHS + slot]. kern_of[g] is
  the slot's glyph index (or 0), kern_left[slot] the glyph it copies and
  kern_right[slot] a bitmap over the glyphs it kerns before (letters, ".",
  and ","): the pairs whose ink is at least 3 px apart at any two pixels, in
  the largest of their x and y differences, so a pixel closer leaves the
  2 px ordinary neighbours have. A layout that sees
  such a pair draws the left glyph's variant (torii_kern in font.h).
Why that layout: a GBA 4bpp tile row is one little-endian word whose low
nibble is the leftmost pixel. With bit 0 leftmost, a row expands to that word
by table lookup (bit i to nibble i) with no bit reversal, and drawing at pixel
x of a tile is `word << 4x`, the bits shifted out going to the next tile's
word as `word >> (32 - 4x)` (none when x is 0: a shift by 32 is undefined in
C). The second byte expands to the next word the same way. Storing only the
inked rows skips the blank ones (about a third of a 12 px line), and keeping
them contiguous lets the blitter walk one pointer.
Why rows of 1 or 2 bytes by glyph: most glyphs are 8 px or narrower, and a
1 byte row takes half the ROM; the blitter (source/engine/text_blit.s) reads
a row's first byte the same way at either size, and in the same cycles, so
only the step to the next row differs. A wider glyph (the body font's few 9 px
ones: the copyright and fraction signs, the replacement glyph) keeps its
shape in 2 byte rows.
"""

import argparse
import hashlib
import struct
import sys
import zlib
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
    kern: dict[int, list[int]] = field(
        default_factory=dict
    )  # left index -> right indices

    @property
    def max_width(self) -> int:
        return max(
            (max(x for x, _ in g.pixels) + 1 for g in self.glyphs if g.pixels),
            default=0,
        )

    @property
    def row_bytes(self) -> int:
        if self.max_width > 16:
            raise ValueError(
                f"{self.name}: glyphs {self.max_width} px wide, at most 16 fit"
            )
        return 2

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


def subset_bdf(text: str, keep: set[int] | None = None) -> str:
    """The BDF with only the glyphs font.py reads (U+0020-U+00FF and U+FFFD),
    or those code points in keep (tools/galmuri.py's script glyphs)."""
    if keep is None:
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
        kerning(table),
    )


# The glyphs that kern before a following glyph, and the ones they kern with:
# letters and the full stop and comma (a T before an o, an r before a full stop)
KERN_LEFTS = "AFLPTVWYfrvwy"
KERN_MAX = 256 - (REPLACEMENT + 1)  # glyph slots the table has free


def kern_rights(cp: int) -> bool:
    return (
        chr(cp).isalpha()
        and (cp < 0x80 or (0xC0 <= cp <= 0xFF and cp not in (0xD7, 0xF7)))
    ) or chr(cp) in ".,"


def ink_distance(left: Glyph, right: Glyph, kern: int) -> int:
    """The smallest distance between a pixel of left and one of right, right's
    pen kern pixels before left's advance, as the largest of the two
    differences (so a pixel diagonally next to another counts 1)."""
    return min(
        (
            max(abs(x - (x2 + left.advance - kern)), abs(y - y2))
            for x, y in left.pixels
            for x2, y2 in right.pixels
        ),
        default=99,
    )


def kerning(glyphs: list[Glyph]) -> dict[int, list[int]]:
    """Pairs that sit a pixel closer: the ones whose ink stays two pixels
    apart at least, the spacing of ordinary neighbours, after it (so at least
    three before). Left glyph index -> right glyph indices, lefts with none left out."""
    out: dict[int, list[int]] = {}
    for lc in KERN_LEFTS:
        left = glyphs[index(ord(lc))]
        rights = [
            index(rc)
            for rc in range(FIRST, LAST + 1)
            if kern_rights(rc)
            and glyphs[index(rc)].pixels
            and ink_distance(left, glyphs[index(rc)], 0) >= 3
        ]
        if rights:
            out[index(ord(lc))] = rights
    if len(out) > KERN_MAX:
        raise ValueError(f"{len(out)} glyphs kern, the table has room for {KERN_MAX}")
    return out


# The menu fonts as `just font` makes them (the justfile's options): a script
# font is placed on the same lines
BUILTIN = {
    "body": ("assets/fonts/galmuri/Galmuri9.bdf", 12),
    "heading": ("assets/fonts/galmuri/Galmuri11-Bold.bdf", 16),
}


def builtin_baseline(root: Path, name: str) -> tuple[int, int]:
    """(line height, baseline) of a menu font."""
    path, height = BUILTIN[name]
    args = argparse.Namespace(
        src=root / path,
        line_height=height,
        baseline=None,
        proportional=False,
        spacing=1,
        space=None,
        squeeze=True,
        clip=True,
        png_cell=None,
        png_first=FIRST,
        png_baseline=None,
        png_advance=None,
        name=name,
    )
    font = load(args)
    return font.line_height, font.baseline


# --- Script fonts: the glyphs a language pack brings (source/engine/script.h) ---

SCRIPT_MAX = 0x50000  # bytes of blob the kernel has room for in PSRAM
SCRIPT_FIRST = 0x100  # code points below are the menu font's own
# The kernel's own glyphs beyond Latin-1 (--extra, assets/fonts/galmuri/
# Galmuri9-extra.bdf, tools/galmuri.py): Latin Extended-A and -B, the spacing
# accents, Greek, Cyrillic, Latin Extended Additional (Vietnamese), general
# punctuation, currency, letterlike symbols, arrows, stars and card suits
EXTRA_RANGES = (
    "0100-024F, 02C7, 02D0, 02D8-02DD, 0370-03FF, 0400-045F, 0490-0491, "
    "1E00-1EFF, 2010-205F, 20A0-20CF, 2100-214F, 2190-2193, 2605-2606, 2660-266F"
)


def parse_ranges(text: str) -> list[tuple[int, int]]:
    """'0370-03FF, 0400-04FF, 3042' (hex code points) -> [(first, last)]."""
    out = []
    for part in text.replace(" ", "").split(","):
        if not part:
            continue
        a, _, b = part.partition("-")
        first, last = int(a, 16), int(b or a, 16)
        if not SCRIPT_FIRST <= first <= last <= 0xFFFF:
            raise ValueError(
                f"range {part}: code points run from {SCRIPT_FIRST:04X} to FFFF"
            )
        out.append((first, last))
    return out


def script_glyphs(
    raw: Raw,
    ranges: list[tuple[int, int]] | None,
    line_height: int,
    baseline: int,
    proportional: bool = False,
    spacing: int = 1,
    space: int = 3,
) -> dict[int, Glyph]:
    """The glyphs of a font for its code points in ranges (all above U+00FF if
    None), placed on a line like the menu font's: line_height rows, baseline rows
    above the baseline, blank rows inside an accent closed and ink still outside
    the line dropped (the menu fonts' --squeeze and --clip)."""
    wanted = sorted(
        cp
        for cp in raw
        if SCRIPT_FIRST <= cp <= 0xFFFF
        and (ranges is None or any(a <= cp <= b for a, b in ranges))
    )
    out: dict[int, Glyph] = {}
    for cp in wanted:
        advance, pixels = raw[cp]
        if proportional:
            if pixels:
                left = min(x for x, _ in pixels)
                right = max(x for x, _ in pixels)
                pixels = {(x - left, y) for x, y in pixels}
                advance = right - left + 1 + spacing
            else:
                advance = space
        left = min((x for x, _ in pixels if x < 0), default=0)
        placed = {(x - left, baseline - 1 - y) for x, y in pixels}
        placed = squeeze(placed, line_height)
        inside = {(x, y) for x, y in placed if 0 <= y < line_height}
        advance -= left
        if not 0 <= advance <= 255:
            raise ValueError(f"U+{cp:04X}: advance {advance} is outside 0-255")
        if inside and max(x for x, _ in inside) >= 16:
            raise ValueError(f"U+{cp:04X}: wider than 16 px")
        out[cp] = Glyph(advance, inside)
    return out


def script_record(glyphs: dict[int, Glyph]) -> bytes:
    """One font's record of the script blob (script.h): ranges of consecutive
    code points, the glyph table and the bitmaps, offsets from the record's start."""
    if not glyphs:
        raise ValueError("a script font with no glyphs")
    cps = sorted(glyphs)
    ranges: list[tuple[int, int, int]] = []
    for i, cp in enumerate(cps):
        if ranges and cp == ranges[-1][1] + 1:
            ranges[-1] = (ranges[-1][0], cp, ranges[-1][2])
        else:
            ranges.append((cp, cp, i))
    bits = bytearray()
    shapes: dict[tuple, int] = {}  # a shape already stored -> its offset
    table = bytearray()
    for cp in cps:
        g = glyphs[cp]
        top, rows = rows_of(g)
        width = max((x for x, _ in g.pixels), default=-1) + 1
        key = (width, tuple(rows))
        if rows and key not in shapes:
            rb = 2 if width > 8 else 1
            while len(bits) % 4:
                bits.append(0)  # offsets count 4 byte units
            shapes[key] = len(bits)
            for row in rows:
                bits.extend(row >> (8 * b) & 0xFF for b in range(rb))
        offset = shapes[key] // 4 if rows else 0
        if offset > 0xFFFF:
            raise ValueError("more than 256 KB of bitmaps in one script font")
        table += struct.pack("<HBBBB", offset, g.advance, top, len(rows), width)
    while len(bits) % 4:
        bits.append(0)
    head = 12 + 6 * len(ranges)
    table_at = head + (-head % 4)
    bits_at = table_at + len(table) + (-len(table) % 4)
    rec = bytearray(struct.pack("<HHII", len(ranges), len(cps), table_at, bits_at))
    for first, last, base in ranges:
        rec += struct.pack("<HHH", first, last, base)
    rec += bytes(table_at - len(rec))
    rec += table
    rec += bytes(bits_at - len(rec))
    rec += bits
    return bytes(rec)


def script_blob(records: list[bytes]) -> bytes:
    """The blob: the body font's record, then the heading font's if given."""
    if not 1 <= len(records) <= 2:
        raise ValueError("a script blob has one or two fonts")
    at = 24
    offsets = []
    body = bytearray()
    for rec in records:
        offsets.append(at + len(body))
        body += rec + bytes(-len(rec) % 4)
    total = 24 + len(body)
    if total > SCRIPT_MAX:
        raise ValueError(f"the script fonts take {total} bytes, over {SCRIPT_MAX}")
    offsets += [0] * (2 - len(offsets))
    blob = bytearray(
        struct.pack("<4sHHIII", b"TSCF", 1, len(records), 0, total, offsets[0])
        + struct.pack("<I", offsets[1])
    )
    blob += body
    struct.pack_into("<I", blob, 8, zlib.crc32(bytes(blob[12:])) & 0xFFFFFFFF)
    return bytes(blob)


def squeeze(pixels: set[tuple[int, int]], height: int) -> set[tuple[int, int]]:
    """Fit a glyph that sticks out above the line without losing ink or
    moving its baseline: close blank rows inside it, the topmost first (an
    accent moves down onto its capital). One that sticks out below the line
    closes the lowest blank row inside it, and so brings its tail up (a
    cedilla onto its letter); with none, it rises by what it sticks out, keeping
    its tail whole."""
    if not pixels:
        return pixels
    while min(y for _, y in pixels) < 0:
        ys = {y for _, y in pixels}
        gaps = [y for y in range(min(ys), max(ys)) if y not in ys]
        if not gaps:
            break
        pixels = {(x, y + 1 if y < gaps[0] else y) for x, y in pixels}
    while max(y for _, y in pixels) >= height:
        ys = {y for _, y in pixels}
        gaps = [y for y in range(min(ys), max(ys)) if y not in ys]
        if not gaps:
            break
        pixels = {(x, y - 1 if y > gaps[-1] else y) for x, y in pixels}
    over = max(y for _, y in pixels) - height + 1
    if over > 0 and min(y for _, y in pixels) >= over:
        pixels = {(x, y - over) for x, y in pixels}  # no gap to close: rise
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
    kerned: list[int]  # the glyph each variant after the entries copies


def pack(font: Font) -> Packed:
    _ = font.row_bytes  # checks the widest glyph fits
    bits = bytearray()
    offsets: dict[int, int] = {}  # id(glyph) -> offset, so shared glyphs share bits
    entries, art = [], []
    for i, glyph in enumerate(font.glyphs):
        top, rows = rows_of(glyph)
        width = max(x for x, _ in glyph.pixels) + 1 if glyph.pixels else 0
        if id(glyph) not in offsets:
            rb = 2 if width > 8 else 1
            if rb == 2 and len(bits) % 2:
                bits.append(0)  # 2 byte rows are read as halfwords
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
    # A kerned variant: its glyph with an advance one less, the same bitmap
    kerned = sorted(font.kern)
    for left in kerned:
        o, a, t, r, w = entries[left]
        entries.append((o, a - 1, t, r, w))
    return Packed(bytes(bits), entries, art, kerned)


def emit(font: Font) -> str:
    """The .c text."""
    packed = pack(font)
    name = font.name
    data = [
        "\t" + " ".join(f"0x{b:02X}," for b in packed.bits[start : start + 12])
        for start in range(0, len(packed.bits), 12)
    ]
    entries = [
        f"\t{{{o}, {a}, {t}, {r}, {w}}}, /* {label(i + FIRST)} */"
        for i, (o, a, t, r, w) in enumerate(packed.entries[: REPLACEMENT + 1])
    ] + [
        f"\t{{{o}, {a}, {t}, {r}, {w}}}, /* kerned {label(left + FIRST)} */"
        for (o, a, t, r, w), left in zip(
            packed.entries[REPLACEMENT + 1 :], packed.kerned, strict=True
        )
    ]
    kern_of = [0] * (REPLACEMENT + 1)
    for slot, left in enumerate(packed.kerned):
        kern_of[left] = REPLACEMENT + 1 + slot
    right_bytes = (REPLACEMENT + 7) // 8
    kern_right = []
    for left in packed.kerned:
        row = bytearray(right_bytes)
        for right in font.kern[left]:
            row[right // 8] |= 1 << (right % 8)
        kern_right.append(
            "\t{"
            + ", ".join(f"0x{b:02X}" for b in row)
            + f"}}, /* kerns after {label(left + FIRST)} */"
        )
    kern_text = ""
    if packed.kerned:
        kern_text = (
            f"\n/* Kerning (torii_kern): {sum(len(r) for r in font.kern.values())} pairs "
            f"of {len(packed.kerned)} left glyphs. */\n"
            f"static const uint8_t font_{name}_kern_of[TORII_FONT_GLYPHS] = {{\n"
            + "\n".join(
                "\t" + ", ".join(str(v) for v in kern_of[start : start + 16]) + ","
                for start in range(0, len(kern_of), 16)
            )
            + "\n};\n"
            f"static const uint8_t font_{name}_kern_left[] = {{"
            + ", ".join(str(left) for left in packed.kerned)
            + "};\n"
            f"static const uint8_t font_{name}_kern_right[][TORII_FONT_RIGHT_BYTES] = {{\n"
            + "\n".join(kern_right)
            + "\n};\n"
        )
    source = (
        f"/* Made by tools/font.py from {font.source}. Do not edit.\n"
        f" * {len(packed.bits)} bytes of bitmaps in 1 byte rows (2 for glyphs over 8 px). */\n"
        '#include <stddef.h>\n\n#include "font.h"\n\n'
        f"static const uint8_t font_{name}_bits[] __attribute__((aligned(4))) = {{\n"
        + "\n".join(data)
        + "\n};\n\n/* The bitmaps, '#' inked:\n"
        + "\n".join(f" * {line}" for line in packed.art)
        + "\n */\n\n"
        f"static const struct torii_glyph font_{name}_glyphs[TORII_FONT_GLYPHS + {len(packed.kerned)}] = {{\n"
        + "\n".join(entries)
        + "\n};\n"
        + kern_text
        + "\n"
        f"const struct torii_font font_{name} = {{\n"
        f"\tfont_{name}_glyphs,\n\tfont_{name}_bits,\n"
        f"\t{font.line_height},\n\t{font.baseline},\n\t{font.row_bytes},\n\t{font.max_width},\n"
        f"\t{len(packed.kerned)},\n"
        + (
            f"\tfont_{name}_kern_of,\n\tfont_{name}_kern_left,\n\tfont_{name}_kern_right,\n"
            if packed.kerned
            else "\tNULL,\n\tNULL,\n\tNULL,\n"
        )
        + "};\n"
    )
    return source


def emit_extra(name: str, blob: bytes, source: str, glyphs: int) -> str:
    """The .c text of a script blob in ROM (--extra)."""
    words = struct.unpack(f"<{len(blob) // 4}I", blob)
    data = [
        "\t" + " ".join(f"0x{w:08X}," for w in words[start : start + 6])
        for start in range(0, len(words), 6)
    ]
    return (
        f"/* Made by tools/font.py from {source}. Do not edit.\n"
        f" * {glyphs} glyphs beyond Latin-1, a script blob (script.h) of {len(blob)} bytes. */\n"
        '#include "script.h"\n\n'
        f"const u32 font_{name}_extra[{len(words)}] = {{\n" + "\n".join(data) + "\n};\n"
        f"const u32 font_{name}_extra_bytes = {len(blob)};\n"
    )


def embolden(raw: Raw) -> Raw:
    """A regular font made bold: each glyph's ink smeared a pixel right, its
    advance a pixel wider (the heading's extra glyphs, which Galmuri11 Bold lacks)."""
    return {
        cp: (advance + 1, set(pixels) | {(x + 1, y) for x, y in pixels})
        for cp, (advance, pixels) in raw.items()
    }


def extra(args: argparse.Namespace) -> int:
    """--extra: the built-in glyphs beyond Latin-1 as a blob in ROM."""
    data = args.src.read_bytes()
    raw, _ = read_bdf(data.decode("latin-1"))
    ranges = parse_ranges(args.extra)
    root = Path(__file__).resolve().parent.parent
    height, baseline = builtin_baseline(root, args.name)
    glyphs = script_glyphs(raw, ranges, height, baseline)
    records = [script_record(glyphs)]
    count = len(glyphs)
    digest = hashlib.sha256(data).hexdigest()[:16]
    source = f"{args.src.name} (sha256 {digest}), --extra {args.extra}"
    if args.extra_heading:
        hdata = args.extra_heading.read_bytes()
        hraw, _ = read_bdf(hdata.decode("latin-1"))
        height, baseline = builtin_baseline(root, "heading")
        hglyphs = script_glyphs(embolden(hraw), ranges, height, baseline)
        records.append(script_record(hglyphs))
        count += len(hglyphs)
        hdigest = hashlib.sha256(hdata).hexdigest()[:16]
        source += f", --extra-heading {args.extra_heading.name} (sha256 {hdigest})"
    blob = script_blob(records)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        text = emit_extra(args.name, blob, source, count)
        args.out.with_suffix(".c").write_text(text, newline="\n")
    return 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Convert a pixel font (see the module docstring)."
    )
    p.add_argument("src", type=Path)
    p.add_argument("-o", "--out", type=Path, help="write OUT.c; omit to only check")
    p.add_argument("--name", required=True)
    p.add_argument("--line-height", type=int)
    p.add_argument("--baseline", type=int)
    p.add_argument("--proportional", action="store_true")
    p.add_argument("--spacing", type=int, default=1)
    p.add_argument("--space", type=int)
    p.add_argument("--squeeze", action="store_true")
    p.add_argument("--clip", action="store_true")
    p.add_argument("--subset-bdf", type=Path)
    p.add_argument("--extra", nargs="?", const=EXTRA_RANGES)
    p.add_argument("--extra-heading", type=Path)
    p.add_argument("--png-cell")
    p.add_argument("--png-first", type=lambda s: int(s, 0), default=FIRST)
    p.add_argument("--png-baseline", type=int)
    p.add_argument("--png-advance", type=int)
    args = p.parse_args(argv)
    if not args.name.isidentifier():
        p.error("--name must be a C identifier")
    if args.extra is not None and args.name not in BUILTIN:
        p.error(
            f"--extra places glyphs on a built-in font's line: {', '.join(BUILTIN)}"
        )
    if args.extra is None and args.line_height is None:
        p.error("--line-height is required")
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
    if args.extra is not None:
        try:
            return extra(args)
        except ValueError as e:
            print(f"font.py: {args.src}: {e}", file=sys.stderr)
            return 1
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
        source = emit(font)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.with_suffix(".c").write_text(source, newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
