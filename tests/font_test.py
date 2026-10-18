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
        rows = packed.bits[offset : offset + 10]
        self.assertEqual(list(rows[0::2]), [2, 5, 7, 5, 5])
        self.assertEqual(list(rows[1::2]), [0] * 5)  # 2 bytes a row
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

    def test_deep_descender_clipped_not_lifted(self):
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
        self.assertEqual(f.clipped, [0xE7])
        self.assertEqual(sorted({y for _, y in glyph(f, "ç").pixels}), [2, 3, 4, 5])

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


class Vendored(unittest.TestCase):
    """The fonts in assets/fonts convert as `just font` converts them."""

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

    @unittest.skipUnless(shutil.which("cc"), "no C compiler")
    def test_compiles_without_warnings(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "tiny.bdf"
            src.write_text(BDF)
            args = [str(src), "-o", str(Path(tmp) / "font_t"), "--name", "t"]
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(
                    font.main([*args, "--line-height", "6", "--squeeze"]), 0
                )
            # The kernel's WARNFLAGS (Makefile) and -pedantic, on the host's compiler
            flags = "-std=gnu11 -Wall -Wextra -Wshadow -Wundef -Wformat=2 -Wstrict-prototypes"
            flags += " -Wmissing-prototypes -Werror -pedantic"
            run = subprocess.run(
                [
                    "cc",
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
