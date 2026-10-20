#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# ///
"""Make the names database for an SD card: /torii/names.db and /torii/crc.txt.

usage: names_db.py update SD_PATH [--no-crc] [--offline] [--refresh] [--dats DIR]

`just names-update SD_PATH` runs this. It needs SD_PATH, the card's folder
on this PC (a mounted card, say /Volumes/OMEGA), and:

1. Downloads the No-Intro dats of the Game Boy Advance, Game Boy Color,
   Game Boy and NES from libretro-database (metadat/no-intro), one request
   each, into a cache on this PC (dist/dats, git-ignored). Later runs use the
   cache; --refresh downloads them again, --offline never does. --dats
   reads and caches them in another folder.
2. Writes SD_PATH/torii/names.db: per console the CRC32 of each game in the
   dats and its No-Intro name, and the Game Boy Advance games' codes
   (source/namesdb.h has the format).
3. Computes the CRC32 of every game on the card (by extension, outside the
   folders the kernel hides) and writes SD_PATH/torii/crc.txt, so the card
   shows the names at once. A file whose path and size crc.txt already has
   isn't read again. --no-crc leaves crc.txt alone: the kernel's Identify
   library and its launches compute CRCs too, far more slowly.

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
        version.encode("ascii", "replace")[:16],
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


def db_name(tables, codes, console, crc, code):
    """The name the kernel shows (source/namesdb.c, names_db_name) and how it
    found it: ("...", "crc"), ("...", "code") or (None, None)."""
    other = {1: 2, 2: 1}.get(console)
    for i in (console, other):
        if i is not None and crc is not None and crc in tables[i]:
            return tables[i][crc], "crc"
    if console == GBA and code and code_key(code) in codes:
        return codes[code_key(code)], "code"
    return None, None


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
    m = re.search(rb"(?:^|\n)(#crc32 ([0-9a-fA-F]{8})[ \t\r]*\n?)$", data)
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
        if rec and rec[0] is not None and rec[2] == size:
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
    write_safe(torii / CRC_FILE, "".join(lines))
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
    tables, codes, version = make_tables(texts)
    db = build_db(tables, codes, version)
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
        f"wrote {torii / 'names.db'}: dats {version or '(no version)'}, {counts}, {len(codes)} GBA codes, {len(db)} bytes"
    )
    if args.no_crc:
        return
    games = walk_card(sd)
    found, computed, reused = update_cache(sd, games)
    print(
        f"wrote {torii / CRC_FILE}: {len(found)} games, {computed} CRCs computed, {reused} kept"
    )
    for i, (label, _, _) in enumerate(CONSOLES):
        mine = [found[p] for p, _, c in games if c == i and p in found]
        if not mine:
            continue
        how = [db_name(tables, codes, i, crc, code)[1] for crc, code in mine]
        by_crc, by_code = how.count("crc"), how.count("code")
        print(
            f"  {label}: {len(mine)} games, {by_crc} named by CRC, {by_code} by game code, "
            f"{len(mine) - by_crc - by_code} keep their file names"
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
    args = p.parse_args(argv)
    try:
        update(args)
    except NamesError as e:
        print(f"names-update: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
