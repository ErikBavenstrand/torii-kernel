#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""Fetch box art for an SD card's games from libretro-thumbnails.

usage: art_scrape.py SD_PATH [--console C[,C...]] [--dry-run] [--offline]
                     [--retry-missing] [--delay S] [--cache DIR] [--dats DIR]

`just art-scrape SD_PATH` runs this. SD_PATH is the card's folder on this PC
(a mounted card, say /Volumes/OMEGA). For every game on the card (the ones
the library index lists and the ones a walk finds, outside the folders the
kernel hides) it:

1. Takes the game's CRC32 from /torii/crc.txt, or computes it and writes
   crc.txt as `just names-update` does, so the kernel finds the art at once.
2. Keeps what the card has: custom art (/torii/art/custom/<file name>.bin,
   from `just art-add`) and art already at /torii/art/<CRC32>.bin (or, for
   a GBA game, /torii/art/<game code>.bin).
3. Looks the CRC up in libretro-database's No-Intro dats (downloaded once to
   dist/dats, shared with `just names-update`), then for a GBA game its
   game code. Each No-Intro name found, with &*/:`<>?\\|"
   replaced by _, names the picture:
   https://thumbnails.libretro.com/<System>/Named_Boxarts/<name>.png,
   and failing that the name cut at its first "(" ("Game (USA)": "Game").
4. Converts the picture as `just art-add` does (80x80, 15 colours) and
   writes /torii/art/<CRC32>.bin.

Fetching is polite: one request at a time, --delay seconds apart (default
0.5), with a User-Agent; only a server error (5xx) is retried. Pictures are
kept in dist/art-cache (git-ignored), so a second run downloads nothing it
has, and the names the server doesn't have are remembered there too
(--retry-missing asks again). --offline uses only that cache. --dry-run
downloads no pictures and changes nothing on the card: it prints what it
would fetch. --console limits the run to GBA, GBC, GB or NES. At the end it
prints, per console, how many games have art.

Box art has no licence grant: it belongs to the games' publishers and the
libretro-thumbnails contributors. This tool fetches it on your PC, at your
request, for your own card. Keep the pictures and the converted files to
yourself: never commit or publish them.
"""

import argparse
import contextlib
import http.client
import os
import re
import sys
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.append(str(ROOT / "tests"))  # library_idx, after tools
import art
import asset
import library_idx
import names_db

BASE_URL = "https://thumbnails.libretro.com/"
CACHE = ROOT / "dist" / "art-cache"
MISSES = "misses.txt"
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
USER_AGENT = "torii-kernel art-scrape (https://github.com/ErikBavenstrand/torii-kernel)"
DELAY = 0.5  # seconds between requests
RETRIES = 3  # attempts after a server error
RETRY_WAIT = 5.0  # seconds before the first retry, doubled for each next one
TIMEOUT = 60
ART_DIR = Path("torii") / "art"
ARTSET_MAX = asset.c_defines(ROOT / "source" / "artkey.h")["ARTSET_MAX"]

# libretro-thumbnails' systems are the dats' names (names_db.CONSOLES' order)
SYSTEMS = [dat.removesuffix(".dat") for _, dat, _ in names_db.CONSOLES]
LABELS = [label for label, _, _ in names_db.CONSOLES]
GBA, GBC, GB = 0, 1, 2

# The characters libretro-thumbnails replaces with _ in file names
UNSAFE = re.compile(r'[&*/:`<>?\\|"]')


class ScrapeError(Exception):
    pass


# --- Names ---------------------------------------------------------------------------


def thumbnail_name(name):
    """A No-Intro name as libretro-thumbnails names its file (without .png)."""
    return UNSAFE.sub("_", name)


def short_name(name):
    """The name cut at its first "(", RetroArch's fallback, or None."""
    cut = name.split("(", 1)[0].rstrip()
    return cut if cut and cut != name else None


def candidates(tables, codes, console, crc, code):
    """The (system, No-Intro name) pairs to look for, in order: the CRC in the
    console's dat (a GB or GBC file in either's), then a GBA game code's name."""
    out = []
    other = {GBC: GB, GB: GBC}.get(console)
    for i in (console, other):
        if i is not None and crc is not None and crc in tables[i]:
            out.append((i, tables[i][crc]))
            break
    if console == GBA and code and names_db.code_key(code) in codes:
        pair = (GBA, codes[names_db.code_key(code)])
        if pair not in out:
            out.append(pair)
    return out


def tries(pairs):
    """The pictures to try for candidates' pairs: (system, file name) each."""
    out = []
    for system, name in pairs:
        for n in (name, short_name(name)):
            if n is not None and (system, thumbnail_name(n)) not in out:
                out.append((system, thumbnail_name(n)))
    return out


# --- The card --------------------------------------------------------------------------


def index_games(sd):
    """The games the card's library index lists: {card path: console}, or {}."""
    torii = Path(sd) / "torii"
    found = []
    for file in ("library.idx", "library.bak"):
        path = torii / file
        if path.is_file():
            idx = library_idx.parse(str(path))
            if not isinstance(idx, str):
                found.append(idx)
    if not found:
        return {}
    idx = max(found, key=lambda i: i["generation"])
    out = {}
    for folder, flags, _, name, _ in idx["recs"]:
        console = flags & 15
        if console >= len(SYSTEMS):
            continue
        parent = library_idx.cstr(idx["pool"], idx["folder_offs"][folder]).rstrip("/")
        out[parent + "/" + library_idx.cstr(idx["pool"], name & 0xFFFFFF)] = console
    return out


def card_games(sd):
    """(card path, file path, console) of each game: the walk's, and any the
    index lists that the walk missed and the card still has."""
    games = names_db.walk_card(sd)
    seen = {folded(p) for p, _, _ in games}
    for path, console in sorted(index_games(sd).items()):
        file = Path(sd) / path.lstrip("/")
        if folded(path) not in seen and file.is_file():
            games.append((path, str(file), console))
            seen.add(folded(path))
    return games


def read_crcs(sd, games):
    """{card path: (crc, code)} from crc.txt where it holds (as
    names_db.update_cache decides), else computed; nothing is written."""
    old = names_db.parse_cache(
        names_db.read_safe(Path(sd) / "torii" / names_db.CRC_FILE)
    )
    out = {}
    for card_path, file_path, console in games:
        size = os.path.getsize(file_path)
        rec = old.get(card_path.lower())
        if (
            rec
            and rec[0] is not None
            and rec[2] == size
            and rec[3] == names_db.fat_date(file_path)
        ):
            crc, code = rec[0], rec[1]
        else:
            crc, code, _ = names_db.file_crc(file_path)
        out[card_path] = (crc, code if console == GBA else None)
    return out


def folded(name):
    return unicodedata.normalize("NFC", name).lower()


def ascii_lower(name):
    """A name folded as the kernel folds it (source/artkey.c): ASCII case only."""
    return "".join(c.lower() if c < "\x80" else c for c in name)


def listing(folder, fold=folded):
    """The folded names of a folder's files, or an empty set."""
    try:
        return {
            fold(e.name)
            for e in os.scandir(folder)
            if e.is_file() and not e.name.startswith("._")
        }
    except OSError:
        return set()


# --- Fetching ---------------------------------------------------------------------------


class Fetcher:
    """libretro-thumbnails' pictures through the download cache, one request
    at a time, delay seconds apart."""

    def __init__(self, base_url, cache, delay, retry_wait, offline, retry_missing):
        self.base_url = base_url.rstrip("/") + "/"
        self.cache = Path(cache)
        self.delay = delay
        self.retry_wait = retry_wait
        self.offline = offline
        self.stopped = None  # why fetching stopped for this run
        self.requests = 0
        self.downloaded = 0
        self.last = 0.0
        self.misses_file = self.cache / MISSES
        self.misses = set()
        # --retry-missing only skips the list: a miss asked again is added again
        if self.misses_file.exists() and not retry_missing:
            self.misses = set(self.misses_file.read_text("utf-8").splitlines())

    def path(self, system, name):
        return self.cache / SYSTEMS[system] / (name + ".png")

    def url(self, system, name):
        return self.base_url + urllib.parse.quote(
            f"{SYSTEMS[system]}/Named_Boxarts/{name}.png"
        )

    def known(self, system, name):
        """ "cached", "missing" (the server hasn't it) or None (not asked yet)."""
        if self.path(system, name).is_file():
            return "cached"
        if f"{SYSTEMS[system]}/{name}" in self.misses:
            return "missing"
        return None

    def forget(self, system, name):
        """Drops a cached picture that isn't one."""
        self.path(system, name).unlink(missing_ok=True)

    def miss(self, system, name):
        key = f"{SYSTEMS[system]}/{name}"
        self.misses.add(key)
        self.cache.mkdir(parents=True, exist_ok=True)
        with open(self.misses_file, "a", encoding="utf-8") as f:
            f.write(key + "\n")

    def request(self, url):
        wait = self.last + self.delay - time.monotonic()
        if wait > 0:
            time.sleep(wait)
        self.requests += 1
        try:
            req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
                return r.read()
        finally:
            self.last = time.monotonic()

    def get(self, system, name):
        """The cached picture's path, downloading it if need be; None if the
        server hasn't it or fetching is off."""
        state = self.known(system, name)
        if state == "cached":
            return self.path(system, name)
        if state == "missing" or self.offline or self.stopped:
            return None
        url = self.url(system, name)
        for attempt in range(RETRIES + 1):
            try:
                data = self.request(url)
                break
            except urllib.error.HTTPError as e:
                if e.code in (404, 410):
                    self.miss(system, name)
                    return None
                if 500 <= e.code < 600 and attempt < RETRIES:
                    time.sleep(self.retry_wait * 2**attempt)
                    continue
                if e.code == 429:
                    self.stopped = (
                        "the server asks for fewer requests (429): run again later"
                    )
                elif e.code >= 500:
                    self.stopped = (
                        f"the server keeps failing ({e.code}): run again later"
                    )
                else:
                    print(f"art-scrape: {url}: HTTP {e.code}", file=sys.stderr)
                return None
            except (urllib.error.URLError, OSError, http.client.HTTPException) as e:
                self.stopped = (
                    f"can't reach {self.base_url} ({getattr(e, 'reason', e)})"
                )
                return None
        if not data.startswith(PNG_MAGIC):
            # Not taken for a miss or cached: a proxy's page, say
            print(f"art-scrape: {url}: not a PNG", file=sys.stderr)
            return None
        path = self.path(system, name)
        tmp = path.with_name(path.name + ".part")
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            tmp.write_bytes(data)
            tmp.replace(path)
        except (OSError, ValueError) as e:
            print(f"art-scrape: can't cache {path}: {e}", file=sys.stderr)
            with contextlib.suppress(OSError, ValueError):
                tmp.unlink(missing_ok=True)
            return None
        self.downloaded += 1
        return path


# --- The run -------------------------------------------------------------------------------


def write_art(path, data):
    tmp = path.with_name(path.name + ".part")
    try:
        tmp.write_bytes(data)
        tmp.replace(path)
    except OSError as e:
        with contextlib.suppress(OSError):
            tmp.unlink(missing_ok=True)
        raise ScrapeError(f"can't write {path}: {e}") from e


def scrape_one(fetcher, picks, out, dry_run):
    """Fetches and converts the first picture of picks that exists into out;
    returns (how, what): ("new", url), ("cached", url), ("missing", None),
    ("not fetched", None) when fetching is off, or ("would fetch", url)."""
    unasked = False
    for system, name in picks:
        state = fetcher.known(system, name)
        if dry_run:
            if state == "cached":
                return "would convert", fetcher.url(system, name)
            if state is None:
                return "would fetch", fetcher.url(system, name)
            continue
        had = state == "cached"
        path = fetcher.get(system, name)
        if path is None:
            if fetcher.known(system, name) is None:
                unasked = True
            continue
        try:
            data = art.convert(path)
        except (art.ArtError, OSError, Image.DecompressionBombError) as e:
            print(
                f"art-scrape: {path.name}: not a picture art.py reads ({e})",
                file=sys.stderr,
            )
            fetcher.forget(system, name)
            continue
        write_art(out, data)
        return ("cached" if had else "new"), fetcher.url(system, name)
    return ("not fetched" if unasked else "missing"), None


def scrape(args):
    sd = Path(args.sd_path)
    if not sd.is_dir():
        raise ScrapeError(
            f"{sd} isn't a folder: give the card's folder, e.g. /Volumes/OMEGA"
        )
    wanted = set(range(len(SYSTEMS)))
    if args.console:
        wanted = set()
        for c in args.console.split(","):
            if c.strip().upper() not in LABELS:
                raise ScrapeError(f"--console takes {', '.join(LABELS)}, not {c!r}")
            wanted.add(LABELS.index(c.strip().upper()))

    texts = [
        names_db.fetch(Path(args.dats), dat, args.offline, False).read_text(
            "utf-8", "replace"
        )
        for _, dat, _ in names_db.CONSOLES
    ]
    tables, codes, version = names_db.make_tables(texts)

    games = card_games(sd)
    if args.dry_run:
        crcs = read_crcs(sd, [g for g in games if g[2] in wanted])
    else:
        # All games, so crc.txt keeps every console's records
        crcs, computed, _ = names_db.update_cache(sd, games)
        if computed:
            print(f"wrote {sd / 'torii' / names_db.CRC_FILE}: {computed} CRCs computed")
    games = [g for g in games if g[2] in wanted and g[0] in crcs]

    art_dir = sd / ART_DIR
    have = listing(art_dir)
    custom = listing(art_dir / "custom", ascii_lower)
    fetcher = Fetcher(
        args.base_url,
        args.cache,
        args.delay,
        args.retry_wait,
        args.offline or args.dry_run,
        args.retry_missing,
    )
    if not args.dry_run:
        art_dir.mkdir(parents=True, exist_ok=True)

    results = {i: [] for i in range(len(SYSTEMS))}
    tty = sys.stderr.isatty()
    for n, (card_path, _, console) in enumerate(games):
        if tty:
            print(
                f"\r\033[Kart {n + 1}/{len(games)}: {card_path[-60:]}",
                end="",
                file=sys.stderr,
            )
        crc, code = crcs[card_path]
        file = f"{crc:08X}{art.EXT}"
        if ascii_lower(Path(card_path).name + art.EXT) in custom:
            how, what = "custom", None
        elif folded(file) in have or (code and folded(code + art.EXT) in have):
            # Art by CRC, or by game code (which the kernel takes when it has
            # no art by CRC, so art written by CRC would hide it)
            how, what = "on card", None
        else:
            picks = tries(candidates(tables, codes, console, crc, code))
            if not picks:
                how, what = "unknown", None
            else:
                how, what = scrape_one(fetcher, picks, art_dir / file, args.dry_run)
                if how in ("new", "cached"):
                    have.add(folded(file))
        results[console].append((card_path, how, what))
    if tty:
        print("\r\033[K", end="", file=sys.stderr)

    report(results, args.dry_run, fetcher, version)
    if fetcher.stopped:
        print(f"art-scrape: stopped fetching: {fetcher.stopped}", file=sys.stderr)
    if not args.dry_run and len(have) > ARTSET_MAX:
        print(
            f"warning: {art_dir} holds {len(have)} files; the kernel reads the first {ARTSET_MAX}",
            file=sys.stderr,
        )


WITH_ART = ("new", "cached", "on card", "custom", "would convert")
REASONS = {
    "unknown": "not in the dats",
    "missing": "no thumbnail",
    "not fetched": "not fetched",
}


def report(results, dry_run, fetcher, version):
    misses = []
    for console, rows in results.items():
        if not rows:
            continue
        hows = [how for _, how, _ in rows]
        hit = sum(how in WITH_ART for how in hows)
        parts = []
        for how, label in (
            ("new", "downloaded"),
            ("cached", "from the cache"),
            ("would fetch", "to download"),
            ("would convert", "in the cache"),
            ("on card", "already on the card"),
            ("custom", "custom"),
        ):
            if hows.count(how):
                parts.append(f"{hows.count(how)} {label}")
        for how, label in REASONS.items():
            if hows.count(how):
                parts.append(f"{hows.count(how)} {label}")
        verb = "with art now or from the cache" if dry_run else "with art"
        print(
            f"  {LABELS[console]}: {len(rows)} games, {hit} {verb} ({100 * hit // len(rows)}%): {', '.join(parts)}"
        )
        misses += [(path, REASONS[how]) for path, how, _ in rows if how in REASONS]
        if dry_run:
            for path, how, what in rows:
                if how.startswith("would"):
                    print(f"    {how}: {path}: {urllib.parse.unquote(what)}")
    if misses:
        print("without art (`just art-add` makes custom art):")
        for path, why in misses:
            print(f"  {path}: {why}")
    print(
        f"{fetcher.requests} requests, {fetcher.downloaded} pictures downloaded; dats {version or '(no version)'}"
    )


def main(argv):
    p = argparse.ArgumentParser(
        prog="art_scrape.py",
        description=__doc__.split("\n\n")[0],
        epilog=__doc__.split("\n\n", 1)[1],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("sd_path", metavar="SD_PATH", help="the card's folder on this PC")
    p.add_argument(
        "--console", help="only these consoles: GBA, GBC, GB, NES, comma-separated"
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="download no pictures, change nothing on the card",
    )
    p.add_argument(
        "--offline", action="store_true", help="never download: use the caches"
    )
    p.add_argument(
        "--retry-missing",
        action="store_true",
        help="ask again for pictures the server didn't have",
    )
    p.add_argument(
        "--delay",
        type=float,
        default=DELAY,
        help=f"seconds between requests (default {DELAY})",
    )
    p.add_argument(
        "--cache",
        default=str(CACHE),
        help=f"the pictures' cache (default {CACHE.relative_to(ROOT)})",
    )
    p.add_argument(
        "--dats",
        default=str(names_db.DATS),
        help=f"the dats' cache (default {names_db.DATS.relative_to(ROOT)})",
    )
    p.add_argument("--base-url", default=BASE_URL, help=argparse.SUPPRESS)
    p.add_argument(
        "--retry-wait", type=float, default=RETRY_WAIT, help=argparse.SUPPRESS
    )
    args = p.parse_args(argv)
    try:
        scrape(args)
    except (ScrapeError, names_db.NamesError) as e:
        print(f"art-scrape: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nart-scrape: stopped", file=sys.stderr)
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
