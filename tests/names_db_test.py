#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# ///
"""Tests for tools/names_db.py, run by `just test-host`: names.db and crc.txt
from the fixture dats in tests/names, with no network."""

import contextlib
import io
import os
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests" / "names"))
import fixture
import names_db

DATS = ROOT / "tests" / "names"


def update(sd, *extra):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        status = names_db.main(
            ["update", str(sd), "--offline", "--dats", str(DATS), *extra]
        )
    return status, out.getvalue(), err.getvalue()


def cache_lines(sd):
    text = names_db.read_safe(Path(sd) / "torii" / "crc.txt")
    return [
        line.split("\t")
        for line in text.splitlines()
        if line and not line.startswith("#")
    ]


class Dats(unittest.TestCase):
    def test_fixture_is_current(self):
        with tempfile.TemporaryDirectory() as tmp:
            fixture.write_dats(Path(tmp))
            for dat in fixture.DATS:
                self.assertEqual(
                    (Path(tmp) / dat).read_text(), (DATS / dat).read_text(), dat
                )

    def test_parse(self):
        version, games = names_db.parse_dat((DATS / fixture.DATS[0]).read_text())
        self.assertEqual(version, "2026.01.02")
        names = [g[0] for g in games]
        self.assertIn('Quote "Test" (World)', names)  # escaped quotes
        minish = games[names.index("Legend of Zelda, The - The Minish Cap (USA)")]
        self.assertEqual(minish[2], "BZME")  # a serial on the rom only
        self.assertEqual(minish[1][0][1], 8192)

    def test_codes(self):
        self.assertEqual(names_db.game_code("AXVE"), "AXVE")
        self.assertEqual(names_db.game_code("AGB-AXVE-USA"), "AXVE")
        self.assertIsNone(names_db.game_code(""))
        self.assertIsNone(names_db.game_code("axve"))
        self.assertIsNone(names_db.game_code("AGB"))
        self.assertEqual(names_db.code_key("AXWE"), 0x45575841)

    def test_tables(self):
        texts = [(DATS / d).read_text() for d in fixture.DATS]
        tables, codes, version = names_db.make_tables(texts)
        self.assertEqual(version, "2026.01.02")
        self.assertEqual([len(t) for t in tables], [5, 2, 1, 1])
        # A game code names the retail game, though the dat lists its beta first
        self.assertEqual(codes[names_db.code_key("AWRE")], "Advance Wars (USA)")
        self.assertEqual(
            codes[names_db.code_key("BZME")],
            "Legend of Zelda, The - The Minish Cap (USA)",
        )
        self.assertEqual(len(codes), 3)


class Db(unittest.TestCase):
    def tables(self):
        return names_db.make_tables([(DATS / d).read_text() for d in fixture.DATS])

    def test_round_trip(self):
        tables, codes, version = self.tables()
        data = names_db.build_db(tables, codes, version)
        self.assertEqual(len(data) % 4, 0)
        v, back, back_codes = names_db.read_db(data)
        self.assertEqual(v, "2026.01.02")
        self.assertEqual(back, tables)
        self.assertEqual(back_codes, codes)

    def test_damage_is_caught(self):
        tables, codes, version = self.tables()
        data = bytearray(names_db.build_db(tables, codes, version))
        for at in (0, 8, 64, len(data) - 8):
            bad = bytearray(data)
            bad[at] ^= 1
            with self.assertRaises(names_db.NamesError):
                names_db.read_db(bytes(bad))
        with self.assertRaises(names_db.NamesError):
            names_db.read_db(bytes(data[:-4]))

    def test_empty(self):
        data = names_db.build_db([{}, {}, {}, {}], {}, "")
        self.assertEqual(names_db.read_db(data), ("", [{}, {}, {}, {}], {}))


class Card(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.sd = Path(self.tmp.name)
        fixture.write_roms(self.sd)
        # Where the library doesn't look: hidden folders, dot files, past the depth cap
        for hidden in (
            "SAVER/x.gba",
            "torii/x.gba",
            "System Volume Information/x.gba",
            ".Trash/x.gba",
            "GBA/._Wars.gba",
            "1/2/3/4/5/6/7/8/deep.gba",
        ):
            p = self.sd / hidden
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b"x" * 300)
        (self.sd / "1/2/3/4/5/6/7/seven.gba").write_bytes(b"y" * 300)
        (self.sd / "GBA/notes.txt").write_bytes(b"not a game")

    def tearDown(self):
        self.tmp.cleanup()

    def test_update(self):
        status, out, err = update(self.sd)
        self.assertEqual(status, 0, err)
        names_db.read_db((self.sd / "torii" / "names.db").read_bytes())
        lines = {line[4]: line for line in cache_lines(self.sd)}
        want = {p for p, _ in fixture.FILES} | {"/1/2/3/4/5/6/7/seven.gba"}
        self.assertEqual(set(lines), want)
        wars = lines["/GBA/Wars.gba"]
        self.assertEqual(
            wars[:4],
            [f"{zlib.crc32(fixture.rom(1, 4096, 'AWRE')):08X}", "AWRE", "4096", "-"],
        )
        self.assertEqual(lines["/Colour.gbc"][1], "-")  # codes are GBA's only
        self.assertEqual(lines["/NES/Famicom.nes"][2], "16400")
        self.assertIn(
            "GBA: 5 games, 2 named by CRC, 1 by game code, 2 keep their file names", out
        )
        self.assertIn("GB: 2 games, 2 named by CRC", out)
        # Both copies, the same, each sealed by its CRC line
        main = (self.sd / "torii" / "crc.txt").read_bytes()
        self.assertEqual(main, (self.sd / "torii" / "crc.txt.bak").read_bytes())
        self.assertIsNotNone(names_db.safe_text(main))
        self.assertTrue(main.startswith(b"# Torii's CRC cache"))

    def test_kept_and_recomputed(self):
        update(self.sd)
        # The kernel took a date for one; a file changed size; a game left
        path = self.sd / "torii" / "crc.txt"
        text = names_db.read_safe(path).replace(
            "\t4096\t-\t/GBA/Wars.gba", "\t4096\t5A3C7B20\t/GBA/Wars.gba"
        )
        names_db.write_safe(path, text)
        (self.sd / "GB" / "Mono.gb").write_bytes(b"changed" * 100)
        os.remove(self.sd / "Colour.gbc")
        status, out, err = update(self.sd)
        self.assertEqual(status, 0, err)
        self.assertIn("8 games, 1 CRCs computed, 7 kept", out)
        lines = {line[4]: line for line in cache_lines(self.sd)}
        self.assertEqual(lines["/GBA/Wars.gba"][3], "5A3C7B20")  # kept with its date
        self.assertEqual(
            lines["/GB/Mono.gb"][:4],
            [f"{zlib.crc32(b'changed' * 100):08X}", "-", "700", "-"],
        )
        self.assertNotIn("/Colour.gbc", lines)

    def test_safe_copies(self):
        torii = self.sd / "torii"
        torii.mkdir(exist_ok=True)
        names_db.write_safe(torii / "crc.txt", "old\n")
        names_db.write_safe(torii / "crc.txt.bak", "backup\n")
        self.assertEqual(names_db.read_safe(torii / "crc.txt"), "old\n")
        (torii / "crc.txt").write_bytes(b"")  # a write cut short
        self.assertEqual(names_db.read_safe(torii / "crc.txt"), "backup\n")
        (torii / "crc.txt").write_bytes(b"edited by hand\n")
        self.assertEqual(names_db.read_safe(torii / "crc.txt"), "edited by hand\n")

    def test_no_crc(self):
        status, _, _ = update(self.sd, "--no-crc")
        self.assertEqual(status, 0)
        self.assertTrue((self.sd / "torii" / "names.db").exists())
        self.assertFalse((self.sd / "torii" / "crc.txt").exists())

    def test_offline_without_dats(self):
        with tempfile.TemporaryDirectory() as empty:
            err = io.StringIO()
            with (
                contextlib.redirect_stderr(err),
                contextlib.redirect_stdout(io.StringIO()),
            ):
                status = names_db.main(
                    ["update", str(self.sd), "--offline", "--dats", empty]
                )
            self.assertEqual(status, 1)
            self.assertIn("--offline downloads nothing", err.getvalue())
        self.assertFalse((self.sd / "torii" / "names.db").exists())


if __name__ == "__main__":
    unittest.main()
