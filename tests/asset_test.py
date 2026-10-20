#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""Tests for tools/asset.py's PNG converter, run by `just test-host`: round
trips, colour reduction, indexed PNGs, the limits it shares with the C
headers, the LZ77 stream, and determinism; then the menu's art (`asset.py
ui`): the committed C matches assets/ui, the tiles decode back to the
pictures, and broken pictures are refused."""

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

    def test_command_line(self):
        out = self.dir / "w.tas"
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            status = asset.main(
                ["encode", str(self.png("cl.png", self.gradient(32, 16))), str(out)]
            )
        self.assertEqual(status, 0)
        self.assertTrue(out.exists())
        with contextlib.redirect_stdout(io.StringIO()):
            bad = asset.main(
                [
                    "encode",
                    str(self.png("bad.png", Image.new("RGB", (10, 10)))),
                    str(out),
                ]
            )
        self.assertEqual(bad, 1)
        junk = self.dir / "junk.png"
        junk.write_bytes(b"not a picture")
        for args in (
            ["encode", str(self.dir / "none.png"), str(out)],
            ["encode", str(junk), str(out)],
        ):
            with contextlib.redirect_stdout(io.StringIO()) as said:
                self.assertEqual(asset.main(args), 1)
            self.assertIn("asset.py:", said.getvalue())


def tile_pixels(words: bytes, w: int, h: int) -> list[list[int]]:
    """Rows of colour indices from 4bpp tiles in 1D order, w x h pixels."""
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            b = words[((y // 8) * (w // 8) + x // 8) * 32 + (y % 8) * 4 + (x % 8) // 2]
            row.append(b >> 4 if x % 2 else b & 15)
        rows.append(row)
    return rows


class ArtTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def png(self, name: str, img: Image.Image) -> Path:
        path = self.dir / name
        img.save(path)
        return path

    def test_committed_art_is_current(self):
        with contextlib.redirect_stdout(io.StringIO()) as said:
            status = asset.main(["ui", "--check"])
        self.assertEqual(status, 0, said.getvalue())

    def test_limits_come_from_art_h(self):
        self.assertEqual(asset.SPRITE_SIZE, 64)
        self.assertEqual(asset.SHADOW_INDEX, 15)
        self.assertLess(asset.SPRITE_COLOURS, asset.SHADOW_INDEX)
        self.assertEqual(asset.BOX_SIZE, 80)

    def test_every_picture_round_trips(self):
        ids, _ = asset.art_sources()
        self.assertIn("ART_ITEM_COUNT", ids)
        manifest = asset.tomllib.loads(
            (asset.ART_DIR / "art.toml").read_text(encoding="utf-8")
        )
        names, icons, banks, bank_of = asset.art_icons16(manifest["icon16_banks"])
        for name, rows, b in zip(names, icons, bank_of, strict=True):
            src = asset.rgba_pixels(asset.ART_DIR / "icons16" / f"{name}.png")
            back = tile_pixels(asset.tiles_of(rows, 4), 16, 16)
            bank = banks[b]
            for y in range(16):
                for x in range(16):
                    c = src[y][x]
                    self.assertEqual(back[y][x] == 0, c is None, name)
                    if c is not None:
                        self.assertEqual(bank[back[y][x]], asset.bgr555(*c), name)
        for path in asset.art_files("sprites"):
            rows, palette, w, h = asset.art_sprite(path)
            src = asset.rgba_pixels(path)
            ys = [y for y, r in enumerate(src) if any(c is not None for c in r)]
            xs = [x for r in src for x, c in enumerate(r) if c is not None]
            x0, y0 = (64 - w) // 2, 64 - h
            back = tile_pixels(asset.tiles_of(rows, 4), 64, 64)
            for y in range(h):
                for x in range(w):
                    c = src[min(ys) + y][min(xs) + x]
                    i = back[y0 + y][x0 + x]
                    self.assertEqual(i == 0, c is None, path.name)
                    if c is not None:
                        self.assertEqual(palette[i], asset.bgr555(*c), path.name)
            self.assertEqual(sum(map(any, back)), h, path.name)  # nothing outside
        for path in asset.art_files("shadows"):
            half, width, _ = asset.art_shadow(path)
            src = asset.rgba_pixels(path)
            for y in range(8):
                full = half[y] + half[y][::-1]
                self.assertEqual(
                    [bool(v) for v in full], [c is not None for c in src[y]], path.name
                )
            self.assertEqual(width, int(path.stem[1:]))

    def test_icons8_keep_their_shape(self):
        img = Image.new("RGBA", (8, 8))
        img.putpixel((0, 0), (10, 20, 30, 255))
        img.putpixel((7, 3), (10, 20, 30, 255))
        rows = asset.one_colour(self.dir, asset.rgba_pixels(self.png("a.png", img)))
        self.assertEqual(rows[0][0], 1)
        self.assertEqual(rows[3][7], 1)
        self.assertEqual(sum(map(sum, rows)), 2)
        img.putpixel((4, 4), (200, 20, 30, 255))
        with self.assertRaisesRegex(asset.ArtError, "2 colours"):
            asset.one_colour(
                self.dir / "b.png", asset.rgba_pixels(self.png("b.png", img))
            )

    def test_broken_pictures_are_refused(self):
        many = Image.new("RGBA", (40, 40))
        many.putdata(
            [(i * 8, 0, 0, 255) for i in range(15)] + [(0, 0, 0, 255)] * (1600 - 15)
        )
        with self.assertRaisesRegex(asset.ArtError, "more than 14 colours"):
            asset.art_sprite(self.png("many.png", many))
        with self.assertRaisesRegex(asset.ArtError, "65x10"):
            asset.art_sprite(
                self.png("wide.png", Image.new("RGBA", (65, 10), (9, 9, 9, 255)))
            )
        with self.assertRaisesRegex(asset.ArtError, "empty"):
            asset.art_sprite(self.png("none.png", Image.new("RGBA", (8, 8))))
        lopsided = Image.new("RGBA", (64, 8))
        lopsided.putpixel((10, 4), (9, 9, 9, 255))
        with self.assertRaisesRegex(asset.ArtError, "mirrored"):
            asset.art_shadow(self.png("w16.png", lopsided))
        lopsided.putpixel((53, 4), (9, 9, 9, 255))
        with self.assertRaisesRegex(asset.ArtError, "44 px drawn"):
            asset.art_shadow(self.png("w16.png", lopsided))
        with self.assertRaisesRegex(asset.ArtError, "wNN"):
            asset.art_shadow(self.png("big.png", lopsided))
        with self.assertRaisesRegex(asset.ArtError, "indexed 80x80"):
            asset.art_box(self.png("box.png", Image.new("RGB", (80, 80))))
        with self.assertRaisesRegex(asset.ArtError, "put icons16/fav.png"):
            asset.art_icons16([["file"]])
        with self.assertRaisesRegex(asset.ArtError, "which icons16/ hasn't"):
            asset.art_icons16(
                [
                    [n]
                    for n in (
                        "fav",
                        "file",
                        "folder",
                        "gb",
                        "gba",
                        "gbc",
                        "info",
                        "nes",
                        "nor",
                        "recent",
                        "sd",
                        "set",
                        "torii",
                        "home",
                    )
                ]
            )
        with self.assertRaisesRegex(asset.ArtError, "lower-case"):
            asset.art_name(Path("Icon-A.png"))


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
