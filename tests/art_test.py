#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""Tests for tools/art.py, run by `just test-host`: the box art converter
(crop, size, colours, dither, determinism, the asset it writes), `art-add`'s
card layout and refusals, and that the goldens' covers in tests/shots/data
are its output for tests/art. No network."""

import contextlib
import io
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import art
import asset


def indices(data: bytes) -> list[list[int]]:
    """Box art's colour indices, rows of pixels."""
    (_, w, h, _, _), _, tiles = asset.read_asset(data)
    rows = []
    for y in range(h * 8):
        row = []
        for x in range(w * 8):
            b = tiles[((y // 8) * w + x // 8) * 32 + (y % 8) * 4 + (x % 8) // 2]
            row.append(b >> 4 if x % 2 else b & 15)
        rows.append(row)
    return rows


class ArtTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def picture(self, name: str, img: Image.Image) -> Path:
        path = self.dir / name
        img.save(path)
        return path

    def gradient(self, w: int, h: int) -> Image.Image:
        img = Image.new("RGB", (w, h))
        img.putdata(
            [
                (x * 255 // w, y * 255 // h, (x + y) * 127 // (w + h))
                for y in range(h)
                for x in range(w)
            ]
        )
        return img

    def test_sizes_come_from_the_headers(self):
        self.assertEqual(art.SIZE, 80)
        self.assertEqual(art.COLOURS, 16)
        self.assertEqual(art.FILE_BYTES, 16 + 32 + 3200)

    def test_an_asset_of_80x80_4bpp_tiles(self):
        data = art.convert(self.picture("g.png", self.gradient(300, 200)))
        self.assertEqual(len(data), art.FILE_BYTES)
        magic, bpp, w, h, flags, colours, reserved, size = struct.unpack_from(
            "<4sBBBBHHI", data
        )
        self.assertEqual(
            (magic, bpp, w, h, flags, colours, reserved, size),
            (b"TAS2", 4, 10, 10, 0, 16, 0, 3200),
        )
        rows = indices(data)
        used = {i for row in rows for i in row}
        # Colour 0 never: on a BG it is transparent
        self.assertNotIn(0, used)
        self.assertLessEqual(len(used), 15)
        # A gradient needs every colour, dithered between them
        self.assertGreaterEqual(len(used), 12)

    def test_palette_is_the_gbas_15_bits_darkest_first(self):
        data = art.convert(self.picture("g.png", self.gradient(80, 80)))
        _, palette, _ = asset.read_asset(data)
        self.assertEqual(palette[0], 0)
        lum = []
        for c in palette[1:]:
            if c:
                r, g, b = c & 31, c >> 5 & 31, c >> 10 & 31
                lum.append(r * 299 + g * 587 + b * 114)
        self.assertEqual(lum, sorted(lum))

    def test_flat_pictures_stay_flat(self):
        img = Image.new("RGB", (64, 64), (200, 40, 40))
        img.paste((20, 20, 120), (0, 32, 64, 64))
        rows = indices(art.convert(self.picture("flat.png", img)))
        # One colour each side of the edge (Lanczos rings a few rows), no dither noise
        self.assertEqual(len({i for row in rows[:34] for i in row}), 1)
        self.assertEqual(len({i for row in rows[46:] for i in row}), 1)

    def test_crop_keeps_the_part_asked_for(self):
        # A tall picture: red top, blue bottom; a wide one: red left, blue right
        tall = Image.new("RGB", (80, 240), (0, 0, 255))
        tall.paste((255, 0, 0), (0, 0, 80, 80))
        wide = tall.rotate(90, expand=True)  # red on the left
        tall_p, wide_p = self.picture("tall.png", tall), self.picture("wide.png", wide)

        def colour_at(data: bytes, x: int, y: int) -> int:
            _, palette, _ = asset.read_asset(data)
            return palette[indices(data)[y][x]]

        red, blue = asset.bgr555(255, 0, 0), asset.bgr555(0, 0, 255)
        self.assertEqual(colour_at(art.convert(tall_p), 40, 40), red)  # top by default
        self.assertEqual(colour_at(art.convert(tall_p, "centre"), 40, 40), blue)
        self.assertEqual(colour_at(art.convert(tall_p, "bottom"), 40, 40), blue)
        self.assertEqual(
            colour_at(art.convert(wide_p), 40, 40), blue
        )  # wide: centred by default
        self.assertEqual(colour_at(art.convert(wide_p, "left"), 40, 40), red)

    def test_transparency_goes_white_and_jpeg_reads(self):
        img = Image.new("RGBA", (40, 40), (0, 0, 0, 0))
        data = art.convert(self.picture("clear.png", img))
        _, palette, _ = asset.read_asset(data)
        self.assertEqual({palette[i] for row in indices(data) for i in row}, {0x7FFF})
        jpeg = self.dir / "g.jpg"
        self.gradient(120, 90).save(jpeg, quality=90)
        self.assertEqual(len(art.convert(jpeg)), art.FILE_BYTES)

    def test_the_same_bytes_every_time(self):
        path = self.picture("g.png", self.gradient(150, 100))
        self.assertEqual(art.convert(path), art.convert(path))
        self.assertEqual(zlib.crc32(art.convert(path)), zlib.crc32(art.convert(path)))

    def test_goldens_covers_are_current(self):
        for name in ("sunset", "night"):
            want = (ROOT / "tests" / "shots" / "data" / f"art-{name}.bin").read_bytes()
            self.assertEqual(
                art.convert(ROOT / "tests" / "art" / f"{name}.png"),
                want,
                f"tests/shots/data/art-{name}.bin is stale: uv run tools/art.py convert tests/art/{name}.png it",
            )

    def test_add_writes_custom_art_by_file_name(self):
        sd = self.dir / "card"
        sd.mkdir()
        pic = self.picture("g.png", self.gradient(90, 90))
        with contextlib.redirect_stdout(io.StringIO()) as said:
            status = art.main(
                ["add", "/Volumes/X/GBA/Pokemon Prism (v1).gbc", str(pic), str(sd)]
            )
        self.assertEqual(status, 0, said.getvalue())
        out = sd / "torii" / "art" / "custom" / "Pokemon Prism (v1).gbc.bin"
        self.assertEqual(out.read_bytes(), art.convert(pic))
        # A bare file name does too, and --preview writes a PNG
        preview = self.dir / "p.png"
        with contextlib.redirect_stdout(io.StringIO()) as said:
            status = art.main(
                ["add", "Mono.gb", str(pic), str(sd), "--preview", str(preview)]
            )
        self.assertEqual(status, 0)
        self.assertTrue((sd / "torii" / "art" / "custom" / "Mono.gb.bin").exists())
        with Image.open(preview) as shown:
            self.assertEqual(shown.size, (240, 240))
        # A name without a game's extension is written with a note
        with contextlib.redirect_stdout(io.StringIO()) as said:
            self.assertEqual(art.main(["add", "readme", str(pic), str(sd)]), 0)
        self.assertIn("no game extension", said.getvalue())

    def test_refusals(self):
        sd = self.dir / "card"
        sd.mkdir()
        pic = self.picture("g.png", self.gradient(90, 90))
        junk = self.dir / "junk.png"
        junk.write_bytes(b"not a picture")
        cases = [
            ["add", "Game.gba", str(pic), str(self.dir / "no card")],
            ["add", "Game.gba", str(junk), str(sd)],
            ["add", "Game.gba", str(self.dir / "none.png"), str(sd)],
            ["add", "x" * 252 + ".gba", str(pic), str(sd)],
            [
                "add",
                "Game.gba",
                str(self.picture("tiny.png", Image.new("RGB", (4, 4)))),
                str(sd),
            ],
        ]
        for args in cases:
            with contextlib.redirect_stdout(io.StringIO()) as said:
                self.assertEqual(art.main(args), 1, args)
            self.assertIn("art.py:", said.getvalue())
        self.assertFalse((sd / "torii").exists())  # nothing written

    def test_names_are_nfc(self):
        # macOS may give a decomposed name; cards hold the composed one
        self.assertEqual(art.custom_name("/x/Pokemo\u0301n.gb"), "Pokem\u00f3n.gb.bin")

    def test_convert_writes_anywhere(self):
        pic = self.picture("g.png", self.gradient(90, 60))
        out = self.dir / "out.bin"
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(art.main(["convert", str(pic), str(out)]), 0)
        self.assertEqual(out.read_bytes(), art.convert(pic))


if __name__ == "__main__":
    result = unittest.main(exit=False, verbosity=0).result
    ok = result.wasSuccessful()
    print(f"{'PASS' if ok else 'FAIL'}: box art converter ({result.testsRun} tests)")
    sys.exit(0 if ok else 1)
