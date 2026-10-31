#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Write cheat files for an SD card's GBA games from GameHacking.org.

usage: cheats.py SD_PATH [--gamehacking] [--import FILE...] [--only CRC,...]
                 [--base-game] [--similar PCT] [--dry-run] [--delay S]
                 [--downloads DIR] [--refresh] [--cache DIR]

`just cheats SD_PATH` runs this. SD_PATH is the card's folder on this PC (a
mounted card, say /Volumes/OMEGA). Cheats come from GameHacking.org's mGBA
exports, matched to each GBA game on the card by its exact CRC32 (from
/torii/crc.txt where its record holds, else computed): no title or region
guessing.

Fetching (--gamehacking) happens in your own browser, never from this tool:
it writes a harvester script, dist/cheats/gamehacking/harvest.js, holding
the CRCs it still needs, copies it to the clipboard (macOS), opens
gamehacking.org's GBA list and tells you how to paste it into the browser's
console. In the page, the harvester crawls the GBA index pages it still
needs, exports the mGBA cheats of each wanted dump, --delay seconds between
requests (default 5; the site's robots.txt asks for 30, and the owner has
GameHacking's approval for less), and downloads
what it got as torii-gamehacking-*.json bundles (every 25 exports and at the
end). It stops at anything that isn't the page or export it expects (a
Cloudflare check, say). Meanwhile this tool watches your downloads folder
(--downloads, default ~/Downloads) and imports each bundle into
dist/cheats/gamehacking (index.json and <CRC>.cheats); Ctrl-C stops waiting,
and what was imported stays. --import FILE... imports bundles by hand.
Without --gamehacking only that cache is used. --only CRC,... limits the
run to those CRCs (on the card or not, for fetching); CRC=ID, with the
dump's GameHacking game ID (gamehacking.org/game/ID), exports it at once
without crawling the index for it.

Writing a game's file, /torii/cheats/<rom path without extension>.cht:

- The source is the export whose CRC is the ROM's own, or
  dist/cheats/custom/<CRC>.cht (a libretro file you supply), which wins.
- Each mGBA cheat set becomes a libretro cheat. Its device directive
  (!GSAv1, !GSAv1 raw, !PARv3, !PARv3 raw) is written as cheatN_format; a
  set with none gets none, and the kernel detects its format. Sets with no
  name (or only "?"), of more than 256 code lines, or the same as an earlier
  one (device and code) are dropped, then those the kernel would grey
  (sift), and the largest sets while the file is over the kernel's limit:
  a file past CHEATFILE_TEXT_MAX or CHEATFILE_MAX cheats is read in
  windows (source/cheatstore.h), so up to BIG_CHEATS cheats and BIG_BYTES
  bytes. An older kernel (v0.9 and before) refuses a file over 192 KB.
- Every encrypted code is written decrypted (decrypt: GameShark and Action
  Replay codes raw, CodeBreaker ones plain, key and seed lines left out),
  and the master codes go: Torii hooks the game itself, so with nothing
  encrypted they do nothing. A file past the kernel's one read gets its
  index beside it, <rom path less extension>.cdx (source/cheatindex.h),
  which the kernel reads it in windows by; a smaller file's old index goes.
- Picks carry over by what the cheat does (its program's hash), from the old
  file's enable lines and its old index (where the kernel keeps a larger
  file's picks), so a pick outlives its code being written anew.
- The first line is "# for crc32 XXXXXXXX", the ROM the file is for (the
  kernel refuses a file made for another dump), then where it came from.
- An existing file (read as the kernel reads it) keeps its picks, matched
  by code, cheatN_format and cheatN_user, placeholders filled in on the cart
  (00??) and, after the source's cheats, every code of yours: those marked
  cheatN_user or picked, or any the source lacks when the file wasn't
  written by this tool. NAME is written as plain text with no CRC line, as
  edited by hand; NAME.bak is left alone, and so is a file that wouldn't
  change.

Romhacks and modified dumps (a CRC GameHacking doesn't list) get their base
game's cheats only when the base is certain: the index's dumps whose serial
holds the ROM header's game code (0xAC) and whose revision is its version
byte (0xBC) must be exactly one, and that dump's ROM must be on the card.
If at least --similar percent (default 90) of the base's 4 KB blocks are the
same in the hack, the base's cheats are written, each named "... (base
game)", less any ROM patch over bytes the hack changed (decoded by the
kernel's own decoder, tools/cheat_patches.c, built with the host's cc).
--base-game writes them for every hack whatever the similarity, and without
the base ROM on the card drops every cheat that patches the ROM.

It never touches /CHEAT or EZ-Flash's files. It reports each game: written,
updated, unchanged, base game, no cheats on GameHacking, not fetched yet,
hack too different, base dump not on card or ambiguous base; and every set
it dropped, with why.

The cheats are fetched on your PC, for your own card, and never bundled
with Torii.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import webbrowser
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import names_db

CACHE = ROOT / "dist" / "cheats"
GAMEHACKING = "gamehacking"
CUSTOM = "custom"
INDEX = "index.json"
HARVEST = "harvest.js"
HARVEST_TEMPLATE = ROOT / "tools" / "cheats_harvest.js"
HARVEST_CONFIG = "/*TORII_CONFIG*/ null"
BUNDLE_GLOB = "torii-gamehacking-*.json"
BUNDLE_FORMAT = "torii-gamehacking"
GBA_PAGE = "https://gamehacking.org/system/gba"
GAME_URL = "https://gamehacking.org/game/{}"
DELAY = 5.0  # seconds between requests (robots.txt's Crawl-delay is 30; GameHacking approved less)
SIMILAR = 90.0  # percent of the base's blocks the same: a light hack
BLOCK = 4096
CHEATS_DIR = Path("torii") / "cheats"
GBA = names_db.GBA

# The kernel's limits (source/cheatfile.h; tests/cheats_test.py checks them)
TEXT_MAX = 0x30000  # CHEATFILE_TEXT_MAX: a longer file is refused
CHEATS_MAX = 1024  # CHEATFILE_MAX
LINES_MAX = 256  # CHEATFILE_OPS_MAX: a set of more lines could never run
# Left free under TEXT_MAX: a pick adds its enable line (unpicked cheats have
# none), and codes typed on the cart's keypad are added to the text; 32 KB
# covers a pick of every cheat (about 24 bytes each) and room to type
ROOM = 0x8000
# A larger file's windows (source/cheatindex.h, cheatstore.h): one closes at
# WINDOW_CHEATS cheats or WINDOW_BYTES bytes, at most WINDOWS of them; a
# file is fit to BIG_CHEATS and BIG_BYTES, and fewer while its index (which
# tools/cheat_patches.c --index cuts as the kernel does) needs more windows
WINDOW_CHEATS = 250  # CHEATINDEX_CHEATS
WINDOW_BYTES = 0xA000  # CHEATINDEX_BYTES
WINDOWS = 64  # CHEATSTORE_WINDOWS
BIG_CHEATS = 15000
BIG_BYTES = 0x240000
INDEX_EXT = ".cdx"  # CHEATINDEX_EXT: a larger file's index, beside it
# cheatN_format's values by enum cheat_format (source/cheatfile.c's
# cheatfile_format_names); the first four are mGBA's GBA directives
FORMAT_NAMES = (None, "GSAv1", "GSAv1 raw", "PARv3", "PARv3 raw", "CodeBreaker", "VBA")
DIRECTIVES = FORMAT_NAMES[1:5]
FOR_CRC = "# for crc32 "


class CheatsError(Exception):
    pass


def plural(n, word):
    return f"{n} {word}{'' if n == 1 else 's'}"


# --- libretro cheat files (what the kernel reads) -----------------------------------------

LINE = re.compile(r"^\s*([A-Za-z0-9_]+)\s*=\s*(.*?)\s*$")


def value(raw):
    """A config value byte for byte, without the quote at each end. A quote
    missing at one end isn't doubled when the file is written back."""
    return raw.removeprefix('"').removesuffix('"')


# The keys Torii adds to a cheat (source/cheatfile.h), kept as they are:
# cheatN_format, a format to decode it in (quoted), cheatN_user, a code
# typed on the cart, and cheatN_renamed, a database cheat named on the cart
# (bare bools); cheatN_folder, the heading the Cheats screen shows it under
# (quoted; set in the file, never on the cart, so a merge takes the
# database's when it has one)
KEPT_KEYS = ("format", "user", "renamed", "folder")
BARE_KEYS = ("enable", "user", "renamed")
# A code's digits left for the user to fill in (cheatcode.c's is_mark)
PLACEHOLDER = re.compile(r"[?XxYyZz]")


class Cheat(tuple):
    """A cheat as (desc, code, enabled), with Torii's own keys
    (KEPT_KEYS) in .extra; equal to a plain tuple of the same three when it
    has none."""

    def __new__(cls, desc, code, enabled, extra=None):
        cheat = super().__new__(cls, (desc, code, enabled))
        cheat.extra = dict(extra or {})
        return cheat

    def __eq__(self, other):
        return tuple.__eq__(self, other) and self.extra == getattr(other, "extra", {})

    def __ne__(self, other):
        return not self == other

    __hash__ = tuple.__hash__

    def __repr__(self):
        base = tuple.__repr__(self)
        return f"{base[:-1]}, {self.extra!r})" if self.extra else base


def is_true(v):
    return str(v).casefold() in ("true", "1")


def user_code(cheat):
    return is_true(getattr(cheat, "extra", {}).get("user", ""))


def parse_cht(text):
    """A libretro cheat file's cheats, in their order: [Cheat(desc, code,
    enabled)], Torii's keys in each one's extra. '#' lines are comments; a
    cheat without a code is dropped."""
    found = {}
    keys = "|".join(("desc", "code", "enable") + KEPT_KEYS)
    for raw in text.split("\n"):
        line = raw.removesuffix("\r")
        if line.lstrip().startswith("#"):
            continue
        m = LINE.match(line)
        if not m:
            continue
        k = re.fullmatch(rf"cheat(\d+)_({keys})", m.group(1))
        if k:
            found.setdefault(int(k.group(1)), {})[k.group(2)] = value(m.group(2))
    out = []
    for n in sorted(found):
        c = found[n]
        if "code" in c:
            out.append(
                Cheat(
                    c.get("desc", ""),
                    c["code"],
                    is_true(c.get("enable", "")),
                    {k: c[k] for k in KEPT_KEYS if k in c},
                )
            )
    return out


def cheat_lines(n, cheat):
    # No enable line for a cheat that isn't picked: off is the default, and
    # the kernel adds the line when one is picked (compact: FireRed's 773
    # cheats are 18 KB smaller)
    desc, code, enabled = cheat
    lines = [f'cheat{n}_desc = "{desc}"', f'cheat{n}_code = "{code}"']
    if enabled:
        lines.append(f"cheat{n}_enable = true")
    extra = getattr(cheat, "extra", {})
    for k in KEPT_KEYS:
        if k in extra:
            v = extra[k]
            lines.append(
                f"cheat{n}_{k} = {v}" if k in BARE_KEYS else f'cheat{n}_{k} = "{v}"'
            )
    return lines


def format_cht(cheats, comments=()):
    """libretro's format, as its files are laid out, after the '#' comment
    lines given; Torii's keys after a cheat's enable line, as the kernel
    writes them."""
    lines = [f"# {c}" if c else "#" for c in comments]
    lines += [f"cheats = {len(cheats)}", ""]
    for n, cheat in enumerate(cheats):
        lines += cheat_lines(n, cheat) + [""]
    return "\n".join(lines)


def head_comments(text):
    """The '#' lines before a file's first key line: the ones this tool writes."""
    out = []
    for raw in text.split("\n"):
        line = raw.strip()
        if line and not line.startswith("#"):
            break
        if line:
            out.append(line)
    return out


def for_crc(text):
    """The CRC a file's "# for crc32 XXXXXXXX" line names (its first line
    that isn't blank), or None."""
    for raw in text.split("\n"):
        line = raw.strip()
        if line:
            m = re.fullmatch(r"# for crc32 ([0-9A-Fa-f]{8})", line)
            return int(m.group(1), 16) if m else None
    return None


def fills(placeholder, code):
    """Whether code is placeholder with its digits to fill in (00??) filled:
    the same length, every other character the same."""
    return len(code) == len(placeholder) and all(
        PLACEHOLDER.match(p) or p == c for p, c in zip(placeholder, code, strict=True)
    )


def merge(database, old, keep_all=True):
    """The database's cheats with old's picks and Torii's keys (by code),
    and old's name for one renamed on the cart (cheatN_renamed), then
    old's own cheats: those the database lacks that are cheatN_user or
    picked, or all of them with keep_all. A database cheat with digits to
    fill in (00??) that old has filled in (the same desc, a code without
    them, as the kernel's keypad saves it in place) is old's, in the
    database's place. Returns (cheats, own, stale): own counts the old
    cheats after the database's, stale those left out."""
    by_code = {}
    for cheat in old:
        by_code.setdefault(cheat[1], cheat)
    codes = {c[1] for c in database}
    filled = {}
    for cheat in old:
        # The user's own codes are never a database placeholder filled in
        if (
            cheat[1] not in codes
            and not PLACEHOLDER.search(cheat[1])
            and not user_code(cheat)
        ):
            filled.setdefault(cheat[0], cheat)
    out, seen, taken = [], set(), set()
    for cheat in database:
        desc, code, enabled = cheat
        if code in seen:
            continue
        seen.add(code)
        mine = None
        if PLACEHOLDER.search(code):
            # By name, or, once renamed on the cart, by the code's other digits
            mine = filled.get(desc)
            if mine is None or id(mine) in taken:
                mine = next(
                    (
                        c
                        for c in filled.values()
                        if id(c) not in taken and fills(code, c[1])
                    ),
                    None,
                )
        if mine is not None and id(mine) not in taken:
            taken.add(id(mine))
            out.append(mine)
            continue
        was = by_code.get(code)
        extra = getattr(cheat, "extra", {})
        if was is not None:
            kept = getattr(was, "extra", {})
            name = was[0] if is_true(kept.get("renamed", "")) else desc
            mine = {**extra, **kept}
            if "folder" in extra:
                mine["folder"] = extra["folder"]
            out.append(Cheat(name, code, was[2], mine))
        else:
            out.append(Cheat(desc, code, enabled, extra))
    rest = [c for c in old if c[1] not in seen and id(c) not in taken]
    own = [c for c in rest if keep_all or c[2] or user_code(c)]
    return out + own, len(own), len(rest) - len(own)


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


def write_file(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".part")
    try:
        tmp.write_bytes(data)
        tmp.replace(path)
    except OSError:
        tmp.unlink(missing_ok=True)
        raise


def write_card_file(path, text):
    write_file(path, text.encode("utf-8", "surrogateescape"))


# --- mGBA exports (GameHacking.org's) ----------------------------------------------------


@dataclass
class MgbaSet:
    name: str
    directive: str | None  # its own GS directive, as mGBA's parser reads it
    lines: list


def parse_mgba(text):
    """An mGBA cheat file's sets, as mGBA's mCheatParseFile reads them: '!'
    lines are directives ("!disabled", "!reset", "!GSAv1"...) for the set
    the next '#' line starts, '# NAME' starts a set, any other line is a
    code line of the set (lines before the first '#' make a nameless one).
    Each set's directive is the last of GSAv1, GSAv1 raw, PARv3 and PARv3
    raw (mGBA's names, case and all) given since the set before. mGBA keeps
    a directive for every later set too until "!reset"; Torii doesn't: a set
    with none of its own is left to the kernel's detection."""
    sets, pending, cur = [], [], None
    for raw in text.split("\n"):
        line = raw.strip()
        if not line:
            continue
        if line[0] == "#":
            cur = MgbaSet(line[1:].strip(), own_directive(pending), [])
            sets.append(cur)
            pending = []
        elif line[0] == "!":
            d = line[1:].strip()
            if d.casefold() == "reset":
                pending = []
            elif d.casefold() != "disabled":
                pending.append(d)
        else:
            if cur is None:
                cur = MgbaSet("", own_directive(pending), [])
                sets.append(cur)
                pending = []
            cur.lines.append(" ".join(line.split()))
    return sets


def own_directive(pending):
    found = [d for d in pending if d in DIRECTIVES]
    return found[-1] if found else None


def clean_desc(name):
    """A desc as the kernel's keypad writes one: no control characters, '"'
    as "'"."""
    return (
        "".join(c for c in name if c >= " " and c != "\x7f").replace('"', "'").strip()
    )


def convert(sets, suffix=""):
    """The libretro cheats of an export's sets, and the sets dropped:
    ([Cheat], [(name, why)]). suffix goes after each name."""
    out, dropped, seen = [], [], {}
    for s in sets:
        name = clean_desc(s.name)
        if not name.strip("? "):
            dropped.append((name or "(no name)", "no name"))
            continue
        if not s.lines:
            dropped.append((name, "no code lines"))
            continue
        if len(s.lines) > LINES_MAX:
            dropped.append((name, f"{len(s.lines)} code lines, over {LINES_MAX}"))
            continue
        code = "+".join(s.lines)
        if any(c == '"' or c < " " for c in code):
            dropped.append((name, "a quote or control character in its code"))
            continue
        key = (s.directive, code.upper())
        if key in seen:
            dropped.append((name, f"the same as {seen[key]}"))
            continue
        seen[key] = name
        extra = {"format": s.directive} if s.directive else {}
        out.append(Cheat(name + suffix, code, False, extra))
    return out, dropped


def fit(cheats, removable, comments, limit=BIG_BYTES, most=BIG_CHEATS):
    """Drops cheats until the file's text is within limit bytes and holds at
    most `most` cheats: the largest of the removable ones (indices) that
    aren't picked first, then picked ones. Returns (cheats, [dropped])."""
    cheats = list(cheats)

    def size(cs):
        return len(format_cht(cs, comments).encode("utf-8", "surrogateescape"))

    sizes = {
        i: len("\n".join(cheat_lines(0, c)).encode("utf-8", "surrogateescape")) + 2
        for i, c in enumerate(cheats)
    }
    order = sorted(removable, key=lambda i: (cheats[i][2], -sizes[i], i))
    gone = set()
    total = size(cheats)
    for i in order:
        if total <= limit and len(cheats) - len(gone) <= most:
            # The estimate fits: check it on the text itself
            total = size([c for j, c in enumerate(cheats) if j not in gone])
            if total <= limit:
                break
        gone.add(i)
        total -= sizes[i]
    kept = [c for i, c in enumerate(cheats) if i not in gone]
    return kept, [cheats[i][0] for i in sorted(gone)]


# --- The cache: GameHacking.org's index and exports ----------------------------------------


def gh_dir(cache):
    return Path(cache) / GAMEHACKING


def crc_hex(crc):
    return f"{crc:08X}"


def parse_crc(text):
    t = text.strip().upper().removeprefix("0X")
    if not re.fullmatch(r"[0-9A-F]{1,8}", t):
        raise CheatsError(f"{text!r} isn't a CRC32 (8 hex digits)")
    return int(t, 16)


class Index:
    """dist/cheats/gamehacking/index.json: the dumps the GBA index pages
    list ({CRC hex: {id, name, title, serial, size, codes}}), the pages
    crawled, the last page, and the bundles imported."""

    def __init__(self, cache):
        self.path = gh_dir(cache) / INDEX
        data = {}
        if self.path.is_file():
            try:
                data = json.loads(self.path.read_text("utf-8"))
            except ValueError as e:
                raise CheatsError(f"{self.path}: {e}") from e
        self.entries = dict(data.get("entries", {}))
        self.pages = set(data.get("pages", []))
        self.last_page = int(data.get("last_page", -1))
        self.imported = list(data.get("imported", []))

    def complete(self):
        """Whether every index page is crawled: a CRC it lacks isn't listed."""
        return self.last_page >= 0 and all(
            n in self.pages for n in range(self.last_page + 1)
        )

    def save(self):
        data = {
            "last_page": self.last_page,
            "pages": sorted(self.pages),
            "entries": dict(sorted(self.entries.items())),
            "imported": self.imported,
        }
        write_file(self.path, (json.dumps(data, indent=1) + "\n").encode())

    def get(self, crc):
        return self.entries.get(crc_hex(crc))


def export_path(cache, crc):
    return gh_dir(cache) / f"{crc_hex(crc)}.cheats"


def custom_path(cache, crc):
    return Path(cache) / CUSTOM / f"{crc_hex(crc)}.cht"


def revision(name):
    """The revision a dump's name gives: "(Rev 1)" is 1, none 0 (the ROM
    header's version byte). cheats_harvest.js's revision()."""
    m = re.search(r"\(Rev (\d+)\)", name or "", re.IGNORECASE)
    return int(m.group(1)) if m else 0


def base_candidates(entries, code, version):
    """The index's dumps (CRC hex) whose serial holds a ROM header's game
    code and whose revision is its version. cheats_harvest.js's
    baseCandidates()."""
    out = []
    for crc, e in entries.items():
        serials = re.split(r"[^A-Z0-9]+", (e.get("serial") or "").upper())
        if code.upper() in serials and revision(e.get("name")) == version:
            out.append(crc)
    return sorted(out)


# The harvester's index-page parser, in Python (cheats_harvest.js's
# parseIndexPage): the tests hold the two to one fixture


def cell_text(html_text):
    import html

    return " ".join(html.unescape(re.sub(r"<[^>]*>", " ", html_text)).split())


def parse_index_page(text):
    if not re.search(r"Number of Codes", text, re.IGNORECASE) or not re.search(
        r"<table\b", text, re.IGNORECASE
    ):
        return None
    last = max((int(n) for n in re.findall(r"/system/gba/all/(\d+)", text)), default=-1)
    rows, title = [], ""
    for tr in re.finditer(r"<tr\b[^>]*>([\s\S]*?)</tr>", text, re.IGNORECASE):
        th = re.search(
            r"<th\b[^>]*\bcolspan=[\"']?8[\"']?[^>]*>([\s\S]*?)</th>",
            tr.group(1),
            re.IGNORECASE,
        )
        if th:
            title = cell_text(th.group(1))
            continue
        cells = re.findall(r"<td\b[^>]*>([\s\S]*?)</td>", tr.group(1), re.IGNORECASE)
        if len(cells) < 5:
            continue
        link = re.search(
            r"<a\b[^>]*\bhref=[\"']/game/(\d+)[\"'][^>]*>([\s\S]*?)</a>",
            cells[0],
            re.IGNORECASE,
        )
        crc = cell_text(cells[3]).upper()
        if not link or not re.fullmatch(r"[0-9A-F]{8}", crc):
            continue
        codes = re.match(r"\d+", cell_text(cells[4]))
        rows.append(
            {
                "crc": crc,
                "id": int(link.group(1)),
                "name": cell_text(link.group(2)),
                "title": title,
                "serial": cell_text(cells[1]),
                "size": cell_text(cells[2]),
                "codes": int(codes.group()) if codes else 0,
            }
        )
    return {"lastPage": last, "rows": rows}


ENTRY_KEYS = ("id", "name", "title", "serial", "size", "codes")


def import_bundle(path, cache, index):
    """Imports a harvester bundle into the cache: its index rows, pages and
    exports. Returns (bundle, exports written), or (None, why) for a file
    that isn't one."""
    try:
        bundle = json.loads(Path(path).read_text("utf-8"))
    except (OSError, ValueError) as e:
        return None, f"not readable JSON ({e})"
    if not isinstance(bundle, dict) or bundle.get("format") != BUNDLE_FORMAT:
        return None, "not a torii-gamehacking bundle"
    entries = bundle.get("index") or {}
    exports = bundle.get("exports") or {}
    if not isinstance(entries, dict) or not isinstance(exports, dict):
        return None, "its index or exports aren't objects"
    for key, e in entries.items():
        crc = key.upper()
        if (
            re.fullmatch(r"[0-9A-F]{8}", crc)
            and isinstance(e, dict)
            and isinstance(e.get("id"), int)
        ):
            index.entries[crc] = {k: e.get(k) for k in ENTRY_KEYS}
    index.pages |= {n for n in bundle.get("pages") or [] if isinstance(n, int)}
    if isinstance(bundle.get("lastPage"), int):
        index.last_page = max(index.last_page, bundle["lastPage"])
    written = 0
    for crc, text in sorted(exports.items()):
        if not re.fullmatch(r"[0-9A-Fa-f]{8}", crc) or not isinstance(text, str):
            continue
        if text.lstrip().startswith("<"):
            print(
                f"cheats: {Path(path).name}: {crc.upper()} is a web page, not cheats: left out",
                file=sys.stderr,
            )
            continue
        write_file(export_path(cache, int(crc, 16)), text.encode("utf-8"))
        written += 1
    if bundle.get("id") and bundle["id"] not in index.imported:
        index.imported.append(bundle["id"])
    index.save()
    return bundle, written


def bundle_files(folder):
    """{path: (size, mtime)} of the bundles in a downloads folder."""
    out = {}
    for p in sorted(Path(folder).glob(BUNDLE_GLOB)):
        try:
            st = p.stat()
        except OSError:
            continue
        out[p] = (st.st_size, st.st_mtime)
    return out


def watch(folder, cache, index, run_id, poll=1.0, sleep=time.sleep, rounds=None):
    """Imports the bundles that show up in folder (any not imported yet,
    once its size holds for a poll) until this run's last one (done)
    arrives. Returns that bundle, or None if stopped (Ctrl-C, or after
    `rounds` polls)."""
    seen, sizes = set(), {}
    n = 0
    try:
        while rounds is None or n < rounds:
            n += 1
            for path, stat in bundle_files(folder).items():
                if (path, stat) in seen:
                    continue
                if sizes.get(path) != stat:
                    sizes[path] = stat  # still being written, perhaps
                    continue
                seen.add((path, stat))
                try:
                    head = json.loads(path.read_text("utf-8"))
                except (OSError, ValueError):
                    continue
                if not isinstance(head, dict) or head.get("id") in index.imported:
                    continue
                bundle, got = import_bundle(path, cache, index)
                if bundle is None:
                    print(f"cheats: {path.name}: {got}: left out", file=sys.stderr)
                    continue
                print(
                    f"imported {path.name}: {plural(got, 'export')}, "
                    f"{plural(len(bundle.get('pages') or []), 'index page')}"
                )
                if bundle.get("run") == run_id and bundle.get("done"):
                    if bundle.get("stopped"):
                        print(f"the harvester stopped: {bundle['stopped']}")
                    return bundle
            sleep(poll)
    except KeyboardInterrupt:
        print("\nstopped waiting: what was imported stays")
    return None


# --- Romhacks -------------------------------------------------------------------------------


def similarity(base, hack, block=BLOCK):
    """The share of base's blocks the same, at the same offset, in hack."""
    n = (len(base) + block - 1) // block
    if not n:
        return 0.0
    same = sum(
        base[i : i + block] == hack[i : i + block] for i in range(0, len(base), block)
    )
    return same / n


def patch_unchanged(base, hack, patch):
    """Whether the bytes a ROM patch (offset, value, mask) writes are the
    same in base and hack."""
    offset, _, mask = patch
    for k in (0, 1):
        if (mask >> (8 * k)) & 0xFF:
            at = offset + k
            if at >= len(base) or at >= len(hack) or base[at] != hack[at]:
                return False
    return True


PATCH_SOURCES = (
    "tools/cheat_patches.c",
    "source/cheatfile.c",
    "source/cheatcode.c",
    "source/cheatlower.c",
    "source/cheatindex.c",
    "source/crc32.c",
)
PATCH_DEPS = PATCH_SOURCES + (
    "source/cheatfile.h",
    "source/cheatcode.h",
    "source/cheatindex.h",
    "source/crc32.h",
    "tests/host/gba_base.h",
)


def patch_tool():
    """tools/cheat_patches.c built for this PC (dist/host/cheat_patches-HASH),
    or None without a C compiler."""
    import hashlib

    h = hashlib.sha256()
    for f in PATCH_DEPS:
        h.update((ROOT / f).read_bytes())
    exe = ROOT / "dist" / "host" / f"cheat_patches-{h.hexdigest()[:16]}"
    if exe.is_file():
        return exe
    cc = (
        os.environ.get("CC")
        or shutil.which("cc")
        or shutil.which("gcc")
        or shutil.which("clang")
    )
    if not cc:
        return None
    exe.parent.mkdir(parents=True, exist_ok=True)
    tmp = exe.with_name(exe.name + f".{os.getpid()}")
    cmd = [
        cc,
        "-std=gnu11",
        "-O1",
        "-funsigned-char",
        "-DCHEAT_RAW_LINES",
        "-I",
        str(ROOT / "tests" / "host"),
        "-o",
        str(tmp),
    ]
    r = subprocess.run(
        cmd + [str(ROOT / f) for f in PATCH_SOURCES],
        capture_output=True,
        text=True,
        check=False,
    )
    if r.returncode:
        print(
            f"cheats: can't build tools/cheat_patches.c:\n{r.stderr}", file=sys.stderr
        )
        tmp.unlink(missing_ok=True)
        return None
    tmp.replace(exe)
    return exe


# The kernel's enum cheatfile_status (source/cheatfile.h), as cheat_patches prints it
(
    ST_OK,
    ST_MASTER,
    ST_BIG,
    ST_UNREADABLE,
    ST_PLACEHOLDER,
    ST_CANT_RUN,
    ST_NOTHING,
    ST_SAME,
) = range(1, 9)
# Why sift drops a cheat of each status the kernel would grey
SIFTED = {
    ST_BIG: "too big to run",
    ST_UNREADABLE: "the kernel can't read it",
    ST_CANT_RUN: "can't run on the cart",
    ST_NOTHING: "does nothing",
    ST_SAME: "does what an earlier cheat does",
}
PART_CHEATS = 1000  # a part's cheats when cheat_patches reads a list in parts
PART_BYTES = 180000  # and its text at most, under CHEATFILE_TEXT_MAX


def parts(cheats):
    """cheats split into runs that each make a file the kernel reads whole:
    [(start, end)]."""
    out, start = [], 0
    while start < len(cheats):
        end = min(start + PART_CHEATS, len(cheats))
        while (
            end - start > 1
            and len(format_cht(cheats[start:end]).encode("utf-8", "surrogateescape"))
            > PART_BYTES
        ):
            end = start + max(1, (end - start) * 3 // 4)
        out.append((start, end))
        start = end
    return out


def decode_cheats(cheats, raw=False):
    """Each cheat's (status, hash, patches, plain) as the kernel decodes the
    list in order (tools/cheat_patches.c, in parts past one file's limits),
    or None if the decoder can't be built. patches are [(offset, value,
    mask)]; plain, with raw, is the cheat decrypted (format, code) or None
    where it can't be written plain."""
    exe = patch_tool()
    if exe is None:
        return None
    if not cheats:
        return []
    spans = parts(cheats)
    with tempfile.TemporaryDirectory() as tmp:
        files = []
        for k, (start, end) in enumerate(spans):
            f = Path(tmp) / f"part{k}.cht"
            f.write_bytes(
                format_cht(cheats[start:end]).encode("utf-8", "surrogateescape")
            )
            files.append(str(f))
        args = [str(exe), *(["--raw"] if raw else []), *files]
        r = subprocess.run(args, capture_output=True, text=True, check=False)
    if r.returncode:
        raise CheatsError(f"cheat_patches: {r.stderr.strip()}")
    out = [(ST_UNREADABLE, 0, [], None) for _ in cheats]
    last = None
    for line in r.stdout.splitlines():
        fields = line.split("\t")
        if fields[0] == "R":
            if last is not None and fields[1] != "-" and len(fields) > 2 and fields[2]:
                st, h, p, _ = out[last]
                out[last] = (st, h, p, (fields[1], fields[2]))
            continue
        i = spans[int(fields[0])][0] + int(fields[1])
        last = i
        out[i] = (
            int(fields[2]),
            int(fields[3], 16),
            [tuple(int(x, 16) for x in p.split("/")) for p in fields[4:]],
            None,
        )
    if len(spans) > 1:
        # The kernel greys a cheat that runs what an earlier one of its
        # window runs; these parts aren't its windows, and the decoder's
        # table of cheats seen fills at CHEATFILE_SEEN_SLOTS: one that
        # repeats any earlier cheat counts as the same, so no window finds one
        seen = set()
        for i, (st, h, p, plain) in enumerate(out):
            if st == ST_OK and h in seen:
                out[i] = (ST_SAME, h, p, plain)
            elif st == ST_OK:
                seen.add(h)
    return out


def decrypt(cheats):
    """cheats with every encrypted code written decrypted, as the kernel
    reads it (tools/cheat_patches.c --raw): GameShark and Action Replay
    codes in their raw form, CodeBreaker ones plain, key and seed lines left
    out. Master codes go: once nothing is encrypted, Torii has no use for
    them (it hooks the game itself). Each cheat must read alone as it did
    with the keys before it: one that doesn't goes too. Returns (cheats,
    how many were rewritten, [(name, why)])."""
    got = decode_cheats(cheats, raw=True)
    if got is None:
        return cheats, 0, []
    plain, was, dropped = [], [], []
    for c, (st, h, _, raw) in zip(cheats, got, strict=True):
        p = raw if st == ST_OK else None  # only a cheat that reads is written anew
        if st == ST_MASTER:
            dropped.append(
                (c[0], "a master code: not needed once the codes are decrypted")
            )
            continue
        new = c
        if p is not None and (p[1] != c[1] or p[0] != c.extra.get("format")):
            new = Cheat(c[0], p[1], c[2], {**c.extra, "format": p[0]})
        plain.append(new)
        was.append((st, h))
    again = decode_cheats(plain)
    out, n = [], 0
    for c, w, h, orig in zip(
        plain,
        was,
        again,
        (c for c, g in zip(cheats, got, strict=True) if g[0] != ST_MASTER),
        strict=True,
    ):
        if w != (h[0], h[1]):
            dropped.append((c[0], "reads otherwise without its master code's key"))
            continue
        out.append(c)
        n += c is not orig
    return out, n, dropped


def index_text(text):
    """The index the kernel would build for a cheat file's text
    (source/cheatindex.h, by tools/cheat_patches.c --index), or None: no C
    compiler, or its cheats need more windows than the kernel has."""
    exe = patch_tool()
    if exe is None:
        return None
    with tempfile.TemporaryDirectory() as tmp:
        f = Path(tmp) / "file.cht"
        f.write_bytes(text.encode("utf-8", "surrogateescape"))
        r = subprocess.run(
            [str(exe), "--index", str(f)], capture_output=True, text=True, check=False
        )
    return r.stdout if r.returncode == 0 else None


def index_picks(text):
    """The program hashes of an index's picks (its "p" lines)"""
    out = set()
    for line in (text or "").splitlines():
        f = line.split()
        if len(f) >= 3 and f[0] == "p" and re.fullmatch(r"[0-9A-Fa-f]{1,8}", f[2]):
            out.add(int(f[2], 16))
    out.discard(0)
    return out


def carry_picks(database, old, hashes):
    """The database's cheats picked where a pick of the old file, or of its
    old index (hashes), runs the same program (the kernel's hash): a pick
    outlives its code being written decrypted. A name given on the cart
    (cheatN_renamed) is carried the same way. The old cheats so carried
    leave old when their code changed, else merge would keep them as the
    user's own. Returns (database, old)."""
    got = decode_cheats(database)
    if got is None:
        return database, old
    # An old index's picks are the file's (hashes; None without one): an
    # un-pick lives only there, the text's enable lines as the file was
    # written. With none, the text's.
    og = decode_cheats(old) if old else []
    if hashes is not None and og:
        old = [
            Cheat(c[0], c[1], bool(g[1]) and g[1] in hashes, getattr(c, "extra", {}))
            for c, g in zip(old, og, strict=True)
        ]
    picked = set(hashes or ())
    for c, g in zip(old, og or [], strict=False):
        if c[2] and g[1]:
            picked.add(g[1])
    codes = {c[1] for c in database}
    names = {}
    for c, g in zip(old, og or [], strict=False):
        extra = getattr(c, "extra", {})
        if g[1] and c[1] not in codes and is_true(extra.get("renamed", "")):
            names.setdefault(g[1], c[0])
    have = {g[1] for g in got if g[1]}
    out = []
    for c, g in zip(database, got, strict=True):
        h, cheat = g[1], c
        if h and h in names:
            cheat = Cheat(
                names[h], cheat[1], cheat[2], {**cheat.extra, "renamed": "true"}
            )
        if not cheat[2] and h and h in picked:
            cheat = Cheat(cheat[0], cheat[1], True, cheat.extra)
        out.append(cheat)
    if og:
        old = [
            c
            for c, g in zip(old, og, strict=True)
            if not (
                (c[2] or g[1] in names)
                and g[1] in have
                and c[1] not in codes
                and not user_code(c)
            )
        ]
    return out, old


def rom_patches(cheats):
    """Each cheat's ROM patches [(offset, value, mask)] as the kernel decodes
    the file they make, or None if the decoder can't be built."""
    got = decode_cheats(cheats)
    return None if got is None else [p for _, _, p, _ in got]


def sift(cheats, keep=lambda cheat: False):
    """cheats less those the kernel would grey: unreadable, too big, can't
    run, doing nothing, doing what an earlier one does, and master codes (a
    code can read as one only once the codes before it are decrypted), but
    those keep says to. As Settings' Clean up cheats does (cheatstore_clean),
    the master codes stay if the rest read otherwise without them: their
    keys decode the cheats after them. Dropping a cheat can change how later
    ones decode (a key in it), so the rest are decoded again and any whose
    reading changed goes too, until none does (each round drops one at
    least, so it ends). Returns (kept, [(name, why)]), or (cheats, [])
    without a C compiler."""
    got = decode_cheats(cheats)
    if got is None:
        return cheats, []
    masters = sift_round(cheats, got, keep, {**SIFTED, ST_MASTER: "a master code"})
    if masters is not None:
        return masters
    return sift_round(cheats, got, keep, SIFTED, strict=False)


def sift_round(cheats, got, keep, why, strict=True):
    """sift with the statuses in why dropped: (kept, dropped), or None if
    strict and a cheat left reads otherwise once they are gone."""
    drop = [g[0] in why and not keep(c) for c, g in zip(cheats, got, strict=True)]
    dropped = [
        (c[0], why[g[0]]) for c, g, d in zip(cheats, got, drop, strict=True) if d
    ]
    kept = [(c, g) for c, g, d in zip(cheats, got, drop, strict=True) if not d]
    while True:
        again = decode_cheats([c for c, _ in kept])
        changed = {
            i
            for i, ((c, g), h) in enumerate(zip(kept, again, strict=True))
            if g[:2] != h[:2] and not keep(c)
        }
        if not changed:
            break
        if strict:
            return None
        dropped += [
            (kept[i][0][0], "decodes otherwise once the junk is gone")
            for i in sorted(changed)
        ]
        kept = [
            (c, h)
            for i, ((c, _), h) in enumerate(zip(kept, again, strict=True))
            if i not in changed
        ]
    return [c for c, _ in kept], dropped


# --- The card ------------------------------------------------------------------------------


@dataclass
class Game:
    card_path: str
    file_path: str
    crc: int
    code: str | None  # the header's game code (0xAC)
    version: int  # its version byte (0xBC)


def header_info(path):
    """A GBA game's header code (0xAC) and version byte (0xBC): (code or
    None, version)."""
    code = names_db.header_code(path)
    try:
        with open(path, "rb") as f:
            f.seek(0xBC)
            v = f.read(1)
    except OSError:
        v = b""
    return code, (v[0] if v else 0)


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


def card_games(sd):
    games = [g for g in names_db.walk_card(sd) if g[2] == GBA]
    crcs = read_crcs(sd, games)
    out = []
    for card_path, file_path, _ in games:
        code, version = header_info(file_path)
        out.append(Game(card_path, file_path, crcs[card_path], code, version))
    return out


def cheat_path(sd, card_path):
    """/torii/cheats/<rom path without its extension>.cht on the card."""
    rel = card_path.lstrip("/")
    stem = rel[: rel.rindex(".")] if "." in rel.rsplit("/", 1)[-1] else rel
    return Path(sd) / CHEATS_DIR / (stem + ".cht")


# --- What each game gets --------------------------------------------------------------------


@dataclass
class Source:
    cheats: list  # [Cheat]
    dropped: list  # [(name, why)]
    comments: list  # the file's comment lines after "for crc32"
    label: str  # the report's
    detail: str


@dataclass
class Skip:
    label: str
    detail: str


def gh_source(cache, index, crc, suffix=""):
    """The converted cheats of a cached export, or a Skip."""
    e = index.get(crc)
    if e is not None and e.get("codes") == 0:
        return Skip("no cheats on GameHacking", f"{crc_hex(crc)} lists no codes")
    path = export_path(cache, crc)
    if not path.is_file():
        return Skip("not fetched yet", f"{crc_hex(crc)}: run with --gamehacking")
    cheats, dropped = convert(parse_mgba(decode(path.read_bytes())), suffix)
    cheats, _, gone = decrypt(cheats)
    dropped += gone
    cheats, sifted = sift(cheats)
    dropped += sifted
    if not cheats:
        return Skip(
            "no cheats on GameHacking",
            f"{crc_hex(crc)}'s export has none Torii can use",
        )
    gid = e.get("id") if e else None
    where = GAME_URL.format(gid) if gid else "GameHacking.org"
    name = (
        f"{e.get('title') or ''} {e.get('name') or ''}".strip() if e else ""
    ) or crc_hex(crc)
    return Source(
        cheats,
        dropped,
        [f"Cheats from GameHacking.org: {name}, {where}"],
        "",
        f"from {where}",
    )


def resolve(game, cache, index, by_crc, args):
    """A game's Source, or a Skip with why."""
    crc = game.crc
    custom = custom_path(cache, crc)
    if custom.is_file():
        cheats = [
            Cheat(
                c[0],
                c[1],
                False,
                {"format": c.extra["format"]} if "format" in c.extra else {},
            )
            for c in parse_cht(decode(custom.read_bytes()))
        ]
        rel = f"{CUSTOM}/{custom.name}"
        return Source(
            cheats, [], [f"Cheats from {rel} (dist/cheats/{rel})"], "", f"from {rel}"
        )
    if index.get(crc) is not None:
        return gh_source(cache, index, crc)
    if not index.complete():
        return Skip(
            "not fetched yet",
            "GameHacking.org's index isn't all fetched: run with --gamehacking",
        )
    # A CRC GameHacking doesn't list: a romhack or a modified dump
    if game.code is None:
        return Skip(
            "no cheats on GameHacking",
            f"{crc_hex(crc)} isn't listed and its header has no game code",
        )
    tag = f"{game.code} version {game.version}"
    bases = base_candidates(index.entries, game.code, game.version)
    if not bases:
        return Skip(
            "no cheats on GameHacking",
            f"{crc_hex(crc)} isn't listed, nor any dump of {tag}",
        )
    if len(bases) > 1:
        names = "; ".join(
            f"{b} {index.entries[b].get('title')} {index.entries[b].get('name')}"
            for b in bases
        )
        return Skip(
            "ambiguous base", f"{crc_hex(crc)} isn't listed; {tag} could be {names}"
        )
    base = int(bases[0], 16)
    src = gh_source(cache, index, base, " (base game)")
    if isinstance(src, Skip):
        return Skip(src.label, f"its base dump {src.detail}")
    entry = index.entries[bases[0]]
    base_name = f"{entry.get('title')} {entry.get('name')}".strip()
    base_game = by_crc.get(base)
    hack = Path(game.file_path).read_bytes()
    if base_game is None:
        if not args.base_game:
            return Skip(
                "base dump not on card",
                f"{base_name} ({bases[0]}) is its base; put it on the card",
            )
        base_rom, share = None, None
    else:
        base_rom = Path(base_game.file_path).read_bytes()
        share = similarity(base_rom, hack)
        if share * 100 < args.similar and not args.base_game:
            return Skip(
                "hack too different",
                f"{share:.0%} of {base_name} ({bases[0]}) is the same, under {args.similar:g}%",
            )
    patches = rom_patches(src.cheats)
    if patches is None:
        return Skip(
            "base game",
            "no C compiler to check its ROM patches with (tools/cheat_patches.c)",
        )
    kept, gone = [], 0
    for cheat, p in zip(src.cheats, patches, strict=True):
        if p and (
            base_rom is None or not all(patch_unchanged(base_rom, hack, x) for x in p)
        ):
            gone += 1
            src.dropped.append(
                (
                    cheat[0],
                    "patches ROM bytes the hack changed"
                    if base_rom
                    else "patches the ROM, and the base ROM isn't on the card to check it",
                )
            )
            continue
        kept.append(cheat)
    src.cheats = kept
    same = f"{share:.0%} the same" if share is not None else "base ROM not on card"
    src.label = "base game"
    src.detail = f"light hack of {base_name} ({bases[0]}, {same}): {plural(len(kept), 'cheat')}, {plural(gone, 'ROM patch')} dropped"
    src.comments = [
        f"Cheats for its base game, {base_name} (crc32 {bases[0]}), from GameHacking.org: {GAME_URL.format(entry.get('id'))}"
    ]
    return src


def write_game(sd, game, src, dry_run):
    """Merges a game's Source into its card file. Returns (state, detail,
    dropped): state written, updated or unchanged."""
    target = cheat_path(sd, game.card_path)
    index = target.with_suffix(INDEX_EXT)
    old_text = read_card_file(target)
    old = parse_cht(old_text) if old_text is not None else []
    ours = old_text is not None and for_crc(old_text) is not None
    old_index = read_card_file(index)
    database, old = carry_picks(
        src.cheats, old, index_picks(old_index) if old_index is not None else None
    )
    cheats, own, stale = merge(database, old, keep_all=not ours)
    comments = (
        [FOR_CRC[2:] + crc_hex(game.crc)]
        + src.comments
        + ["Written by `just cheats`; picks and your own codes are kept."]
    )
    n_source = len(cheats) - own
    # Master codes stay: their keys decode the cheats after them
    got = decode_cheats(cheats)
    keys = {i for i, g in enumerate(got or []) if g[0] == ST_MASTER}
    removable = [
        i for i in range(n_source) if not user_code(cheats[i]) and i not in keys
    ]
    most = BIG_CHEATS
    while True:
        fitted, over = fit(cheats, removable, comments, most=most)
        text = format_cht(fitted, comments)
        big = (
            len(text.encode("utf-8", "surrogateescape")) > TEXT_MAX
            or len(fitted) > CHEATS_MAX
        )
        new_index = index_text(text) if big else None
        if not big or new_index is not None or most < CHEATS_MAX:
            break
        most = most * 9 // 10  # more windows than the kernel has: fewer cheats
    cheats = fitted
    dropped = list(src.dropped) + [
        (name, "over the kernel's size limit") for name in over
    ]
    if over:
        # A cheat the fit dropped may have held a key the later ones need
        cheats, broke = sift(cheats, keep=user_code)
        dropped += broke
    text = format_cht(cheats, comments)
    if old_text is None:
        state = "written"
    elif text == old_text or (
        cheats == old and head_comments(text) == head_comments(old_text)
    ):
        # The same cheats, picks and comments, laid out as the kernel left
        # them (a pick's enable line added at the end): left as it is
        state = "unchanged"
    else:
        state = "updated"
    if state != "unchanged" and not dry_run:
        write_card_file(target, text)
    # A larger file's index beside it (the kernel reads it in windows by
    # it), written with the file, so it is the bytes on the card's; a file
    # left as it is keeps the index the kernel keeps. A file small enough to
    # read whole needs none, so an old one goes.
    if not dry_run:
        if new_index is not None:
            if state != "unchanged":
                write_card_file(index, new_index)
        else:
            for stale_index in (index, index.with_name(index.name + ".bak")):
                if stale_index.exists():
                    stale_index.unlink()
    n = n_source - len(over)
    detail = src.detail if src.label else f"{plural(n, 'cheat')} {src.detail}"
    if over and src.label:
        detail += f", {len(over)} more dropped over the size limit"
    if own:
        detail += f", {own} of your own"
    picked = sum(c[2] for c in cheats)
    if picked:
        detail += f", {picked} picked"
    if stale:
        detail += f", {plural(stale, 'old cheat')} the source no longer has left out"
    if len(text.encode("utf-8", "surrogateescape")) > BIG_BYTES:
        detail += f"; over {BIG_BYTES} bytes with your own codes alone: the kernel won't read it"
    rel = "/" + target.relative_to(sd).as_posix()
    return state, f"{rel}: {detail}", dropped


# --- Fetching (in the user's browser) --------------------------------------------------------


def wanted(games, extra_crcs, cache, index, refresh, ids=None):
    """The harvester's wanted dumps, [{crc, code, version}]: each game's own
    CRC, or for a romhack (a CRC the complete index lacks) its base dump,
    less what's cached, what lists no codes, and custom files. ids: {crc:
    GameHacking game ID} given by hand (--only CRC=ID), so the harvester
    exports those without looking for them in the index."""
    out = {}
    ids = ids or {}

    def want(crc, code=None, version=0):
        if refresh or not export_path(cache, crc).is_file():
            w = {"crc": crc_hex(crc), "code": code, "version": version}
            if crc in ids:
                w["id"] = ids[crc]
            out.setdefault(crc, w)

    for g in games:
        if custom_path(cache, g.crc).is_file():
            continue
        e = index.get(g.crc)
        if e is not None:
            if e.get("codes") != 0 or refresh:
                want(g.crc)
        elif not index.complete() or refresh:
            want(g.crc, g.code, g.version)
        elif g.code:
            bases = base_candidates(index.entries, g.code, g.version)
            if len(bases) == 1 and index.entries[bases[0]].get("codes") != 0:
                want(int(bases[0], 16))
    for crc in extra_crcs:
        if index.get(crc) is None or index.get(crc).get("codes") != 0 or refresh:
            want(crc)
    return list(out.values())


def harvest_script(want, index, cache, delay, run_id, refresh):
    """The harvester with its CONFIG filled in."""
    config = {
        "run": run_id,
        "delay": delay,
        "wanted": want,
        "cached": []
        if refresh
        else sorted(p.stem for p in gh_dir(cache).glob("*.cheats")),
        "index": {} if refresh else index.entries,
        "pagesDone": [] if refresh else sorted(index.pages),
        "lastPage": -1 if refresh else index.last_page,
    }
    template = HARVEST_TEMPLATE.read_text("utf-8")
    if HARVEST_CONFIG not in template:
        raise CheatsError(f"{HARVEST_TEMPLATE} has no {HARVEST_CONFIG}")
    return template.replace(HARVEST_CONFIG, json.dumps(config, separators=(",", ":")))


def to_clipboard(text):
    """Copies text to the clipboard where there's a tool for it; whether it
    did."""
    for cmd in (["pbcopy"], ["wl-copy"], ["xclip", "-selection", "clipboard"]):
        if shutil.which(cmd[0]):
            try:
                subprocess.run(cmd, input=text.encode(), check=True, timeout=10)
                return True
            except (OSError, subprocess.SubprocessError):
                pass
    return False


def open_url(url):
    for cmd in (["open"], ["xdg-open"]):
        if (
            shutil.which(cmd[0])
            and subprocess.run(cmd + [url], check=False).returncode == 0
        ):
            return
    webbrowser.open(url)


def fetch(games, args, index):
    want = wanted(
        games, args.only_extra, args.cache, index, args.refresh, args.only_ids
    )
    if not want:
        print("GameHacking.org: nothing to fetch, every game's export is cached")
        return
    run_id = time.strftime("%Y%m%d-%H%M%S")
    script = harvest_script(want, index, args.cache, args.delay, run_id, args.refresh)
    path = gh_dir(args.cache) / HARVEST
    write_file(path, script.encode("utf-8"))
    pages = (
        0
        if index.complete()
        else 77
        if index.last_page < 0
        else index.last_page + 1 - len(index.pages)
    )
    minutes = (len(want) + pages) * args.delay / 60
    copied = not args.no_browser and to_clipboard(script)
    print(
        f"GameHacking.org: {plural(len(want), 'dump')} to export (about {minutes:.0f} minutes at {args.delay:g} s a request)."
    )
    print(f"The harvester is {path}{', copied to the clipboard' if copied else ''}.")
    print(
        f"1. In your browser, on {GBA_PAGE} (opening now), open the console: Cmd+Opt+J (Chrome; Ctrl+Shift+J elsewhere)."
    )
    print("2. If it asks, type: allow pasting  and press Enter.")
    print(
        f"3. Paste{' it' if copied else f' the contents of {path}'} and press Enter. Keep the tab open; its box shows progress."
    )
    print("4. Allow the site to download multiple files if the browser asks.")
    print(
        f"Waiting for torii-gamehacking-*.json in {args.downloads}; Ctrl-C stops waiting (the run then goes on with what was imported)."
    )
    if not args.no_browser:
        open_url(GBA_PAGE)
    watch(args.downloads, args.cache, index, run_id)


# --- The run ----------------------------------------------------------------------------------

LABELS = (
    "written",
    "updated",
    "unchanged",
    "base game",
    "no cheats on GameHacking",
    "not fetched yet",
    "hack too different",
    "base dump not on card",
    "ambiguous base",
)


def parse_only(text):
    """--only's CRCs, each CRC or CRC=ID (a GameHacking game ID, the number
    in its /game/ID address): (set of CRCs or None, {crc: ID})."""
    if not text:
        return None, {}
    crcs, ids = set(), {}
    for item in text.split(","):
        crc, _, gid = item.strip().partition("=")
        if not crc:
            continue
        crcs.add(parse_crc(crc))
        if gid:
            if not gid.strip().isdigit():
                raise CheatsError(f"{item!r}: the game ID after = is a number")
            ids[parse_crc(crc)] = int(gid)
    return crcs, ids


def run(args):
    sd = Path(args.sd_path)
    if not sd.is_dir():
        raise CheatsError(
            f"{sd} isn't a folder: give the card's folder, e.g. /Volumes/OMEGA"
        )
    args.cache = Path(args.cache)
    only, args.only_ids = parse_only(args.only)
    index = Index(args.cache)
    for f in args.import_ or []:
        bundle, got = import_bundle(f, args.cache, index)
        if bundle is None:
            raise CheatsError(f"{f}: {got}")
        print(f"imported {Path(f).name}: {plural(got, 'export')}")
    every = card_games(sd)
    by_crc = {}
    for g in every:
        by_crc.setdefault(g.crc, g)
    games = every if only is None else [g for g in every if g.crc in only]
    args.only_extra = sorted(only - set(by_crc)) if only is not None else []
    if args.gamehacking:
        fetch(games, args, index)
    rows = {k: [] for k in LABELS}
    dropped = []
    for game in games:
        src = resolve(game, args.cache, index, by_crc, args)
        if isinstance(src, Skip):
            rows[src.label].append((game.card_path, src.detail))
            continue
        state, detail, gone = write_game(sd, game, src, args.dry_run)
        label = "base game" if src.label == "base game" else state
        if label == "base game":
            detail += f" ({'would be ' if args.dry_run and state != 'unchanged' else ''}{state})"
        rows[label].append((game.card_path, detail))
        dropped += [(game.card_path, name, why) for name, why in gone]
    report(rows, dropped, args.dry_run, len(games))


def report(rows, dropped, dry_run, total):
    would = "would be " if dry_run else ""
    names = {k: (f"{would}{k}" if k in ("written", "updated") else k) for k in LABELS}
    counts = ", ".join(f"{len(rows[k])} {names[k]}" for k in LABELS if rows[k])
    print(f"{plural(total, 'GBA game')}: {counts or 'nothing to do'}")
    for k in LABELS:
        if rows[k]:
            print(f"{names[k]}:")
            for path, what in sorted(rows[k]):
                print(f"  {path}: {what}")
    if dropped:
        print(f"{plural(len(dropped), 'cheat')} left out:")
        for path, name, why in dropped:
            print(f"  {path}: {name}: {why}")
    print(
        "Cheats from GameHacking.org, fetched on this PC for your own card; never part of Torii."
    )


def main(argv):
    p = argparse.ArgumentParser(
        prog="cheats.py",
        description=__doc__.split("\n\n")[0],
        epilog=__doc__.split("\n\n", 1)[1],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("sd_path", metavar="SD_PATH", help="the card's folder on this PC")
    p.add_argument(
        "--gamehacking",
        action="store_true",
        help="fetch what's missing from GameHacking.org, through a harvester you paste into your browser",
    )
    p.add_argument(
        "--import",
        dest="import_",
        nargs="+",
        metavar="FILE",
        help="import harvester bundles",
    )
    p.add_argument(
        "--only",
        metavar="CRC[=ID],...",
        help="only these CRC32s (on the card, or any to fetch); =ID gives the GameHacking "
        "game ID (gamehacking.org/game/ID), so the harvester needn't search the index",
    )
    p.add_argument(
        "--base-game",
        action="store_true",
        help="give every romhack its base game's cheats, whatever the similarity",
    )
    p.add_argument(
        "--similar",
        type=float,
        default=SIMILAR,
        metavar="PCT",
        help=f"percent of the base's 4 KB blocks a light hack keeps (default {SIMILAR:g})",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="change nothing on the card, print what would change",
    )
    p.add_argument(
        "--delay",
        type=float,
        default=DELAY,
        help=f"the harvester's seconds between requests (default {DELAY:g}; robots.txt asks 30)",
    )
    p.add_argument(
        "--downloads",
        type=Path,
        default=Path.home() / "Downloads",
        help="where the browser saves the bundles (default ~/Downloads)",
    )
    p.add_argument(
        "--refresh", action="store_true", help="fetch the index and the exports again"
    )
    p.add_argument(
        "--cache",
        default=str(CACHE),
        help=f"the cheats' cache (default {CACHE.relative_to(ROOT)})",
    )
    p.add_argument("--no-browser", action="store_true", help=argparse.SUPPRESS)
    args = p.parse_args(argv)
    if args.delay < 0 or not 0 <= args.similar <= 100:
        p.error("--delay is 0 or more, --similar 0 to 100")
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
