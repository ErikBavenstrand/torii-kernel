#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Tests for tools/cheats.py, run by `just test-host`: mGBA exports read and
converted, the cheat files it writes and merges on a fixture card, romhacks
on synthetic ROMs, bundle imports, and the harvester's index-page parser
(tools/cheats_harvest.js, under node when there is one) on a saved page. No
network."""

import contextlib
import io
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path
from typing import ClassVar

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import cheats
import names_db

FIXTURE = ROOT / "tests" / "cheats" / "gamehacking_gba_50.html"
NODE = shutil.which("node")


def rom(seed, code=None, version=0, size=64 * 1024):
    """A ROM's bytes: noise from seed, a game code at 0xAC, its version at
    0xBC."""
    data = bytearray((i * seed * 7 + seed + (i >> 8)) & 0xFF for i in range(size))
    data[0xAC:0xB0] = code.encode("ascii") if code else b"\0\0\0\0"
    data[0xBC] = version
    return bytes(data)


def mgba(*sets):
    """An mGBA export: (name, directive or None, [lines]) each, as
    GameHacking.org writes them."""
    out = []
    for name, directive, lines in sets:
        out.append("!disabled")
        if directive:
            out.append(f"!{directive}")
        out.append(f"# {name}")
        out += lines
        out.append("")
    return "\n".join(out)


def safe(text):
    """A file as the kernel's safe-file writes leave it: text and a CRC line."""
    data = text.encode()
    return data + b"#crc32 %08x\n" % zlib.crc32(data)


class Kernel(unittest.TestCase):
    """The kernel's names and limits the tool writes for."""

    def test_limits(self):
        h = (ROOT / "source" / "cheatfile.h").read_text()
        self.assertEqual(
            int(
                re.search(r"#define CHEATFILE_TEXT_MAX (0x[0-9A-Fa-f]+)", h).group(1),
                16,
            ),
            cheats.TEXT_MAX,
        )
        self.assertEqual(
            int(re.search(r"#define CHEATFILE_MAX (\d+)", h).group(1)),
            cheats.CHEATS_MAX,
        )
        self.assertEqual(
            int(re.search(r"#define CHEATFILE_OPS_MAX (\d+)", h).group(1)),
            cheats.LINES_MAX,
        )

    def test_window_limits(self):
        """The window limits the tool fits big files to are the kernel's."""
        ix = (ROOT / "source" / "cheatindex.h").read_text()
        st = (ROOT / "source" / "cheatstore.h").read_text()
        for text, name, value in (
            (ix, "CHEATINDEX_CHEATS", cheats.WINDOW_CHEATS),
            (ix, "CHEATINDEX_BYTES", cheats.WINDOW_BYTES),
            (st, "CHEATSTORE_WINDOWS", cheats.WINDOWS),
        ):
            got = re.search(rf"#define {name} (0x[0-9A-Fa-f]+|\d+)\n", text).group(1)
            self.assertEqual(int(got, 0), value, name)
        self.assertIn(f'#define CHEATINDEX_EXT "{cheats.INDEX_EXT}"', ix)

    def test_format_names(self):
        c = (ROOT / "source" / "cheatfile.c").read_text()
        body = re.search(r"cheatfile_format_names\[[^]]*\] = \{([^}]*)\}", c).group(1)
        names = [
            None if n.strip() == "NULL" else n.strip().strip('"')
            for n in body.split(",")
        ]
        self.assertEqual(tuple(n for n in names if n != ""), cheats.FORMAT_NAMES)


class Mgba(unittest.TestCase):
    def test_directives(self):
        """Each set's own directive, as mGBA's parser takes it; one with
        none gets none (mGBA would carry the last one over)."""
        sets = cheats.parse_mgba(
            "!disabled\n!GSAv1\n# A\n2B5117B8 1AC6855D\n\n"
            "!disabled\n!GSAv1 raw\n# B\n2B5117B8 1AC6855D\n"
            "!enabled\n!PARv3\n# C\nDDBBB9BA 3DA8ACBA\n55555555 66666666\n"
            "!PARv3 raw\n#D\n77777777 88888888\n"
            "!disabled\n# E\n82001000 0063\n"
            "!parv3\n# F\n02001000:12\n"
            "!PARv3\n!GSAv1\n# G\n2B5117B8 1AC6855D\n"
            "!PARv3\n!reset\n# H\n2B5117B8 1AC6855D\n"
        )
        self.assertEqual(
            [(s.name, s.directive) for s in sets],
            [
                ("A", "GSAv1"),
                ("B", "GSAv1 raw"),
                ("C", "PARv3"),
                ("D", "PARv3 raw"),
                ("E", None),
                ("F", None),  # mGBA's names are case-sensitive
                ("G", "GSAv1"),  # the last given
                ("H", None),
            ],
        )
        self.assertEqual(sets[2].lines, ["DDBBB9BA 3DA8ACBA", "55555555 66666666"])

    def test_lines_before_a_name(self):
        sets = cheats.parse_mgba("  02001000:12  \n# Named\n02001001:34\n")
        self.assertEqual(
            [(s.name, s.lines) for s in sets],
            [("", ["02001000:12"]), ("Named", ["02001001:34"])],
        )

    def test_convert(self):
        """Unnamed, oversized and duplicate sets are dropped; the directive
        becomes cheatN_format; '"' in a name becomes "'"."""
        big = [f"{0x02000000 + i:08X}:01" for i in range(cheats.LINES_MAX + 1)]
        ok = [f"{0x02000000 + i:08X}:01" for i in range(cheats.LINES_MAX)]
        sets = cheats.parse_mgba(
            mgba(
                ('Max "Money"', "PARv3", ["2B5117B8 1AC6855D"]),
                ("???", None, ["02001000:12"]),
                ("", None, ["02001001:12"]),
                ("? ?", "GSAv1", ["2B5117B8 1AC6855D"]),
                ("Nature", None, big),
                ("Long but fine", None, ok),
                ("Money again", "PARv3", ["2B5117B8  1AC6855D"]),
                ("Money as GS", "GSAv1", ["2B5117B8 1AC6855D"]),
                ("Two lines", None, ["82001000 0063", "82001002 0063"]),
            )
        )
        got, dropped = cheats.convert(sets)
        self.assertEqual(
            got,
            [
                cheats.Cheat(
                    "Max 'Money'", "2B5117B8 1AC6855D", False, {"format": "PARv3"}
                ),
                cheats.Cheat("Long but fine", "+".join(ok), False),
                cheats.Cheat(
                    "Money as GS", "2B5117B8 1AC6855D", False, {"format": "GSAv1"}
                ),
                cheats.Cheat("Two lines", "82001000 0063+82001002 0063", False),
            ],
        )
        self.assertEqual(
            dropped,
            [
                ("???", "no name"),
                ("(no name)", "no name"),
                ("? ?", "no name"),
                ("Nature", "257 code lines, over 256"),
                ("Money again", "the same as Max 'Money'"),
            ],
        )
        text = cheats.format_cht(got)
        self.assertIn(
            'cheat0_code = "2B5117B8 1AC6855D"\ncheat0_format = "PARv3"\n', text
        )
        self.assertNotIn("_enable = false", text)  # off is the default: compact
        self.assertNotIn("cheat1_format", text)
        self.assertEqual(cheats.parse_cht(text), got)

    def test_fit(self):
        """Over the cap, the largest unpicked sets go first, then picked
        ones; cheats not removable (your own) stay."""
        small = [
            cheats.Cheat(f"S{i}", f"{0x02000000 + i:08X}:01", False) for i in range(3)
        ]
        big = cheats.Cheat("Big", "+".join(["02000000:01"] * 40), False)
        picked = cheats.Cheat("Picked big", "+".join(["02000100:01"] * 50), True)
        mine = cheats.Cheat(
            "Mine", "+".join(["02000200:01"] * 60), False, {"user": "true"}
        )
        all_ = [small[0], big, picked, small[1], small[2], mine]
        full = len(cheats.format_cht(all_, ["c"]))
        kept, gone = cheats.fit(all_, range(5), ["c"], limit=full)
        self.assertEqual((kept, gone), (all_, []))
        kept, gone = cheats.fit(all_, range(5), ["c"], limit=full - 100)
        self.assertEqual(gone, ["Big"])
        self.assertLessEqual(len(cheats.format_cht(kept, ["c"])), full - 100)
        # Every unpicked set goes before a picked one
        kept, gone = cheats.fit(all_, range(5), ["c"], limit=full - 650)
        self.assertEqual(gone, ["S0", "Big", "S1", "S2"])
        kept, gone = cheats.fit(all_, range(5), ["c"], limit=full - 900)
        self.assertEqual(gone, ["S0", "Big", "Picked big", "S1", "S2"])
        self.assertEqual(kept, [mine])
        kept, gone = cheats.fit(all_, range(5), ["c"], limit=full, most=4)
        self.assertEqual(len(kept), 4)


class Merge(unittest.TestCase):
    def test_parse_and_merge(self):
        """Picks are kept by code, your own codes come after the database's,
        '#' lines are comments and values are kept byte for byte."""
        old = cheats.parse_cht(
            "# a note\ncheats = 3\n"
            'cheat0_desc = "Money"\ncheat0_code = "82000250+E0FF"\ncheat0_enable = true\r\n'
            '#cheat1_code = "commented"\n'
            'cheat1_desc = "Mine"\ncheat1_code = "02001234:12"\ncheat1_enable = true\n'
            'cheat2_desc = "Off"\ncheat2_code = "AAAA0000 0001"\ncheat2_enable = false\n'
        )
        self.assertEqual(old[1], ("Mine", "02001234:12", True))
        database = [
            ("Infinite Money", "82000250+E0FF", False),
            ("HP", "42000538+07CF", False),
        ]
        merged, own, stale = cheats.merge(database, old)
        self.assertEqual((own, stale), (2, 0))
        self.assertEqual(
            merged,
            [
                ("Infinite Money", "82000250+E0FF", True),
                ("HP", "42000538+07CF", False),
                ("Mine", "02001234:12", True),
                ("Off", "AAAA0000 0001", False),
            ],
        )
        self.assertEqual(cheats.parse_cht(cheats.format_cht(merged)), merged)
        # From a file this tool wrote, an unpicked cheat the source dropped
        # isn't yours: it goes
        merged, own, stale = cheats.merge(database, old, keep_all=False)
        self.assertEqual((own, stale), (1, 1))
        self.assertEqual(merged[-1], ("Mine", "02001234:12", True))
        # A quote missing at one end
        self.assertEqual(
            cheats.parse_cht('cheat0_desc = Less HP"\ncheat0_code = "1234 5678"\n'),
            [("Less HP", "1234 5678", False)],
        )

    def test_renamed_kept(self):
        """A database cheat renamed on the cart (cheatN_renamed) keeps its
        name through a merge, and its renamed line; one not renamed takes
        the database's name."""
        old = cheats.parse_cht(
            'cheat0_desc = "My money"\ncheat0_code = "82000250+E0FF"\n'
            "cheat0_renamed = true\ncheat0_enable = true\n"
            'cheat1_desc = "Old HP"\ncheat1_code = "42000538+07CF"\n'
        )
        database = [
            ("Infinite Money", "82000250+E0FF", False),
            ("HP", "42000538+07CF", False),
        ]
        merged, own, _ = cheats.merge(database, old)
        self.assertEqual(own, 0)
        self.assertEqual(merged[0][:3], ("My money", "82000250+E0FF", True))
        self.assertTrue(cheats.is_true(merged[0].extra["renamed"]))
        self.assertEqual(merged[1], ("HP", "42000538+07CF", False))
        text = cheats.format_cht(merged)
        self.assertIn("cheat0_renamed = true\n", text)
        self.assertEqual(cheats.parse_cht(text), merged)
        # A placeholder filled in on the cart, then renamed: matched by its
        # code's other digits, it stays filled in and named
        old = cheats.parse_cht(
            'cheat0_desc = "Cash"\ncheat0_code = "02001510:0063"\ncheat0_renamed = true\n'
        )
        merged, own, stale = cheats.merge(
            [("Max money", "0200??10:0063", False)], old, keep_all=False
        )
        self.assertEqual((own, stale), (0, 0))
        self.assertEqual(merged[0][:2], ("Cash", "02001510:0063"))

    def test_folder_kept(self):
        """cheatN_folder is read, written after the cheat's other lines and
        kept through a merge: the database's folder over the card's, the
        card's when the database gives none."""
        old = cheats.parse_cht(
            'cheat0_desc = "Items: Master Ball"\ncheat0_code = "32001000 0063"\n'
            'cheat0_folder = "Items"\ncheat0_enable = true\n'
            'cheat1_desc = "HP"\ncheat1_code = "32001004 0063"\ncheat1_folder = "Old"\n'
        )
        self.assertEqual(old[0].extra, {"folder": "Items"})
        database = [
            cheats.Cheat("Items: Master Ball", "32001000 0063", False),
            cheats.Cheat("Battle: HP", "32001004 0063", False, {"folder": "Battle"}),
        ]
        merged, own, _ = cheats.merge(database, old)
        self.assertEqual(own, 0)
        self.assertEqual(merged[0].extra, {"folder": "Items"})
        self.assertTrue(merged[0][2])
        self.assertEqual(merged[1].extra, {"folder": "Battle"})
        text = cheats.format_cht(merged)
        self.assertIn('cheat1_folder = "Battle"\n', text)
        self.assertEqual(cheats.parse_cht(text), merged)

    def test_torii_keys(self):
        """cheatN_format and cheatN_user are kept through a merge, the
        user's format over the source's; a placeholder the keypad filled in
        stays in its place; the user's own codes stay whatever keep_all."""
        old = cheats.parse_cht(
            "cheats = 3\r\n\r\n"
            'cheat0_desc = "Pick a level"\r\ncheat0_code = "3300075C+0003"\r\n'
            "cheat0_enable = true\r\n\r\n"
            'cheat1_desc = "Typed"\r\ncheat1_code = "02001000 00000063"\r\n'
            'cheat1_enable = false\r\ncheat1_format = "GSAv1 raw"\r\ncheat1_user = true\r\n\r\n'
            'cheat2_desc = "Money"\r\ncheat2_code = "82000250+E0FF"\r\n'
            'cheat2_enable = true\r\ncheat2_format = "CodeBreaker"\r\n'
        )
        self.assertEqual(old[1].extra, {"format": "GSAv1 raw", "user": "true"})
        database = [
            cheats.Cheat("Infinite Money", "82000250+E0FF", False, {"format": "PARv3"}),
            cheats.Cheat("Pick a level", "3300075C+00??", False),
            cheats.Cheat("HP", "42000538+07CF", False, {"format": "GSAv1"}),
        ]
        for keep_all in (True, False):
            merged, own, _ = cheats.merge(database, old, keep_all)
            self.assertEqual(own, 1)
            self.assertEqual(
                merged,
                [
                    cheats.Cheat(
                        "Infinite Money",
                        "82000250+E0FF",
                        True,
                        {"format": "CodeBreaker"},
                    ),
                    old[0],
                    cheats.Cheat("HP", "42000538+07CF", False, {"format": "GSAv1"}),
                    old[1],
                ],
            )
        text = cheats.format_cht(merged)
        self.assertIn(
            'cheat3_code = "02001000 00000063"\ncheat3_format = "GSAv1 raw"\ncheat3_user = true\n',
            text,
        )
        self.assertEqual(cheats.parse_cht(text), merged)
        # A code the user typed under a placeholder's name stays the user's
        typed = cheats.Cheat("Pick a level", "3300075C+0007", True, {"user": "true"})
        merged, own, _ = cheats.merge(
            [("Pick a level", "3300075C+00??", False)], [typed], False
        )
        self.assertEqual(merged, [("Pick a level", "3300075C+00??", False), typed])

    def test_for_crc(self):
        self.assertEqual(
            cheats.for_crc("\n# for crc32 DD88761C\ncheats = 0\n"), 0xDD88761C
        )
        self.assertIsNone(cheats.for_crc("# note\n# for crc32 DD88761C\n"))
        self.assertIsNone(cheats.for_crc("cheats = 0\n"))


class IndexPage(unittest.TestCase):
    """The harvester's parser (JS) and its Python twin on a saved page."""

    EXPECTED_FIRST: ClassVar[dict] = {
        "crc": "BEDDC67F",
        "id": 5610,
        "name": "(USA)",
        "title": "Pocket Dogs",
        "serial": "AGB-BTDE-USA",
        "size": "8M",
        "codes": 4,
    }

    def check(self, page):
        self.assertEqual(page["lastPage"], 76)
        rows = page["rows"]
        self.assertEqual(len(rows), 19)
        self.assertEqual(rows[0], self.EXPECTED_FIRST)
        by = {r["crc"]: r for r in rows}
        self.assertEqual(by["53A8EE86"]["title"], "Pocky & Rocky with Becky")
        self.assertEqual(by["D12F1FDD"]["serial"], "")
        fire = by["84EE4776"]
        self.assertEqual(
            (fire["id"], fire["name"], fire["codes"]),
            (5632, "(USA, Europe) (Rev 1)", 775),
        )
        self.assertEqual(fire["title"], "Pokemon - FireRed Version")
        self.assertEqual(cheats.base_candidates(by, "BPRE", 1), ["84EE4776"])
        self.assertEqual(cheats.base_candidates(by, "BPRE", 0), ["DD88761C"])
        self.assertEqual(cheats.base_candidates(by, "AXVP", 1), ["61641576"])
        self.assertEqual(cheats.base_candidates(by, "AXVE", 1), [])

    def test_python(self):
        self.check(cheats.parse_index_page(FIXTURE.read_text("utf-8")))
        self.assertIsNone(
            cheats.parse_index_page("<html><title>Just a moment...</title></html>")
        )

    @unittest.skipUnless(NODE, "no node")
    def test_js(self):
        script = (
            "const h = require(process.argv[1]); const fs = require('fs');"
            "const p = h.parseIndexPage(fs.readFileSync(process.argv[2], 'utf8'));"
            "const idx = {}; for (const r of p.rows) idx[r.crc] = r;"
            "console.log(JSON.stringify({page: p, bases: h.baseCandidates(idx, 'BPRE', 1),"
            " none: h.parseIndexPage('<html>Just a moment...</html>')}));"
        )
        out = subprocess.run(
            [NODE, "-e", script, str(cheats.HARVEST_TEMPLATE), str(FIXTURE)],
            capture_output=True,
            text=True,
            check=True,
        ).stdout
        got = json.loads(out)
        self.check(got["page"])
        self.assertEqual(
            got["page"], cheats.parse_index_page(FIXTURE.read_text("utf-8"))
        )
        self.assertEqual(got["bases"], ["84EE4776"])
        self.assertIsNone(got["none"])


# --- A fixture card ---------------------------------------------------------------------------

FIRERED = rom(1, "BPRE", 0)
# A light hack: one block changed, at 0x2000
LIGHT = FIRERED[:0x2000] + bytes(4096) + FIRERED[0x3000:]
# A heavy hack: most blocks changed
HEAVY = FIRERED[:0x1000] + bytes(len(FIRERED) - 0x1000)
EMERALD = rom(2, "BPEE", 0)
LONELY = rom(3, "LONE", 0)
RUBY_HACK = rom(4, "AXVE", 0)  # two dumps fit AXVE version 0
SAPPHIRE_HACK = rom(5, "AXPE", 0)  # its base isn't on the card
NOCODES = rom(6, "NOCO", 0)

CRC = {
    n: zlib.crc32(d)
    for n, d in globals().copy().items()
    if isinstance(d, bytes) and n.isupper()
}


def entry(gid, title, name, serial, codes=10):
    return {
        "id": gid,
        "name": name,
        "title": title,
        "serial": serial,
        "size": "16M",
        "codes": codes,
    }


INDEX = {
    f"{CRC['FIRERED']:08X}": entry(
        5633, "Pokemon - FireRed Version", "(USA)", "AGB-BPRE-USA"
    ),
    f"{CRC['EMERALD']:08X}": entry(
        5630, "Pokemon - Emerald Version", "(USA, Europe)", "AGB-BPEE-USA, AGB-BPEP"
    ),
    f"{CRC['NOCODES']:08X}": entry(9000, "No Codes", "(USA)", "AGB-NOCO-USA", codes=0),
    "11111111": entry(5638, "Pokemon - Ruby Version", "(USA)", "AGB-AXVE-USA"),
    "22222222": entry(5699, "Pokemon - Ruby Version (Alt)", "(USA)", "AGB-AXVE-USA"),
    "33333333": entry(5643, "Pokemon - Sapphire Version", "(USA)", "AGB-AXPE-USA"),
    f"{CRC['LONELY']:08X}": entry(7000, "Lonely", "(USA)", "AGB-LONE-USA"),
}

FIRERED_EXPORT = mgba(
    ("Master Ball", "PARv3", ["2B5117B8 1AC6855D"]),
    ("Money", None, ["02025840:99"]),
    ("Patch in the changed block", None, ["08002100:1234"]),
    ("Patch outside it", None, ["08000400:1234"]),
    ("???", None, ["02000000:01"]),
    ("Master Ball", "PARv3", ["2B5117B8 1AC6855D"]),
)
EMERALD_EXPORT = mgba(("Rare Candy", "GSAv1", ["DDBBB9BA 3DA8ACBA"]))
SAPPHIRE_EXPORT = mgba(
    ("Shiny", None, ["02001000:01"]), ("Patch", None, ["08000400:1234"])
)


class Card(unittest.TestCase):
    CARD: ClassVar[dict] = {
        "/GBA/FireRed.gba": FIRERED,
        "/GBA/Hacks/Light.gba": LIGHT,
        "/GBA/Hacks/Heavy.gba": HEAVY,
        "/GBA/Emerald.gba": EMERALD,
        "/GBA/Lonely.gba": LONELY,
        "/GBA/Hacks/Ruby Hack.gba": RUBY_HACK,
        "/GBA/Hacks/Sapphire Hack.gba": SAPPHIRE_HACK,
        "/GBA/No Codes.gba": NOCODES,
    }

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.sd = base / "sd"
        self.cache = base / "cache"
        self.downloads = base / "downloads"
        self.downloads.mkdir()
        for path, data in self.CARD.items():
            f = self.sd / path.lstrip("/")
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_bytes(data)
        # /CHEAT, EZ-Flash's, is never touched
        (self.sd / "CHEAT").mkdir()
        (self.sd / "CHEAT" / "FireRed.cht").write_text("[Old]\nON=1,2\n")

    def tearDown(self):
        self.tmp.cleanup()

    def fill_cache(self, complete=True):
        idx = cheats.Index(self.cache)
        idx.entries = dict(INDEX)
        idx.last_page = 1
        idx.pages = {0, 1} if complete else {0}
        idx.save()
        for crc, text in (
            (CRC["FIRERED"], FIRERED_EXPORT),
            (CRC["EMERALD"], EMERALD_EXPORT),
            (0x33333333, SAPPHIRE_EXPORT),
        ):
            cheats.write_file(cheats.export_path(self.cache, crc), text.encode())

    def run_tool(self, *args):
        argv = [
            str(self.sd),
            "--cache",
            str(self.cache),
            "--downloads",
            str(self.downloads),
            "--no-browser",
        ]
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            status = cheats.main(argv + list(args))
        return status, out.getvalue(), err.getvalue()

    def card_file(self, path):
        return self.sd / "torii" / "cheats" / path

    def test_exact_and_hacks(self):
        self.fill_cache()
        status, out, err = self.run_tool()
        self.assertEqual(status, 0, err)
        fire = self.card_file("GBA/FireRed.cht").read_text()
        lines = fire.split("\n")
        self.assertEqual(lines[0], f"# for crc32 {CRC['FIRERED']:08X}")
        self.assertIn("GameHacking.org", lines[1])
        self.assertIn("https://gamehacking.org/game/5633", lines[1])
        got = cheats.parse_cht(fire)
        self.assertEqual(
            [(c[0], c.extra.get("format")) for c in got],
            [
                ("Master Ball", "PARv3 raw"),  # decrypted (decrypt)
                ("Money", None),
                ("Patch in the changed block", None),
                ("Patch outside it", None),
            ],
        )
        self.assertEqual(
            got[0][1], "02225840 00000001"
        )  # PARv3 raw: a halfword write of 1
        self.assertIn(
            "/GBA/FireRed.gba: /torii/cheats/GBA/FireRed.cht: 4 cheats from", out
        )
        self.assertIn("/GBA/FireRed.gba: ???: no name", out)
        self.assertIn("/GBA/FireRed.gba: Master Ball: the same as Master Ball", out)
        # A light hack: the base's cheats, less the patch the hack changed
        light = self.card_file("GBA/Hacks/Light.cht").read_text()
        self.assertTrue(light.startswith(f"# for crc32 {CRC['LIGHT']:08X}\n"))
        self.assertIn("its base game, Pokemon - FireRed Version (USA)", light)
        self.assertEqual(
            [c[0] for c in cheats.parse_cht(light)],
            [
                "Master Ball (base game)",
                "Money (base game)",
                "Patch outside it (base game)",
            ],
        )
        self.assertIn("light hack of Pokemon - FireRed Version (USA)", out)
        self.assertIn("94% the same): 3 cheats, 1 ROM patch dropped", out)
        self.assertIn(
            "Patch in the changed block (base game): patches ROM bytes the hack changed",
            out,
        )
        self.assertIn(
            "hack too different:\n  /GBA/Hacks/Heavy.gba: 6% of Pokemon - FireRed", out
        )
        self.assertIn("ambiguous base:\n  /GBA/Hacks/Ruby Hack.gba:", out)
        self.assertIn("AXVE version 0 could be 11111111", out)
        self.assertIn(
            "base dump not on card:\n  /GBA/Hacks/Sapphire Hack.gba: Pokemon - Sapphire Version (USA) (33333333)",
            out,
        )
        self.assertIn("not fetched yet:\n  /GBA/Lonely.gba", out)
        self.assertIn("no cheats on GameHacking:\n  /GBA/No Codes.gba", out)
        for path in (
            "Hacks/Heavy",
            "Hacks/Ruby Hack",
            "Hacks/Sapphire Hack",
            "Lonely",
            "No Codes",
        ):
            self.assertFalse(self.card_file(f"GBA/{path}.cht").exists(), path)
        self.assertEqual(
            (self.sd / "CHEAT" / "FireRed.cht").read_text(), "[Old]\nON=1,2\n"
        )
        # A second run changes nothing
        status, out, _ = self.run_tool()
        self.assertNotIn("\nwritten:", out)
        self.assertNotIn("\nupdated:", out)
        self.assertIn("unchanged:\n  /GBA/Emerald.gba", out)

    def test_base_game_flag(self):
        """--base-game: the heavy hack too, and a base off the card without
        its ROM patches."""
        self.fill_cache()
        status, out, err = self.run_tool("--base-game")
        self.assertEqual(status, 0, err)
        heavy = cheats.parse_cht(self.card_file("GBA/Hacks/Heavy.cht").read_text())
        # Its first block is the base's: the patch there stays
        self.assertEqual(
            [c[0] for c in heavy],
            [
                "Master Ball (base game)",
                "Money (base game)",
                "Patch outside it (base game)",
            ],
        )
        sapphire = self.card_file("GBA/Hacks/Sapphire Hack.cht").read_text()
        self.assertTrue(
            sapphire.startswith(f"# for crc32 {CRC['SAPPHIRE_HACK']:08X}\n")
        )
        self.assertEqual(
            [c[0] for c in cheats.parse_cht(sapphire)], ["Shiny (base game)"]
        )
        self.assertIn(
            "Patch (base game): patches the ROM, and the base ROM isn't on the card",
            out,
        )
        self.assertIn("ambiguous base:", out)

    def test_similar(self):
        self.fill_cache()
        _status, out, _ = self.run_tool("--similar", "95")
        self.assertIn("hack too different:\n  /GBA/Hacks/Heavy.gba", out)
        self.assertIn("/GBA/Hacks/Light.gba: 94% of", out)

    def test_index_incomplete(self):
        """Until every index page is fetched, an unlisted CRC isn't known to
        be a hack."""
        self.fill_cache(complete=False)
        _status, out, _ = self.run_tool()
        self.assertIn(
            "not fetched yet:\n  /GBA/Hacks/Heavy.gba: GameHacking.org's index isn't all fetched",
            out,
        )

    def test_custom_wins(self):
        self.fill_cache()
        custom = cheats.custom_path(self.cache, CRC["FIRERED"])
        custom.parent.mkdir(parents=True)
        custom.write_text(
            'cheats = 1\ncheat0_desc = "Own list"\ncheat0_code = "02000000:01"\n'
            'cheat0_enable = true\ncheat0_format = "VBA"\n'
        )
        status, out, err = self.run_tool("--only", f"{CRC['FIRERED']:08x}")
        self.assertEqual(status, 0, err)
        text = self.card_file("GBA/FireRed.cht").read_text()
        self.assertTrue(
            text.startswith(f"# for crc32 {CRC['FIRERED']:08X}\n# Cheats from custom/")
        )
        self.assertEqual(
            cheats.parse_cht(text),
            [cheats.Cheat("Own list", "02000000:01", False, {"format": "VBA"})],
        )
        self.assertIn("1 GBA game: 1 written", out)

    PICKED = (
        "cheats = 3\n\n"
        'cheat0_desc = "Money"\ncheat0_code = "02025840:99"\ncheat0_enable = true\n\n'
        'cheat1_desc = "My own"\ncheat1_code = "02001234:FF"\ncheat1_enable = false\ncheat1_user = true\n\n'
        'cheat2_desc = "Old libretro"\ncheat2_code = "AAAA0000 0001"\ncheat2_enable = false\n'
    )

    def update(self, main=None, bak=None):
        self.fill_cache()
        target = self.card_file("GBA/Emerald.cht")
        target.parent.mkdir(parents=True)
        if main is not None:
            target.write_bytes(main)
        if bak is not None:
            Path(str(target) + ".bak").write_bytes(bak)
        status, out, err = self.run_tool("--only", f"{CRC['EMERALD']:08X}")
        self.assertEqual(status, 0, err)
        text = target.read_text()
        self.assertNotIn("#crc32", text)  # written as edited by hand
        return text, out

    def test_update_keeps_yours(self):
        """A file this tool didn't write (an old libretro one): the source's
        cheats, then every code it lacks; the picks kept."""
        text, out = self.update(main=safe(self.PICKED), bak=safe("cheats = 0\n"))
        got = cheats.parse_cht(text)
        self.assertEqual(
            [c[0] for c in got], ["Rare Candy", "Money", "My own", "Old libretro"]
        )
        self.assertTrue(got[1][2])
        self.assertEqual(got[2].extra, {"user": "true"})
        self.assertIn(
            "1 cheat from https://gamehacking.org/game/5630, 3 of your own, 1 picked",
            out,
        )
        bak = Path(str(self.card_file("GBA/Emerald.cht")) + ".bak")
        self.assertEqual(bak.read_bytes(), safe("cheats = 0\n"))

    def test_update_own_file(self):
        """A file this tool wrote: an unpicked cheat the source no longer
        has goes; your own codes and picks stay."""
        text, out = self.update(
            main=safe(f"# for crc32 {CRC['EMERALD']:08X}\n" + self.PICKED)
        )
        got = cheats.parse_cht(text)
        self.assertEqual([c[0] for c in got], ["Rare Candy", "Money", "My own"])
        self.assertIn("1 old cheat the source no longer has left out", out)

    def test_update_from_bak(self):
        text, _ = self.update(main=b"", bak=safe(self.PICKED))
        self.assertIn("My own", text)

    def test_unchanged_kernel_file(self):
        """A file the kernel wrote back (its CRC line added, a pick changed
        in place) isn't rewritten."""
        self.fill_cache()
        self.run_tool("--only", f"{CRC['EMERALD']:08X}")
        target = self.card_file("GBA/Emerald.cht")
        # The kernel adds a picked cheat's enable line at the end
        picked = target.read_text() + "cheat0_enable = true\n"
        target.write_bytes(safe(picked))
        _status, out, _ = self.run_tool("--only", f"{CRC['EMERALD']:08X}")
        self.assertIn("1 GBA game: 1 unchanged", out)
        self.assertEqual(target.read_bytes(), safe(picked))

    def test_dry_run(self):
        self.fill_cache()
        status, out, err = self.run_tool("--dry-run")
        self.assertEqual(status, 0, err)
        self.assertIn("2 would be written", out)
        self.assertIn("(would be written)", out)
        self.assertFalse((self.sd / "torii").exists())

    def test_crc_txt(self):
        """The CRC comes from crc.txt where its record holds."""
        self.fill_cache()
        file = self.sd / "GBA" / "Lonely.gba"
        torii = self.sd / "torii"
        torii.mkdir()
        names_db.write_safe(
            torii / names_db.CRC_FILE,
            names_db.CRC_HEADER
            + f"{CRC['EMERALD']:08X}\t{len(LONELY)}\t{names_db.fat_date(file)}\t/GBA/Lonely.gba\n",
        )
        status, _out, err = self.run_tool("--only", f"{CRC['EMERALD']:08X}")
        self.assertEqual(status, 0, err)
        self.assertTrue(self.card_file("GBA/Lonely.cht").exists())

    def test_no_card(self):
        self.sd = self.sd / "nope"
        status, _, err = self.run_tool()
        self.assertEqual(status, 1)
        self.assertIn("isn't a folder", err)


class Fetching(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.cache = base / "cache"
        self.downloads = base / "downloads"
        self.downloads.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def bundle(self, name, **fields):
        b = {
            "format": "torii-gamehacking",
            "version": 1,
            "run": "R",
            "id": name,
            "done": False,
        }
        b.update(fields)
        path = self.downloads / f"torii-gamehacking-{name}.json"
        path.write_text(json.dumps(b))
        return path

    def test_import(self):
        index = cheats.Index(self.cache)
        path = self.bundle(
            "R-1",
            lastPage=76,
            pages=[0, 1],
            index={
                "dd88761c": {
                    "crc": "DD88761C",
                    "id": 5633,
                    "name": "(USA)",
                    "title": "FireRed",
                    "serial": "AGB-BPRE-USA",
                    "size": "16M",
                    "codes": 527,
                },
                "BAD": {"id": 1},
            },
            exports={
                "DD88761C": FIRERED_EXPORT,
                "12345678": "<html>Just a moment</html>",
            },
        )
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            _bundle, got = cheats.import_bundle(path, self.cache, index)
        self.assertEqual(got, 1)
        self.assertIn("12345678 is a web page", err.getvalue())
        again = cheats.Index(self.cache)
        self.assertEqual(again.entries["DD88761C"]["id"], 5633)
        self.assertNotIn("BAD", again.entries)
        self.assertEqual(
            (again.last_page, again.pages, again.imported), (76, {0, 1}, ["R-1"])
        )
        self.assertEqual(
            cheats.export_path(self.cache, 0xDD88761C).read_text(), FIRERED_EXPORT
        )
        self.assertFalse(again.complete())
        bad = self.downloads / "torii-gamehacking-x.json"
        bad.write_text('{"format": "other"}')
        self.assertEqual(
            cheats.import_bundle(bad, self.cache, index),
            (None, "not a torii-gamehacking bundle"),
        )

    def test_watch(self):
        """Bundles are imported once each, until this run's last one."""
        index = cheats.Index(self.cache)
        index.imported = ["OLD-1"]
        self.bundle("OLD-1", exports={"AAAAAAAA": "# A\n02000000:01\n"})
        self.bundle("R-1", exports={"DD88761C": FIRERED_EXPORT})
        out = io.StringIO()
        polls = []

        def sleep(_):
            polls.append(1)
            if len(polls) == 2:
                self.bundle(
                    "R-2",
                    done=True,
                    stopped="stopped by you",
                    exports={"BBBBBBBB": EMERALD_EXPORT},
                )

        with contextlib.redirect_stdout(out):
            got = cheats.watch(
                self.downloads, self.cache, index, "R", sleep=sleep, rounds=10
            )
        self.assertEqual(got["id"], "R-2")
        self.assertEqual(index.imported, ["OLD-1", "R-1", "R-2"])
        self.assertFalse(cheats.export_path(self.cache, 0xAAAAAAAA).exists())
        self.assertTrue(cheats.export_path(self.cache, 0xBBBBBBBB).exists())
        self.assertIn("the harvester stopped: stopped by you", out.getvalue())
        # Nothing new: stops after its rounds
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertIsNone(
                cheats.watch(
                    self.downloads,
                    self.cache,
                    index,
                    "R",
                    sleep=lambda _: None,
                    rounds=3,
                )
            )

    def test_harvest_script(self):
        """The wanted dumps: games not cached, a hack's base once the index
        is complete, extra CRCs; none listing no codes."""
        index = cheats.Index(self.cache)
        index.entries = dict(INDEX)
        index.last_page, index.pages = 1, {0, 1}
        cheats.write_file(
            cheats.export_path(self.cache, CRC["EMERALD"]), b"# x\n02000000:01\n"
        )
        games = [
            cheats.Game("/a.gba", "", CRC["FIRERED"], "BPRE", 0),
            cheats.Game("/b.gba", "", CRC["EMERALD"], "BPEE", 0),
            cheats.Game("/c.gba", "", CRC["NOCODES"], "NOCO", 0),
            cheats.Game("/d.gba", "", 0x0BADF00D, "AXPE", 0),  # Sapphire's hack
            cheats.Game("/e.gba", "", 0x0BADF00E, "AXVE", 0),  # ambiguous
        ]
        want = cheats.wanted(games, [0x12345678], self.cache, index, False)
        self.assertEqual(
            want,
            [
                {"crc": f"{CRC['FIRERED']:08X}", "code": None, "version": 0},
                {"crc": "33333333", "code": None, "version": 0},
                {"crc": "12345678", "code": None, "version": 0},
            ],
        )
        # --only CRC=ID: the game ID goes to the harvester
        self.assertEqual(
            cheats.parse_only("dd88761c=5633, 12345678"),
            ({0xDD88761C, 0x12345678}, {0xDD88761C: 5633}),
        )
        with self.assertRaises(cheats.CheatsError):
            cheats.parse_only("DD88761C=x")
        self.assertEqual(
            cheats.wanted(
                [], [0xDD88761C], self.cache, index, False, {0xDD88761C: 5633}
            ),
            [{"crc": "DD88761C", "code": None, "version": 0, "id": 5633}],
        )
        # An incomplete index: the hack's own CRC, with its header for the
        # harvester to find the base by
        index.pages = {0}
        want = cheats.wanted(games[3:4], [], self.cache, index, False)
        self.assertEqual(want, [{"crc": "0BADF00D", "code": "AXPE", "version": 0}])
        script = cheats.harvest_script(want, index, self.cache, 30, "R", False)
        config = json.loads(re.search(r"const CONFIG = (\{.*?\});\n", script).group(1))
        self.assertEqual(config["wanted"], want)
        self.assertEqual(config["cached"], [f"{CRC['EMERALD']:08X}"])
        self.assertEqual(
            (config["delay"], config["run"], config["pagesDone"], config["lastPage"]),
            (30, "R", [0], 1),
        )
        if NODE:
            f = Path(self.tmp.name) / "harvest.js"
            f.write_text(script)
            subprocess.run([NODE, "--check", str(f)], check=True)

    def test_fetch_flow(self):
        """--gamehacking writes the harvester and waits for its bundles;
        --import imports by hand."""
        sd = Path(self.tmp.name) / "sd"
        (sd / "GBA").mkdir(parents=True)
        (sd / "GBA" / "Emerald.gba").write_bytes(EMERALD)
        bundle = self.bundle(
            "H-1",
            done=True,
            run="other",
            exports={f"{CRC['EMERALD']:08X}": EMERALD_EXPORT},
            index={f"{CRC['EMERALD']:08X}": INDEX[f"{CRC['EMERALD']:08X}"]},
        )
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            status = cheats.main(
                [
                    str(sd),
                    "--cache",
                    str(self.cache),
                    "--import",
                    str(bundle),
                    "--dry-run",
                ]
            )
        self.assertEqual(status, 0)
        self.assertIn("imported torii-gamehacking-H-1.json: 1 export", out.getvalue())
        self.assertIn("1 would be written", out.getvalue())
        # Everything cached: nothing to fetch, no waiting
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            status = cheats.main(
                [
                    str(sd),
                    "--cache",
                    str(self.cache),
                    "--gamehacking",
                    "--no-browser",
                    "--downloads",
                    str(self.downloads),
                ]
            )
        self.assertEqual(status, 0)
        self.assertIn("nothing to fetch", out.getvalue())


class Romhack(unittest.TestCase):
    def test_similarity(self):
        self.assertEqual(cheats.similarity(FIRERED, FIRERED), 1.0)
        self.assertEqual(cheats.similarity(FIRERED, LIGHT), 15 / 16)
        self.assertEqual(cheats.similarity(FIRERED, FIRERED[:0x8000]), 0.5)

    def test_patch_unchanged(self):
        self.assertTrue(cheats.patch_unchanged(FIRERED, LIGHT, (0x400, 0x1234, 0xFFFF)))
        self.assertFalse(
            cheats.patch_unchanged(FIRERED, LIGHT, (0x2100, 0x1234, 0xFFFF))
        )
        # Only the bytes the mask writes count
        changed = bytearray(FIRERED)
        changed[0x401] ^= 0xFF
        self.assertTrue(
            cheats.patch_unchanged(FIRERED, bytes(changed), (0x400, 0x12, 0x00FF))
        )
        self.assertFalse(
            cheats.patch_unchanged(FIRERED, bytes(changed), (0x400, 0x1234, 0xFFFF))
        )
        self.assertFalse(
            cheats.patch_unchanged(FIRERED, FIRERED[:0x400], (0x400, 0x12, 0x00FF))
        )

    def test_sift(self):
        """sift drops what the kernel would grey, keeping the first of
        cheats alike."""
        kept, dropped = cheats.sift(
            [
                cheats.Cheat("Lives", "32001010 0003", False),
                cheats.Cheat("Nothing", "72001000 0001", False),
                cheats.Cheat("Lives again", "32001010 0003", False),
                cheats.Cheat("Garbage", "ZZZZ", False),
                cheats.Cheat("Fill in", "3300075C+00??", False),
                cheats.Cheat("Patch", "08001000:1234", False),
            ]
        )
        self.assertEqual([c[0] for c in kept], ["Lives", "Fill in", "Patch"])
        self.assertEqual(
            dropped,
            [
                ("Nothing", "does nothing"),
                ("Lives again", "does what an earlier cheat does"),
                ("Garbage", "the kernel can't read it"),
            ],
        )

    def test_sift_master_codes(self):
        """A master code goes, as Settings' Clean up cheats removes it, unless
        a cheat after it reads otherwise without its key: then it stays."""
        master = cheats.Cheat(
            "(M) Must Be On", "930CFE0E61A7+5E4E1F94523A+DB3A64369A43", False
        )
        kept, dropped = cheats.sift(
            [master, cheats.Cheat("Lives", "32001010 0003", False)]
        )
        self.assertEqual([c[0] for c in kept], ["Lives"])
        self.assertEqual(dropped, [("(M) Must Be On", "a master code")])
        encrypted = cheats.Cheat("Infinite HP", "D86098902576+777F4707DA29", False)
        kept, dropped = cheats.sift([master, encrypted])
        self.assertEqual([c[0] for c in kept], ["(M) Must Be On", "Infinite HP"])
        self.assertEqual(dropped, [])

    def test_decrypt(self):
        """A code encrypted under a master code's CodeBreaker key is written
        plain, the master code dropped: it decodes alone as it did."""
        out, n, dropped = cheats.decrypt(
            [
                cheats.Cheat(
                    "(M) Must Be On", "930CFE0E61A7+5E4E1F94523A+DB3A64369A43", False
                ),
                cheats.Cheat("Infinite HP", "D86098902576+777F4707DA29", True),
            ]
        )
        self.assertEqual(n, 1)
        self.assertEqual([c[0] for c in out], ["Infinite HP"])
        self.assertEqual(out[0].extra.get("format"), "CodeBreaker")
        self.assertTrue(out[0][2])  # the pick stays
        self.assertNotEqual(out[0][1], "D86098902576+777F4707DA29")
        self.assertEqual(
            [why for _, why in dropped],
            ["a master code: not needed once the codes are decrypted"],
        )
        alone = cheats.decode_cheats(out)
        self.assertEqual(alone[0][0], cheats.ST_OK)

    def test_index_and_picks_by_hash(self):
        """A file past one read gets the kernel's index; a pick of the old
        file carries to the new by what it does, its code written anew."""
        many = [
            cheats.Cheat(f"W{i}", f"{0x02000000 + 2 * i:08X}:0001", i == 7)
            for i in range(cheats.CHEATS_MAX + 10)
        ]
        text = cheats.format_cht(many)
        index = cheats.index_text(text)
        self.assertIsNotNone(index)
        lines = index.splitlines()
        self.assertEqual(lines[0], "torii-cheat-index 1")
        self.assertEqual(lines[1], f"file {len(text):X}")
        windows = [line for line in lines if line.startswith("w ")]
        self.assertEqual(len(windows), -(-len(many) // cheats.WINDOW_CHEATS))
        self.assertEqual(windows[0].split()[1], "0")
        self.assertEqual(
            int(windows[1].split()[3], 16), cheats.WINDOW_CHEATS
        )  # its first cheat's number
        picks = [line for line in lines if line.startswith("p ")]
        self.assertEqual([p.split()[1] for p in picks], ["7"])
        # The same write as a VBA code and as CodeBreaker: the pick carries
        old = [cheats.Cheat("Lives", "02001000:0063", True)]
        new = [cheats.Cheat("Lives", "82001000 0063", False)]
        got, rest = cheats.carry_picks(new, old, None)
        self.assertTrue(got[0][2])
        self.assertEqual(rest, [])
        # With an index, its picks are the file's: the text's stale pick isn't
        got, rest = cheats.carry_picks(new, old, set())
        self.assertFalse(got[0][2])
        got, _ = cheats.carry_picks(new, [], cheats.index_picks(index))
        self.assertFalse(got[0][2])  # the index's pick is another program
        # A name given on the cart carries by program too, and the old cheat
        # leaves old (else merge would keep it as the user's)
        old = [cheats.Cheat("My lives", "02001000:0063", False, {"renamed": "true"})]
        got, rest = cheats.carry_picks(new, old, None)
        self.assertEqual(got[0][0], "My lives")
        self.assertTrue(cheats.is_true(got[0].extra["renamed"]))
        self.assertEqual(rest, [])

    def test_sift_in_parts(self):
        """A list past one file's limits is read in parts as one: a cheat
        alike across the parts is still a duplicate."""
        many = [
            cheats.Cheat(f"W{i}", f"{0x02000000 + 2 * i:08X}:0001", False)
            for i in range(cheats.PART_CHEATS + 5)
        ]
        many.append(cheats.Cheat("W0 again", "02000000:0001", False))
        kept, dropped = cheats.sift(many)
        self.assertEqual(len(kept), cheats.PART_CHEATS + 5)
        self.assertEqual(dropped, [("W0 again", "does what an earlier cheat does")])
        # Past the decoder's table of cheats seen (CHEATFILE_SEEN_SLOTS), and
        # in another part than its first: the kernel's windows may hold both
        many = [
            cheats.Cheat(f"W{i}", f"{0x02000000 + 2 * i:08X}:0001", False)
            for i in range(2 * cheats.CHEATS_MAX + 100)
        ]
        late = 2 * cheats.CHEATS_MAX + 50
        many.append(
            cheats.Cheat("Late again", f"{0x02000000 + 2 * late:08X}:0001", False)
        )
        kept, dropped = cheats.sift(many)
        self.assertEqual(len(kept), 2 * cheats.CHEATS_MAX + 100)
        self.assertEqual(dropped, [("Late again", "does what an earlier cheat does")])

    def test_rom_patches(self):
        """The kernel's decoder (tools/cheat_patches.c) finds the patches."""
        got = cheats.rom_patches(
            [
                cheats.Cheat("RAM", "02001000:12", False),
                cheats.Cheat("ROM", "08001000:1234", False),
                cheats.Cheat("Odd", "08001001:56", False),
            ]
        )
        self.assertEqual(
            got, [[], [(0x1000, 0x1234, 0xFFFF)], [(0x1000, 0x5600, 0xFF00)]]
        )


if __name__ == "__main__":
    unittest.main(verbosity=1)
