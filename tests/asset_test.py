#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""Tests for tools/asset.py's PNG converter, run by `just test-host`: round
trips, colour reduction, indexed PNGs, the limits it shares with the C
headers, the LZ77 stream, determinism, and the example theme's wallpaper."""

import contextlib
import io
import random
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import asset


def decode_pixels(data: bytes) -> tuple[list[list[int]], list[int]]:
    """An asset's colour indices (rows) and palette, from its tiles."""
    (bpp, w, h, _, _), palette, tiles = asset.read_asset(data)
    per_tile = bpp * 8
    rows = []
    for y in range(h * 8):
        row = []
        for x in range(w * 8):
            tile = tiles[((y // 8) * w + x // 8) * per_tile :]
            if bpp == 8:
                row.append(tile[(y % 8) * 8 + x % 8])
            else:
                b = tile[(y % 8) * 4 + (x % 8) // 2]
                row.append(b >> 4 if x % 2 else b & 15)
        rows.append(row)
    return rows, palette


def rgb15(c: tuple[int, ...]) -> int:
    return asset.bgr555(c[0], c[1], c[2])


def lz77_distances(packed: bytes) -> list[int]:
    """Every match distance in an LZ77 stream."""
    size = int.from_bytes(packed[1:4], "little")
    out, i, dists = 0, 4, []
    while out < size:
        flags = packed[i]
        i += 1
        for bit in range(8):
            if out >= size:
                break
            if flags & (0x80 >> bit):
                v = packed[i] << 8 | packed[i + 1]
                i += 2
                dists.append((v & 0xFFF) + 1)
                out += (v >> 12) + 3
            else:
                i += 1
                out += 1
    return dists


class AssetTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def png(self, name: str, img: Image.Image) -> Path:
        path = self.dir / name
        img.save(path)
        return path

    def gradient(self, w: int = 64, h: int = 64) -> Image.Image:
        img = Image.new("RGB", (w, h))
        img.putdata(
            [
                (x * 4 % 256, y * 4 % 256, (x + y) * 2 % 256)
                for y in range(h)
                for x in range(w)
            ]
        )
        return img

    def test_limits_come_from_the_headers(self):
        self.assertEqual(asset.TILE_BYTES_MAX, 0x2000)
        self.assertEqual(asset.FILE_MAX, 0x4000)
        self.assertEqual(asset.TILES_PER_SIDE_MAX, 32)
        self.assertEqual(asset.WALLPAPER_TILES, 256)
        # A wallpaper at its tile limit fits the asset format's decoded cap
        self.assertLessEqual(asset.WALLPAPER_TILES * 32, asset.TILE_BYTES_MAX)

    def test_round_trip_keeps_every_pixel(self):
        rnd = random.Random(15)
        colours = [
            (rnd.randrange(256), rnd.randrange(256), rnd.randrange(256), 255)
            for _ in range(15)
        ]
        img = Image.new("RGBA", (40, 24))
        data = [rnd.choice(colours) for _ in range(40 * 24)]
        data[5] = (1, 2, 3, 0)  # transparent: colour 0
        img.putdata(data)
        packed = asset.encode_png(self.png("a.png", img), 4)
        rows, palette = decode_pixels(packed)
        self.assertLessEqual(len(palette), 16)
        for y in range(24):
            for x in range(40):
                want = data[y * 40 + x]
                got = rows[y][x]
                if want[3] < 128:
                    self.assertEqual(got, 0)
                else:
                    self.assertEqual(palette[got], rgb15(want))
                    self.assertNotEqual(got, 0)

    def test_colours_the_gba_cant_tell_apart_are_one(self):
        img = Image.new("RGB", (8, 8))
        img.putdata([(8 * (i % 16) + (i // 16 % 8), 0, 0) for i in range(64)])
        _, palette = decode_pixels(asset.encode_png(self.png("b.png", img), 4))
        self.assertEqual(len(palette), 1 + 16 - 1)  # 16 reds, 15 + colour 0
        img.putdata([(8 * (i % 8) + i // 8, 0, 0) for i in range(64)])
        _, palette = decode_pixels(asset.encode_png(self.png("c.png", img), 4))
        self.assertEqual(len(palette), 1 + 8)

    def test_many_colours_are_reduced_deterministically(self):
        path = self.png("g.png", self.gradient())
        for dither in (False, True):
            a = asset.encode_png(path, 4, dither)
            b = asset.encode_png(path, 4, dither)
            self.assertEqual(a, b)
            rows, palette = decode_pixels(a)
            self.assertLessEqual(len(palette), 16)
            self.assertEqual(len(palette), len(set(palette[1:])) + 1)
            self.assertTrue(all(0 < i < len(palette) for row in rows for i in row))
        # Pinned Pillow, integer maths: the same bytes on every machine
        self.assertEqual(zlib.crc32(asset.encode_png(path, 4)), 0xBAD66578)
        self.assertEqual(zlib.crc32(asset.encode_png(path, 4, True)), 0x505E0D19)
        self.assertNotEqual(asset.encode_png(path, 4), asset.encode_png(path, 4, True))

    def test_8bpp_takes_255_colours(self):
        path = self.png("g8.png", self.gradient(32, 32))
        _, palette = decode_pixels(asset.encode_png(path, 8))
        self.assertLessEqual(len(palette), 256)
        self.assertGreater(len(palette), 16)

    def test_indexed_png_is_taken_as_it_is(self):
        img = Image.new("P", (16, 8))
        img.putpalette([0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255] + [9, 9, 9] * 252)
        indices = [(x + y) % 4 for y in range(8) for x in range(16)]
        img.putdata(indices)
        rows, palette = decode_pixels(asset.encode_png(self.png("p.png", img), 4))
        self.assertEqual([i for row in rows for i in row], indices)
        self.assertEqual(
            palette, [0, rgb15((255, 0, 0)), rgb15((0, 255, 0)), rgb15((0, 0, 255))]
        )

    def test_indexed_png_past_16_entries_is_reduced(self):
        img = Image.new("P", (8, 8))
        img.putpalette([v for i in range(32) for v in (i * 8, 255 - i * 8, i * 4)])
        img.putdata([i % 32 for i in range(64)])
        _, palette = decode_pixels(asset.encode_png(self.png("p32.png", img), 4))
        self.assertLessEqual(len(palette), 16)

    def test_limits(self):
        with self.assertRaisesRegex(ValueError, "multiples of 8"):
            asset.encode_png(self.png("odd.png", Image.new("RGB", (12, 8))), 4)
        with self.assertRaisesRegex(ValueError, "up to 256"):
            asset.encode_png(self.png("wide.png", Image.new("RGB", (264, 8))), 4)
        with self.assertRaisesRegex(ValueError, "bytes of tiles"):
            asset.encode_png(self.png("big8.png", Image.new("RGB", (128, 72))), 8)
        # A wallpaper: 256 tiles fit, 257 or more don't
        ok, line = asset.encode_wallpaper(self.png("w.png", self.gradient(128, 128)))
        self.assertIn("256 of 256 tiles", line)
        self.assertEqual(asset.read_asset(ok)[0][0], 4)
        with self.assertRaisesRegex(ValueError, "a wallpaper takes 256"):
            asset.encode_wallpaper(self.png("w2.png", Image.new("RGB", (136, 128))))

    def test_lz77_round_trip_and_no_distance_1(self):
        rnd = random.Random(7)
        samples = [
            b"",
            b"\0" * 5000,
            bytes(rnd.randrange(4) for _ in range(6000)),
            bytes(rnd.randrange(256) for _ in range(3000)),
            b"ab" * 2000,
        ]
        for data in samples:
            packed = asset.lz77_compress(data)
            self.assertEqual(len(packed) % 4, 0)
            self.assertEqual(asset.lz77_decompress(packed), data)
            self.assertNotIn(1, lz77_distances(packed))

    def test_header_fields(self):
        packed = asset.encode_png(self.png("h.png", self.gradient(16, 24)), 4)
        magic, bpp, w, h, flags, colours, reserved, lz = struct.unpack_from(
            "<4sBBBBHHI", packed
        )
        self.assertEqual((magic, bpp, w, h, flags, reserved), (b"TAS1", 4, 2, 3, 0, 0))
        self.assertEqual(len(packed), 16 + (colours * 2 + 3) // 4 * 4 + lz)

    def test_example_wallpaper_is_the_converters_output(self):
        theme = ROOT / "themes" / "example"
        made, _ = asset.encode_wallpaper(theme / "wallpaper.png")
        self.assertEqual(made, (theme / "wallpaper.tas").read_bytes())

    def test_command_line(self):
        out = self.dir / "w.tas"
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            status = asset.main(
                ["wallpaper", str(self.png("cl.png", self.gradient(32, 16))), str(out)]
            )
        self.assertEqual(status, 0)
        self.assertIn("8 of 256 tiles", buf.getvalue())
        self.assertTrue(out.exists())
        with contextlib.redirect_stdout(io.StringIO()):
            bad = asset.main(
                [
                    "wallpaper",
                    str(self.png("bad.png", Image.new("RGB", (10, 10)))),
                    str(out),
                ]
            )
        self.assertEqual(bad, 1)


if __name__ == "__main__":
    stream = io.StringIO()
    result = unittest.TextTestRunner(stream=stream).run(
        unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    )
    if result.wasSuccessful():
        print(f"PASS: asset converter ({result.testsRun} tests)")
    else:
        print(stream.getvalue(), end="")
        print("FAIL: asset converter")
        sys.exit(1)
