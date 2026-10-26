#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Tests for tools/cheats.py, run by `just test-host`: the name matching,
the cheat files it writes and merges on a fixture card, and its fetcher
against a local server standing in for GitHub. No network."""

import contextlib
import http.server
import io
import json
import sys
import tempfile
import threading
import unittest
import urllib.parse
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import cheats
import names_db


def rom(seed, code=None):
    """A small file's bytes, with a GBA game code at 0xAC and its version
    at 0xBC: code is "AGSE" (version 0) or "AGSE:1"."""
    data = bytearray((i * seed + seed) & 0xFF for i in range(1024))
    code, _, version = (code or "").partition(":")
    data[0xAC:0xB0] = code.encode("ascii") if code else b"\0\0\0\0"
    data[0xBC] = int(version or 0)
    return bytes(data)


# The card's games: (card path, seed, header code[:version], the dat's name
# or None for a game the dat doesn't list)
CARD = [
    ("/GBA/RPG/Golden Sun.gba", 1, "AGSE", "Golden Sun (USA, Europe)"),
    (
        "/GBA/Mario & Luigi.gba",
        2,
        "A88E",
        "Mario & Luigi - Superstar Saga (USA, Europe)",
    ),
    ("/GBA/Fusion.gba", 3, "AMTP", "Metroid Fusion (Europe) (En,Fr,De,Es,It)"),
    ("/GBA/Wars EU.gba", 4, "AWRP", "Advance Wars (Europe) (En,Fr,De,Es)"),
    ("/GBA/Drill Dozer VC.gba", 5, "V49E", "Drill Dozer (USA) (Virtual Console)"),
    ("/GBA/Ruby.gba", 6, "AXVE", "Pokemon - Ruby Version (USA, Europe)"),
    ("/GBA/Sonic Beta.gba", 7, "SONE", "Sonic Advance (USA) (Beta)"),
    ("/GBA/Lonely.gba", 8, "LONE", "Lonely Game (USA)"),
    ("/GBA/Hack.gba", 9, "AGSE", None),
    ("/GBA/No Header.gba", 10, None, "Drill Dozer (USA) (Virtual Console) (Alt)"),
    # libretro's only FireRed file is for revision 1
    ("/GBA/FireRed.gba", 11, "BPRE", "Pokemon - FireRed Version (USA)"),
    # "Leaf Green" in libretro's name, GoodTools' tags: its region and version
    (
        "/GBA/LeafGreen.gba",
        12,
        "BPGE:1",
        "Pokemon - LeafGreen Version (USA, Europe) (Rev 1)",
    ),
]

# The dat's other games: (name, serial), CRCs no file has
DAT_EXTRA = [
    ("Advance Wars (USA)", "AWRE"),
    ("Drill Dozer (USA)", "V49E"),
    ("Drill Dozer (Japan)", "V49J"),
    ("Pokemon - Ruby Version (Germany)", "AXVD"),
    ("Sonic Advance (USA)", "SONE"),
    ("Sonic Advance (USA, Australia)", "SONE"),
    ("Pokemon - FireRed Version (USA, Europe) (Rev 1)", "BPRE"),
    ("Pokemon - LeafGreen Version (USA, Europe)", "BPGE"),
]


def cht(*cheats_):
    """A libretro cheat file: (desc, code, enabled) each."""
    return cheats.format_cht(list(cheats_))


# libretro's files: name: text
DATABASE = {
    "Golden Sun (USA, Europe).cht": cht(
        ("Infinite HP", "42000538+07CF+00000004+014C", False),
        ("Infinite Money", "82000250+E0FF+82000252+05F5", False),
        ("Max Level", "32000500 0063", False),
    ),
    "Mario _ Luigi - Superstar Saga (USA, Europe) (Code Breaker).cht": cht(
        ("Max Coins", "82003E3C 270F", False),
    ),
    "Mario _ Luigi - Superstar Saga (USA, Europe) (Gameshark).cht": cht(
        ("Max Coins (GS)", "1A2B3C4D 5E6F7A8B", False),
        ("Max Coins", "82003E3C 270F", False),  # also in the other file
    ),
    "Metroid Fusion (Europe) (Code Breaker).cht": cht(
        ("Missiles", "83003000 00FF", False)
    ),
    "Advance Wars (USA) (Code Breaker).cht": cht(("Funds", "83001000 FFFF", False)),
    "Drill Dozer (USA) (CodeBreaker).cht": cht(("Gears", "33000100 0003", False)),
    "Drill Dozer (Japan) (Code Breaker).cht": cht(("Gears J", "33000200 0003", False)),
    "Pokemon - Ruby Version (Code Breaker)(USA).cht": cht(
        ("Rare Candy", "83005000 0044", False)
    ),
    "Pokemon - Ruby Version (Germany) (Code Breaker).cht": cht(
        ("Sonderbonbon", "83005100 0044", False)
    ),
    "Sonic Advance (USA) (Code Breaker).cht": cht(("Rings", "32000000 0063", False)),
    "Sonic Advance (USA, Australia) Game Shark.cht": cht(
        ("Rings 1", "32000010 0063", False)
    ),
    "Pokemon - FireRed Version (USA, Europe) (Rev 1).cht": cht(
        ("Master Ball", "82025840 0001", False)
    ),
    "Pokemon - Leaf Green Version (V1.1)(USA)(Code Breaker).cht": cht(
        ("Master Ball LG", "82025800 0001", False)
    ),
}


def dat_text(games):
    """A clrmamepro dat: (name, crc, size, serial) each."""
    out = ['clrmamepro (\n\tname "Nintendo - Game Boy Advance"\n\tversion "test"\n)\n']
    for name, crc, size, serial in games:
        quoted = name.replace('"', '\\"')
        ser = f'\n\tserial "{serial}"' if serial else ""
        out.append(
            f'\ngame (\n\tname "{quoted}"{ser}\n'
            f'\trom ( name "{quoted}.gba" size {size} crc {crc:08X} )\n)\n'
        )
    return "".join(out)


def safe(text):
    """A file as the kernel's safe-file writes leave it: text and a CRC line."""
    data = text.encode()
    return data + b"#crc32 %08x\n" % zlib.crc32(data)


class Server:
    """GitHub stand-in: the contents API's listing and raw files."""

    def __init__(self, files):
        self.files = dict(files)
        self.requests = []
        server = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                path = urllib.parse.unquote(self.path)
                server.requests.append((path, self.headers.get("User-Agent")))
                if path == "/api/" + cheats.CHT_DIR:
                    body = json.dumps(
                        [
                            {
                                "name": n,
                                "type": "file",
                                "sha": cheats.blob_sha(t.encode()),
                            }
                            for n, t in server.files.items()
                        ]
                        + [{"name": "Pokemon Unbound", "type": "dir", "sha": "x"}]
                    ).encode()
                elif path.startswith("/raw/" + cheats.CHT_DIR + "/"):
                    name = path.rsplit("/", 1)[1]
                    if name not in server.files:
                        self.send_error(404)
                        return
                    body = server.files[name].encode()
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *args):
                pass

        self.httpd = http.server.HTTPServer(("127.0.0.1", 0), Handler)
        self.url = f"http://127.0.0.1:{self.httpd.server_address[1]}/"
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()


class Names(unittest.TestCase):
    def test_strip_type(self):
        for stem, name in (
            ("Golden Sun (USA, Europe)", "Golden Sun (USA, Europe)"),
            ("Metroid Fusion (World) (Code Breaker)", "Metroid Fusion (World)"),
            ("Drill Dozer (USA) (CodeBreaker)", "Drill Dozer (USA)"),
            ("Mario _ Luigi (USA) (Gameshark)", "Mario _ Luigi (USA)"),
            ("Spider-Man 3 (USA) Game Shark", "Spider-Man 3 (USA)"),
            (
                "Pokemon - Ruby Version (Code Breaker)(USA)",
                "Pokemon - Ruby Version (USA)",
            ),
            (
                "Iridion 3D (USA, Europe) (Code Breaker) (diff)",
                "Iridion 3D (USA, Europe)",
            ),
        ):
            self.assertEqual(cheats.strip_type(stem), name)

    def test_key(self):
        """libretro writes "&" and "/" as "_", but not always: all compare alike."""
        self.assertEqual(cheats.key("Mario & Luigi"), cheats.key("Mario _ Luigi"))
        self.assertEqual(
            cheats.key("Final Fantasy I & II"), cheats.key("final fantasy i _ ii")
        )
        self.assertEqual(cheats.key("A/B (USA)"), cheats.key("A_B (USA)"))
        self.assertNotEqual(cheats.key("Golden Sun"), cheats.key("Golden Sun 2"))

    def test_region_and_revision(self):
        self.assertEqual(cheats.region_letters("Game (USA, Europe)"), "E")
        self.assertEqual(cheats.region_letters("Game (Europe) (En,Fr)"), "PXYZ")
        self.assertEqual(
            cheats.region_letters("Pokemon - Rubin-Edition (G) (V1.1)"), "D"
        )
        self.assertIn("P", cheats.region_letters("Metroid Fusion (World)"))
        self.assertEqual(cheats.revision("Pokemon - Rubin-Edition (G) (V1.1)"), 1)
        self.assertEqual(cheats.revision("Game (USA) (Rev 2)"), 2)
        self.assertEqual(cheats.revision("Game (USA)"), 0)

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
        merged, own = cheats.merge(
            [
                ("Infinite Money", "82000250+E0FF", False),
                ("HP", "42000538+07CF", False),
            ],
            old,
        )
        self.assertEqual(own, 2)
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
        # A quote missing at one end, as in one of libretro's files
        self.assertEqual(
            cheats.parse_cht('cheat0_desc = Less HP"\ncheat0_code = "1234 5678"\n'),
            [("Less HP", "1234 5678", False)],
        )

    def test_torii_keys(self):
        """The keys the kernel's code keypad adds (cheatN_format, cheatN_user)
        are read, kept through a merge and written back as the kernel writes
        them; a placeholder the keypad filled in stays in its place."""
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
        self.assertEqual(old[0].extra, {})
        self.assertNotEqual(old[1], cheats.Cheat("Typed", "02001000 00000063", False))
        merged, own = cheats.merge(
            [
                ("Infinite Money", "82000250+E0FF", False),
                ("Pick a level", "3300075C+00??", False),
                ("HP", "42000538+07CF", False),
            ],
            old,
        )
        self.assertEqual(own, 1)
        self.assertEqual(
            merged,
            [
                cheats.Cheat(
                    "Infinite Money", "82000250+E0FF", True, {"format": "CodeBreaker"}
                ),
                old[0],
                ("HP", "42000538+07CF", False),
                old[1],
            ],
        )
        text = cheats.format_cht(merged)
        self.assertIn(
            'cheat3_enable = false\ncheat3_format = "GSAv1 raw"\ncheat3_user = true\n',
            text,
        )
        self.assertEqual(cheats.parse_cht(text), merged)
        # A file of the database's alone merges as before: nothing filled in
        merged, own = cheats.merge([("Pick a level", "3300075C+00??", False)], [])
        self.assertEqual(merged, [("Pick a level", "3300075C+00??", False)])
        # A code the user typed under a placeholder's name stays the user's,
        # and the placeholder stays too
        typed = cheats.Cheat("Pick a level", "3300075C+0007", True, {"user": "true"})
        merged, own = cheats.merge([("Pick a level", "3300075C+00??", False)], [typed])
        self.assertEqual(own, 1)
        self.assertEqual(merged, [("Pick a level", "3300075C+00??", False), typed])


class Card(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.sd = base / "sd"
        self.dats = base / "dats"
        self.cache = base / "cache"
        games = []
        for path, seed, code, name in CARD:
            file = self.sd / path.lstrip("/")
            file.parent.mkdir(parents=True, exist_ok=True)
            data = rom(seed, code)
            file.write_bytes(data)
            if name:
                games.append(
                    (name, zlib.crc32(data), len(data), code and code.partition(":")[0])
                )
        games += [(n, 0xDEAD0000 + i, 1024, s) for i, (n, s) in enumerate(DAT_EXTRA)]
        self.dats.mkdir()
        (self.dats / names_db.CONSOLES[0][1]).write_text(dat_text(games), "utf-8")
        # /CHEAT, EZ-Flash's, is never touched
        (self.sd / "CHEAT").mkdir()
        (self.sd / "CHEAT" / "Golden Sun.cht").write_text("[Old]\nON=1,2\n")

    def tearDown(self):
        self.tmp.cleanup()

    def fill_cache(self, files=DATABASE):
        folder = self.cache / cheats.SYSTEM
        folder.mkdir(parents=True, exist_ok=True)
        listing = {}
        for name, text in files.items():
            (folder / name).write_text(text, "utf-8")
            listing[name] = cheats.blob_sha(text.encode())
        (self.cache / cheats.LISTING).write_text(json.dumps(listing), "utf-8")

    def run_tool(self, *args, url=None):
        argv = [str(self.sd), "--cache", str(self.cache), "--dats", str(self.dats)]
        if url:
            argv += [
                "--api-url",
                url + "api/",
                "--raw-url",
                url + "raw/",
                "--delay",
                "0",
            ]
        else:
            argv.append("--offline")
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            status = cheats.main(argv + list(args))
        return status, out.getvalue(), err.getvalue()

    def card_file(self, path):
        return self.sd / "torii" / "cheats" / path

    def test_matches(self):
        self.fill_cache()
        status, out, err = self.run_tool()
        self.assertEqual(status, 0, err)
        written = self.card_file("GBA/RPG/Golden Sun.cht")
        self.assertEqual(written.read_text(), DATABASE["Golden Sun (USA, Europe).cht"])
        self.assertFalse(Path(str(written) + ".bak").exists())
        self.assertIn("12 GBA games: 6 written, 0 updated, 0 unchanged", out)
        self.assertIn("1 without cheats in the database, 5 not matched", out)
        # "&" in the No-Intro name, "_" in libretro's; both of its files, the
        # code they share once
        mario = cheats.parse_cht(self.card_file("GBA/Mario & Luigi.cht").read_text())
        self.assertEqual([c[1] for c in mario], ["82003E3C 270F", "1A2B3C4D 5E6F7A8B"])
        # Without its language group
        self.assertIn("Missiles", self.card_file("GBA/Fusion.cht").read_text())
        # Title only, settled by the serial: the USA's, not Japan's
        self.assertIn('"Gears"', self.card_file("GBA/Drill Dozer VC.cht").read_text())
        # Title only, the file's name not in the dat: its region's letter
        self.assertIn("Rare Candy", self.card_file("GBA/Ruby.cht").read_text())
        self.assertIn("/GBA/Lonely.gba: Lonely Game (USA)", out)
        self.assertIn("/GBA/Hack.gba: CRC", out)
        self.assertIn("isn't in the dat", out)
        self.assertIn(
            "/GBA/Sonic Beta.gba: Sonic Advance (USA) (Beta): more than one file fits its game (SONE version 0)",
            out,
        )
        self.assertIn(
            "/GBA/Wars EU.gba: Advance Wars (Europe) (En,Fr,De,Es): cheats only for other versions (this is AWRP version 0; Advance Wars (USA): AWRE)",
            out,
        )
        # The header's version byte against the name's revision
        self.assertIn(
            "/GBA/FireRed.gba: Pokemon - FireRed Version (USA): cheats only for other "
            "versions (this is BPRE version 0; Pokemon - FireRed Version (USA, Europe) "
            "(Rev 1): BPRE)",
            out,
        )
        self.assertIn("Master Ball LG", self.card_file("GBA/LeafGreen.cht").read_text())
        # A title-only match needs the header's game code
        self.assertIn(
            "/GBA/No Header.gba: Drill Dozer (USA) (Virtual Console) (Alt): "
            "no game code in its header to pick between",
            out,
        )
        for path in ("Wars EU", "Sonic Beta", "Lonely", "Hack", "FireRed"):
            self.assertFalse(self.card_file(f"GBA/{path}.cht").exists())
        self.assertEqual(
            (self.sd / "CHEAT" / "Golden Sun.cht").read_text(), "[Old]\nON=1,2\n"
        )
        self.assertIn("CC BY-SA 4.0", out)
        # A second run changes nothing
        status, out, _ = self.run_tool()
        self.assertIn("0 written, 0 updated, 6 unchanged", out)

    def test_crc_txt(self):
        """The CRC comes from crc.txt where its record holds."""
        self.fill_cache()
        file = self.sd / "GBA" / "Lonely.gba"
        crc = zlib.crc32(rom(1, "AGSE"))  # Golden Sun's
        torii = self.sd / "torii"
        torii.mkdir()
        names_db.write_safe(
            torii / names_db.CRC_FILE,
            names_db.CRC_HEADER
            + f"{crc:08X}\t1024\t{names_db.fat_date(file)}\t/GBA/Lonely.gba\n",
        )
        status, _, err = self.run_tool()
        self.assertEqual(status, 0, err)
        self.assertTrue(self.card_file("GBA/Lonely.cht").exists())

    def update_case(self, main=None, bak=None):
        """Golden Sun's card file in its copies; returns its cheats after a run."""
        self.fill_cache()
        target = self.card_file("GBA/RPG/Golden Sun.cht")
        target.parent.mkdir(parents=True)
        if main is not None:
            target.write_bytes(main)
        if bak is not None:
            Path(str(target) + ".bak").write_bytes(bak)
        status, out, err = self.run_tool()
        self.assertEqual(status, 0, err)
        text = target.read_text()
        if "1 unchanged" not in out:
            self.assertNotIn("#crc32", text)  # written as edited by hand
        return cheats.parse_cht(text), out

    PICKED = (
        "cheats = 2\n\n"
        'cheat0_desc = "Max Level"\ncheat0_code = "32000500 0063"\ncheat0_enable = true\n\n'
        'cheat1_desc = "My own"\ncheat1_code = "02001234:FF"\ncheat1_enable = true\n'
    )
    EXPECTED = (
        ("Infinite HP", "42000538+07CF+00000004+014C", False),
        ("Infinite Money", "82000250+E0FF+82000252+05F5", False),
        ("Max Level", "32000500 0063", True),
        ("My own", "02001234:FF", True),
    )

    def test_update_crc_line(self):
        """NAME with a CRC line that holds (the kernel's write)."""
        got, out = self.update_case(main=safe(self.PICKED), bak=safe("cheats = 0\n"))
        self.assertEqual(got, list(self.EXPECTED))
        self.assertIn("1 updated", out)
        self.assertIn(
            "3 cheats from Golden Sun (USA, Europe).cht, 1 of your own, 2 picked", out
        )
        # NAME.bak is left alone
        bak = Path(str(self.card_file("GBA/RPG/Golden Sun.cht")) + ".bak")
        self.assertEqual(bak.read_bytes(), safe("cheats = 0\n"))

    def test_update_hand_edited(self):
        """NAME without a CRC line: edited by hand, read as it is."""
        got, _ = self.update_case(main=self.PICKED.encode(), bak=safe("cheats = 0\n"))
        self.assertEqual(got, list(self.EXPECTED))

    def test_update_from_bak(self):
        """An empty NAME (a write cut short): NAME.bak, whose CRC line holds."""
        got, _ = self.update_case(main=b"", bak=safe(self.PICKED))
        self.assertEqual(got, list(self.EXPECTED))

    def test_update_broken_bak(self):
        """NAME empty and NAME.bak's CRC line wrong: no picks to keep."""
        broken = safe(self.PICKED).replace(b"true", b"TRUE", 1)
        got, out = self.update_case(main=b"", bak=broken)
        self.assertEqual(got, [c[:2] + (False,) for c in self.EXPECTED[:3]])
        self.assertIn("6 written", out)

    def test_unchanged_kernel_file(self):
        """A file the kernel wrote with the same cheats isn't rewritten."""
        text = DATABASE["Golden Sun (USA, Europe).cht"]
        _, out = self.update_case(main=safe(text))
        self.assertIn("1 unchanged", out)

    def test_dry_run(self):
        self.fill_cache()
        status, out, err = self.run_tool("--dry-run")
        self.assertEqual(status, 0, err)
        self.assertIn("6 would be written", out)
        self.assertIn(
            "/GBA/RPG/Golden Sun.gba: /torii/cheats/GBA/RPG/Golden Sun.cht: 3 cheats",
            out,
        )
        self.assertFalse((self.sd / "torii").exists())

    def test_offline_without_cache(self):
        status, _, err = self.run_tool()
        self.assertEqual(status, 1)
        self.assertIn("--offline downloads nothing", err)

    def test_fetch(self):
        """The listing and the files are fetched once, one request at a time,
        with a User-Agent; --refresh gets a changed file again."""
        server = Server(DATABASE)
        try:
            status, out, err = self.run_tool(url=server.url)
            self.assertEqual(status, 0, err)
            paths = [p for p, _ in server.requests]
            self.assertEqual(paths[0], "/api/" + cheats.CHT_DIR)
            # Golden Sun, Mario & Luigi's two, Fusion, Drill Dozer's, Ruby's
            self.assertEqual(len(paths), 8)
            self.assertTrue(all(ua == cheats.USER_AGENT for _, ua in server.requests))
            self.assertIn("6 written", out)
            listing = json.loads((self.cache / cheats.LISTING).read_text())
            self.assertNotIn("Pokemon Unbound", listing)
            server.requests.clear()
            status, out, _ = self.run_tool(url=server.url)
            self.assertEqual(server.requests, [])
            self.assertIn("6 unchanged", out)
            server.files["Golden Sun (USA, Europe).cht"] = cht(
                ("New", "12345678 0001", False)
            )
            status, out, _ = self.run_tool("--refresh", url=server.url)
            self.assertEqual(len(server.requests), 2)
            self.assertIn(
                "/GBA/RPG/Golden Sun.gba: /torii/cheats/GBA/RPG/Golden Sun.cht: 1 cheat from",
                out,
            )
            # The old database cheats stay, as codes the new file lacks
            got = cheats.parse_cht(self.card_file("GBA/RPG/Golden Sun.cht").read_text())
            self.assertEqual(got[0], ("New", "12345678 0001", False))
            self.assertEqual(len(got), 4)
        finally:
            server.close()

    def test_no_card(self):
        self.sd = self.sd / "nope"
        status, _, err = self.run_tool()
        self.assertEqual(status, 1)
        self.assertIn("isn't a folder", err)


if __name__ == "__main__":
    unittest.main(verbosity=1)
