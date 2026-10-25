#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# ///
"""Tests for tools/names_db.py, run by `just test-host`: names.db and crc.txt
from the fixture dats in tests/names, with no network."""

import contextlib
import io
import os
import struct
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


def cart_dates(sd, shift=0, only=None):
    """Dates crc.txt's records as the cart's walk does, shift seconds off the
    PC's local time (the cart reads exFAT dates an hour off in summer)."""
    path = Path(sd) / "torii" / "crc.txt"
    rows = []
    for old in names_db.read_safe(path).splitlines(keepends=True):
        f = old.rstrip("\n").split("\t")
        row = old
        if len(f) == 4 and (only is None or f[3] in only):
            t = os.path.getmtime(Path(sd) / f[3].lstrip("/")) + shift
            with_shift = Path(sd) / "torii" / "stamp"
            with_shift.write_bytes(b"")
            os.utime(with_shift, (t, t))
            f[2] = names_db.fat_date(with_shift)
            with_shift.unlink()
            row = "\t".join(f) + "\n"
        rows.append(row)
    names_db.write_safe(path, "".join(rows))


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
        # Only LF (and CRLF) ends a line: other separators stay in a name, as
        # the kernel's Rename keeps them
        self.names.write_text(
            "/GB/Tetris.gb\tA\u2028B\r\n/GBA/Wars.gba\t \n", newline=""
        )
        status, _, err = add(self.sd, "Wars\tW\u0085ars\r\n")
        self.assertEqual(status, 0, err)
        self.assertEqual(
            names_db.read_safe(self.names),
            names_db.NAMES_HEADER
            + "/GB/Tetris.gb\tA\u2028B\n/GBA/Wars.gba\tW\u0085ars\n",
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
        tables, _, version = self.tables()
        data = names_db.build_db(tables, version)
        self.assertEqual(len(data) % 4, 0)
        self.assertEqual(struct.unpack_from("<H", data, 4)[0], 2)
        self.assertEqual(struct.unpack_from("<I", data, 48)[0], 0)  # no code table
        v, back = names_db.read_db(data)
        self.assertEqual(v, "2026.01.02")
        self.assertEqual(back, tables)

    def test_version_1_reads(self):
        # v0.4.1's names.db: version 1, with a code table between the CRCs
        # and the pool
        tables, _, version = self.tables()
        data = bytearray(names_db.build_db(tables, version))
        entries = sum(len(t) for t in tables)
        at = 64 + entries * 8
        pool = data[at:]
        codes = struct.pack("<II", names_db.code_key("AWRE"), 0)
        body = data[64:at] + codes + pool
        head = bytearray(data[:64])
        struct.pack_into("<H", head, 4, 1)
        struct.pack_into("<I", head, 8, 64 + len(body))
        struct.pack_into("<I", head, 12, zlib.crc32(body))
        struct.pack_into("<I", head, 48, 1)
        struct.pack_into("<I", head, 60, zlib.crc32(head[:60]))
        self.assertEqual(names_db.read_db(bytes(head + body)), (version, tables))
        # A code table in version 2 doesn't read
        struct.pack_into("<H", head, 4, 2)
        struct.pack_into("<I", head, 60, zlib.crc32(head[:60]))
        with self.assertRaises(names_db.NamesError):
            names_db.read_db(bytes(head + body))

    def test_damage_is_caught(self):
        tables, _, version = self.tables()
        data = bytearray(names_db.build_db(tables, version))
        for at in (0, 8, 64, len(data) - 8):
            bad = bytearray(data)
            bad[at] ^= 1
            with self.assertRaises(names_db.NamesError):
                names_db.read_db(bytes(bad))
        with self.assertRaises(names_db.NamesError):
            names_db.read_db(bytes(data[:-4]))

    def test_empty(self):
        data = names_db.build_db([{}, {}, {}, {}], "")
        self.assertEqual(names_db.read_db(data), ("", [{}, {}, {}, {}]))


class Cache(unittest.TestCase):
    def test_old_nes_lines_dropped(self):
        # Before v0.6.2 a NES CRC took in the iNES header: without the header
        # line that says otherwise, NES lines are dropped and computed again
        text = (
            "# Torii's CRC cache: CRC32, size, date, path\n"
            "E3C788B0\t262160\t28E66405\t/NES/Zelda II.nes\n"
            "5A0E6E6A\t16777216\t5A3C7B20\t/GBA/Minish Cap.gba\n"
        )
        self.assertEqual(list(names_db.parse_cache(text)), ["/gba/minish cap.gba"])

    def test_nes_crc_without_header(self):
        with tempfile.TemporaryDirectory() as d:
            rom = bytes(range(256)) * 8
            for name, data, crc in (
                ("game.nes", b"NES\x1a" + bytes(12) + rom, zlib.crc32(rom)),
                (
                    "game.gb",
                    b"NES\x1a" + bytes(12) + rom,
                    zlib.crc32(b"NES\x1a" + bytes(12) + rom),
                ),
                ("raw.nes", rom, zlib.crc32(rom)),
            ):
                path = Path(d) / name
                path.write_bytes(data)
                self.assertEqual(names_db.file_crc(path), (crc, len(data)), name)

    def test_lines(self):
        text = (
            "# Torii's CRC cache: CRC32, size, date, path (a NES game's CRC leaves out its 16-byte iNES header)\n"
            "5A0E6E6A\t16777216\t5A3C7B20\t/GBA/Minish Cap.gba\n"
            "-\t40976\t-\t/NES/Odd\tname.nes\n"
            # v0.4.1's lines, with a GBA game code second
            "8C5D6F2B\tAWRE\t4096\t-\t/GBA/Wars.gba\n"
            "-\t-\t300\t5A3C7B20\t/Odd\tdate.gb\n"
            "1234ABCD\tawre\t4096\t-\t/GBA/bad code.gba\n"
            "1234ABCD\t4096\t-\tno root.gba\n"
            # Fields the kernel doesn't read, nor does this
            "1234ABC\t4096\t-\t/seven digits.gba\n"
            "1234ABCD\t+4096\t-\t/plus.gba\n"
            "1234ABCD\t4096\tzz\t/bad date.gba\n"
            # The kernel splits lines at LF alone
            "1234ABCD\t4096\t-\t/a\u2028b.gba\n"
        )
        got = names_db.parse_cache(text)
        self.assertEqual(
            got,
            {
                "/gba/minish cap.gba": (
                    0x5A0E6E6A,
                    16777216,
                    "5A3C7B20",
                    "/GBA/Minish Cap.gba",
                ),
                "/nes/odd\tname.nes": (None, 40976, None, "/NES/Odd\tname.nes"),
                "/gba/wars.gba": (0x8C5D6F2B, 4096, None, "/GBA/Wars.gba"),
                "/odd\tdate.gb": (None, 300, "5A3C7B20", "/Odd\tdate.gb"),
                "/a\u2028b.gba": (0x1234ABCD, 4096, None, "/a\u2028b.gba"),
            },
        )


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
        lines = {line[3]: line for line in cache_lines(self.sd)}
        want = {p for p, _ in fixture.FILES} | {"/1/2/3/4/5/6/7/seven.gba"}
        self.assertEqual(set(lines), want)
        wars = lines["/GBA/Wars.gba"]
        self.assertEqual(
            wars[:3],
            [
                f"{zlib.crc32(fixture.rom(1, 4096, 'AWRE')):08X}",
                "4096",
                # The PC's date: the cart allows it whole hours off, and
                # forgets the CRC if the file changes before its walk
                names_db.fat_date(self.sd / "GBA" / "Wars.gba"),
            ],
        )
        self.assertEqual(lines["/NES/Famicom.nes"][1], "16400")
        # No game codes: a romhack ("Hack of Minish") keeps its file name
        self.assertIn("GBA: 5 games, 2 named by CRC, 3 keep their file names", out)
        self.assertIn("GB: 2 games, 2 named by CRC", out)
        # Both copies, the same, each sealed by its CRC line
        main = (self.sd / "torii" / "crc.txt").read_bytes()
        self.assertEqual(main, (self.sd / "torii" / "crc.txt.bak").read_bytes())
        self.assertIsNotNone(names_db.safe_text(main))
        self.assertTrue(
            main.startswith(
                b"# Torii's CRC cache: CRC32, size, date, path (a NES game's CRC leaves out its 16-byte iNES header)\n"
            )
        )
        # A run right after reads nothing again: the files keep their sizes
        # and dates
        status, out, err = update(self.sd)
        self.assertEqual(status, 0, err)
        self.assertIn("9 games, 0 CRCs computed, 9 kept", out)
        # The cart dated the records an hour off the PC (DST): they hold,
        # with the cart's dates
        cart_dates(self.sd, shift=3600)
        dated = {line[3]: line[2] for line in cache_lines(self.sd)}
        status, out, err = update(self.sd)
        self.assertIn("9 games, 0 CRCs computed, 9 kept", out)
        self.assertEqual({line[3]: line[2] for line in cache_lines(self.sd)}, dated)
        # A file of the same size with another date (a romhack unzipped over
        # a game, with the archive's older date) is read again
        os.utime(self.sd / "GBA" / "Wars.gba", (1_500_000_000, 1_500_000_000))
        status, out, err = update(self.sd)
        self.assertIn("9 games, 1 CRCs computed, 8 kept", out)
        # An undated record (names-update's before v0.4.2) holds only while
        # its file is older than crc.txt
        crc_txt = self.sd / "torii" / "crc.txt"
        rows = []
        for line in names_db.read_safe(crc_txt).splitlines(keepends=True):
            f = line.split("\t")
            if len(f) == 4 and f[3] == "/GBA/Wars.gba\n":
                f[2] = "-"
            rows.append("\t".join(f))
        names_db.write_safe(crc_txt, "".join(rows))
        status, out, err = update(self.sd)
        self.assertIn("9 games, 0 CRCs computed, 9 kept", out)
        t = os.path.getmtime(crc_txt) + 10
        os.utime(self.sd / "GBA" / "Wars.gba", (t, t))
        status, out, err = update(self.sd)
        self.assertIn("9 games, 1 CRCs computed, 8 kept", out)

    def test_old_cache_is_kept(self):
        # v0.4.1's crc.txt, with codes: its CRCs hold, written back without them
        update(self.sd)
        path = self.sd / "torii" / "crc.txt"
        rows = []
        for line in names_db.read_safe(path).splitlines():
            f = line.split("\t")
            if len(f) == 4:
                date = names_db.fat_date(self.sd / f[3].lstrip("/"))
                f = [f[0], "-", f[1], date, f[3]]
            rows.append("\t".join(f) + "\n")
        names_db.write_safe(path, "".join(rows))
        status, out, err = update(self.sd)
        self.assertEqual(status, 0, err)
        self.assertIn("9 games, 0 CRCs computed, 9 kept", out)
        self.assertTrue(all(len(line) == 4 for line in cache_lines(self.sd)))

    def test_read_only_card(self):
        update(self.sd)
        torii = self.sd / "torii"
        for p in torii.iterdir():
            p.chmod(0o444)
        torii.chmod(0o555)
        try:
            status, _, err = update(self.sd)
        finally:
            torii.chmod(0o755)
        self.assertEqual(status, 1)
        self.assertIn("names-update: ", err)
        self.assertIn("stopped there", err)
        self.assertNotIn("Traceback", err)

    def test_kept_and_recomputed(self):
        update(self.sd)
        cart_dates(self.sd)
        # One date is another's, one record has none (it holds: its file is
        # older than crc.txt); a file changed size; a game left
        path = self.sd / "torii" / "crc.txt"
        out_lines = []
        for old in names_db.read_safe(path).splitlines(keepends=True):
            f = old.rstrip("\n").split("\t")
            row = old
            if len(f) == 4 and f[3] in ("/NES/Famicom.nes", "/GBA/Wars.gba"):
                f[2] = "-" if f[3] == "/NES/Famicom.nes" else "5A3C7B20"
                row = "\t".join(f) + "\n"
            out_lines.append(row)
        names_db.write_safe(path, "".join(out_lines))
        (self.sd / "GB" / "Mono.gb").write_bytes(b"changed" * 100)
        os.remove(self.sd / "Colour.gbc")
        status, out, err = update(self.sd)
        self.assertEqual(status, 0, err)
        self.assertIn("8 games, 2 CRCs computed, 6 kept", out)
        lines = {line[3]: line for line in cache_lines(self.sd)}
        hb = "/GBA/Homebrew.gba"
        self.assertEqual(
            lines[hb][2], names_db.fat_date(self.sd / hb.lstrip("/"))
        )  # kept with its date
        wars = self.sd / "GBA" / "Wars.gba"
        self.assertEqual(
            lines["/GBA/Wars.gba"][2], names_db.fat_date(wars)
        )  # computed again
        self.assertTrue(wars.exists())
        self.assertEqual(
            lines["/GB/Mono.gb"][:3],
            [
                f"{zlib.crc32(b'changed' * 100):08X}",
                "700",
                names_db.fat_date(self.sd / "GB" / "Mono.gb"),
            ],
        )
        self.assertNotIn("/Colour.gbc", lines)

    def test_before_1980(self):
        # FAT holds no date before 1980: such a file gets no date, and its
        # CRC holds while the file is older than crc.txt, also once the cart
        # dated the record 0 (a file written with no clock)
        old = self.sd / "GBA" / "Wars.gba"
        os.utime(old, (100_000_000, 100_000_000))  # 1973
        status, out, err = update(self.sd)
        self.assertEqual(status, 0, err)
        self.assertEqual(
            {l[3]: l[2] for l in cache_lines(self.sd)}["/GBA/Wars.gba"], "-"
        )
        status, out, err = update(self.sd)
        self.assertIn("9 games, 0 CRCs computed, 9 kept", out)
        path = self.sd / "torii" / "crc.txt"
        rows = []
        for line in names_db.read_safe(path).splitlines(keepends=True):
            f = line.split("\t")
            if len(f) == 4 and f[3] == "/GBA/Wars.gba\n":
                f[2] = "00000000"
            rows.append("\t".join(f))
        names_db.write_safe(path, "".join(rows))
        status, out, err = update(self.sd)
        self.assertIn("9 games, 0 CRCs computed, 9 kept", out)

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
