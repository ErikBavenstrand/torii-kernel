#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""Tests for tools/font.py, run by `just test-host`."""

import contextlib
import io
import shutil
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import font

# A 4x6 cell font: ascent 5, descent 1. 'A' is 3 px wide, 'g' descends,
# U+00C9 has an accent with a blank row above the capital.
BDF = """\
STARTFONT 2.1
FONT -test-tiny
SIZE 6 75 75
FONTBOUNDINGBOX 4 8 0 -1
STARTPROPERTIES 2
FONT_ASCENT 5
FONT_DESCENT 1
ENDPROPERTIES
CHARS 5
STARTCHAR space
ENCODING 32
DWIDTH 4 0
BBX 0 0 0 0
BITMAP
ENDCHAR
STARTCHAR A
ENCODING 65
DWIDTH 4 0
BBX 3 5 0 0
BITMAP
40
A0
E0
A0
A0
ENDCHAR
STARTCHAR i
ENCODING 105
DWIDTH 4 0
BBX 1 4 1 0
BITMAP
80
00
80
80
ENDCHAR
STARTCHAR g
ENCODING 103
DWIDTH 4 0
BBX 3 4 0 -1
BITMAP
E0
A0
60
C0
ENDCHAR
STARTCHAR Eacute
ENCODING 201
DWIDTH 4 0
BBX 3 6 0 0
BITMAP
40
00
E0
C0
80
E0
ENDCHAR
ENDFONT
"""


def convert(*options: str, text: str = BDF) -> font.Font:
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "tiny.bdf"
        src.write_text(text)
        return font.load(font.parse_args([str(src), "--name", "t", *options]))


def glyph(f: font.Font, ch: str) -> font.Glyph:
    return f.glyphs[font.index(ord(ch))]


class Widths(unittest.TestCase):
    def test_advances_from_the_font(self):
        f = convert("--line-height", "6", "--squeeze")
        self.assertEqual([glyph(f, c).advance for c in " Agi"], [4, 4, 4, 4])
        self.assertEqual(f.width("Ag"), 8)

    def test_proportional_trims_to_ink(self):
        f = convert("--line-height", "6", "--squeeze", "--proportional", "--space", "2")
        self.assertEqual([glyph(f, c).advance for c in " Agi"], [2, 4, 4, 2])
        self.assertEqual(min(x for x, _ in glyph(f, "i").pixels), 0)

    def test_negative_bearing_moves_right(self):
        shifted = BDF.replace("BBX 1 4 1 0", "BBX 1 4 -1 0")
        f = convert("--line-height", "6", "--squeeze", text=shifted)
        self.assertEqual(glyph(f, "i").advance, 5)
        self.assertEqual(min(x for x, _ in glyph(f, "i").pixels), 0)


class Layout(unittest.TestCase):
    def test_baseline_and_rows(self):
        f = convert("--line-height", "6", "--squeeze")
        self.assertEqual(f.baseline, 5)
        packed = font.pack(f)
        offset, _, top, rows, width = packed.entries[font.index(ord("A"))]
        self.assertEqual((top, rows, width), (0, 5, 3))
        # bit 0 is the leftmost pixel: ".#." is 0b010, "#.#" is 0b101
        rows = packed.bits[offset : offset + 5]
        self.assertEqual(list(rows), [2, 5, 7, 5, 5])  # 1 byte a row, 3 px wide
        _, _, top, rows, _ = packed.entries[font.index(ord("g"))]
        self.assertEqual((top, rows), (2, 4))

    def test_wide_glyphs_use_two_byte_rows(self):
        wide = BDF.replace(
            "BBX 3 5 0 0\nBITMAP\n40\nA0\nE0\nA0\nA0",
            "BBX 9 5 0 0\nBITMAP\n4080\n0000\n0000\n0000\n0000",
        )
        f = convert("--line-height", "6", "--squeeze", text=wide)
        self.assertEqual(f.row_bytes, 2)
        packed = font.pack(f)
        offset = packed.entries[font.index(ord("A"))][0]
        self.assertEqual(offset % 2, 0)
        self.assertEqual(packed.bits[offset : offset + 2], bytes([0x02, 0x01]))

    def test_narrow_glyphs_stay_one_byte_beside_a_wide_one(self):
        wide = BDF.replace(
            "BBX 3 5 0 0\nBITMAP\n40\nA0\nE0\nA0\nA0",
            "BBX 9 5 0 0\nBITMAP\n4080\n0000\n0000\n0000\n0000",
        )
        packed = font.pack(convert("--line-height", "6", "--squeeze", text=wide))
        # every glyph's bytes are rows * (2 if wider than 8 px, else 1), and
        # a 2 byte row starts at an even offset
        for offset, _, _, rows, width in packed.entries:
            if rows and width > 8:
                self.assertEqual(offset % 2, 0)
        end = max(o + r * (2 if w > 8 else 1) for o, _, _, r, w in packed.entries if r)
        self.assertLessEqual(len(packed.bits), end)

    def test_short_bitmap_row_fails(self):
        short = BDF.replace("BBX 3 5 0 0", "BBX 9 5 0 0")
        with self.assertRaisesRegex(ValueError, "narrower"):
            convert("--line-height", "6", "--squeeze", text=short)

    def test_accent_squeezed_onto_its_capital(self):
        f = convert("--line-height", "6", "--squeeze")
        self.assertEqual(f.squeezed, [0xC9])
        ys = sorted({y for _, y in glyph(f, "É").pixels})
        self.assertEqual(ys, [0, 1, 2, 3, 4])

    def test_overflow_fails_without_squeeze(self):
        with self.assertRaisesRegex(ValueError, "U\\+00C9"):
            convert("--line-height", "6")

    def test_deep_descender_rises_whole(self):
        deep = BDF.replace(
            "BBX 3 4 0 -1\nBITMAP\nE0\nA0\n60\nC0",
            "BBX 3 5 0 -2\nBITMAP\nE0\nA0\n60\n20\nC0",
        )
        deep = deep.replace(
            "ENCODING 103", "ENCODING 231"
        )  # as U+00E7, so ASCII still fits
        f = convert(
            "--line-height", "6", "--baseline", "5", "--squeeze", "--clip", text=deep
        )
        # no blank row to close, so it rises by what stuck out, tail whole
        self.assertEqual((f.squeezed, f.clipped), ([0xC9, 0xE7], []))
        self.assertEqual(sorted({y for _, y in glyph(f, "ç").pixels}), [1, 2, 3, 4, 5])

    def test_deep_descender_closes_its_gap(self):
        gap = BDF.replace(
            "BBX 3 4 0 -1\nBITMAP\nE0\nA0\n60\nC0",
            "BBX 3 6 0 -3\nBITMAP\nE0\nA0\n60\n00\n20\nC0",
        ).replace("ENCODING 103", "ENCODING 231")
        f = convert(
            "--line-height", "6", "--baseline", "5", "--squeeze", "--clip", text=gap
        )
        # the blank row closes (the tail comes up onto the letter), and the
        # one row still out rises with the rest
        self.assertEqual((f.squeezed, f.clipped), ([0xC9, 0xE7], []))
        self.assertEqual(sorted({y for _, y in glyph(f, "ç").pixels}), [1, 2, 3, 4, 5])

    def test_nonstandard_encoding_ignored(self):
        f = convert(
            "--line-height",
            "6",
            "--squeeze",
            text=BDF.replace("ENCODING 65", "ENCODING -1 65"),
        )
        self.assertIn(ord("A"), f.missing)

    def test_advance_must_fit_a_byte(self):
        with self.assertRaisesRegex(ValueError, "outside 0-255"):
            convert(
                "--line-height",
                "6",
                "--squeeze",
                text=BDF.replace("DWIDTH 4 0", "DWIDTH 300 0", 1),
            )

    def test_clip_drops_rows(self):
        f = convert("--line-height", "6", "--clip")
        self.assertEqual(f.clipped, [0xC9])
        self.assertTrue(all(0 <= y < 6 for _, y in glyph(f, "É").pixels))

    def test_ascii_must_fit(self):
        with self.assertRaisesRegex(ValueError, "ink outside"):
            convert("--line-height", "5", "--baseline", "4", "--squeeze")
        with self.assertRaisesRegex(ValueError, "ASCII does not fit"):
            convert("--line-height", "5", "--baseline", "4", "--squeeze", "--clip")


class Missing(unittest.TestCase):
    def test_replacement_glyph(self):
        f = convert("--line-height", "6", "--squeeze")
        replacement = f.glyphs[font.REPLACEMENT]
        self.assertTrue(replacement.pixels)
        self.assertIs(glyph(f, "Z"), replacement)
        self.assertIs(glyph(f, "Ā"), replacement)
        self.assertIs(glyph(f, "\x85"), replacement)
        self.assertIn(ord("Z"), f.missing)
        self.assertNotIn(0x85, f.missing)  # C1 controls are never drawn
        self.assertNotIn(ord("A"), f.missing)

    def test_aliases(self):
        f = convert("--line-height", "6", "--squeeze")
        self.assertEqual(glyph(f, " ").advance, glyph(f, " ").advance)
        self.assertNotIn(0xA0, f.missing)

    def test_font_replacement_glyph_wins(self):
        with_fffd = BDF.replace(
            "ENDFONT",
            "STARTCHAR fffd\nENCODING 65533\nDWIDTH 2 0\nBBX 1 1 0 0\nBITMAP\n80\nENDCHAR\nENDFONT",
        )
        f = convert("--line-height", "6", "--squeeze", text=with_fffd)
        self.assertEqual(f.glyphs[font.REPLACEMENT].advance, 2)
        self.assertEqual(glyph(f, "Z").advance, 2)

    def test_shared_glyphs_share_bits(self):
        f = convert("--line-height", "6", "--squeeze")
        packed = font.pack(f)
        z, y = (
            packed.entries[font.index(ord("Z"))],
            packed.entries[font.index(ord("Y"))],
        )
        self.assertEqual(z, y)
        self.assertEqual(packed.entries[font.REPLACEMENT], z)


class Output(unittest.TestCase):
    def emit(self, directory: str) -> str:
        src = Path(directory) / "tiny.bdf"
        src.write_text(BDF)
        f = font.load(
            font.parse_args(
                [str(src), "--name", "t", "--line-height", "6", "--squeeze"]
            )
        )
        return font.emit(f)

    def test_deterministic(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            self.assertEqual(self.emit(a), self.emit(b))  # no paths or dates

    def test_c_text(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = self.emit(tmp)
        self.assertIn("const struct torii_font font_t = {", source)
        self.assertIn('#include "font.h"', source)
        self.assertIn("tiny.bdf (sha256 ", source)
        self.assertEqual(source.count("/* U+"), font.REPLACEMENT)
        self.assertNotIn("*/ *", source.split("The bitmaps")[1].split("\n */")[0])

    def test_main_writes_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "tiny.bdf"
            src.write_text(BDF)
            out = Path(tmp) / "out" / "font_t"
            subset = Path(tmp) / "subset.bdf"
            args = [
                str(src),
                "-o",
                str(out),
                "--name",
                "t",
                "--line-height",
                "6",
                "--squeeze",
            ]
            with contextlib.redirect_stderr(io.StringIO()) as err:
                self.assertEqual(font.main([*args, "--subset-bdf", str(subset)]), 0)
            self.assertIn("replacement glyph for U+0021", err.getvalue())
            self.assertTrue(out.with_suffix(".c").exists())
            self.assertIn("CHARS 5", subset.read_text())
            with contextlib.redirect_stderr(io.StringIO()) as err:
                self.assertEqual(font.main([*args[:-1]]), 1)  # É needs --squeeze
            self.assertIn("U+00C9", err.getvalue())

    def test_subset_converts_the_same(self):
        extra = BDF.replace(
            "ENDFONT",
            "STARTCHAR a\nENCODING 12354\nDWIDTH 4 0\nBBX 1 1 0 0\nBITMAP\n80\nENDCHAR\nENDFONT",
        ).replace("CHARS 5", "CHARS 6")
        subset = font.subset_bdf(extra)
        self.assertNotIn("ENCODING 12354", subset)
        self.assertIn("CHARS 5", subset)
        self.assertEqual(
            font.read_bdf(subset),
            font.read_bdf(extra[: extra.index("STARTCHAR a")] + "ENDFONT\n"),
        )

    def test_subset_keeps_the_code_points_asked_for(self):
        extra = BDF.replace(
            "ENDFONT",
            "STARTCHAR a\nENCODING 12354\nDWIDTH 4 0\nBBX 1 1 0 0\nBITMAP\n80\nENDCHAR\nENDFONT",
        ).replace("CHARS 5", "CHARS 6")
        subset = font.subset_bdf(extra, {12354})
        self.assertIn("ENCODING 12354", subset)
        self.assertIn("CHARS 1", subset)
        self.assertIn(12354, font.read_bdf(subset)[0])


class Vendored(unittest.TestCase):
    """The fonts in assets/fonts convert as `just font` converts them."""

    def test_hangul_subset(self):
        """Galmuri9's script glyphs for lang/ko.ini (tools/galmuri.py, from
        the pinned release): Hangul, jamo and kana, as Galmuri draws them."""
        raw, ascent = font.read_bdf(
            (ROOT / "assets/fonts/galmuri/Galmuri9-hangul.bdf").read_text("latin-1")
        )
        self.assertEqual(ascent, 11)
        self.assertTrue(set(range(0xAC00, 0xD7A4)) <= set(raw))
        self.assertTrue(set(range(0x3131, 0x318F)) <= set(raw))
        self.assertTrue({0x3042, 0x30AB} <= set(raw))
        self.assertFalse(any(cp < 0x3000 or 0x4E00 <= cp < 0xAC00 for cp in raw))
        # Every glyph is 10 px apart and fits the menu line as the pack places it
        self.assertEqual(
            {adv for adv, _ in (raw[c] for c in range(0xAC00, 0xD7A4))}, {10}
        )

    def test_menu_fonts(self):
        for name, height in (("Galmuri9", 12), ("Galmuri11-Bold", 16)):
            path = ROOT / "assets/fonts/galmuri" / f"{name}.bdf"
            args = [str(path), "--name", "x", "--line-height", str(height)]
            f = font.load(font.parse_args([*args, "--squeeze", "--clip"]))
            self.assertEqual(f.missing, [], name)
            # only the bottom of descenders too deep for the line
            self.assertLessEqual(set(f.clipped), {ord(c) for c in "¡µ¿Çç"}, name)
            self.assertEqual(f.line_height, height)

    def test_committed_c_is_current(self):
        """source/engine/font_*.c is what `just font` makes from the BDFs now,
        with the options its first line names."""
        for name in ("body", "heading"):
            path = ROOT / "source/engine" / f"font_{name}.c"
            committed = path.read_text()
            first = committed.splitlines()[0]
            # "/* Made by tools/font.py from X.bdf (sha256 ...), OPTIONS. Do not edit."
            bdf = first.split(" from ")[1].split(" (")[0]
            options = first.split("), ")[1].removesuffix(". Do not edit.").split()
            src = ROOT / "assets/fonts/galmuri" / bdf
            f = font.load(font.parse_args([str(src), "--name", name, *options]))
            self.assertEqual(font.emit(f), committed, f"{path.name}: run `just font`")

    @unittest.skipUnless(
        shutil.which("arm-none-eabi-gcc") or shutil.which("cc"), "no C compiler"
    )
    def test_compiles_without_warnings(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "tiny.bdf"
            src.write_text(BDF)
            args = [str(src), "-o", str(Path(tmp) / "font_t"), "--name", "t"]
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(
                    font.main([*args, "--line-height", "6", "--squeeze"]), 0
                )
            # The kernel's WARNFLAGS (Makefile) and -pedantic, on devkitARM's GCC
            # when it is on PATH (the kernel's compiler), else the host's
            compiler = shutil.which("arm-none-eabi-gcc") and "arm-none-eabi-gcc" or "cc"
            flags = "-std=gnu11 -Wall -Wextra -Wshadow -Wundef -Wformat=2 -Wstrict-prototypes"
            flags += " -Wmissing-prototypes -Werror -pedantic"
            run = subprocess.run(
                [
                    compiler,
                    *flags.split(),
                    "-I",
                    str(ROOT / "source/engine"),
                    "-c",
                    "-o",
                    str(Path(tmp) / "t.o"),
                    str(Path(tmp) / "font_t.c"),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(run.returncode, 0, run.stderr)


def tiny_fonts():
    """The two menu fonts as `just font` makes them."""
    out = {}
    for name, (path, height) in font.BUILTIN.items():
        args = [str(ROOT / path), "--name", name, "--line-height", str(height)]
        out[name] = font.load(font.parse_args([*args, "--squeeze", "--clip"]))
    return out


class Kerning(unittest.TestCase):
    """A pair that sits a pixel closer (torii_kern in source/engine/font.h)."""

    def test_pairs_keep_their_ink_apart(self):
        for name, f in tiny_fonts().items():
            pairs = f.kern
            self.assertTrue(pairs, name)
            self.assertLessEqual(len(pairs), font.KERN_MAX, name)
            for left, rights in pairs.items():
                self.assertIn(chr(left + font.FIRST), font.KERN_LEFTS)
                for right in rights:
                    # a pixel closer, no ink within one pixel of the other's: the
                    # 2 px that ordinary neighbours keep
                    self.assertGreaterEqual(
                        font.ink_distance(f.glyphs[left], f.glyphs[right], 1), 2, name
                    )

    def test_expected_pairs(self):
        for f in tiny_fonts().values():
            t = font.index(ord("T"))
            self.assertIn(font.index(ord("o")), f.kern[t])
            self.assertIn(font.index(ord(".")), f.kern[t])
            self.assertNotIn(font.index(ord("H")), f.kern[t])
            self.assertNotIn(font.index(ord(" ")), f.kern[t])  # no spaces
            # Only letters, the full stop and the comma follow, and only the listed letters kern
            for rights in f.kern.values():
                for right in rights:
                    cp = right + font.FIRST
                    self.assertTrue(chr(cp).isalpha() or chr(cp) in ".,")

    def test_variants_in_the_table(self):
        for name, f in tiny_fonts().items():
            packed = font.pack(f)
            self.assertEqual(len(packed.kerned), len(f.kern))
            for slot, left in enumerate(packed.kerned):
                base = packed.entries[left]
                variant = packed.entries[font.REPLACEMENT + 1 + slot]
                # the same bitmap, an advance one less
                self.assertEqual(variant, (base[0], base[1] - 1, *base[2:]), name)
            self.assertLessEqual(len(packed.entries), 256)

    def test_c_has_the_tables(self):
        f = tiny_fonts()["body"]
        source = font.emit(f)
        self.assertIn("kern_of", source)
        self.assertIn(f"font_body_glyphs[TORII_FONT_GLYPHS + {len(f.kern)}]", source)
        self.assertEqual(source.count("/* kerned "), len(f.kern))
        # A font with no pair has no tables
        none = tiny_fonts()["body"]
        none.kern = {}
        source = font.emit(none)
        self.assertNotIn("kern_of", source)
        self.assertIn("\tNULL,", source)


def script_glyph(advance, pixels):
    return font.Glyph(advance, set(pixels))


class ScriptFont(unittest.TestCase):
    """The blob a pack carries (source/engine/script.h)."""

    def blob(self):
        glyphs = {
            0x141: script_glyph(6, {(0, 2), (1, 2), (2, 2)}),
            0x391: script_glyph(7, {(0, 3), (4, 4)}),
            0x392: script_glyph(7, {(0, 3), (4, 4)}),  # the same shape as 0x391
            0x414: script_glyph(11, {(0, 1), (9, 2)}),  # 10 px wide: 2 byte rows
            0x415: script_glyph(5, set()),  # blank
        }
        return font.script_blob([font.script_record(glyphs)]), glyphs

    def test_header(self):
        blob, _ = self.blob()
        u16 = lambda at: int.from_bytes(blob[at : at + 2], "little")
        u32 = lambda at: int.from_bytes(blob[at : at + 4], "little")
        self.assertEqual(blob[:4], b"TSCF")
        self.assertEqual((u16(4), u16(6)), (1, 1))
        self.assertEqual(u32(8), zlib.crc32(blob[12:]))
        self.assertEqual(u32(12), len(blob))
        self.assertEqual((u32(16), u32(20)), (24, 0))
        self.assertEqual(len(blob) % 4, 0)

    def test_record(self):
        blob, _ = self.blob()
        rec = int.from_bytes(blob[16:20], "little")
        u16 = lambda at: int.from_bytes(blob[rec + at : rec + at + 2], "little")
        u32 = lambda at: int.from_bytes(blob[rec + at : rec + at + 4], "little")
        ranges, count, table, bits = u16(0), u16(2), u32(4), u32(8)
        # 0x141, 0x391-0x392, 0x414-0x415
        self.assertEqual((ranges, count), (3, 5))
        self.assertEqual(
            [(u16(12 + 6 * i), u16(14 + 6 * i), u16(16 + 6 * i)) for i in range(3)],
            [(0x141, 0x141, 0), (0x391, 0x392, 1), (0x414, 0x415, 3)],
        )
        self.assertEqual((table % 4, bits % 4), (0, 0))
        entry = lambda i: tuple(blob[rec + table + 6 * i + 2 : rec + table + 6 * i + 6])
        offset = lambda i: u16(table + 6 * i)
        self.assertEqual(entry(0), (6, 2, 1, 3))  # advance, top, rows, width
        self.assertEqual(entry(1), (7, 3, 2, 5))
        self.assertEqual(entry(3), (11, 1, 2, 10))
        self.assertEqual(entry(4)[2], 0)  # blank: no rows
        self.assertEqual(offset(1), offset(2))  # one shape, stored once
        # 1 byte rows for the narrow, 2 for the 10 px glyph; offsets count 4 byte units
        data = blob[rec + bits :]
        self.assertEqual(
            data[offset(1) * 4 : offset(1) * 4 + 2], bytes([0b00001, 0b10000])
        )
        self.assertEqual(
            data[offset(3) * 4 : offset(3) * 4 + 4],
            bytes([0b1, 0b0, 0b0, 0b10]),  # rows 0b0000000001, 0b1000000000
        )

    def test_two_fonts(self):
        glyphs = {0x391: script_glyph(7, {(0, 3)})}
        rec = font.script_record(glyphs)
        blob = font.script_blob([rec, rec])
        u32 = lambda at: int.from_bytes(blob[at : at + 4], "little")
        self.assertEqual(int.from_bytes(blob[6:8], "little"), 2)
        self.assertEqual(u32(20) - u32(16), len(rec) + (-len(rec) % 4))
        with self.assertRaises(ValueError):
            font.script_blob([])
        with self.assertRaises(ValueError):
            font.script_blob([rec] * 3)
        with self.assertRaises(ValueError):
            font.script_record({})

    def test_too_big(self):
        # 20,000 distinct 12 x 12 glyphs are over 320 KB
        glyphs = {
            0x4E00 + i: script_glyph(
                13,
                {(11, 11), (0, 0)}
                | {(bit // 12 + 1, bit % 12) for bit in range(15) if i >> bit & 1},
            )
            for i in range(0x6000)
        }
        with self.assertRaisesRegex(ValueError, "256 KB"):
            font.script_blob([font.script_record(glyphs)])
        # and more glyphs than the PSRAM holds tables for, all of one shape
        blanks = {cp: script_glyph(5, set()) for cp in range(0x100, 0xFFFF)}
        with self.assertRaisesRegex(ValueError, "over"):
            font.script_blob([font.script_record(blanks)])

    def test_glyphs_from_a_bdf(self):
        raw, _ = font.read_bdf(
            (ROOT / "assets/fonts/test/script-test-body.bdf").read_text()
        )
        height, baseline = font.builtin_baseline(ROOT, "body")
        self.assertEqual(height, 12)
        glyphs = font.script_glyphs(raw, [(0x391, 0x393)], height, baseline)
        self.assertEqual(sorted(glyphs), [0x391, 0x392, 0x393])
        g = glyphs[0x391]
        self.assertEqual(g.advance, 7)
        rows = sorted({y for _, y in g.pixels})
        self.assertEqual((rows[0], rows[-1]), (baseline - 8, baseline - 1))
        # No range: every code point past Latin-1 the font has, none below
        every = font.script_glyphs(raw, None, height, baseline)
        self.assertEqual(min(every), 0x141)
        self.assertEqual(len(every), len(raw))

    def test_ranges(self):
        self.assertEqual(
            font.parse_ranges("0370-03FF, 0400-04FF,3042"),
            [(0x370, 0x3FF), (0x400, 0x4FF), (0x3042, 0x3042)],
        )
        for bad in ("00FF", "0300-0200", "0200-10000", "xyz"):
            with self.assertRaises(ValueError):
                font.parse_ranges(bad)

    def test_builtin_metrics_match_the_committed_fonts(self):
        for name in ("body", "heading"):
            source = (ROOT / "source/engine" / f"font_{name}.c").read_text()
            tail = source.split(f"const struct torii_font font_{name} = {{")[1]
            numbers = [t.strip(",") for t in tail.split()[2:6]]
            height, baseline = font.builtin_baseline(ROOT, name)
            self.assertEqual((int(numbers[0]), int(numbers[1])), (height, baseline))

    def test_test_font_is_current(self):
        sys.path.insert(0, str(ROOT / "tools"))
        import script_test_font

        for name, height, narrow, wide in script_test_font.FONTS:
            path = ROOT / "assets/fonts/test" / name
            self.assertEqual(
                path.read_text(),
                script_test_font.make(height, narrow, wide),
                f"{name}: run `just script-test-font`",
            )


class Png(unittest.TestCase):
    def test_light_on_dark(self):
        image = Image.new("RGB", (4, 6), "black")
        image.putpixel(
            (0, 0), (255, 255, 255)
        )  # ink in the corner, where a cell's background used to be read
        image.putpixel((1, 4), (255, 255, 255))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "grid.png"
            image.save(path)
            args = [
                str(path),
                "--name",
                "p",
                "--line-height",
                "6",
                "--baseline",
                "5",
                "--png-cell",
                "4x6",
            ]
            f = font.load(
                font.parse_args(
                    [*args, "--png-first", "0x41", "--png-baseline", "5", "--clip"]
                )
            )
        self.assertEqual(sorted(glyph(f, "A").pixels), [(0, 0), (1, 4)])

    def test_grid(self):
        # Two 4x6 cells from U+0041, black ink on white; baseline 5 rows down
        image = Image.new("RGB", (8, 6), "white")
        for x, y in [(1, 0), (0, 1), (2, 1), (0, 2), (1, 2), (2, 2), (0, 4), (2, 4)]:
            image.putpixel((x, y), (0, 0, 0))
        image.putpixel((4, 4), (0, 0, 0))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "grid.png"
            image.save(path)
            args = font.parse_args(
                [
                    str(path),
                    "--name",
                    "p",
                    "--line-height",
                    "6",
                    "--baseline",
                    "5",
                    "--png-cell",
                    "4x6",
                ]
                + ["--png-first", "0x41", "--png-baseline", "5", "--proportional"]
            )
            f = font.load(args)
        self.assertEqual(glyph(f, "A").advance, 4)
        self.assertEqual(glyph(f, "B").advance, 2)
        self.assertEqual(sorted(glyph(f, "B").pixels), [(0, 4)])
        self.assertIn((1, 0), glyph(f, "A").pixels)


if __name__ == "__main__":
    # One PASS or FAIL line, as `just test` expects; details only on failure
    stream = io.StringIO()
    result = unittest.TextTestRunner(stream=stream, verbosity=1).run(
        unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    )
    if result.wasSuccessful():
        print(f"PASS: font converter ({result.testsRun} tests)")
    else:
        print(stream.getvalue(), end="")
        print("FAIL: font converter")
        sys.exit(1)
