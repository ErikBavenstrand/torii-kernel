#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""Build the full art pack on an SD card: box art for every No-Intro game.

usage: art_pack.py SD_PATH [--console C[,C...]] [--offline] [--retry-missing]
                   [--jobs N] [--delay S] [--cache DIR] [--dats DIR] [--out FILE]

`just art-pack SD_PATH` runs this, once: afterwards a new official game put
on the card gets its cover on the cart, with no PC. (A romhack, a
translation or a patched dump is in no dat, so it still needs `just
art-add`, which wins over the pack, as /torii/art/<CRC32>.bin from `just
art-scrape` does.)

For every CRC32 the No-Intro dats list for the consoles (GBA, GBC and GB;
NES too when the card has NES games, or with --console) it looks for
libretro-thumbnails' box art as `just art-scrape` does: the dat's name
with &*/:`<>?\\|" replaced by _, then the name cut at its first "(":
https://thumbnails.libretro.com/<System>/Named_Boxarts/<name>.png.
Each picture is converted as `just art-add` does (80x80, 15 colours) and
stored once, however many CRCs (regions, revisions, clones) share it, in
/torii/art.pak (source/artpack.h has the format; tools/artpack.py writes it).
The pack is written whole or not at all: a temporary file, then a rename.

Fetching is polite: --jobs requests at a time (default 4), starting at
most one each --delay seconds (default 0.25), with a User-Agent; a server
error is retried, and a 429 or an unreachable server stops fetching. The
pictures and their conversions are cached in dist/art-cache (shared with
`just art-scrape`; git-ignored), and the names the server doesn't have are
remembered there, so a second run downloads only what is new, and a run
that was stopped (Ctrl-C, a 429, a lost connection) picks up where it left
off. A stopped run still writes a pack of what it has; run it again to
finish. --offline uses only the caches. The first run makes about 8,000
requests for GBA, GBC and GB (some 40 minutes) and writes a pack of about
20 MB.

Box art has no licence grant: it belongs to the games' publishers and the
libretro-thumbnails contributors. This tool fetches it on your PC, at your
request, for your own card. Keep the pack on your card: never commit or
publish it, nor the cache.
"""

import argparse
import concurrent.futures
import hashlib
import sys
import threading
import time
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import art
import art_scrape
import artpack
import names_db

PACK = Path("torii") / "art.pak"
JOBS = 4
DELAY = 0.25
LABELS = art_scrape.LABELS
NES = LABELS.index("NES")
DEFAULT = [LABELS.index(c) for c in ("GBA", "GBC", "GB")]
# Converted covers are cached per version of the converter
CONVERTER = hashlib.sha1((ROOT / "tools" / "art.py").read_bytes()).hexdigest()[:10]


class PackFetcher(art_scrape.Fetcher):
    """art_scrape's fetcher, for a few threads at once: requests start at
    most delay seconds apart, and misses are written one at a time."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.lock = threading.Lock()

    def request(self, url):
        with self.lock:
            wait = self.last + self.delay - time.monotonic()
            if wait > 0:
                time.sleep(wait)
            self.last = time.monotonic()
            self.requests += 1
        req = art_scrape.urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        with art_scrape.urllib.request.urlopen(req, timeout=art_scrape.TIMEOUT) as r:
            return r.read()

    def miss(self, system, name):
        with self.lock:
            super().miss(system, name)

    def known(self, system, name):
        with self.lock:
            return super().known(system, name)


USER_AGENT = art_scrape.USER_AGENT.replace("art-scrape", "art-pack")


class Progress:
    """One line on a terminal (else a line every few hundred): done of
    total, and the time left at the pace so far."""

    def __init__(self, what, total):
        self.what, self.total, self.done = what, total, 0
        self.start = time.monotonic()
        self.tty = sys.stderr.isatty()
        self.shown = 0.0

    def step(self, note=""):
        self.done += 1
        now = time.monotonic()
        if self.tty and now - self.shown < 0.2 and self.done < self.total:
            return
        if not self.tty and self.done % 500 and self.done < self.total:
            return
        self.shown = now
        spent = now - self.start
        left = spent / self.done * (self.total - self.done)
        line = (
            f"{self.what} {self.done}/{self.total} ({100 * self.done // self.total}%)"
            f", {clock(left)} left{note}"
        )
        if self.tty:
            print(f"\r\033[K{line}", end="", file=sys.stderr, flush=True)
        else:
            print(line, file=sys.stderr, flush=True)

    def end(self):
        if self.tty and self.total:
            print("\r\033[K", end="", file=sys.stderr)


def clock(seconds):
    seconds = int(seconds)
    if seconds >= 3600:
        return f"{seconds // 3600}h{seconds % 3600 // 60:02d}m"
    return f"{seconds // 60}m{seconds % 60:02d}s"


def wanted_consoles(arg, sd):
    """The consoles to pack: --console's, else GBA, GBC and GB, and NES when
    the card has NES games."""
    if arg:
        out = []
        for part in arg.split(","):
            c = part.strip().upper()
            if c not in LABELS:
                raise art_scrape.ScrapeError(
                    f"--console takes {', '.join(LABELS)}, not {c!r}"
                )
            out.append(LABELS.index(c))
        return sorted(set(out))
    out = list(DEFAULT)
    if any(console == NES for _, _, console in names_db.walk_card(sd)):
        out.append(NES)
    return sorted(out)


def picks_by_crc(tables, consoles):
    """{crc: [(system, picture name), ...]} for every CRC of the consoles'
    dats, in the order art_scrape tries them (a CRC in two dats, a GB game
    the GBC dat also lists, keeps the first console's)."""
    out = {}
    for console in consoles:
        for crc, name in tables[console].items():
            if crc not in out:
                out[crc] = art_scrape.tries([(console, name)])
    return out


def converted(cache, fetcher, system, name):
    """The cover of a cached picture, converted once and cached; None if it
    isn't a picture art.py reads (the picture is then dropped)."""
    png = fetcher.path(system, name)
    out = Path(cache) / f"pack-{CONVERTER}" / png.parent.name / (png.stem + ".bin")
    if out.is_file():
        return out.read_bytes()
    try:
        data = art.convert(png)
    except (art.ArtError, OSError, ValueError, Image.DecompressionBombError) as e:
        print(
            f"art-pack: {png.name}: not a picture art.py reads ({e})", file=sys.stderr
        )
        fetcher.forget(system, name)
        return None
    out.parent.mkdir(parents=True, exist_ok=True)
    art_scrape.write_art(out, data)
    return data


def fetch_round(fetcher, wanted, jobs):
    """Downloads the (system, name) pictures wanted, jobs at a time, until
    done or the fetcher stops; returns how many were new."""
    if not wanted:
        return 0
    progress = Progress("fetching", len(wanted))
    new = 0
    pool = concurrent.futures.ThreadPoolExecutor(jobs)
    try:
        futures = [pool.submit(fetcher.get, s, n) for s, n in wanted]
        for f in concurrent.futures.as_completed(futures):
            if f.result() is not None:
                new += 1
            progress.step(f", {fetcher.downloaded} new" if fetcher.downloaded else "")
            if fetcher.stopped:
                break
    finally:
        # A stop (Ctrl-C, a 429) cancels what hasn't started; what has
        # finishes, and its picture is cached for the next run
        pool.shutdown(wait=True, cancel_futures=True)
        progress.end()
    return new


def pack(args):
    started = time.monotonic()
    sd = Path(args.sd_path)
    if not sd.is_dir():
        raise art_scrape.ScrapeError(
            f"{sd} isn't a folder: give the card's folder, e.g. /Volumes/OMEGA"
        )
    consoles = wanted_consoles(args.console, sd)
    texts = [
        names_db.fetch(Path(args.dats), dat, args.offline, False).read_text(
            "utf-8", "replace"
        )
        for _, dat, _ in names_db.CONSOLES
    ]
    tables, _, version = names_db.make_tables(texts)
    picks = picks_by_crc(tables, consoles)
    fetcher = PackFetcher(
        args.base_url,
        args.cache,
        args.delay,
        args.retry_wait,
        args.offline,
        args.retry_missing,
    )

    # Round by round: every CRC's first picture, then the next for those
    # whose first the server hasn't (the name cut at "(")
    found = {}  # crc: (system, name) of its picture
    pending = dict(picks)
    for _ in range(max((len(p) for p in picks.values()), default=0)):
        for crc, tries in list(pending.items()):
            left = [t for t in tries if fetcher.known(*t) != "missing"]
            if not left:
                del pending[crc]
            else:
                pending[crc] = left
        wanted = sorted(
            {t[0] for t in pending.values() if fetcher.known(*t[0]) is None}
        )
        if not fetcher.offline and not fetcher.stopped:
            fetch_round(fetcher, wanted, args.jobs)
        for crc, tries in list(pending.items()):
            state = fetcher.known(*tries[0])
            if state == "cached":
                found[crc] = tries[0]
                del pending[crc]
            elif state == "missing" or fetcher.offline or fetcher.stopped:
                # Not there, or not asked (a later run asks): the next name
                pending[crc] = tries[1:]
        if not pending:
            break

    # Convert each picture once; CRCs whose pictures are equal share an image
    progress = Progress("converting", len(set(found.values())))
    covers, mapping = {}, {}
    for crc in sorted(found):
        key = found[crc]
        if key not in covers:
            covers[key] = converted(args.cache, fetcher, *key)
            progress.step()
        if covers[key] is not None:
            mapping[crc] = covers[key]
    progress.end()
    if not mapping:
        raise art_scrape.ScrapeError(
            "no box art found: nothing to pack (offline with an empty cache?)"
        )
    data = artpack.build(mapping)
    out = Path(args.out) if args.out else sd / PACK
    out.parent.mkdir(parents=True, exist_ok=True)
    artpack.write(out, data)

    images = artpack.Pack(data).images
    print(
        f"wrote {out}: {images} covers for {len(mapping)} CRCs, {len(data) / 1e6:.1f} MB"
    )
    for console in consoles:
        crcs = [c for c in tables[console] if picks.get(c) is not None]
        hit = sum(c in mapping for c in crcs)
        if crcs:
            print(
                f"  {LABELS[console]}: {hit} of {len(crcs)} CRCs with art ({100 * hit // len(crcs)}%)"
            )
    print(
        f"{fetcher.requests} requests, {fetcher.downloaded} pictures downloaded in "
        f"{clock(time.monotonic() - started)}; dats {version or '(no version)'}"
    )
    if fetcher.stopped:
        print(
            f"art-pack: stopped fetching: {fetcher.stopped}; the pack has what was "
            "fetched so far: run it again to finish",
            file=sys.stderr,
        )
        return 3
    return 0


def main(argv):
    p = argparse.ArgumentParser(
        prog="art_pack.py",
        description=__doc__.split("\n\n")[0],
        epilog=__doc__.split("\n\n", 1)[1],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("sd_path", metavar="SD_PATH", help="the card's folder on this PC")
    p.add_argument(
        "--console", help="only these consoles: GBA, GBC, GB, NES, comma-separated"
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
        "--jobs", type=int, default=JOBS, help=f"requests at a time (default {JOBS})"
    )
    p.add_argument(
        "--delay",
        type=float,
        default=DELAY,
        help=f"seconds between request starts (default {DELAY})",
    )
    p.add_argument(
        "--cache",
        default=str(art_scrape.CACHE),
        help=f"the pictures' cache (default {art_scrape.CACHE.relative_to(ROOT)})",
    )
    p.add_argument(
        "--dats",
        default=str(names_db.DATS),
        help=f"the dats' cache (default {names_db.DATS.relative_to(ROOT)})",
    )
    p.add_argument("--out", help="write the pack here instead of SD_PATH/torii/art.pak")
    p.add_argument("--base-url", default=art_scrape.BASE_URL, help=argparse.SUPPRESS)
    p.add_argument(
        "--retry-wait",
        type=float,
        default=art_scrape.RETRY_WAIT,
        help=argparse.SUPPRESS,
    )
    args = p.parse_args(argv)
    if not 1 <= args.jobs <= 8:
        p.error("--jobs takes 1 to 8")
    try:
        return pack(args)
    except (art_scrape.ScrapeError, names_db.NamesError, artpack.PackError) as e:
        print(f"art-pack: {e}", file=sys.stderr)
        return 1
    except OSError as e:
        print(f"art-pack: {names_db.os_error(e)}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print(
            "\nart-pack: stopped; what was fetched is cached: run it again to go on",
            file=sys.stderr,
        )
        return 130


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
