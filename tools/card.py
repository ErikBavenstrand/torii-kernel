#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Set up an SD card's games in one step: copy games onto it, then give them
names, box art and cheats.

usage: card.py SD_PATH [GAME_OR_FOLDER...] [--to FOLDER] [--offline]
               [--no-art] [--no-cheats] [--fetch-cheats] [--new-card]
               [--dry-run]

`just card SD_PATH [GAMES...]` runs this. SD_PATH is the card's folder on
this PC (a mounted card, say /Volumes/OMEGA) with the kernel or /torii on
it (--new-card takes a card's root with neither). In order, it:

1. Copies each game given (.gba, .gbc, .gb, .nes; a folder's games with its
   folders) to the card: to its root, or to --to FOLDER on it. A game the
   card already has at that place is left alone, so its saves stay with it.
2. Names the card's games: `just names-update` (No-Intro names by CRC).
3. Converts your own pictures in /torii/art/custom-src (named after a
   game's file, say "Pokemon Prism.png" for "Pokemon Prism.gbc"):
   `just art-add`. The folder is made if the card lacks it, so there is
   always a place to drop them.
4. Fetches box art for the rest: `just art-scrape` (--no-art skips 3 and 4).
5. Writes cheat files for the GBA games from the cheats cached on this PC:
   `just cheats` (--no-cheats skips it). --fetch-cheats fetches the ones
   missing from GameHacking.org through your browser first
   (`just cheats --gamehacking`).
6. Reports what still lacks a name or box art: `just library-report`.

A step that fails is reported and the next runs; the end lists each step's
outcome. --offline downloads nothing (the names and art steps use what this
PC has cached). --dry-run lists what would be copied and run, and changes
nothing. Each step is one of the tools above, which say more with --help.

On the cart, without a PC: a game's popup renames it (Rename) or gives it
its name back (Original name); its Cheats screen picks, adds, edits,
renames and deletes cheats, and Settings' Clean up cheats removes the ones
greyed out.
"""

import argparse
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
GAME_EXTS = (".gba", ".gbc", ".gb", ".nes")
ART_SOURCE = Path("torii") / "art" / "custom-src"


class CardError(Exception):
    pass


def check_card(sd, new_card=False):
    """sd as a card's folder: one that holds the kernel or /torii, or, with
    new_card, any mounted volume's root but the system's, so a typo can't
    fill some other folder or disk with games."""
    if not sd.is_dir():
        raise CardError(f"{sd}: no such folder (the card's, such as /Volumes/OMEGA)")
    if (sd / "torii").is_dir() or (sd / "ezkernelnew.bin").is_file():
        return
    if new_card and os.path.ismount(sd) and sd.resolve() != Path("/"):
        return
    raise CardError(
        f"{sd}: not a Torii card (no /torii and no ezkernelnew.bin on it); "
        "copy the kernel to it first, or give --new-card for a card's root"
    )


def is_game(path):
    return path.suffix.lower() in GAME_EXTS and not path.name.startswith(".")


@dataclass
class Copies:
    """What copy_games did (or would do): card paths, from the card's root."""

    copied: list = field(default_factory=list)
    kept: list = field(default_factory=list)  # already on the card: left alone
    skipped: list = field(default_factory=list)  # given, but not a game


def games_in(source):
    """(file, its path under the card folder) for a game file, or each game
    in a folder and its subfolders, the folder's name kept."""
    if source.is_file():
        return [(source, Path(source.name))] if is_game(source) else []
    out = []
    for root, dirs, files in os.walk(source):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for name in sorted(files):
            f = Path(root) / name
            if is_game(f):
                out.append((f, Path(source.name) / f.relative_to(source)))
    return out


def copy_games(sd, sources, to=None, dry_run=False):
    """Copies the games in sources (files or folders) to the card at sd,
    under to (a folder on the card) if given. A game whose place on the card
    is taken is left alone. Copies without extended attributes, which a FAT
    card would keep as ._ files the kernel lists as games."""
    done = Copies()
    base = sd / to.strip("/") if to else sd
    if not base.resolve().is_relative_to(sd.resolve()):
        raise CardError(f"--to {to}: not a folder on the card")
    for given in sources:
        source = Path(given).expanduser()
        if not source.exists():
            raise CardError(f"{source}: no such file or folder")
        found = games_in(source)
        if not found:
            done.skipped.append(str(source))
        for f, rel in found:
            dest = base / rel
            shown = "/" + dest.relative_to(sd).as_posix()
            if dest.exists():
                done.kept.append(shown)
                continue
            if not dry_run:
                dest.parent.mkdir(parents=True, exist_ok=True)
                part = dest.with_name(dest.name + ".part")
                try:
                    shutil.copyfile(f, part)
                    os.replace(part, dest)
                except OSError:
                    part.unlink(
                        missing_ok=True
                    )  # a full card: nothing half-copied left
                    raise
            done.copied.append(shown)
    return done


@dataclass
class Step:
    name: str
    argv: list | None  # None: skipped
    why: str = ""  # a skipped step's reason


def steps(sd, args):
    """The steps after the copy, in order, as tool command lines."""

    def tool(name, *rest):
        return ["uv", "run", "-q", str(TOOLS / name), *rest]

    offline = ["--offline"] if args.offline else []
    out = [Step("names", tool("names_db.py", "update", str(sd), *offline))]
    pictures = sd / ART_SOURCE
    has_pictures = pictures.is_dir() and any(
        not p.name.startswith(".") and p.is_file() for p in pictures.iterdir()
    )
    if args.no_art:
        out += [Step("your art", None, "--no-art"), Step("box art", None, "--no-art")]
    else:
        out.append(
            Step("your art", tool("art.py", "add", str(sd)))
            if has_pictures
            else Step("your art", None, f"no pictures in /{ART_SOURCE.as_posix()}")
        )
        out.append(Step("box art", tool("art_scrape.py", str(sd), *offline)))
    if args.no_cheats:
        out.append(Step("cheats", None, "--no-cheats"))
    elif args.fetch_cheats and args.offline:
        out.append(
            Step(
                "cheats",
                None,
                "--fetch-cheats needs the network, and --offline was given",
            )
        )
    else:
        fetch = ["--gamehacking"] if args.fetch_cheats else []
        out.append(Step("cheats", tool("cheats.py", str(sd), *fetch)))
    out.append(Step("report", tool("library_report.py", str(sd))))
    return out


def main(argv=None):
    p = argparse.ArgumentParser(
        prog="just card",
        description="Copy games to an SD card, then give them names, box art and cheats.",
    )
    p.add_argument("sd", type=Path, help="the card's folder, such as /Volumes/OMEGA")
    p.add_argument(
        "games", nargs="*", help="game files or folders of games to copy onto the card"
    )
    p.add_argument(
        "--to", help="a folder on the card for the games (default: its root)"
    )
    p.add_argument("--offline", action="store_true", help="download nothing")
    p.add_argument("--no-art", action="store_true", help="leave box art alone")
    p.add_argument("--no-cheats", action="store_true", help="leave cheat files alone")
    p.add_argument(
        "--fetch-cheats",
        action="store_true",
        help="fetch missing cheats from GameHacking.org first",
    )
    p.add_argument(
        "--new-card",
        action="store_true",
        help="take a mounted card with nothing of Torii's on it yet",
    )
    p.add_argument(
        "--dry-run", action="store_true", help="show what would happen, change nothing"
    )
    args = p.parse_args(argv)
    sd = args.sd.expanduser()
    try:
        check_card(sd, args.new_card)
        copies = copy_games(sd, args.games, args.to, args.dry_run)
        if not args.dry_run:
            (sd / ART_SOURCE).mkdir(parents=True, exist_ok=True)
    except (CardError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    would = "would copy" if args.dry_run else "copied"
    for g in copies.copied:
        print(f"{would}: {g}")
    for g in copies.kept:
        print(f"already on the card, left alone: {g}")
    for s in copies.skipped:
        print(f"no games in: {s}")
    results = []
    for step in steps(sd, args):
        if step.argv is None:
            results.append((step.name, f"skipped ({step.why})"))
            continue
        if args.dry_run:
            results.append((step.name, "would run: " + " ".join(step.argv[3:])))
            continue
        print(f"\n== {step.name}", flush=True)
        try:
            code = subprocess.run(step.argv, check=False).returncode
        except OSError as e:
            print(f"error: {e}", file=sys.stderr)
            code = -1
        results.append((step.name, "done" if code == 0 else f"failed (status {code})"))
    if not args.dry_run and sys.platform == "darwin":
        # macOS keeps a file's extended attributes on a FAT card as ._ files
        # beside it (the kernel skips them, but they clutter the card)
        subprocess.run(["dot_clean", "-m", str(sd)], check=False)
    print("\nAll steps:")
    print(f"  games: {len(copies.copied)} {would}, {len(copies.kept)} already there")
    for name, outcome in results:
        print(f"  {name}: {outcome}")
    if not args.dry_run:
        print(
            f"Drop your own box art in {sd / ART_SOURCE} (named after the game's file) and run this again."
        )
        print("Eject the card before taking it out.")
    return 1 if any(o.startswith("failed") for _, o in results) else 0


if __name__ == "__main__":
    sys.exit(main())
