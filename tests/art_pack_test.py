#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""Tests for tools/artpack.py (the pack's format) and tools/art_pack.py
(`just art-pack`), run by `just test-host`: the writer and the lookup, and
the tool against the fixture dats of tests/names and a local fixture server
standing in for thumbnails.libretro.com. No network."""

import contextlib
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.append(str(ROOT / "tests"))
sys.path.append(str(ROOT / "tests" / "names"))
import art
import art_pack
import artpack
import fixture
from art_scrape_test import GB_SYS, GBA_SYS, GBC_SYS, NES_SYS, Server, png, thumb

DATS = ROOT / "tests" / "names"
WARS, BETA, MINISH, UNSEEN = 0x5D5C613E, 0xA3605C52, 0x079EFED0, 0x57FC4313
COLOUR, COLGB, MONO, FAMICOM = 0x79F840AB, 0xC85A63AE, 0x8AA94607, 0x0C6CB870


def cover(colour, colours=15):
    """A cover as tools/art.py writes one: a header, the palette, the tiles."""
    head = struct.pack("<IBBBBHHI", artpack.ASSET_MAGIC, 4, 10, 10, 0, colours, 0, 3200)
    return head + bytes([colour]) * (((colours * 2 + 3) & ~3) + 3200)


class Format(unittest.TestCase):
    def test_round_trip_and_sharing(self):
        a, b = cover(1), cover(2, colours=16)
        data = artpack.build({0xFFFFFFFF: a, 5: b, 0: a, 7: b})
        p = artpack.Pack(data)
        self.assertEqual(p.images, 2)  # equal covers are stored once
        self.assertEqual(p.crcs, [0, 5, 7, 0xFFFFFFFF])
        self.assertEqual(len(data), 24 + 4 * 8 + 2 * artpack.RECORD_BYTES)
        self.assertEqual(p.find(0), 0)
        self.assertEqual(p.find(0xFFFFFFFF), 0)
        self.assertEqual(p.find(7), 1)
        self.assertIsNone(p.find(6))
        # Each image is its cover, zero-padded to a record
        self.assertEqual(p.image(0), a + bytes(artpack.RECORD_BYTES - len(a)))
        self.assertEqual(p.image(1), b)
        self.assertEqual(data[:4], b"TAPK")

    def test_refuses(self):
        with self.assertRaises(artpack.PackError):
            artpack.build({})
        with self.assertRaises(artpack.PackError):
            artpack.build({1: b"TAS2" + bytes(100)})  # not a cover
        with self.assertRaises(artpack.PackError):
            artpack.build({1: cover(1)[:-1]})  # cut short
        with self.assertRaises(artpack.PackError):
            artpack.build({i: cover(1) for i in range(artpack.MAX + 1)})
        good = artpack.build({1: cover(1)})
        # An index entry naming an image past the last
        past = bytearray(good)
        past[24 + 4 : 24 + 8] = (1).to_bytes(4, "little")
        with self.assertRaises(artpack.PackError):
            artpack.Pack(bytes(past))
        for bad in (
            good[:-1],
            good + b"\0",
            b"TAPX" + good[4:],
            good[:4] + b"\2\0" + good[6:],
        ):
            with self.assertRaises(artpack.PackError):
                artpack.Pack(bad)

    def test_write_is_whole_or_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "art.pak"
            out.write_bytes(b"old")
            artpack.write(out, b"new")
            self.assertEqual(out.read_bytes(), b"new")
            self.assertEqual(sorted(p.name for p in Path(tmp).iterdir()), ["art.pak"])

            with self.assertRaises(TypeError):
                artpack.write(out, object())  # fails mid-write: the old pack stays
            self.assertEqual(out.read_bytes(), b"new")
            self.assertEqual(sorted(p.name for p in Path(tmp).iterdir()), ["art.pak"])

    def test_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            c = Path(tmp) / "c.bin"
            c.write_bytes(cover(3))
            out = Path(tmp) / "art.pak"
            self.assertEqual(
                artpack.main(["write", str(out), f"0000BEEF={c}", f"1={c}"]), 0
            )
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                artpack.main(["find", str(out), "BEEF"])
                artpack.main(["find", str(out), "2"])
            self.assertEqual(buf.getvalue().split(), ["0", "none"])


class Tool(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.sd = base / "sd"
        self.sd.mkdir()
        self.cache = base / "cache"
        wars = png((200, 30, 30))
        self.routes = {
            thumb(GBA_SYS, "Advance Wars (USA)"): wars,
            # The beta's own name 404s; the name cut at "(" is the same picture
            thumb(GBA_SYS, "Advance Wars"): wars,
            thumb(GBA_SYS, "Legend of Zelda, The - The Minish Cap"): png((30, 160, 40)),
            thumb(GBA_SYS, "Unseen (Europe)"): 403,
            thumb(GBC_SYS, "Colour Game (USA)"): [500, png((240, 200, 0))],
            thumb(GBC_SYS, "Colour Named Gb (World)"): png((0, 120, 200)),
            thumb(GB_SYS, "Mono Game (Japan)"): png((90, 90, 90)),
            thumb(NES_SYS, "Famicom Game (Japan)"): png((90, 0, 90)),
        }
        self.server = Server(self.routes)

    def tearDown(self):
        self.server.close()
        self.tmp.cleanup()

    def run_tool(self, *extra):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            status = art_pack.main(
                [
                    str(self.sd),
                    "--dats",
                    str(DATS),
                    "--cache",
                    str(self.cache),
                    "--base-url",
                    self.server.url,
                    "--delay",
                    "0",
                    "--retry-wait",
                    "0",
                    *extra,
                ]
            )
        return status, out.getvalue(), err.getvalue()

    def pack(self):
        return artpack.Pack((self.sd / "torii" / "art.pak").read_bytes())

    def test_packs_every_crc_once_per_picture(self):
        status, out, err = self.run_tool()
        self.assertEqual(status, 0, err)
        p = self.pack()
        self.assertEqual(p.crcs, sorted([WARS, BETA, MINISH, COLOUR, COLGB, MONO]))
        self.assertEqual(p.images, 5)  # the beta shares Advance Wars' cover
        self.assertEqual(p.find(BETA), p.find(WARS))
        self.assertIsNone(p.find(UNSEEN))
        self.assertIsNone(p.find(FAMICOM))  # no NES games on the card: no NES
        # The covers are art.py's conversions of the pictures
        pic = self.cache / GBA_SYS
        wars_png = next(pic.glob("Advance Wars (USA).*.png"))
        conv = art.convert(wars_png)
        self.assertEqual(p.image(p.find(WARS))[: len(conv)], conv)
        self.assertIn("5 covers for 6 CRCs", out)
        self.assertIn("GBA: 3 of 5 CRCs with art", out)
        # Every request carries the tool's User-Agent
        self.assertTrue(all("art-pack" in ua for _, ua in self.server.requests))
        self.assertFalse(list(self.sd.glob("torii/*.part")))

        # A second run asks for nothing: pictures and misses are cached
        before = len(self.server.requests)
        status, out, err = self.run_tool()
        self.assertEqual(status, 0, err)
        self.assertEqual(len(self.server.requests), before)
        self.assertEqual(self.pack().data, p.data)
        self.assertIn("0 requests", out)

    def test_nes_when_the_card_has_nes_games(self):
        fixture.write_roms(self.sd)
        status, _, err = self.run_tool()
        self.assertEqual(status, 0, err)
        self.assertIsNotNone(self.pack().find(FAMICOM))
        status, _, err = self.run_tool("--console", "NES")
        self.assertEqual(self.pack().crcs, [FAMICOM])
        self.assertEqual(self.run_tool("--console", "SNES")[0], 1)

    def test_a_stopped_run_writes_what_it_has_and_resumes(self):
        # The server asks for fewer requests on one picture: fetching stops,
        # the pack holds what came in, and the next run finishes it
        self.server.routes[thumb(GB_SYS, "Mono Game (Japan)")] = [
            429,
            png((90, 90, 90)),
        ]
        status, _, err = self.run_tool("--jobs", "1")
        self.assertEqual(status, 3)
        self.assertIn("run it again", err)
        first = self.pack()
        self.assertIsNone(first.find(MONO))
        status, _, err = self.run_tool()
        self.assertEqual(status, 0, err)
        self.assertEqual(len(self.pack().crcs), 6)

    def test_a_bad_picture_falls_back_to_the_next_name(self):
        # The beta's own name is a PNG Pillow can't read: the run goes on to
        # its short name, and remembers the bad one as a miss
        bad = b"\x89PNG\r\n\x1a\n" + b"junk" * 50
        self.server.routes[thumb(GBA_SYS, "Advance Wars (USA) (Beta)")] = bad
        status, _, err = self.run_tool()
        self.assertEqual(status, 0, err)
        self.assertIn("not a picture art.py reads", err)
        p = self.pack()
        self.assertEqual(p.find(BETA), p.find(WARS))
        before = len(self.server.requests)
        self.assertEqual(self.run_tool()[0], 0)
        self.assertEqual(len(self.server.requests), before)

    def test_ctrl_c_while_fetching_still_writes_a_pack(self):
        real = art_pack.concurrent.futures.as_completed

        def interrupted(futures):
            for n, f in enumerate(real(futures)):
                if n == 2:
                    raise KeyboardInterrupt
                yield f

        art_pack.concurrent.futures.as_completed = interrupted
        try:
            status, _, err = self.run_tool("--jobs", "1")
        finally:
            art_pack.concurrent.futures.as_completed = real
        self.assertEqual(status, 3)
        self.assertIn("Ctrl-C", err)
        self.assertGreater(len(self.pack().crcs), 0)
        self.assertEqual(self.run_tool()[0], 0)
        self.assertEqual(len(self.pack().crcs), 6)

    def test_offline(self):
        status, _, err = self.run_tool("--offline")
        self.assertEqual(status, 1)  # nothing cached: no pack
        self.assertIn("nothing to pack", err)
        self.assertFalse((self.sd / "torii" / "art.pak").exists())
        self.assertEqual(self.server.requests, [])
        self.run_tool()
        n = len(self.server.requests)
        self.assertEqual(self.run_tool("--offline")[0], 0)
        self.assertEqual(len(self.server.requests), n)

    def test_unreachable_and_bad_pictures(self):
        self.server.routes[thumb(GB_SYS, "Mono Game (Japan)")] = (
            b"<html>not a png</html>"
        )
        status, _, err = self.run_tool()
        self.assertEqual(status, 0, err)
        self.assertIsNone(self.pack().find(MONO))  # skipped, the rest packed
        self.assertIn("not a PNG", err)
        self.server.close()
        self.server = Server({})
        self.server.close()  # nothing listens: fetching stops, the cache still packs
        status, _, err = self.run_tool("--retry-missing")
        self.assertEqual(status, 3)
        self.assertEqual(len(self.pack().crcs), 5)


if __name__ == "__main__":
    unittest.main(verbosity=1)
