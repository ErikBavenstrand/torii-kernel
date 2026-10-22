#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pillow==13.0.0"]
# ///
"""Report the games on an SD card that need a name or box art.

usage: library_report.py SD_PATH [--write-templates] [--out DIR] [--dats DIR] [--cache DIR]

`just library-report SD_PATH` runs this. It reads the card only, offline,
and changes nothing on it. For every game names-update finds (outside the
folders the kernel hides) it takes:

- the CRC32 from /torii/crc.txt, where its record still holds (the file's
  size and date); a game without one has no CRC yet: the cart identifies it
  on its next walk, or `just names-update` does;
- the name the kernel shows: /torii/names.txt's (`just names-add`, Rename),
  else the dats' name of the CRC from /torii/names.db (the cached dats in
  dist/dats if the card has none), else the file name;
- the art the kernel shows: /torii/art/custom/<file name>.bin, else
  /torii/art/<CRC32>.bin, else the full art pack /torii/art.pak.

It prints only the games that need attention, in groups: no name (a CRC the
dats don't list: a romhack, a translation, a bad dump), no CRC yet, no art
(with why: not in the dats, no thumbnail on libretro-thumbnails as
art-scrape's cache in dist/art-cache remembers, not fetched yet, no CRC
yet), and possible wrong matches (a dats name far from the file's name).
Then a line per console: games, named, with art.

--write-templates writes two files into the current folder (or --out DIR),
never onto the card: names-todo.txt in `just names-add`'s FILE format (a
game's card path, a tab, its file or dats name, to edit; a line left as it
is pins that name) for the unnamed games and the possible wrong matches, and art-todo.txt, the games needing
`just art-add`.
"""

import argparse
import difflib
import re
import sys
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import art
import art_scrape
import artpack
import names_db

# Words too common to make two titles the same game
STOP = {
    "the", "a", "an", "of", "and", "in", "on", "to", "no", "de", "la", "le",
    "version", "edition", "game", "games", "rev", "vol",
}  # fmt: skip
ROMAN = {
    "i": "1", "ii": "2", "iii": "3", "iv": "4", "v": "5", "vi": "6",
    "vii": "7", "viii": "8", "ix": "9", "x": "10", "xi": "11", "xii": "12",
}  # fmt: skip
SIMILAR = 0.5  # difflib ratio under which two titles may be different games


class ReportError(Exception):
    pass


# --- Comparing names ---------------------------------------------------------------


def title_words(name):
    """A name's words to compare: no extension, (...) and [...] tags or scene
    number, the trailing article ("Zelda, The") dropped, accents and case
    folded, punctuation split, Roman numerals as digits."""
    low = name.lower()
    if any(low.endswith(e) for _, _, exts in names_db.CONSOLES for e in exts):
        name = name[: name.rindex(".")]
    if " " not in name:
        name = name.replace("_", " ")
    while True:
        cut = names_db.GROUP.sub(" ", name)
        if cut == name:
            break
        name = cut
    name = re.sub(r"^\s*\d{4,5}\s*-\s*", "", name)
    name = re.sub(r",\s*(the|an?)\b", " ", name, flags=re.IGNORECASE)
    text = unicodedata.normalize("NFKD", name).casefold().replace("&", " and ")
    text = "".join(c for c in text if c.isascii())
    words = re.findall(r"[a-z]+|[0-9]+", text.replace("'", ""))
    return [ROMAN.get(w, w) for w in words]


def significant(words):
    return {w for w in words if w not in STOP and (len(w) >= 3 or w.isdigit())}


def initials(words, stop=STOP):
    return "".join(w if w.isdigit() else w[0] for w in words if w not in stop)


def acronym(short, words, subtitle):
    """Whether short (a file name's joined words) is an acronym of the dats
    name's words or of its subtitle (the part after " - "), with or without
    its stop words, a trailing number allowed ("FE8", "TMC", "SML2")."""
    core = short.rstrip("0123456789")
    if len(core) < 2:
        return False
    forms = [initials(w, stop) for w in (words, subtitle) for stop in (STOP, ())]
    return any(f and (short == f or f.startswith(core)) for f in forms)


def unlike(file_name, dat_name):
    """Whether a file's name and its dats name look like different games: no
    significant word shared, not an acronym of the other, and little alike
    (difflib ratio under SIMILAR, with and without spaces). False when
    either has nothing to compare (a name in another script)."""
    a, b = title_words(file_name), title_words(dat_name)
    if not a or not b:
        return False
    if any(not w.isdigit() for w in significant(a) & significant(b)):
        return False
    ja, jb = "".join(a), "".join(b)
    sub = title_words(dat_name.split(" - ", 1)[1]) if " - " in dat_name else []
    if acronym(ja, b, sub) or jb == initials(a):
        return False
    if ja in jb or jb in ja:
        return False
    ratio = max(
        difflib.SequenceMatcher(None, " ".join(a), " ".join(b)).ratio(),
        difflib.SequenceMatcher(None, ja, jb).ratio(),
    )
    return ratio < SIMILAR


# --- The card ------------------------------------------------------------------------


def load_tables(sd, dats):
    """(tables, codes, where): names.db's tables if the card has one that
    reads, else the cached dats'; codes (GBA game codes) from the dats when
    cached. tables is None when neither is there."""
    dat_tables, codes = None, {}
    try:
        texts = [
            names_db.fetch(Path(dats), d, True, False).read_text("utf-8", "replace")
            for _, d, _ in names_db.CONSOLES
        ]
        dat_tables, codes, _ = names_db.make_tables(texts)
    except names_db.NamesError:
        pass
    db = Path(sd) / "torii" / "names.db"
    if db.is_file():
        try:
            version, tables = names_db.read_db(db.read_bytes())
            where = f"/torii/names.db (dats {version or '?'})"
            if not codes:
                where += "; no cached dats, so patched GBA dumps count as unnamed"
            return tables, codes, where
        except (names_db.NamesError, UnicodeError, ValueError) as e:
            print(f"library-report: {db}: {e}; not used", file=sys.stderr)
    if dat_tables is not None:
        return (
            dat_tables,
            codes,
            f"the cached dats in {dats} (the card has no names.db)",
        )
    return None, codes, None


def read_pack(sd):
    path = Path(sd) / "torii" / "art.pak"
    if not path.is_file():
        return None
    try:
        return artpack.Pack(path.read_bytes())
    except artpack.PackError as e:
        print(f"library-report: {path}: {e}; not used", file=sys.stderr)
        return None


def file_stem(card_path):
    name = Path(card_path).name
    return name[: name.rindex(".")]


def survey(sd, dats, cache):
    """Each game as a dict: path, console, crc, name (shown), source of the
    name ("own", "dats", "title", "file"), dat (the dats' name), art (where
    from, or None) and why (no art's reason)."""
    torii = Path(sd) / "torii"
    games = names_db.walk_card(sd)
    records = names_db.parse_cache(names_db.read_safe(torii / names_db.CRC_FILE))
    written = names_db.cache_mtime(torii)
    _, own = names_db.parse_names(names_db.read_safe(torii / names_db.NAMES_FILE))
    tables, codes, where = load_tables(sd, dats)
    art_dir = torii / "art"
    have = art_scrape.listing(art_dir)
    custom = art_scrape.listing(art_dir / "custom", art_scrape.ascii_lower)
    pack = read_pack(sd)
    misses = set()
    misses_file = Path(cache) / art_scrape.MISSES
    if misses_file.is_file():
        misses = set(misses_file.read_text("utf-8", "replace").splitlines())
    out = []
    for card_path, file_path, console in games:
        g = {"path": card_path, "console": console, "dat": None, "art": None}
        try:
            size = Path(file_path).stat().st_size
        except OSError as e:
            print(f"library-report: {card_path}: {e}; skipped", file=sys.stderr)
            continue
        rec = records.get(card_path.lower())
        crc = rec[0] if names_db.record_holds(rec, file_path, size, written) else None
        g["crc"] = crc
        title = None
        if crc is not None and tables is not None:
            g["dat"] = names_db.db_name(tables, console, crc)
            if g["dat"] is None and console == names_db.GBA:
                code = names_db.header_code(file_path)
                title = names_db.code_match(codes, console, code, Path(card_path).name)
        mine = own.get(names_db.ascii_fold(card_path))
        if mine:
            g["name"], g["source"] = mine[1], "own"
        elif g["dat"]:
            g["name"], g["source"] = g["dat"], "dats"
        else:
            g["name"] = file_stem(card_path)
            g["source"] = "title" if title else "file"
        if art_scrape.ascii_lower(Path(card_path).name + art.EXT) in custom:
            g["art"] = "custom"
        elif crc is not None and art_scrape.folded(f"{crc:08X}{art.EXT}") in have:
            g["art"] = "card"
        elif crc is not None and pack is not None and pack.find(crc) is not None:
            g["art"] = "pack"
        g["why"] = (
            None if g["art"] else no_art_reason(g, tables, codes, file_path, misses)
        )
        out.append(g)
    return out, where, pack


def no_art_reason(g, tables, codes, file_path, misses):
    crc, console = g["crc"], g["console"]
    if crc is None:
        return "no CRC yet"
    if tables is None:
        return "no names database to look it up in"
    code = art_scrape.game_code(tables, console, crc, file_path)
    picks = art_scrape.tries(
        art_scrape.candidates(tables, codes, console, crc, code, Path(g["path"]).name)
    )
    if not picks:
        return "not in the dats"
    if all(f"{art_scrape.SYSTEMS[s]}/{n}" in misses for s, n in picks):
        return "no thumbnail"
    return "not fetched yet (`just art-scrape` or `just art-pack`)"


# --- The report ------------------------------------------------------------------------


def report(sd, games, where, pack, out=print):
    out(
        f"library report for {sd}: {len(games)} games; names from {where or 'nowhere'}; "
        + (f"art.pak with {len(pack.crcs)} CRCs" if pack else "no art.pak")
    )
    if where is None:
        out("  no /torii/names.db and no cached dats: run `just names-update` first")
    no_crc = [g for g in games if g["crc"] is None and g["source"] != "own"]
    no_name = [
        g for g in games if g["crc"] is not None and where and g["source"] == "file"
    ]
    no_art = [g for g in games if not g["art"]]
    wrong = [
        g
        for g in games
        if g["source"] == "dats" and unlike(Path(g["path"]).name, g["dat"])
    ]
    groups = [
        (
            "No name",
            no_name,
            (
                "the dats don't list their CRC (romhacks, translations, bad dumps); "
                "fix: `just names-add SD FILE`"
            ),
            lambda g: f"{g['crc']:08X}",
        ),
        (
            "No CRC yet",
            no_crc,
            "the cart identifies them on its next walk, or run `just names-update SD`",
            None,
        ),
        (
            "No art",
            no_art,
            "fix: `just art-add SD` (pictures named after the files)",
            lambda g: g["why"],
        ),
        (
            "Possible wrong match",
            wrong,
            (
                "the dats' name is far from the file's: check the file; "
                "a `just names-add` name wins"
            ),
            lambda g: g["dat"],
        ),
    ]
    for title, rows, how, detail in groups:
        if not rows:
            continue
        out(f"{title} ({len(rows)}): {how}")
        for g in rows:
            out(f"  {g['path']}" + (f": {detail(g)}" if detail else ""))
    out("Summary:")
    for i, (label, _, _) in enumerate(names_db.CONSOLES):
        mine = [g for g in games if g["console"] == i]
        if not mine:
            continue
        named = sum(g["source"] != "file" for g in mine)
        with_art = sum(bool(g["art"]) for g in mine)
        out(f"  {label}: {len(mine)} games, {named} named, {with_art} with art")
    return no_name, no_crc, no_art, wrong


def write_templates(folder, no_name, wrong, no_art):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    names = [
        "# For `just names-add SD names-todo.txt`: a game's card path, a tab, its name.\n",
        "# Edit the names; delete the lines to leave alone (an empty name removes one).\n",
    ]
    seen = set()
    for g in no_name + wrong:
        if g["path"] not in seen:
            seen.add(g["path"])
            names.append(f"{g['path']}\t{g['name']}\n")
    arts = [
        "# Games without box art: put a picture named after each file (Game.png for\n",
        "# /GBA/Game.gba) in the card's /torii/art/custom-src, then `just art-add SD`.\n",
    ]
    arts += [f"{g['path']}\n" for g in no_art]
    (folder / "names-todo.txt").write_text("".join(names), "utf-8")
    (folder / "art-todo.txt").write_text("".join(arts), "utf-8")
    return folder / "names-todo.txt", folder / "art-todo.txt"


def main(argv):
    p = argparse.ArgumentParser(
        prog="library_report.py",
        description=__doc__.split("\n\n")[0],
        epilog=__doc__.split("\n\n", 1)[1],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("sd_path", metavar="SD_PATH", help="the card's folder on this PC")
    p.add_argument(
        "--write-templates",
        action="store_true",
        help="write names-todo.txt and art-todo.txt (not onto the card)",
    )
    p.add_argument("--out", default=".", help="the templates' folder (default: here)")
    p.add_argument(
        "--dats",
        default=str(names_db.DATS),
        help=f"the dats' cache (default {names_db.DATS.relative_to(ROOT)})",
    )
    p.add_argument(
        "--cache",
        default=str(art_scrape.CACHE),
        help=f"art-scrape's cache (default {art_scrape.CACHE.relative_to(ROOT)})",
    )
    args = p.parse_args(argv)
    sd = Path(args.sd_path)
    try:
        if not sd.is_dir():
            raise ReportError(
                f"{sd} isn't a folder: give the card's folder, e.g. /Volumes/OMEGA"
            )
        games, where, pack = survey(sd, args.dats, args.cache)
    except ReportError as e:
        print(f"library-report: {e}", file=sys.stderr)
        return 1
    except OSError as e:
        print(f"library-report: {names_db.os_error(e)}", file=sys.stderr)
        return 1
    no_name, _, no_art, wrong = report(sd, games, where, pack)
    if args.write_templates:
        try:
            files = write_templates(args.out, no_name, wrong, no_art)
        except OSError as e:
            print(f"library-report: {e}", file=sys.stderr)
        else:
            print("wrote " + ", ".join(str(f) for f in files))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
