#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# ///
"""Make the names database for an SD card: /torii/names.db and /torii/crc.txt.

usage: names_db.py update SD_PATH [--no-crc] [--offline] [--refresh] [--dats DIR]
       names_db.py add SD_PATH FILE [--dry-run]

`just names-update SD_PATH` runs this. It needs SD_PATH, the card's folder
on this PC (a mounted card, say /Volumes/OMEGA), and:

1. Downloads the No-Intro dats of the Game Boy Advance, Game Boy Color,
   Game Boy and NES from libretro-database (metadat/no-intro), one request
   each, into a cache on this PC (dist/dats, git-ignored). Later runs use the
   cache; --refresh downloads them again, --offline never does. --dats
   reads and caches them in another folder.
2. Writes SD_PATH/torii/names.db: per console the CRC32 of each game in the
   dats and its No-Intro name (source/namesdb.h has the format). A game is
   named only by its CRC: one the dats don't list (a romhack, a translation,
   a patched or bad dump) keeps its file name, even when its header's game
   code is a listed game's, since a romhack keeps its base game's code.
3. Computes the CRC32 of every game on the card (by extension, outside the
   folders the kernel hides) and writes SD_PATH/torii/crc.txt, so the card
   shows the names at once. A file whose path and size crc.txt already has
   isn't read again. --no-crc leaves crc.txt alone: the kernel's Identify
   library and its launches compute CRCs too, far more slowly.

`just names-add SD_PATH FILE` runs `add`: your own names for many games at
once, into SD_PATH/torii/names.txt, the file the cart's Rename writes (your
names win over every other). FILE has a line per game: the game's file name
(with or without its extension, any case; or its path on the card, from
"/"), a tab, and its name; an empty name removes the game's own name, and
lines starting with # are skipped. Every game on the card with that file
name gets the name (copies in different folders too). It prints which lines
named which games and which named none, and keeps the names and comments
names.txt has.

Licence: the dats are libretro-database's (CC BY-SA 4.0), which imports them
from No-Intro (https://no-intro.org). They are downloaded to this PC at your
request and their names written to your card; nothing from them is part of
Torii, its repository or its releases. Don't publish a names.db without
crediting No-Intro and libretro-database and checking No-Intro's terms.
"""

import argparse
import os
import re
import struct
import sys
import time
import unicodedata
import urllib.parse
import urllib.request
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATS = ROOT / "dist" / "dats"
URL = "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/no-intro/"
USER_AGENT = (
    "torii-kernel names-update (https://github.com/ErikBavenstrand/torii-kernel)"
)

# Library order (source/library.h): GBA, GBC, GB, NES
CONSOLES = [
    ("GBA", "Nintendo - Game Boy Advance.dat", (".gba",)),
    ("GBC", "Nintendo - Game Boy Color.dat", (".gbc",)),
    ("GB", "Nintendo - Game Boy.dat", (".gb",)),
    ("NES", "Nintendo - Nintendo Entertainment System.dat", (".nes",)),
]
GBA = 0

MAGIC = 0x42444E54  # "TNDB"
VERSION = 1
HEADER = struct.Struct("<IHHII16s4IIIII")
assert HEADER.size == 64
DB_MAX = 0x1C0000  # PSRAM_NAMES_DB_BYTES (source/engine/psram.h)

# The kernel's hidden folders in the card's root (source/gamename.c), and its
# depth cap (source/library_scan.c's LIB_DEPTH)
HIDDEN_ROOT = {
    "torii",
    "saver",
    "rts",
    "system",
    "patch",
    "cheat",
    "system volume information",
}
DEPTH = 8
CRC_FILE = "crc.txt"
CACHE_MAX = 4608  # NAMES_CACHE_MAX (source/namesdb.h)
CACHE_POOL = (
    0xA0000  # NAMES_CACHE_POOL_BYTES: the paths, each padded to 2 bytes with its NUL
)
CACHE_FILE_MAX = 0x100000  # names_store.c's NAMES_CACHE_FILE_MAX
CRC_HEADER = "# Torii's CRC cache: CRC32, GBA game code, size, date, path\n"

# Tags of dumps a game code shouldn't name first: the retail game wins
LESSER = re.compile(
    r"\((Beta|Proto|Demo|Sample|Kiosk|Pirate|Unl|Debug)[^)]*\)", re.IGNORECASE
)


class NamesError(Exception):
    pass


# --- The dats ------------------------------------------------------------------

TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|([()])|([^\s()"]+)')


def tokens(text):
    for m in TOKEN.finditer(text):
        if m.group(1) is not None:
            yield ("str", m.group(1).replace('\\"', '"').replace("\\\\", "\\"))
        elif m.group(2):
            yield (m.group(2), m.group(2))
        else:
            yield ("word", m.group(3))


def parse_block(it):
    """A clrmamepro block after its '(' as a list of (key, value) pairs, a
    value being a string or a nested list."""
    out = []
    for kind, val in it:
        if kind == ")":
            return out
        if kind == "(":
            raise NamesError("a block without a key")
        key = val
        kind2, val2 = next(it, (None, None))
        if kind2 == "(":
            out.append((key, parse_block(it)))
        elif kind2 in ("str", "word"):
            out.append((key, val2))
        else:
            raise NamesError(f"{key} has no value")
    raise NamesError("a block doesn't end")


def parse_dat(text):
    """The dat's version and its games: (name, [(crc, size)], serial)."""
    it = tokens(text)
    version, games = "", []
    for kind, val in it:
        if kind != "word":
            continue
        k2, _ = next(it, (None, None))
        if k2 != "(":
            continue
        block = parse_block(it)
        fields = {k: v for k, v in block if isinstance(v, str)}
        if val == "clrmamepro":
            version = fields.get("version", "")
        elif val == "game":
            roms = []
            serial = fields.get("serial", "")
            for k, v in block:
                if k == "rom" and isinstance(v, list):
                    rf = {a: b for a, b in v if isinstance(b, str)}
                    if re.fullmatch(r"[0-9A-Fa-f]{8}", rf.get("crc", "")):
                        roms.append((int(rf["crc"], 16), int(rf.get("size", "0") or 0)))
                    serial = serial or rf.get("serial", "")
            if "name" in fields and roms:
                games.append((fields["name"], roms, serial))
    return version, games


def game_code(serial):
    """A GBA game code from a dat's serial ("AXVE", or "AGB-AXVE-USA"), or None."""
    for part in re.split(r"[-\s,]+", serial.strip()):
        if re.fullmatch(r"[0-9A-Z]{4}", part) and part != "AGB":
            return part
    return None


def code_key(code):
    return struct.unpack("<I", code.encode("ascii"))[0]


def fetch(dats, file, offline, refresh):
    path = dats / file
    if path.exists() and not refresh:
        return path
    if offline:
        raise NamesError(f"{path} isn't there, and --offline downloads nothing")
    dats.mkdir(parents=True, exist_ok=True)
    url = URL + urllib.parse.quote(file)
    print(f"downloading {file}", file=sys.stderr)
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            data = r.read()
    except OSError as e:
        raise NamesError(f"can't download {url}: {e}") from e
    tmp = path.with_suffix(".part")
    tmp.write_bytes(data)
    tmp.replace(path)
    return path


# --- names.db ------------------------------------------------------------------


def build_db(tables, codes, version):
    """names.db's bytes. tables: per console {crc: name}; codes: {code: name}."""
    pool = bytearray()
    offsets = {}

    def name_off(name):
        if name not in offsets:
            offsets[name] = len(pool)
            pool.extend(name.encode("utf-8") + b"\0")
        return offsets[name]

    body = bytearray()
    for table in tables:
        for crc in sorted(table):
            body += struct.pack("<II", crc, name_off(table[crc]))
    for key in sorted(codes):
        body += struct.pack("<II", key, name_off(codes[key]))
    while len(pool) % 4:
        pool.append(0)
    if not pool:
        pool = bytearray(4)
    body += pool
    file_bytes = HEADER.size + len(body)
    head = HEADER.pack(
        MAGIC,
        VERSION,
        HEADER.size,
        file_bytes,
        zlib.crc32(body),
        version.encode("ascii", "replace")[:15],  # the kernel wants a NUL
        *[len(t) for t in tables],
        len(codes),
        len(pool),
        0,
        0,
    )
    head = head[:60] + struct.pack("<I", zlib.crc32(head[:60]))
    return bytes(head + body)


def read_db(data):
    """names.db's tables back, checking it as the kernel does: (version,
    [ {crc: name} per console ], {code: name})."""
    if len(data) < HEADER.size:
        raise NamesError("shorter than its header")
    f = HEADER.unpack_from(data)
    magic, version, header_bytes, file_bytes, body_crc, dat_version = f[:6]
    counts, code_count, pool_bytes, _, header_crc = f[6:10], f[10], f[11], f[12], f[13]
    if magic != MAGIC or version != VERSION or header_bytes != 64:
        raise NamesError("not a names.db this reads")
    if zlib.crc32(data[:60]) != header_crc:
        raise NamesError("bad header CRC")
    entries = sum(counts) + code_count
    if file_bytes != len(data) or file_bytes != 64 + entries * 8 + pool_bytes:
        raise NamesError("its size doesn't add up")
    if zlib.crc32(data[64:]) != body_crc:
        raise NamesError("bad body CRC")
    pool = data[64 + entries * 8 :]
    if pool_bytes % 4 or pool[-1] != 0:
        raise NamesError("its pool doesn't end")

    def name(off):
        return pool[off : pool.index(b"\0", off)].decode("utf-8")

    at, tables = 64, []
    for n in list(counts) + [code_count]:
        t, last = {}, -1
        for i in range(n):
            key, off = struct.unpack_from("<II", data, at + 8 * i)
            if key <= last:
                raise NamesError("a table isn't sorted")
            last = key
            t[key] = name(off)
        tables.append(t)
        at += 8 * n
    return dat_version.rstrip(b"\0").decode("ascii"), tables[:4], tables[4]


def make_tables(dat_texts):
    """Per console {crc: name} and the GBA {code: name}, and the dats' version."""
    tables, codes, versions = [], {}, []
    for i, text in enumerate(dat_texts):
        version, games = parse_dat(text)
        versions.append(version)
        table, ranked = {}, {}
        for order, (name, roms, serial) in enumerate(games):
            for crc, _ in roms:
                table.setdefault(crc, name)  # unique in No-Intro: the first wins
            code = game_code(serial) if i == GBA else None
            if code:
                rank = (bool(LESSER.search(name)), order)
                if code not in ranked or rank < ranked[code][0]:
                    ranked[code] = (rank, name)
        tables.append(table)
        if i == GBA:
            codes = {code_key(c): n for c, (_, n) in ranked.items()}
    return tables, codes, max((v for v in versions if v), default="")


def db_name(tables, console, crc):
    """The name the kernel shows (source/namesdb.c, names_db_name): the dats'
    name of the CRC, in the console's table or, for a GB or GBC file, the
    other's; None for a CRC they don't list."""
    other = {1: 2, 2: 1}.get(console)
    for i in (console, other):
        if i is not None and crc is not None and crc in tables[i]:
            return tables[i][crc]
    return None


# --- Romhacks (C4) -----------------------------------------------------------------

# Trailing articles No-Intro moves behind the title ("Legend of Zelda, The")
ARTICLE = re.compile(
    r"^(.*), (the|an?|les?|la|l'|der|die|das|el|los|las|il|lo|gli|de|het|een)$",
    re.IGNORECASE,
)
GROUP = re.compile(r"\([^()]*\)|\[[^\[\]]*\]")


def title_key(name):
    """A title to compare a file's name with a dat's: no extension, (...) and
    [...] tags or scene number, the article in front, and only the letters
    and digits, accents and case folded. "" if nothing is left."""
    low = name.lower()
    if any(low.endswith(e) for _, _, exts in CONSOLES for e in exts):
        name = name[: name.rindex(".")]
    if " " not in name:
        name = name.replace("_", " ")
    while True:  # inner groups first, so nested ones go too
        cut = GROUP.sub(" ", name)
        if cut == name:
            break
        name = cut
    name = re.sub(r"^\s*\d{4,5}\s*-\s*", "", name)
    title, sep, rest = " ".join(name.split()).partition(" - ")
    m = ARTICLE.match(title)
    if m:
        title = f"{m.group(2)} {m.group(1)}"
    text = unicodedata.normalize("NFKD", title + sep + rest)
    return "".join(c for c in text.casefold() if c.isascii() and c.isalnum())


def code_match(codes, console, code, file_name):
    """For a GBA game the dats don't list by CRC: the dats' name of its game
    code, but only when its file name is that game's title too ("Pokemon -
    Emerald Version (Erik).gba", a patched dump of the game), never a romhack
    with its base game's code ("Pokemon - Radical Red.gba"). None otherwise.
    `just art-scrape` takes art for such a game; the kernel names it by its
    file name, which cleans to the same title."""
    if console != GBA or not code or code_key(code) not in codes:
        return None
    name = codes[code_key(code)]
    key = title_key(file_name)
    return name if key and key == title_key(name) else None


# --- The card ---------------------------------------------------------------------


def console_of(name):
    low = name.lower()
    for i, (_, _, exts) in enumerate(CONSOLES):
        if low.endswith(exts) and low != exts[0] and not low.startswith("."):
            return i
    return None


def walk_card(sd):
    """The card's games as (card path, file path, console), as the library
    finds them: outside the hidden folders and dot files, at most DEPTH deep."""
    out = []

    def walk(folder, card_path, depth):
        try:
            entries = sorted(os.scandir(folder), key=lambda e: e.name)
        except OSError as e:
            print(f"can't read {folder}: {e}", file=sys.stderr)
            return
        for e in entries:
            if e.name.startswith("."):
                continue
            path = f"{card_path}/{e.name}"
            if e.is_dir(follow_symlinks=False):
                if depth == 0 and e.name.lower() in HIDDEN_ROOT:
                    continue
                if depth + 1 < DEPTH:
                    walk(e.path, path, depth + 1)
            elif e.is_file(follow_symlinks=False):
                c = console_of(e.name)
                if c is not None:
                    out.append((path, e.path, c))

    walk(sd, "", 0)
    return out


def safe_text(data):
    """A safe file's text without its CRC line, if the line holds; else None."""
    m = re.search(rb"(?:^|\n)(#crc32 ([0-9a-fA-F]{8})[ \t\r]*(?:\n[ \t\r]*)*)$", data)
    if not m:
        return None
    body = data[: m.start(1)]
    return body if zlib.crc32(body) == int(m.group(2), 16) else None


def read_safe(path):
    """The text of a safe file (source/safefile.h): NAME if its CRC line holds,
    NAME edited by hand, else NAME.bak; "" if neither."""
    main, bak = Path(path), Path(str(path) + ".bak")
    if main.exists():
        data = main.read_bytes()
        text = safe_text(data)
        if text is not None:
            return text.decode("utf-8", "replace")
        if data and b"\0" not in data:
            return data.decode("utf-8", "replace")  # edited by hand
    if bak.exists():
        text = safe_text(bak.read_bytes())
        if text is not None:
            return text.decode("utf-8", "replace")
    return ""


def write_safe(path, text):
    """Both copies, the backup first, each ending in its CRC line."""
    data = text.encode("utf-8")
    data += b"#crc32 %08x\n" % zlib.crc32(data)
    for p in (Path(str(path) + ".bak"), Path(path)):
        tmp = p.with_name(p.name + ".part")
        tmp.write_bytes(data)
        tmp.replace(p)


def parse_cache(text):
    """crc.txt's records: {folded path: (crc or None, code or None, size, date or None, path)}."""
    out = {}
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t", 4)
        if len(parts) != 5 or not parts[4].startswith("/"):
            continue
        crc, code, size, date, path = parts
        try:
            rec = (
                None if crc == "-" else int(crc, 16),
                None if code == "-" else code,
                int(size),
                None if date == "-" else date,
                path,
            )
        except ValueError:
            continue
        out[path.lower()] = rec
    return out


# --- Games by file name (names-add, art-add) ---------------------------------------


def fold(name):
    """A file name as names-add and art-add match it: NFC, case folded."""
    return unicodedata.normalize("NFC", name).casefold()


def games_by_name(games):
    """walk_card's games by their folded file name and by that name without
    its extension: {name: [(card path, file path, console), ...]}."""
    out = {}
    for game in games:
        name = Path(game[0]).name
        keys = {fold(name), fold(name[: name.rindex(".")])}
        for key in keys:
            out.setdefault(key, []).append(game)
    return out


# --- Your own names: /torii/names.txt (names-add) -------------------------------------

NAMES_FILE = "names.txt"
NAMES_MAX = 1024  # GAMENAME_CUSTOM_MAX (source/gamename.h)
NAME_BYTES = 255  # GAMENAME_MAX
# The text Rename and the walk read: PSRAM_NAMES_TEXT_BYTES less the CRC line
# and its newline (source/gamename_store.c's TEXT_CAP)
NAMES_TEXT_MAX = 0x1D000 - 15 - 2
NAMES_HEADER = "# Torii's own names: a game's path, a tab, its name\n"


def ascii_fold(path):
    """A path folded as the kernel folds it: ASCII case only."""
    return "".join(c.lower() if c < "\x80" else c for c in path)


def parse_names(text):
    """names.txt as gamename.h reads it: ([other lines], {folded path: (path,
    name)}), a later line for a path winning."""
    other, names = [], {}
    for line in text.lstrip("\ufeff").splitlines():
        path, tab, name = line.partition("\t")
        if path.startswith("/") and tab and name:
            names[ascii_fold(path)] = (path, name)
        elif line.strip() and not (path.startswith("/") and tab):
            other.append(line)
    return other, names


def format_names(other, names):
    """names.txt's text as the kernel writes it: the other lines, then a line
    per game sorted by path."""
    lines = other + [f"{p}\t{n}" for _, (p, n) in sorted(names.items())]
    return "".join(line + "\n" for line in lines)


def clean_name(name):
    """A name as Rename keeps it: control characters dropped, the ends
    trimmed. NamesError if it's too long."""
    name = "".join(c for c in name if c >= " " and c != "\x7f").strip()
    if len(name.encode("utf-8")) > NAME_BYTES:
        raise NamesError(f"{name[:40]}...: longer than {NAME_BYTES} bytes")
    return name


def add_names(args):
    sd = Path(args.sd_path)
    if not sd.is_dir():
        raise NamesError(
            f"{sd} isn't a folder: give the card's folder, e.g. /Volumes/OMEGA"
        )
    try:
        wanted = Path(args.file).read_text("utf-8-sig").splitlines()
    except (OSError, UnicodeError) as e:
        raise NamesError(f"{args.file}: {e}") from None
    games = walk_card(sd)
    by_name = games_by_name(games)
    by_path = {ascii_fold(g[0]): g for g in games}
    target = sd / "torii" / NAMES_FILE
    other, names = parse_names(read_safe(target))
    if not other:
        other = [NAMES_HEADER.rstrip("\n")]
    named = removed = 0
    misses, bad = [], []
    for n, line in enumerate(wanted, 1):
        if not line.strip() or line.startswith("#"):
            continue
        key, tab, name = line.partition("\t")
        key = key.strip()
        if not tab or not key:
            bad.append(f"line {n}: no tab between the file name and the name")
            continue
        try:
            name = clean_name(name)
        except NamesError as e:
            bad.append(f"line {n}: {e}")
            continue
        if key.startswith("/"):
            found = [by_path[ascii_fold(key)]] if ascii_fold(key) in by_path else []
        else:
            found = by_name.get(fold(key), [])
        if not found:
            misses.append(key)
            continue
        for path, _, _ in found:
            if name:
                print(f"  {path}: {name}")
                names[ascii_fold(path)] = (path, name)
                named += 1
            elif names.pop(ascii_fold(path), None):
                print(f"  {path}: own name removed")
                removed += 1
    if bad:
        raise NamesError(
            f"{args.file} has lines that don't read:\n  " + "\n  ".join(bad)
        )
    text = format_names(other, names)
    if len(names) > NAMES_MAX or len(text.encode("utf-8")) > NAMES_TEXT_MAX:
        raise NamesError(
            f"names.txt would hold {len(names)} names in {len(text.encode('utf-8'))} bytes; "
            f"the kernel reads {NAMES_MAX} names in {NAMES_TEXT_MAX} bytes"
        )
    for key in misses:
        print(f"  {key}: no game on the card has that file name")
    print(
        f"{named} games named, {removed} names removed, {len(misses)} lines named no game; "
        f"names.txt holds {len(names)} names"
    )
    if args.dry_run:
        print("(dry run: names.txt not written)")
        return
    target.parent.mkdir(exist_ok=True)
    write_safe(target, text)
    print(f"wrote {target}")


def fat_date(path):
    """The file's FAT date and time as FatFs gives them (fdate << 16 | ftime,
    local time, seconds in twos) in crc.txt's form, from its mtime."""
    t = time.localtime(os.path.getmtime(path))
    if t.tm_year < 1980:
        return None
    value = (
        (t.tm_year - 1980) << 25
        | t.tm_mon << 21
        | t.tm_mday << 16
        | t.tm_hour << 11
        | t.tm_min << 5
        | t.tm_sec // 2
    )
    return f"{value:08X}"


def file_crc(path):
    crc, code, size = 0, None, 0
    with open(path, "rb") as f:
        while chunk := f.read(1 << 20):
            if size == 0 and len(chunk) >= 0xB0:
                head = chunk[0xAC:0xB0]
                if re.fullmatch(rb"[0-9A-Z]{4}", head):
                    code = head.decode("ascii")
            crc = zlib.crc32(chunk, crc)
            size += len(chunk)
    return crc, code, size


def update_cache(sd, games):
    """Computes the CRCs crc.txt lacks and writes it; returns its records
    as {card path: (crc, code)} and (computed, reused)."""
    torii = Path(sd) / "torii"
    old = parse_cache(read_safe(torii / CRC_FILE))
    lines, found, computed, reused = [CRC_HEADER], {}, 0, 0
    for n, (card_path, file_path, console) in enumerate(games):
        size = os.path.getsize(file_path)
        rec = old.get(card_path.lower())
        # A CRC holds while the file keeps its size and the date the kernel
        # took for it; one without a date (written here, the cart not yet
        # seen) is computed again
        if (
            rec
            and rec[0] is not None
            and rec[2] == size
            and rec[3] == fat_date(file_path)
        ):
            crc, code, date = rec[0], rec[1], rec[3]
            reused += 1
        else:
            if sys.stderr.isatty():
                print(
                    f"\r\033[KCRC {n + 1}/{len(games)}: {card_path[-60:]}",
                    end="",
                    file=sys.stderr,
                )
            crc, code, size2 = file_crc(file_path)
            if size2 != size:
                continue
            if console != GBA:
                code = None
            date = None  # the kernel's walk takes the file's FAT date
            computed += 1
        found[card_path] = (crc, code if console == GBA else None)
        lines.append(f"{crc:08X}\t{code or '-'}\t{size}\t{date or '-'}\t{card_path}\n")
    if computed and sys.stderr.isatty():
        print("\r\033[K", end="", file=sys.stderr)
    torii.mkdir(exist_ok=True)
    text = "".join(lines)
    if len(text.encode("utf-8")) > CACHE_FILE_MAX - 32:
        print(
            f"warning: crc.txt is over {CACHE_FILE_MAX} bytes: the kernel won't read it",
            file=sys.stderr,
        )
    write_safe(torii / CRC_FILE, text)
    return found, computed, reused


# --- update ---------------------------------------------------------------------------


def update(args):
    sd = Path(args.sd_path)
    if not sd.is_dir():
        raise NamesError(
            f"{sd} isn't a folder: give the card's folder, e.g. /Volumes/OMEGA"
        )
    dats = Path(args.dats)
    texts = []
    for _, file, _ in CONSOLES:
        texts.append(
            fetch(dats, file, args.offline, args.refresh).read_text("utf-8", "replace")
        )
    tables, _, version = make_tables(texts)
    db = build_db(tables, {}, version)  # no game codes: the kernel names by CRC only
    if len(db) > DB_MAX:
        raise NamesError(
            f"names.db would be {len(db)} bytes, over the kernel's {DB_MAX}"
        )
    torii = sd / "torii"
    torii.mkdir(exist_ok=True)
    tmp = torii / "names.db.part"
    tmp.write_bytes(db)
    tmp.replace(torii / "names.db")
    counts = ", ".join(
        f"{c[0]} {len(t)}" for c, t in zip(CONSOLES, tables, strict=True)
    )
    print(
        f"wrote {torii / 'names.db'}: dats {version or '(no version)'}, {counts}, {len(db)} bytes"
    )
    if args.no_crc:
        return
    games = walk_card(sd)
    pool = sum((len(p.encode("utf-8")) + 2) & ~1 for p, _, _ in games)
    if len(games) > CACHE_MAX or pool > CACHE_POOL:
        print(
            f"warning: {len(games)} games with {pool} bytes of paths; the kernel keeps "
            f"{CACHE_MAX} games and {CACHE_POOL} bytes, and leaves crc.txt alone when it holds more",
            file=sys.stderr,
        )
    found, computed, reused = update_cache(sd, games)
    print(
        f"wrote {torii / CRC_FILE}: {len(found)} games, {computed} CRCs computed, {reused} kept"
    )
    for i, (label, _, _) in enumerate(CONSOLES):
        mine = [(p, *found[p]) for p, _, c in games if c == i and p in found]
        if not mine:
            continue
        by_crc = sum(db_name(tables, i, crc) is not None for _, crc, _ in mine)
        print(
            f"  {label}: {len(mine)} games, {by_crc} named by CRC, "
            f"{len(mine) - by_crc} keep their file names (not in the dats: romhacks, translations, other dumps)"
        )


def main(argv):
    p = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        epilog=__doc__.split("\n\n", 1)[1],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = p.add_subparsers(dest="cmd", required=True)
    u = sub.add_parser("update", help="write names.db and crc.txt to a card")
    u.add_argument("sd_path", help="the card's folder on this PC")
    u.add_argument("--no-crc", action="store_true", help="leave crc.txt alone")
    u.add_argument(
        "--offline", action="store_true", help="never download: use the cached dats"
    )
    u.add_argument("--refresh", action="store_true", help="download the dats again")
    u.add_argument(
        "--dats",
        default=str(DATS),
        help=f"the dats' cache (default {DATS.relative_to(ROOT)})",
    )
    a = sub.add_parser("add", help="your own names for many games, into names.txt")
    a.add_argument("sd_path", help="the card's folder on this PC")
    a.add_argument("file", help="lines of a game's file name, a tab and its name")
    a.add_argument("--dry-run", action="store_true", help="print, write nothing")
    args = p.parse_args(argv)
    try:
        if args.cmd == "add":
            add_names(args)
        else:
            update(args)
    except NamesError as e:
        print(f"names-{args.cmd}: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
