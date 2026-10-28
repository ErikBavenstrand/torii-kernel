#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""Tests for tools/art_scrape.py, run by `just test-host`: the match chain,
the polite fetcher and what lands on the card, against a local fixture HTTP
server standing in for thumbnails.libretro.com, with the fixture dats and
games of tests/names. No network."""

import contextlib
import http.server
import io
import socket
import struct
import sys
import tempfile
import threading
import unittest
import urllib.parse
import zlib
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.append(str(ROOT / "tests" / "names"))
import art
import art_scrape
import artpack
import fixture
import names_db

DATS = ROOT / "tests" / "names"
GBA_SYS = "Nintendo - Game Boy Advance"
PATCHED = "/GBA/Legend of Zelda, The - The Minish Cap (USA) [0badc0de].gba"
GBC_SYS = "Nintendo - Game Boy Color"
GB_SYS = "Nintendo - Game Boy"
NES_SYS = "Nintendo - Nintendo Entertainment System"


def png(colour):
    out = io.BytesIO()
    img = Image.new("RGB", (120, 160), colour)
    img.paste((255, 255, 255), (10, 10, 110, 40))
    img.save(out, "PNG")
    return out.getvalue()


def thumb(system, name):
    return f"/{system}/Named_Boxarts/{name}.png"


class Server:
    """A fixture thumbnail server: routes map a path to a PNG's bytes, an
    HTTP status, or a list of statuses and bytes given in turn."""

    def __init__(self, routes):
        self.routes = dict(routes)
        self.requests = []
        server = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                path = urllib.parse.unquote(self.path)
                server.requests.append((path, self.headers.get("User-Agent")))
                answer = server.routes.get(path, 404)
                if isinstance(answer, list):
                    answer = answer.pop(0) if len(answer) > 1 else answer[0]
                if isinstance(answer, int):
                    self.send_error(answer)
                    return
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Content-Length", str(len(answer)))
                self.end_headers()
                self.wfile.write(answer)

            def log_message(self, *args):
                pass

        self.httpd = http.server.HTTPServer(("127.0.0.1", 0), Handler)
        self.url = f"http://127.0.0.1:{self.httpd.server_address[1]}/"
        # A short poll, as shutdown() waits for the next one (0.5 s by default)
        self.thread = threading.Thread(
            target=self.httpd.serve_forever, args=(0.01,), daemon=True
        )
        self.thread.start()

    def paths(self):
        return [p for p, _ in self.requests]

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()


def crc_of(path, sd):
    """A card file's CRC as the kernel takes it (a NES game's without its iNES header)."""
    return f"{names_db.file_crc(Path(sd) / path.lstrip('/'))[0]:08X}"


def library_idx(games):
    """A library index (source/library.h, version art_scrape.library_idx.VERSION) listing games: (card
    path, console)."""
    folders, pool, recs = [], bytearray(), []

    def put(text):
        off = len(pool)
        pool.extend(text.encode("utf-8") + b"\0")
        return off

    folder_offs = {}
    for path, console in games:
        parent, name = path.rsplit("/", 1)
        parent += "/"
        if parent not in folder_offs:
            folder_offs[parent] = len(folders)
            folders.append(put(parent))
        recs.append((folder_offs[parent], console, 1024, put(name), 0))
    counts = [0] * 8
    for _, console in games:
        counts[console] += 1
    order = list(range(len(recs)))
    body = b"".join(struct.pack("<HHIII", *r) for r in recs)
    o = struct.pack(f"<{len(order)}H", *order)
    body += o + b"\0" * (-len(o) % 4)
    body += struct.pack(f"<{len(folders)}I", *folders)
    pool_bytes = len(pool)
    body += bytes(pool) + b"\0" * (-len(pool) % 4)
    head = struct.pack(
        "<IHHHHHH8HIIIIIIII",
        0x42494C54,
        art_scrape.library_idx.VERSION,
        64,
        16,
        0,
        len(recs),
        len(folders),
        *counts,
        len(recs),
        pool_bytes,
        0,
        0,
        zlib.crc32(body),
        len(recs),
        1,
        0,
    )
    return head[:60] + struct.pack("<I", zlib.crc32(head[:60])) + body


class Names(unittest.TestCase):
    def test_thumbnail_names(self):
        self.assertEqual(
            art_scrape.thumbnail_name('Quote "Test" & A/B: <c>? `d` \\ e|f* (World)'),
            "Quote _Test_ _ A_B_ _c__ _d_ _ e_f_ (World)",
        )
        self.assertEqual(art_scrape.short_name("Game (USA) (Rev 1)"), "Game")
        self.assertIsNone(art_scrape.short_name("Game"))
        self.assertIsNone(art_scrape.short_name("(Unl) Game"))

    def test_tries(self):
        picks = art_scrape.tries([(0, "A: B (USA)"), (0, "A: B (Europe)")])
        self.assertEqual(picks, [(0, "A_ B (USA)"), (0, "A_ B"), (0, "A_ B (Europe)")])

    def test_url_is_quoted(self):
        f = art_scrape.Fetcher("https://x/", "/nonexistent", 0, 0, True, False)
        self.assertEqual(
            f.url(0, "Advance Wars (USA)"),
            "https://x/Nintendo%20-%20Game%20Boy%20Advance/Named_Boxarts/Advance%20Wars%20%28USA%29.png",
        )

    def test_candidates(self):
        texts = [(DATS / d).read_text() for d in fixture.DATS]
        tables, codes, _ = names_db.make_tables(texts)
        hack = zlib.crc32(fixture.rom(9, 4096, "BZME"))
        # A romhack has its base game's code, but not its title: no art
        self.assertEqual(
            art_scrape.candidates(tables, codes, 0, hack, "BZME", "Hack of Minish.gba"),
            [],
        )
        # A patched dump under the game's title takes its art
        self.assertEqual(
            art_scrape.candidates(
                tables,
                codes,
                0,
                hack,
                "BZME",
                "The Legend of Zelda - The Minish Cap (U).gba",
            ),
            [(0, "Legend of Zelda, The - The Minish Cap (USA)")],
        )
        # A .gb in the GBC dat looks in the GBC thumbnails
        gb = zlib.crc32(fixture.rom(6, 2048))
        self.assertEqual(
            art_scrape.candidates(tables, codes, 2, gb, None, "Colour named GB.gb"),
            [(1, "Colour Named Gb (World)")],
        )
        self.assertEqual(art_scrape.candidates(tables, codes, 0, 1, None, "x.gba"), [])


class Scrape(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.sd = base / "sd"
        self.cache = base / "cache"
        self.sd.mkdir()
        fixture.write_roms(self.sd)
        # A patched dump of a listed game, under its title: art by its game code
        (self.sd / PATCHED[1:]).write_bytes(fixture.rom(13, 4096, "BZME"))
        self.pics = {
            "wars": png((200, 30, 30)),
            "minish": png((30, 160, 40)),
            "colour": png((240, 200, 0)),
            "colgb": png((0, 120, 200)),
            "nes": png((90, 0, 90)),
        }
        self.server = Server(
            {
                thumb(GBA_SYS, "Advance Wars (USA)"): self.pics["wars"],
                # Only the name cut at "(": the fallback
                thumb(GBA_SYS, "Legend of Zelda, The - The Minish Cap"): self.pics[
                    "minish"
                ],
                # Two server errors, then the picture
                thumb(GBC_SYS, "Colour Game (USA)"): [500, 503, self.pics["colour"]],
                thumb(GBC_SYS, "Colour Named Gb (World)"): self.pics["colgb"],
                thumb(NES_SYS, "Famicom Game (Japan)"): self.pics["nes"],
            }
        )

    def tearDown(self):
        self.server.close()
        self.tmp.cleanup()

    def run_tool(self, *extra, url=None):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            status = art_scrape.main(
                [
                    str(self.sd),
                    "--dats",
                    str(DATS),
                    "--cache",
                    str(self.cache),
                    "--base-url",
                    url or self.server.url,
                    "--delay",
                    "0",
                    "--retry-wait",
                    "0",
                    *extra,
                ]
            )
        return status, out.getvalue(), err.getvalue()

    def cached(self, system, name):
        """The download cache's file of a picture (a short name's)."""
        return self.cache / system / f"{name}.{zlib.crc32(name.encode()):08x}.png"

    def art(self, card_path):
        return self.sd / "torii" / "art" / f"{crc_of(card_path, self.sd)}.bin"

    def test_scrape(self):
        status, out, err = self.run_tool()
        self.assertEqual(status, 0, err)
        for path in (
            "/GBA/Wars.gba",
            "/GBA/zz Minish.gba",
            PATCHED,  # by its game code, as its file name is the game's title
            "/Colour.gbc",
            "/Colour named GB.gb",
            "/NES/Famicom.nes",
        ):
            data = self.art(path).read_bytes()
            self.assertEqual(len(data), art.FILE_BYTES, path)
            self.assertEqual(data[:4], b"TAS2")
        self.assertFalse(self.art("/GB/Mono.gb").exists())
        self.assertFalse(self.art("/GBA/Homebrew.gba").exists())
        self.assertEqual(
            self.art("/GBA/zz Minish.gba").read_bytes(), self.art(PATCHED).read_bytes()
        )
        # A romhack gets no art: not its base game's by its game code
        self.assertFalse(self.art("/GBA/Hack of Minish.gba").exists())
        self.assertIn("/GBA/Hack of Minish.gba: not in the dats", out)
        self.assertIn(
            "GBA: 5 games, 3 with art (60%): 2 downloaded, 1 from the cache, 2 not in the dats",
            out,
        )
        self.assertIn("GB: 2 games, 1 with art (50%)", out)
        self.assertIn("GBC: 1 games, 1 with art (100%)", out)
        self.assertIn("NES: 1 games, 1 with art (100%)", out)
        self.assertIn("/GB/Mono.gb: no thumbnail", out)
        self.assertIn("/GBA/Homebrew.gba: not in the dats", out)
        # One request per picture tried, the retries, each with the User-Agent
        paths = self.server.paths()
        self.assertEqual(paths.count(thumb(GBC_SYS, "Colour Game (USA)")), 3)
        self.assertEqual(
            paths.count(thumb(GBA_SYS, "Legend of Zelda, The - The Minish Cap (USA)")),
            1,
        )
        self.assertEqual(
            paths.count(thumb(GBA_SYS, "Legend of Zelda, The - The Minish Cap")), 1
        )
        self.assertIn(thumb(GB_SYS, "Mono Game"), paths)
        self.assertTrue(
            all(ua == art_scrape.USER_AGENT for _, ua in self.server.requests)
        )
        # crc.txt now has every game, so the kernel finds the art
        self.assertEqual(
            len(
                names_db.parse_cache(names_db.read_safe(self.sd / "torii" / "crc.txt"))
            ),
            9,
        )
        # The cache holds the pictures and the misses
        self.assertEqual(
            self.cached(GBA_SYS, "Advance Wars (USA)").read_bytes(),
            self.pics["wars"],
        )
        self.assertIn(
            f"{GB_SYS}/Mono Game (Japan)", (self.cache / "misses.txt").read_text()
        )

        # A second run asks for nothing: the art is on the card
        before = len(self.server.requests)
        status, out, _ = self.run_tool()
        self.assertEqual(status, 0)
        self.assertEqual(len(self.server.requests), before)
        self.assertIn("GBA: 5 games, 3 with art (60%): 3 already on the card", out)

        # --retry-missing asks the server again for what it hadn't
        status, _, _ = self.run_tool("--retry-missing")
        self.assertEqual(status, 0)
        self.assertIn(thumb(GB_SYS, "Mono Game (Japan)"), self.server.paths()[before:])

    def test_keeps_custom_and_existing_art(self):
        custom = self.sd / "torii" / "art" / "custom"
        custom.mkdir(parents=True)
        (custom / "Wars.gba.bin").write_bytes(b"mine")
        self.art("/NES/Famicom.nes").write_bytes(b"old")
        # Art by the Minish games' code, which v0.4.1's kernel read: no longer
        (custom.parent / "bzme.bin").write_bytes(b"by code")
        status, out, err = self.run_tool()
        self.assertEqual(status, 0, err)
        self.assertNotIn(thumb(GBA_SYS, "Advance Wars (USA)"), self.server.paths())
        self.assertNotIn(thumb(NES_SYS, "Famicom Game (Japan)"), self.server.paths())
        self.assertFalse(self.art("/GBA/Wars.gba").exists())
        self.assertEqual((custom / "Wars.gba.bin").read_bytes(), b"mine")
        self.assertEqual(self.art("/NES/Famicom.nes").read_bytes(), b"old")
        self.assertIn(
            "1 files named like game codes (bzme.bin...), which Torii doesn't read",
            err,
        )
        self.assertTrue(self.art("/GBA/zz Minish.gba").exists())
        self.assertFalse(self.art("/GBA/Hack of Minish.gba").exists())
        self.assertIn(
            "GBA: 5 games, 3 with art (60%): 1 downloaded, 1 from the cache, 1 custom, 2 not in the dats",
            out,
        )
        self.assertIn("NES: 1 games, 1 with art (100%): 1 already on the card", out)

    def test_skips_games_the_pack_covers(self):
        art_dir = self.sd / "torii" / "art"
        art_dir.mkdir(parents=True)
        pic = Path(self.tmp.name) / "cover.png"
        pic.write_bytes(png((10, 20, 30)))
        cover = art.convert(pic)
        crcs = [
            int(crc_of(p, self.sd), 16) for p in ("/GBA/Wars.gba", "/NES/Famicom.nes")
        ]
        artpack.write(
            self.sd / "torii" / "art.pak", artpack.build(dict.fromkeys(crcs, cover))
        )
        status, out, err = self.run_tool()
        self.assertEqual(status, 0, err)
        self.assertNotIn(thumb(GBA_SYS, "Advance Wars (USA)"), self.server.paths())
        self.assertNotIn(thumb(NES_SYS, "Famicom Game (Japan)"), self.server.paths())
        self.assertFalse(self.art("/GBA/Wars.gba").exists())
        self.assertFalse(self.art("/NES/Famicom.nes").exists())
        self.assertTrue(self.art("/GBA/zz Minish.gba").exists())
        self.assertIn(
            "GBA: 5 games, 3 with art (60%): 1 downloaded, 1 from the cache, 1 in art.pak",
            out,
        )
        self.assertIn("NES: 1 games, 1 with art (100%): 1 in art.pak", out)
        self.assertNotIn("/GBA/Wars.gba:", out)
        self.assertIn("art.pak covers 2 games (skipped)", out)

    def test_a_bad_pack_is_left_alone(self):
        (self.sd / "torii").mkdir()
        (self.sd / "torii" / "art.pak").write_bytes(b"not a pack" * 4)
        status, out, err = self.run_tool()
        self.assertEqual(status, 0, err)
        self.assertIn("art.pak: not an art pack; not used", err)
        self.assertTrue(self.art("/GBA/Wars.gba").exists())
        self.assertNotIn("art.pak covers", out)

    def test_deletes_romhack_art_from_v041(self):
        # v0.4.1 gave the romhack the Minish Cap's box under its own CRC; a
        # patched dump's art and custom art for the romhack stay
        art_dir = self.sd / "torii" / "art"
        (art_dir / "custom").mkdir(parents=True)
        hack, patched = self.art("/GBA/Hack of Minish.gba"), self.art(PATCHED)
        hack.write_bytes(b"base box")
        patched.write_bytes(b"its box")
        (art_dir / "custom" / "Hack of Minish.gba.bin").write_bytes(b"mine")
        name = hack.name

        for extra, kept in (
            (["--dry-run"], True),
            (["--keep-stale"], True),
            ([], False),
        ):
            status, out, err = self.run_tool(*extra)
            self.assertEqual(status, 0, err)
            self.assertIn(f"  {name}: /GBA/Hack of Minish.gba", out)
            self.assertEqual(hack.exists(), kept, extra)
        self.assertIn("art for games the dats don't list (romhacks), deleted:", out)
        self.assertEqual(patched.read_bytes(), b"its box")
        self.assertEqual(
            (art_dir / "custom" / "Hack of Minish.gba.bin").read_bytes(), b"mine"
        )
        status, out, _ = self.run_tool()
        self.assertNotIn("romhacks", out)

    def test_dry_run_changes_nothing(self):
        status, out, err = self.run_tool("--dry-run")
        self.assertEqual(status, 0, err)
        self.assertEqual(self.server.requests, [])
        self.assertFalse((self.sd / "torii").exists())
        self.assertFalse(self.cache.exists())
        self.assertIn(
            "would fetch: /GBA/Wars.gba: "
            + self.server.url
            + GBA_SYS
            + "/Named_Boxarts/Advance Wars (USA).png",
            out,
        )
        self.assertIn(
            "GBA: 5 games, 0 with art now or from the cache (0%): 3 to download, 2 not in the dats",
            out,
        )

    def test_offline_uses_the_cache(self):
        self.run_tool()
        before = len(self.server.requests)
        art_dir = self.sd / "torii" / "art"
        wars = self.art("/GBA/Wars.gba").read_bytes()
        for f in art_dir.glob("*.bin"):
            f.unlink()
        status, out, err = self.run_tool("--offline")
        self.assertEqual(status, 0, err)
        self.assertEqual(len(self.server.requests), before)
        self.assertEqual(self.art("/GBA/Wars.gba").read_bytes(), wars)
        self.assertIn(
            "GBA: 5 games, 3 with art (60%): 3 from the cache, 2 not in the dats", out
        )
        # Offline, an unasked picture isn't a miss
        for f in art_dir.glob("*.bin"):
            f.unlink()
        self.cached(GBA_SYS, "Advance Wars (USA)").unlink()
        _, out, _ = self.run_tool("--offline")
        self.assertIn("/GBA/Wars.gba: not fetched", out)

    def test_console_filter(self):
        status, out, err = self.run_tool("--console", "gb,NES")
        self.assertEqual(status, 0, err)
        self.assertTrue(
            all(
                p.startswith((f"/{GB_SYS}/", f"/{GBC_SYS}/", f"/{NES_SYS}/"))
                for p in self.server.paths()
            )
        )
        self.assertNotIn("GBA:", out)
        self.assertIn("GB: 2 games", out)
        self.assertEqual(
            len(
                names_db.parse_cache(names_db.read_safe(self.sd / "torii" / "crc.txt"))
            ),
            9,
        )
        status, _, err = self.run_tool("--console", "SNES")
        self.assertEqual(status, 1)
        self.assertIn("--console takes GBA, GBC, GB, NES", err)

    def test_server_errors_stop_fetching(self):
        self.server.routes[thumb(GB_SYS, "Mono Game (Japan)")] = 503
        status, out, err = self.run_tool("--console", "GB")
        self.assertEqual(status, 0)
        # One picture, then the retries; nothing is taken for a miss
        self.assertEqual(len(self.server.requests), 1 + 1 + art_scrape.RETRIES)
        self.assertIn("stopped fetching: the server keeps failing (503)", err)
        self.assertIn("/GB/Mono.gb: not fetched", out)
        self.assertFalse((self.cache / "misses.txt").exists())

    def test_client_errors_are_misses(self):
        # libretro-thumbnails answers 403 for some names, every time: a miss
        # like 404, remembered, so the next run doesn't ask again
        self.server.routes[thumb(GB_SYS, "Mono Game (Japan)")] = 403
        status, out, err = self.run_tool("--console", "GB")
        self.assertEqual(status, 0, err)
        self.assertIn("/GB/Mono.gb: no thumbnail", out)
        self.assertIn(
            f"{GB_SYS}/Mono Game (Japan)", (self.cache / "misses.txt").read_text()
        )
        before = len(self.server.requests)
        self.run_tool("--console", "GB")
        self.assertNotIn(
            thumb(GB_SYS, "Mono Game (Japan)"), self.server.paths()[before:]
        )

    def test_cache_names_keep_case(self):
        # Names differing only in case get files of their own, on a
        # case-insensitive disk too; an older run's <name>.png is taken in
        # only by the name it was written for
        f = art_scrape.Fetcher(self.server.url, self.cache, 0, 0, True, False)
        self.assertNotEqual(
            f.path(art_scrape.GBA, "Game (USA)").name.lower(),
            f.path(art_scrape.GBA, "GAME (USA)").name.lower(),
        )
        # A name too long for a file name is cut, the CRC keeping it apart
        long = "\u00e9" * 200 + " (USA)"
        self.assertLessEqual(len(f.path(art_scrape.GBA, long).name.encode()) + 5, 255)
        self.assertNotEqual(
            f.path(art_scrape.GBA, long), f.path(art_scrape.GBA, long + "x")
        )
        old = self.cache / GBA_SYS / "Advance Wars (USA).png"
        old.parent.mkdir(parents=True)
        old.write_bytes(self.pics["wars"])
        self.assertIsNone(f.known(art_scrape.GBA, "ADVANCE WARS (USA)"))
        self.assertEqual(f.known(art_scrape.GBA, "Advance Wars (USA)"), "cached")
        self.assertEqual(
            self.cached(GBA_SYS, "Advance Wars (USA)").read_bytes(), self.pics["wars"]
        )
        self.assertFalse(old.exists())

    def test_read_only_card(self):
        self.run_tool()
        torii = self.sd / "torii"
        for p in list(torii.iterdir()) + list((torii / "art").iterdir()):
            p.chmod(0o555 if p.is_dir() else 0o444)
        torii.chmod(0o555)
        (self.sd / "GBA" / "Wars.gba").write_bytes(
            b"changed" * 100
        )  # crc.txt must be written
        try:
            status, _, err = self.run_tool()
        finally:
            torii.chmod(0o755)
            (torii / "art").chmod(0o755)
        self.assertEqual(status, 1)
        self.assertIn("art-scrape: ", err)
        self.assertIn("stopped there", err)
        self.assertNotIn("Traceback", err)

    def test_too_many_requests_stops_at_once(self):
        self.server.routes[thumb(GB_SYS, "Mono Game (Japan)")] = 429
        status, _, err = self.run_tool("--console", "GB")
        self.assertEqual(status, 0)
        self.assertEqual(len(self.server.requests), 2)
        self.assertIn("(429)", err)

    def test_unreachable(self):
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            port = s.getsockname()[1]
        status, out, err = self.run_tool(
            "--console", "GBA", url=f"http://127.0.0.1:{port}/"
        )
        self.assertEqual(status, 0)
        self.assertIn("stopped fetching: can't reach", err)
        self.assertIn("/GBA/Wars.gba: not fetched", out)

    def test_alias_is_followed(self):
        # libretro-thumbnails keeps some pictures as links: the server sends
        # the target's name as text, and the picture is that file's
        real = self.server.routes[thumb(GBA_SYS, "Advance Wars (USA)")]
        self.server.routes[thumb(GBA_SYS, "Advance Wars (USA)")] = (
            b"Advance Wars Alias (USA).png"
        )
        self.server.routes[thumb(GBA_SYS, "Advance Wars Alias (USA)")] = real
        status, _, err = self.run_tool("--console", "GBA")
        self.assertEqual(status, 0, err)
        self.assertNotIn("not a PNG", err)
        self.assertTrue(self.cached(GBA_SYS, "Advance Wars (USA)").is_file())
        self.assertTrue(self.art("/GBA/Wars.gba").exists())

    def test_alias_to_nothing_is_a_miss(self):
        self.server.routes[thumb(GBA_SYS, "Advance Wars (USA)")] = (
            b"Nothing Here (USA).png"
        )
        status, _, err = self.run_tool("--console", "GBA")
        self.assertEqual(status, 0, err)
        self.assertIn(
            f"{GBA_SYS}/Advance Wars (USA)", (self.cache / "misses.txt").read_text()
        )

    def test_not_a_picture(self):
        self.server.routes[thumb(GBA_SYS, "Advance Wars (USA)")] = b"<html>oops</html>"
        status, out, err = self.run_tool("--console", "GBA")
        self.assertEqual(status, 0)
        self.assertIn("not a PNG", err)
        self.assertIn("/GBA/Wars.gba: not fetched", out)
        self.assertNotIn("Advance Wars (USA)", (self.cache / "misses.txt").read_text())
        # A PNG that doesn't decode is dropped from the cache
        self.server.routes[thumb(GBA_SYS, "Advance Wars (USA)")] = (
            b"\x89PNG\r\n\x1a\nbroken"
        )
        status, out, err = self.run_tool("--console", "GBA")
        self.assertEqual(status, 0)
        self.assertIn("not a picture art.py reads", err)
        self.assertIn("/GBA/Wars.gba: no thumbnail", out)  # the short name isn't there
        self.assertFalse(self.cached(GBA_SYS, "Advance Wars (USA)").exists())
        self.assertFalse(self.art("/GBA/Wars.gba").exists())

    def test_index_version_is_the_kernels(self):
        # art-scrape reads the card's index: a stale version makes it walk instead
        header = (ROOT / "source" / "library.h").read_text()
        want = int(header.split("#define LIBRARY_VERSION ", 1)[1].split()[0])
        self.assertEqual(art_scrape.library_idx.VERSION, want)

    def test_reads_the_library_index(self):
        # A game only the index lists (here in a folder the walk skips)
        odd = self.sd / "RTS" / "Odd.nes"
        odd.parent.mkdir()
        odd.write_bytes(fixture.rom(7, 16400))
        torii = self.sd / "torii"
        torii.mkdir()
        (torii / "library.idx").write_bytes(
            library_idx([("/RTS/Odd.nes", 3), ("/GBA/Wars.gba", 0), ("/Gone.gba", 0)])
        )
        games = {p: c for p, _, c in art_scrape.card_games(self.sd)}
        self.assertEqual(games["/RTS/Odd.nes"], 3)
        self.assertNotIn("/Gone.gba", games)  # no longer on the card
        self.assertEqual(len(games), 10)

    def test_old_placeholders(self):
        """C2's placeholders, box-<console>.bin, are deleted with their
        macOS "._" files, whatever --console says (listed on a dry run);
        other files stay."""
        art_dir = self.sd / "torii" / "art"
        art_dir.mkdir(parents=True)
        for name in (
            "box-gba.bin",
            "._box-gba.bin",
            "BOX-NES.BIN",
            "box-snes.bin",
            "boxes.bin",
        ):
            (art_dir / name).write_bytes(b"old")
        status, out, err = self.run_tool("--console", "GBA", "--dry-run")
        self.assertEqual(status, 0, err)
        self.assertIn("would delete: ._box-gba.bin, BOX-NES.BIN, box-gba.bin", out)
        self.assertTrue((art_dir / "box-gba.bin").exists())
        status, out, err = self.run_tool("--console", "GBA", "--offline")
        self.assertEqual(status, 0, err)
        self.assertIn("deleted: ._box-gba.bin, BOX-NES.BIN, box-gba.bin", out)
        self.assertEqual(
            sorted(p.name for p in art_dir.glob("box*")), ["box-snes.bin", "boxes.bin"]
        )

    def test_no_card(self):
        self.sd = self.sd / "nope"
        status, _, err = self.run_tool()
        self.assertEqual(status, 1)
        self.assertIn("isn't a folder", err)


if __name__ == "__main__":
    unittest.main(verbosity=1)
