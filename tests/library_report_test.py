#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""Tests for tools/library_report.py, run by `just test-host`: the name
comparison against realistic pairs, and the report of a fixture card (the
games and dats of tests/names, names.db and crc.txt from names_db's own
writers, a pack from tools/artpack.py). No network."""

import contextlib
import io
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.append(str(ROOT / "tests" / "names"))
import artpack
import fixture
import library_report
import names_db

DATS = ROOT / "tests" / "names"
PATCHED = "/GBA/Legend of Zelda, The - The Minish Cap (USA) [0badc0de].gba"


def cover(colour):
    """A cover as tools/art.py writes one: a header, the palette, the tiles."""
    head = struct.pack("<IBBBBHHI", artpack.ASSET_MAGIC, 4, 10, 10, 0, 15, 0, 3200)
    return head + bytes([colour]) * (32 + 3200)


class Compare(unittest.TestCase):
    SAME = (
        ("Super Mario Land 2 - 6 Golden Coins.gb", "Super Mario Land 2 - 6 Golden Coins (USA, Europe) (Rev 2)"),
        ("Wario Land 1 - Super Mario Land 3.gb", "Wario Land - Super Mario Land 3 (World)"),
        ("Wars.gba", "Advance Wars (USA)"),
        ("SML2.gb", "Super Mario Land 2 - 6 Golden Coins (USA, Europe)"),
        ("pokered.gb", "Pokemon - Red Version (USA, Europe) (SGB Enhanced)"),
        ("Final Fantasy 6 Advance.gba", "Final Fantasy VI Advance (USA)"),
        ("MZM.gba", "Metroid - Zero Mission (USA)"),
        ("0123 - Golden Sun (U).gba", "Golden Sun (USA, Europe)"),
        ("Pokemon Fire Red.gba", "Pokemon - FireRed Version (USA)"),
        ("Mother3.gba", "Mother 3 (Japan)"),
        ("Megaman Zero.gba", "Mega Man Zero (USA)"),
        ("DKC.gba", "Donkey Kong Country (USA)"),
        ("Oracle of Ages.gbc", "Legend of Zelda, The - Oracle of Ages (USA)"),
        ("Dragon Warrior Monsters.gbc", "Dragon Quest Monsters (Japan)"),
        ("ポケモン.gba", "Pocket Monsters - Ruby (Japan)"),  # nothing to compare
    )  # fmt: skip
    UNLIKE = (
        ("Zelda.gba", "Mario Kart - Super Circuit (USA)"),
        ("game1.gba", "Mario Kart - Super Circuit (USA)"),
        ("Advance Wars.gba", "Golden Sun (USA, Europe)"),
    )

    def test_pairs(self):
        for file, dat in self.SAME:
            self.assertFalse(library_report.unlike(file, dat), (file, dat))
        for file, dat in self.UNLIKE:
            self.assertTrue(library_report.unlike(file, dat), (file, dat))


class Report(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.sd, self.cache, self.out = base / "sd", base / "cache", base / "out"
        fixture.write_roms(self.sd)
        (self.sd / PATCHED[1:]).write_bytes(fixture.rom(13, 4096, "BZME"))
        # Advance Wars under another game's name: a possible wrong match
        (self.sd / "GBA" / "Zelda.gba").write_bytes(fixture.rom(1, 4096, "AWRE"))
        texts = [(DATS / d).read_text() for d in fixture.DATS]
        tables, _, version = names_db.make_tables(texts)
        torii = self.sd / "torii"
        torii.mkdir()
        (torii / "names.db").write_bytes(names_db.build_db(tables, version))
        names_db.update_cache(self.sd, names_db.walk_card(self.sd))
        # A game put on after names-update: no CRC yet
        (self.sd / "GBA" / "New.gba").write_bytes(fixture.rom(21, 4096))
        names_db.write_safe(
            torii / "names.txt",
            names_db.NAMES_HEADER + "/GBA/Homebrew.gba\tMy Homebrew\n",
        )
        art_dir = torii / "art"
        (art_dir / "custom").mkdir(parents=True)
        (art_dir / "custom" / "hack of minish.GBA.bin").write_bytes(b"mine")
        (art_dir / f"{self.crc('/GBA/zz Minish.gba'):08X}.bin").write_bytes(b"x")
        pack = {self.crc(p): cover(1) for p in ("/GBA/Wars.gba", "/NES/Famicom.nes")}
        artpack.write(torii / "art.pak", artpack.build(pack))
        self.cache.mkdir()
        (self.cache / "misses.txt").write_text(
            "Nintendo - Game Boy/Mono Game (Japan)\nNintendo - Game Boy/Mono Game\n"
        )

    def tearDown(self):
        self.tmp.cleanup()

    def crc(self, card_path):
        return zlib.crc32((self.sd / card_path.lstrip("/")).read_bytes())

    def run_tool(self, *extra, sd=None):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            status = library_report.main(
                [
                    str(sd or self.sd),
                    "--dats",
                    str(DATS),
                    "--cache",
                    str(self.cache),
                    "--out",
                    str(self.out),
                    *extra,
                ]
            )
        return status, out.getvalue(), err.getvalue()

    def test_report(self):
        status, out, err = self.run_tool("--write-templates")
        self.assertEqual(status, 0, err)
        hack = self.crc("/GBA/Hack of Minish.gba")
        expected = f"""No name (1): the dats don't list their CRC (romhacks, translations, bad dumps); fix: `just names-add SD FILE`
  /GBA/Hack of Minish.gba: {hack:08X}
No CRC yet (1): the cart identifies them on its next walk, or run `just names-update SD`
  /GBA/New.gba
No art (6): fix: `just art-add SD` (pictures named after the files)
  /Colour named GB.gb: not fetched yet (`just art-scrape` or `just art-pack`)
  /Colour.gbc: not fetched yet (`just art-scrape` or `just art-pack`)
  /GB/Mono.gb: no thumbnail
  /GBA/Homebrew.gba: not in the dats
  /GBA/Legend of Zelda, The - The Minish Cap (USA) [0badc0de].gba: not fetched yet (`just art-scrape` or `just art-pack`)
  /GBA/New.gba: no CRC yet
"""
        self.assertIn(expected, out)
        self.assertIn(
            "Possible wrong match (1): the dats' name is far from the file's: check the file; "
            "a `just names-add` name wins\n  /GBA/Zelda.gba: Advance Wars (USA)\n",
            out,
        )
        self.assertIn(
            "names from /torii/names.db (dats 2026.01.02); art.pak with 2 CRCs", out
        )
        self.assertIn("  GBA: 7 games, 5 named, 4 with art\n", out)
        self.assertIn("  GB: 2 games, 2 named, 0 with art\n", out)
        self.assertIn("  GBC: 1 games, 1 named, 0 with art\n", out)
        self.assertIn("  NES: 1 games, 1 named, 1 with art\n", out)
        names = (self.out / "names-todo.txt").read_text()
        self.assertIn(
            "Hack of Minish.gba\tHack of Minish\nNew.gba\tNew\nZelda.gba\tAdvance Wars (USA)\n",
            names,
        )
        arts = (self.out / "art-todo.txt").read_text().splitlines()
        self.assertEqual(
            [a for a in arts if not a.startswith("#")][-1:], ["/GBA/New.gba"]
        )
        self.assertEqual(len([a for a in arts if not a.startswith("#")]), 6)
        # names-add reads the template as it is
        with contextlib.redirect_stdout(io.StringIO()) as added:
            status = names_db.main(
                ["add", str(self.sd), str(self.out / "names-todo.txt"), "--dry-run"]
            )
        self.assertEqual(status, 0)
        self.assertIn("3 games named", added.getvalue())

    def test_famicom_is_in_the_pack(self):
        # The pack's NES cover: only a missing pack leaves Famicom without art
        (self.sd / "torii" / "art.pak").unlink()
        status, out, _ = self.run_tool()
        self.assertEqual(status, 0)
        self.assertIn("/NES/Famicom.nes: not fetched yet", out)
        self.assertIn("/GBA/Wars.gba: not fetched yet", out)
        self.assertIn("no art.pak", out)

    def test_dats_when_the_card_has_no_names_db(self):
        (self.sd / "torii" / "names.db").unlink()
        status, out, _ = self.run_tool()
        self.assertEqual(status, 0)
        self.assertIn("names from the cached dats in", out)
        self.assertIn("/GBA/Zelda.gba: Advance Wars (USA)", out)

    def test_no_names_at_all(self):
        (self.sd / "torii" / "names.db").unlink()
        status, out, _ = self.run_tool("--dats", str(Path(self.tmp.name) / "none"))
        self.assertEqual(status, 0)
        self.assertIn("run `just names-update` first", out)
        self.assertNotIn("No name (", out)
        self.assertNotIn("/GBA/Wars.gba:", out)

    def test_bad_pack_and_names_db(self):
        (self.sd / "torii" / "art.pak").write_bytes(b"not a pack" * 4)
        (self.sd / "torii" / "names.db").write_bytes(b"junk")
        status, out, err = self.run_tool()
        self.assertEqual(status, 0)
        self.assertIn("art.pak: not an art pack; not used", err)
        self.assertIn("names.db: shorter than its header; not used", err)
        self.assertIn("names from the cached dats", out)

    def test_no_card(self):
        status, _, err = self.run_tool(sd=Path(self.tmp.name) / "missing")
        self.assertEqual(status, 1)
        self.assertIn("isn't a folder", err)


if __name__ == "__main__":
    unittest.main()
