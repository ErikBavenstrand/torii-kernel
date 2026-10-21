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


class Romhacks(unittest.TestCase):
    def test_title_key(self):
        key = names_db.title_key
        self.assertEqual(
            key("Pokemon - Emerald Version (USA, Europe)"), "pokemonemeraldversion"
        )
        self.assertEqual(
            key("Pokemon - Emerald Version (Erik) [e31836b9].gba"),
            "pokemonemeraldversion",
        )
        self.assertEqual(
            key("Pok\u00e9mon_-_Emerald_Version.GBA"), "pokemonemeraldversion"
        )
        self.assertEqual(
            key("Legend of Zelda, The - The Minish Cap (USA)"),
            key("The Legend of Zelda - The Minish Cap.gba"),
        )
        self.assertEqual(key("0042 - Advance Wars (U) [!].gba"), "advancewars")
        self.assertEqual(key("Mega Man & Bass (USA).gba"), "megamanbass")
        self.assertEqual(key("(USA).gba"), "")
        # Only a game's own extension goes: a dat name keeps its dots
        self.assertEqual(key("Mr. Driller 2 (USA)"), "mrdriller2")

    def test_code_match(self):
        codes = {names_db.code_key("BPEE"): "Pokemon - Emerald Version (USA, Europe)"}
        match = names_db.code_match
        name = "Pokemon - Emerald Version (USA, Europe)"
        # A patched dump of the game, under its title, its dat tags, a region
        # or a checksum
        self.assertEqual(match(codes, 0, "BPEE", "pokemon emerald version.gba"), name)
        self.assertEqual(
            match(codes, 0, "BPEE", "Pokemon - Emerald Version (USA, Europe).gba"), name
        )
        self.assertEqual(
            match(codes, 0, "BPEE", "Pokemon - Emerald Version (U) [!].gba"), name
        )
        self.assertEqual(
            match(codes, 0, "BPEE", "Pokemon - Emerald Version [b3e036ae].gba"), name
        )
        # Under its title with a tag of its own: a hack, a translation, or
        # anything else the dat name doesn't say
        for hack in (
            "(Hack by X)",
            "[Hack]",
            "(Kaizo)",
            "[T-Eng]",
            "[h1]",
            "(Erik)",
            "(Europe)",
        ):
            self.assertIsNone(
                match(codes, 0, "BPEE", f"Pokemon - Emerald Version {hack}.gba"), hack
            )
        # Romhacks keep their base game's code under their own titles
        self.assertIsNone(match(codes, 0, "BPEE", "Pokemon - Glazed.gba"))
        self.assertIsNone(match(codes, 0, "BPEE", "Pokemon Emerald Rogue.gba"))
        self.assertIsNone(match(codes, 0, "BPEE", "Pokemon - Emerald.gba"))
        # Not GBA, no code, an unknown code, a name with no title
        self.assertIsNone(match(codes, 1, "BPEE", "Pokemon - Emerald Version.gbc"))
        self.assertIsNone(match(codes, 0, None, "Pokemon - Emerald Version.gba"))
        self.assertIsNone(match(codes, 0, "AXVE", "Pokemon - Emerald Version.gba"))
        self.assertIsNone(
            match({names_db.code_key("ZZZZ"): "(Unl)"}, 0, "ZZZZ", "[x].gba")
        )

    def test_db_name_is_crc_only(self):
        tables = [{1: "Game (USA)"}, {2: "Colour (USA)"}, {3: "Mono (Japan)"}, {}]
        self.assertEqual(names_db.db_name(tables, 0, 1), "Game (USA)")
        self.assertEqual(
            names_db.db_name(tables, 2, 2), "Colour (USA)"
        )  # .gb in the GBC dat
        self.assertEqual(names_db.db_name(tables, 1, 3), "Mono (Japan)")
        self.assertIsNone(names_db.db_name(tables, 0, 2))
        self.assertIsNone(names_db.db_name(tables, 3, 1))
        self.assertIsNone(names_db.db_name(tables, 0, None))


def add(sd, lines, *extra):
    tsv = Path(sd).parent / "names.tsv"
    tsv.write_text(lines, encoding="utf-8")
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        status = names_db.main(["add", str(sd), str(tsv), *extra])
    return status, out.getvalue(), err.getvalue()


class NamesAdd(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.sd = Path(self.tmp.name) / "sd"
        for game in (
            "GBA/Pokemon - Glazed.gba",
            "Hacks/Pokemon - Glazed.gba",
            "GBA/Wars.gba",
            "GB/Tetris.gb",
            "torii/Hidden.gba",
        ):
            (self.sd / game).parent.mkdir(parents=True, exist_ok=True)
            (self.sd / game).write_bytes(b"x")
        self.names = self.sd / "torii" / "names.txt"

    def tearDown(self):
        self.tmp.cleanup()

    def test_adds_names_by_file_name(self):
        # Hand-written names.txt: a comment and a name stay, a later line wins
        self.names.write_text(
            "# mine\n/GB/Tetris.gb\tOld\n/GB/Tetris.gb\tBlocks\n/gone.gba\tGone\n"
        )
        status, out, err = add(
            self.sd,
            "# file name, tab, name\n"
            "pokemon - glazed\tGlazed (Emerald hack)\n"  # no extension, any case
            "Wars.GBA\t  Advance Wars \x01\n"  # trimmed, control characters dropped
            "/GBA/Wars.gba\tAdvance Wars\n"  # a path; the later line wins
            "Hidden.gba\tNo\n"  # hidden from the library
            "Nobody\tNo one\n"
            "\n",
        )
        self.assertEqual(status, 0, err)
        self.assertIn("  /Hacks/Pokemon - Glazed.gba: Glazed (Emerald hack)", out)
        self.assertIn("  Hidden.gba: no game on the card has that file name", out)
        self.assertIn("  Nobody: no game on the card has that file name", out)
        self.assertIn(
            "4 games named, 0 names removed, 2 lines named no game; names.txt holds 5 names",
            out,
        )
        text = names_db.read_safe(self.names)
        self.assertEqual(
            text,
            "# mine\n"
            "/GB/Tetris.gb\tBlocks\n"
            "/GBA/Pokemon - Glazed.gba\tGlazed (Emerald hack)\n"
            "/GBA/Wars.gba\tAdvance Wars\n"
            "/gone.gba\tGone\n"
            "/Hacks/Pokemon - Glazed.gba\tGlazed (Emerald hack)\n",
        )
        # Both copies, sealed by the CRC line the kernel checks
        main = self.names.read_bytes()
        self.assertEqual(main, (self.sd / "torii" / "names.txt.bak").read_bytes())
        self.assertIsNotNone(names_db.safe_text(main))

        # An empty name removes a game's own name; --dry-run writes nothing
        status, out, _ = add(self.sd, "Tetris.gb\t\n", "--dry-run")
        self.assertEqual(status, 0)
        self.assertIn("/GB/Tetris.gb: own name removed", out)
        self.assertEqual(names_db.read_safe(self.names), text)
        status, out, _ = add(self.sd, "Tetris\t\n")
        self.assertEqual(status, 0)
        self.assertNotIn("Tetris", names_db.read_safe(self.names))

    def test_lines_split_as_the_kernel_splits_them(self):
        # Only LF (and CRLF) ends a line; other separators in a name go
        self.names.write_text(
            "/GB/Tetris.gb\tA\u2028B\r\n/GBA/Wars.gba\t \n", newline=""
        )
        status, _, err = add(self.sd, "Wars\tW\u0085ars\r\n")
        self.assertEqual(status, 0, err)
        self.assertEqual(
            names_db.read_safe(self.names),
            names_db.NAMES_HEADER + "/GB/Tetris.gb\tA\u2028B\n/GBA/Wars.gba\tWars\n",
        )

    def test_new_names_txt_and_refusals(self):
        status, _, err = add(self.sd, "Wars\tWars!\n")
        self.assertEqual(status, 0, err)
        self.assertEqual(
            names_db.read_safe(self.names),
            names_db.NAMES_HEADER + "/GBA/Wars.gba\tWars!\n",
        )
        before = self.names.read_bytes()
        for lines in ("Wars Advance Wars\n", "Wars\t" + "x" * 256 + "\n"):
            status, _, err = add(self.sd, lines)
            self.assertEqual(status, 1, lines)
            self.assertIn("names-add:", err)
        self.assertEqual(self.names.read_bytes(), before)  # nothing written
        status, _, err = add(self.sd / "nope", "Wars\tW\n")
        self.assertEqual(status, 1)
        self.assertIn("isn't a folder", err)

    def test_games_by_name(self):
        games = [("/A/Foo.gba", "", 0), ("/B/Foo.gba", "", 0), ("/Foo.gbc", "", 1)]
        by = names_db.games_by_name(games)
        self.assertEqual(len(by["foo"]), 3)
        self.assertEqual(len(by["foo.gba"]), 2)
        self.assertEqual(names_db.fold("Poke\u0301mon"), names_db.fold("POK\u00c9MON"))


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
        self.assertIn("GBA: 5 games, 2 named by CRC, 3 keep their file names", out)
        # No game codes: a romhack ("Hack of Minish") keeps its file name
        self.assertEqual(
            names_db.read_db((self.sd / "torii" / "names.db").read_bytes())[2], {}
        )
        self.assertIn("GB: 2 games, 2 named by CRC", out)
        # Both copies, the same, each sealed by its CRC line
        main = (self.sd / "torii" / "crc.txt").read_bytes()
        self.assertEqual(main, (self.sd / "torii" / "crc.txt.bak").read_bytes())
        self.assertIsNotNone(names_db.safe_text(main))
        self.assertTrue(main.startswith(b"# Torii's CRC cache"))

    def test_kept_and_recomputed(self):
        update(self.sd)
        # The kernel took each file's date; one date is another's, one
        # record has none yet; a file changed size; a game left
        path = self.sd / "torii" / "crc.txt"
        out_lines = []
        for old in names_db.read_safe(path).splitlines(keepends=True):
            f = old.rstrip("\n").split("\t")
            row = old
            if len(f) == 5 and f[4] != "/NES/Famicom.nes":
                f[3] = names_db.fat_date(self.sd / f[4].lstrip("/"))
                if f[4] == "/GBA/Wars.gba":
                    f[3] = "5A3C7B20"
                row = "\t".join(f) + "\n"
            out_lines.append(row)
        names_db.write_safe(path, "".join(out_lines))
        (self.sd / "GB" / "Mono.gb").write_bytes(b"changed" * 100)
        os.remove(self.sd / "Colour.gbc")
        status, out, err = update(self.sd)
        self.assertEqual(status, 0, err)
        self.assertIn("8 games, 3 CRCs computed, 5 kept", out)
        lines = {line[4]: line for line in cache_lines(self.sd)}
        hb = "/GBA/Homebrew.gba"
        self.assertEqual(
            lines[hb][3], names_db.fat_date(self.sd / hb.lstrip("/"))
        )  # kept with its date
        self.assertEqual(lines["/GBA/Wars.gba"][3], "-")  # computed again
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
