#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Write cheat files for an SD card's GBA games from libretro-database.

usage: cheats.py SD_PATH [--dry-run] [--offline] [--refresh] [--delay S]
                 [--cache DIR] [--dats DIR]

`just cheats SD_PATH` runs this. SD_PATH is the card's folder on this PC (a
mounted card, say /Volumes/OMEGA). For every GBA game on the card (outside
the folders the kernel hides) it:

1. Takes the game's CRC32 from /torii/crc.txt where its record holds (as
   `just art-scrape` reads it), else computes it; crc.txt isn't written.
2. Looks the CRC up in the No-Intro GBA dat (downloaded once to dist/dats,
   shared with `just names-update`). A game the dat doesn't list (a romhack)
   gets cheats only if its file name is the title of the game its header's
   code names (a patched dump), as art-scrape decides.
3. Finds that name's file in libretro-database's cht/Nintendo - Game Boy
   Advance: "<No-Intro name> (<code type>).cht", with "&" and "/" written as
   "_" and the code type ("(Code Breaker)", "(Gameshark)", " Game Shark"...)
   sometimes missing. The files for one name ("(diff)", another code type)
   are taken together. Failing the whole name, it tries the name without its
   language group ("(En,Fr,De)"), then the title alone (the region differs):
   then the dat's serial of the file's name, or the region its name gives,
   has to be the ROM header's game code (0xAC), and when that leaves no
   single file the game is reported, not guessed.
4. Writes /torii/cheats/<rom path without extension>.cht in libretro's
   format (`cheats = N`, `cheatN_desc`, `cheatN_code`, `cheatN_enable`),
   the codes byte for byte. A file already there is read as the kernel reads
   it (source/safefile.h: NAME if its CRC line holds, NAME edited by hand,
   else NAME.bak; '#' lines are comments). Its picks (the enable flags,
   matched by code) are kept, and so is every cheat whose code the
   database's file doesn't have (your own codes), after the database's.
   NAME is written as plain text with no CRC line (the kernel takes it as
   edited by hand and adds the line on its next write); NAME.bak is left
   alone, and so is a file whose cheats wouldn't change.

It never touches /CHEAT or EZ-Flash's files. At the end it prints the games
whose cheats were written, updated or unchanged, the games the database has
no cheats for, and the ones it couldn't match, with why.

Fetching is polite: one request at a time, --delay seconds apart (default
0.5), with a User-Agent. The file list (one request to GitHub's API) and the
cheat files are kept in dist/cheats (git-ignored); later runs download only
the files they lack. --refresh asks for the list again and downloads the
files that changed; --offline uses only the cache. --dry-run changes nothing
on the card (it may still fill the cache) and prints what it would write.

Licence: the cheats are libretro-database's (CC BY-SA 4.0), collected from
the web. They are downloaded to this PC at your request and written to your
card; nothing from them is part of Torii, its repository or its releases.
"""

import argparse
import hashlib
import json
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import names_db

SYSTEM = "Nintendo - Game Boy Advance"
CHT_DIR = f"cht/{SYSTEM}"
API_URL = "https://api.github.com/repos/libretro/libretro-database/contents/"
RAW_URL = "https://raw.githubusercontent.com/libretro/libretro-database/master/"
USER_AGENT = "torii-kernel cheats (https://github.com/ErikBavenstrand/torii-kernel)"
CACHE = ROOT / "dist" / "cheats"
LISTING = "listing.json"
DELAY = 0.5  # seconds between requests
TIMEOUT = 60
CHEATS_DIR = Path("torii") / "cheats"
GBA = names_db.GBA
ATTRIBUTION = (
    "Cheats from libretro-database (https://github.com/libretro/libretro-database), "
    "CC BY-SA 4.0"
)


class CheatsError(Exception):
    pass


# --- Names ---------------------------------------------------------------------------

# The code type libretro's file names end in (or hold): "(Code Breaker)",
# "(CodeBreaker)", "(Gameshark)", " Game Shark", and "(diff)", a second file
# for the same game
CODE_TYPE = re.compile(
    r"\s*\((?:code ?breaker|game ?shark|action replay|diff)\)"
    r"|\s+(?:code ?breaker|game ?shark|action replay)\s*$",
    re.IGNORECASE,
)
# A language group, "(En,Fr,De)" or "(En,Zh-Hant)"
LANGUAGES = re.compile(
    r"\s*\([A-Z][a-z](?:-[A-Za-z]+)?(?:,[A-Z][a-z](?:-[A-Za-z]+)?)*\)"
)
# A region's letters in GBA game codes (the 4th character), for No-Intro's
# regions and GoodTools' letters; a list of regions goes by its first, and
# "World", one game for every region, names them all
REGION_LETTERS = {
    "usa": "E",
    "u": "E",
    "europe": "PXYZ",
    "e": "PXYZ",
    "japan": "J",
    "j": "J",
    "germany": "D",
    "g": "D",
    "france": "F",
    "f": "F",
    "spain": "S",
    "s": "S",
    "italy": "I",
    "i": "I",
    "world": "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
}


def strip_type(stem):
    """A libretro file's stem without its code type: the No-Intro name."""
    name = CODE_TYPE.sub("", stem)
    name = re.sub(r"(?<=\S)\(", " (", name)  # "Version(USA)"
    return " ".join(name.split())


def key(name):
    """A name to compare: "&", "/" and "_" alike (libretro writes the first
    two as "_"), spaces collapsed, case folded."""
    name = re.sub(r"[&/_]", "_", name)
    name = re.sub(r"(?<=\S)\(", " (", name)
    return " ".join(name.split()).casefold()


def no_languages(name):
    return LANGUAGES.sub("", name)


def title(name):
    """A name's title to compare, as names_db.title_key makes it: no groups,
    only letters and digits ("Leaf Green" is "LeafGreen", "Tony Hawks" is
    "Tony Hawk's")."""
    return names_db.title_key(name)


def region_letters(name):
    """The game code letters of the region a name's first region group
    gives, or ""."""
    for group in re.findall(r"\(([^()]*)\)", name):
        first = group.split(",")[0].strip().casefold()
        if first in REGION_LETTERS:
            return REGION_LETTERS[first]
    return ""


def revision(name):
    """The revision a name gives, "(Rev 1)" or GoodTools' "(V1.1)": the
    header's version byte (0xBC) it stands for; 0 for none."""
    m = re.search(r"\(Rev (\d+)\)|\(V1\.(\d+)\)", name, re.IGNORECASE)
    return int(m.group(1) or m.group(2)) if m else 0


def header_info(path):
    """A GBA game's header code (0xAC) and version byte (0xBC), or None."""
    code = names_db.header_code(path)
    if code is None:
        return None
    try:
        with open(path, "rb") as f:
            f.seek(0xBC)
            version = f.read(1)
    except OSError:
        return None
    return (code, version[0]) if version else None


class Database:
    """libretro's GBA cheat files grouped by the No-Intro name they're for,
    and the dat's game codes by name."""

    def __init__(self, files, dat_games):
        groups = {}
        for file in files:
            stem = file.removesuffix(".cht")
            name = strip_type(stem)
            groups.setdefault(key(name), (name, []))[1].append(file)
        self.groups = {k: (n, sorted(f)) for k, (n, f) in groups.items()}
        self.by_nolang = {}
        self.by_title = {}
        for k, (name, _) in sorted(self.groups.items()):
            self.by_nolang.setdefault(key(no_languages(name)), []).append(k)
            self.by_title.setdefault(title(name), []).append(k)
        self.codes = {}
        nolang = {}
        for name, _, serial in dat_games:
            code = names_db.game_code(serial)
            self.codes.setdefault(key(name), code)
            nolang.setdefault(key(no_languages(name)), set()).add(code)
        # A name without its languages names a code only if every game it
        # stands for has that one
        self.codes_nolang = {k: next(iter(c)) for k, c in nolang.items() if len(c) == 1}

    def code(self, k):
        """The dat's game code of a group's name, or None."""
        name = self.groups[k][0]
        code = self.codes.get(k)
        return code or self.codes_nolang.get(key(no_languages(name)))

    def fits(self, k, header):
        """Whether a group's file is for the header's game: its game code
        (the dat's, else its region's letter) and its revision."""
        name = self.groups[k][0]
        code, version = header
        if revision(name) != version:
            return False
        if self.code(k) is not None:
            return self.code(k) == code
        return code[3] in region_letters(name)

    def match(self, name, header):
        """The group (key) for a No-Intro name, or (None, why): the files
        for the name, else for it without languages (when one group fits),
        else for its title, when the header settles which (fits); why is
        None for a game the database has no cheats for. header is
        header_info's."""
        k = key(name)
        if k in self.groups:
            return k, None
        found = self.by_nolang.get(key(no_languages(name)), [])
        if len(found) == 1:
            return found[0], None
        found = found or self.by_title.get(title(name), [])
        if not title(name) or not found:
            return None, None
        files = "; ".join(f for k in found for f in self.groups[k][1])
        if header is None:
            return None, f"no game code in its header to pick between: {files}"
        same = [k for k in found if self.fits(k, header)]
        if len(same) == 1:
            return same[0], None
        this = f"{header[0]} version {header[1]}"
        if same:
            return None, f"more than one file fits its game ({this}): {files}"
        codes = "; ".join(
            f"{self.groups[k][0]}: {self.code(k) or 'no code in the dat'}"
            for k in found
        )
        return None, f"cheats only for other versions (this is {this}; {codes})"


# --- The cheat files ---------------------------------------------------------------------

LINE = re.compile(r"^\s*([A-Za-z0-9_]+)\s*=\s*(.*?)\s*$")


def value(raw):
    """A config value byte for byte, without the quote at each end. A quote
    missing at one end (one of libretro's files has `Enemies Have Less HP"`)
    isn't doubled when the file is written back."""
    return raw.removeprefix('"').removesuffix('"')


def parse_cht(text):
    """A libretro cheat file's cheats, in their order: [(desc, code, enabled)].
    '#' lines are comments; a cheat without a code is dropped."""
    found = {}
    for raw in text.split("\n"):
        line = raw.removesuffix("\r")
        if line.lstrip().startswith("#"):
            continue
        m = LINE.match(line)
        if not m:
            continue
        k = re.fullmatch(r"cheat(\d+)_(desc|code|enable)", m.group(1))
        if k:
            found.setdefault(int(k.group(1)), {})[k.group(2)] = value(m.group(2))
    out = []
    for n in sorted(found):
        c = found[n]
        if "code" in c:
            out.append(
                (c.get("desc", ""), c["code"], c.get("enable", "").casefold() == "true")
            )
    return out


def format_cht(cheats):
    """libretro's format, as its files are laid out."""
    lines = [f"cheats = {len(cheats)}", ""]
    for n, (desc, code, enabled) in enumerate(cheats):
        lines += [
            f'cheat{n}_desc = "{desc}"',
            f'cheat{n}_code = "{code}"',
            f"cheat{n}_enable = {'true' if enabled else 'false'}",
            "",
        ]
    return "\n".join(lines)


def merge(database, old):
    """The database's cheats with old's picks (by code), then old's cheats
    whose code the database lacks. Returns (cheats, own): own counts the latter."""
    picks = {}
    for _, code, enabled in old:
        picks.setdefault(code, enabled)
    out, codes = [], set()
    for desc, code, enabled in database:
        if code in codes:
            continue  # the same code in two of the game's files
        codes.add(code)
        out.append((desc, code, picks.get(code, enabled)))
    own = [c for c in old if c[1] not in codes]
    return out + own, len(own)


def read_card_file(path):
    """The text of a card's cheat file as the kernel picks its copy
    (safefile_pick: NAME whose CRC line holds, NAME edited by hand, NAME.bak
    whose CRC line holds), the CRC line left out; None if there's none."""
    main, bak = Path(path), Path(str(path) + ".bak")
    if main.is_file():
        data = main.read_bytes()
        body = names_db.safe_text(data)
        if body is not None:
            return decode(body)
        if data and b"\0" not in data:
            return decode(data)  # edited by hand, or written by this tool
    if bak.is_file():
        body = names_db.safe_text(bak.read_bytes())
        if body is not None:
            return decode(body)
    return None


def decode(data):
    # surrogateescape keeps bytes that aren't UTF-8 (a description in
    # Latin-1) as they were when written back
    return data.decode("utf-8", "surrogateescape")


def write_card_file(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".part")
    try:
        tmp.write_bytes(text.encode("utf-8", "surrogateescape"))
        tmp.replace(path)
    except OSError:
        tmp.unlink(missing_ok=True)
        raise


# --- Fetching ------------------------------------------------------------------------------


def blob_sha(data):
    """Git's blob SHA-1 of data, as GitHub's listing gives each file's."""
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


class Fetcher:
    """libretro-database's GBA cheat files through the cache in dist/cheats,
    one request at a time, delay seconds apart."""

    def __init__(self, cache, api_url, raw_url, delay, offline):
        self.cache = Path(cache)
        self.api_url = api_url.rstrip("/") + "/"
        self.raw_url = raw_url.rstrip("/") + "/"
        self.delay = delay
        self.offline = offline
        self.requests = 0
        self.downloaded = 0
        self.last = 0.0
        self.stopped = None
        self.listing = {}

    def request(self, url, accept=None):
        wait = self.last + self.delay - time.monotonic()
        if wait > 0:
            time.sleep(wait)
        self.requests += 1
        headers = {"User-Agent": USER_AGENT}
        if accept:
            headers["Accept"] = accept
        try:
            req = urllib.request.Request(url, headers=headers)
            with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
                return r.read()
        finally:
            self.last = time.monotonic()

    def load_listing(self, refresh):
        """{file name: blob sha} of the database's GBA cheat files."""
        path = self.cache / LISTING
        if path.is_file() and not refresh:
            self.listing = json.loads(path.read_text("utf-8"))
            return self.listing
        if self.offline:
            if path.is_file():
                self.listing = json.loads(path.read_text("utf-8"))
                return self.listing
            raise CheatsError(f"{path} isn't there, and --offline downloads nothing")
        url = self.api_url + urllib.parse.quote(CHT_DIR)
        print(f"downloading the list of {CHT_DIR}", file=sys.stderr)
        try:
            entries = json.loads(self.request(url, "application/vnd.github+json"))
        except (OSError, ValueError) as e:
            raise CheatsError(f"can't download {url}: {e}") from e
        if not isinstance(entries, list):
            raise CheatsError(f"{url} gave no file list")
        self.listing = {
            e["name"]: e.get("sha", "")
            for e in entries
            if isinstance(e, dict)
            and e.get("type") == "file"
            and str(e.get("name", "")).endswith(".cht")
        }
        self.cache.mkdir(parents=True, exist_ok=True)
        tmp = path.with_name(path.name + ".part")
        tmp.write_text(
            json.dumps(self.listing, indent=1, sort_keys=True) + "\n", "utf-8"
        )
        tmp.replace(path)
        return self.listing

    def path(self, file):
        return self.cache / SYSTEM / file

    def get(self, file):
        """A cheat file's text from the cache, downloading it if it's missing
        or changed (its sha isn't the listing's); None if it can't be had."""
        path = self.path(file)
        if path.is_file():
            data = path.read_bytes()
            sha = self.listing.get(file)
            if self.offline or self.stopped or not sha or blob_sha(data) == sha:
                return decode(data)
        elif self.offline or self.stopped:
            return None
        url = self.raw_url + urllib.parse.quote(f"{CHT_DIR}/{file}")
        try:
            data = self.request(url)
        except urllib.error.HTTPError as e:
            if e.code == 404:
                print(f"cheats: {url}: not there", file=sys.stderr)
                return None
            self.stopped = f"the server answers {e.code}: run again later"
            return decode(path.read_bytes()) if path.is_file() else None
        except OSError as e:
            self.stopped = f"can't reach {self.raw_url} ({getattr(e, 'reason', e)})"
            return decode(path.read_bytes()) if path.is_file() else None
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_name(path.name + ".part")
        tmp.write_bytes(data)
        tmp.replace(path)
        self.downloaded += 1
        return decode(data)


# --- The card ------------------------------------------------------------------------------


def read_crcs(sd, games):
    """{card path: crc} from crc.txt where its record holds, else computed."""
    torii = Path(sd) / "torii"
    old = names_db.parse_cache(names_db.read_safe(torii / names_db.CRC_FILE))
    written = names_db.cache_mtime(torii)
    out = {}
    for card_path, file_path, _ in games:
        size = Path(file_path).stat().st_size
        rec = old.get(card_path.lower())
        if names_db.record_holds(rec, file_path, size, written):
            out[card_path] = rec[0]
        else:
            out[card_path] = names_db.file_crc(file_path)[0]
    return out


def cheat_path(sd, card_path):
    """/torii/cheats/<rom path without its extension>.cht on the card."""
    rel = card_path.lstrip("/")
    stem = rel[: rel.rindex(".")] if "." in rel.rsplit("/", 1)[-1] else rel
    return Path(sd) / CHEATS_DIR / (stem + ".cht")


def run(args):
    sd = Path(args.sd_path)
    if not sd.is_dir():
        raise CheatsError(
            f"{sd} isn't a folder: give the card's folder, e.g. /Volumes/OMEGA"
        )
    dat = names_db.CONSOLES[GBA][1]
    text = names_db.fetch(Path(args.dats), dat, args.offline, False).read_text(
        "utf-8", "replace"
    )
    version, dat_games = names_db.parse_dat(text)
    tables, codes, _ = names_db.make_tables([text, "", "", ""])

    fetcher = Fetcher(args.cache, args.api_url, args.raw_url, args.delay, args.offline)
    db = Database(fetcher.load_listing(args.refresh), dat_games)

    games = [g for g in names_db.walk_card(sd) if g[2] == GBA]
    crcs = read_crcs(sd, games)
    rows = {"written": [], "updated": [], "unchanged": [], "none": [], "unmatched": []}
    for card_path, file_path, _ in games:
        crc = crcs[card_path]
        header = header_info(file_path)
        name = names_db.db_name(tables, GBA, crc)
        if name is None and header is not None:
            name = names_db.code_match(codes, GBA, header[0], Path(card_path).name)
        if name is None:
            rows["unmatched"].append(
                (
                    card_path,
                    f"CRC {crc:08X} isn't in the dat (a romhack or a bad dump?)",
                )
            )
            continue
        group, why = db.match(name, header)
        if group is None:
            if why is None:
                rows["none"].append((card_path, name))
            else:
                rows["unmatched"].append((card_path, f"{name}: {why}"))
            continue
        files = db.groups[group][1]
        texts = [fetcher.get(f) for f in files]
        if any(t is None for t in texts):
            rows["unmatched"].append(
                (card_path, f"{name}: {'; '.join(files)} not downloaded")
            )
            continue
        database = [c for t in texts for c in parse_cht(t)]
        target = cheat_path(sd, card_path)
        old_text = read_card_file(target)
        old = parse_cht(old_text) if old_text is not None else []
        cheats, own = merge(database, old)
        if old_text is None:
            how = "written"
        elif cheats == old:
            how = "unchanged"
        else:
            how = "updated"
        if how != "unchanged" and not args.dry_run:
            write_card_file(target, format_cht(cheats))
        picked = sum(c[2] for c in cheats)
        n = len(cheats) - own
        detail = f"{n} cheat{'' if n == 1 else 's'} from {'; '.join(files)}"
        if own:
            detail += f", {own} of your own"
        if picked:
            detail += f", {picked} picked"
        rel = "/" + target.relative_to(sd).as_posix()
        rows[how].append((card_path, f"{rel}: {detail}"))
    report(rows, args.dry_run, fetcher, version, len(games))


def report(rows, dry_run, fetcher, version, total):
    would = "would be " if dry_run else ""
    labels = [
        ("written", f"{would}written"),
        ("updated", f"{would}updated"),
        ("unchanged", "unchanged"),
        ("none", "without cheats in the database"),
        ("unmatched", "not matched"),
    ]
    counts = ", ".join(f"{len(rows[k])} {label}" for k, label in labels)
    print(f"{total} GBA games: {counts}")
    for k, label in labels:
        if rows[k]:
            print(f"{label}:")
            for path, what in sorted(rows[k]):
                print(f"  {path}: {what}")
    print(
        f"{fetcher.requests} requests, {fetcher.downloaded} cheat files downloaded; "
        f"dat {version or '(no version)'}"
    )
    if fetcher.stopped:
        print(f"cheats: stopped fetching: {fetcher.stopped}", file=sys.stderr)
    print(ATTRIBUTION)


def main(argv):
    p = argparse.ArgumentParser(
        prog="cheats.py",
        description=__doc__.split("\n\n")[0],
        epilog=__doc__.split("\n\n", 1)[1],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("sd_path", metavar="SD_PATH", help="the card's folder on this PC")
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="change nothing on the card, print what would change",
    )
    p.add_argument(
        "--offline", action="store_true", help="never download: use the caches"
    )
    p.add_argument(
        "--refresh",
        action="store_true",
        help="download the database's file list again, and the files that changed",
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
        help=f"the cheat files' cache (default {CACHE.relative_to(ROOT)})",
    )
    p.add_argument(
        "--dats",
        default=str(names_db.DATS),
        help=f"the dats' cache (default {names_db.DATS.relative_to(ROOT)})",
    )
    p.add_argument("--api-url", default=API_URL, help=argparse.SUPPRESS)
    p.add_argument("--raw-url", default=RAW_URL, help=argparse.SUPPRESS)
    args = p.parse_args(argv)
    try:
        run(args)
    except (CheatsError, names_db.NamesError) as e:
        print(f"cheats: {e}", file=sys.stderr)
        return 1
    except OSError as e:
        print(f"cheats: {names_db.os_error(e)}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\ncheats: stopped", file=sys.stderr)
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
