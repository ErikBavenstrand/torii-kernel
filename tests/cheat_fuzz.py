#!/usr/bin/env python3
"""Differential cheat fuzzing, for tests/cheat_fuzz.sh: seeded random cheat
programs (tools/cheat_gen.py: valid encodings in every format, writing only
where the cheat test ROM watches, reading its counters and keys), each run as
a case of tests/cheat_engine.py in mGBA's engine and through Torii with
random keys held. The programs leave out what Torii means to do unlike mGBA
(tests/cheat_expected.toml's allow rules), so any difference is a finding.

Usage:
  cheat_fuzz.py gen DIR SEED COUNT    write COUNT cases (fz-SEED-I) into DIR
                                      as cheat_engine.py gen does, and
                                      DIR/fuzz.json (their programs)
  cheat_fuzz.py minimize DIR NAME...  shrink each failing case (whole cheats,
                                      then whole codes, then the keys) while
                                      it still fails, rerunning it with
                                      tests/cheat_engine.sh --case, and save it
                                      to tests/cheat_fuzz_cases.json
  cheat_fuzz.py failing DIR           the names of DIR's failing cases, from
                                      DIR/report.txt
  cheat_fuzz.py decode-gen DIR SEED COUNT
                                      COUNT random cheats of each format,
                                      addressed all over the GBA's memory, as
                                      .cht files in DIR for tests/
                                      cheat_corpus.sh: decoding only, so
                                      cheap at any count

tests/cheat_engine.py runs the saved cases with the others (in `just test`):
a saved case that fails for a known bug needs an [[engine]] entry with the
bug in tests/cheat_expected.toml; one that fails for a bug not known yet
needs a [[bug]] entry too.
"""

import contextlib
import io
import json
import os
import random
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))
import cheat_engine
from cheat_encode import cht
from cheat_gen import FORMATS, Space, encode, program_units
from cheat_gen import program as random_cheats

SAVED = ROOT / "tests" / "cheat_fuzz_cases.json"
# Keys a run may hold: never R and SELECT, Torii's stand-in for the device's
# button (cheatcode.h's CHEAT_BUTTON_KEYS)
KEY_CHOICES = ["-", "A", "B", "A+B", "UP", "DOWN+LEFT", "START", "L", "RIGHT+A"]


def program(seed, i):
    """One random case's program: a dict tests/cheat_fuzz_cases.json keeps"""
    rng = random.Random(f"{seed}:{i}")
    sym = cheat_engine.symbols()
    fmt = rng.choice(list(FORMATS))
    button = {}
    use_button = fmt.startswith(("gsa", "par")) and rng.random() < 0.3
    if use_button:
        on = rng.choice(cheat_engine.CHECKPOINTS[:-2])
        button = {
            on: 1,
            rng.choice([cp for cp in cheat_engine.CHECKPOINTS if cp > on]): 0,
        }
    keys = {}
    for cp in sorted(rng.sample(cheat_engine.CHECKPOINTS[:-1], rng.randrange(0, 4))):
        keys[cp] = rng.choice(KEY_CHOICES)
    space = Space.rom_space(sym["rom_table"], sym["rom_table2"], keys=not use_button)
    units = program_units(rng, fmt, space, rng.randrange(1, 5), 6)
    # mGBA's libretro reader takes lines of up to 511 characters
    for ci in range(len(units)):
        while (
            len(units[ci][1]) > 1
            and len(encode(fmt, units)[ci][1]) > cheat_engine.CODE_MAX
        ):
            units[ci] = (units[ci][0], units[ci][1][:-1])
    return {
        "name": f"fz-{seed}-{i}",
        "fmt": fmt,
        "keys": keys,
        "button": button,
        "units": units,
    }


def case(p):
    """A cheat_engine Case of a program (with units, or saved code text)"""
    cheats = p["cheats"] if "cheats" in p else encode(p["fmt"], p["units"])
    return cheat_engine.Case(
        p["name"],
        FORMATS[p["fmt"]],
        None,
        cheats=[(d, c, None) for d, c in cheats],
        keys={int(k): v for k, v in p["keys"].items()},
        button={int(k): v for k, v in p["button"].items()},
        effect=False,  # a random program may rightly change nothing
        note=p.get("note", ""),
    )


def saved_cases():
    """tests/cheat_fuzz_cases.json's cases, for tests/cheat_engine.py"""
    if not SAVED.exists():
        return []
    return [case(p) for p in json.loads(SAVED.read_text(encoding="utf-8"))]


def gen(out, seed, count):
    programs = [program(seed, i) for i in range(count)]
    cheat_engine.gen(out, None, [case(p) for p in programs])
    (out / "fuzz.json").write_text(json.dumps(programs), encoding="utf-8")


def fails(work, p):
    """Whether program p's case fails, run alone in work"""
    subprocess.run(["rm", "-rf", str(work)], check=True)
    c = case(p)
    cheat_engine.gen(work, None, [c])
    carts = (
        Path(os.environ.get("TMPDIR", "/tmp")) / "torii-carts" / f"fuzz-min-{work.name}"
    )
    carts.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["tests/cheat_engine.sh", "--case", str(work), c.name, str(carts)], check=True
    )
    subprocess.run(["tests/cheat_engine.sh", "--decode", str(work)], check=True)
    with contextlib.redirect_stdout(io.StringIO()) as text:
        status = cheat_engine.report(work)
    p["why"] = text.getvalue().splitlines()[0] if text.getvalue() else ""
    return status != 0


def minimize(out, name):
    programs = {
        p["name"]: p
        for p in json.loads((out / "fuzz.json").read_text(encoding="utf-8"))
    }
    p = programs[name]
    work = out / f"min-{name}"
    if not fails(work, p):
        print(f"{name}: doesn't fail on its own; not saved")
        return None
    runs = 1
    changed = True
    while changed:
        changed = False
        # Whole cheats
        i = 0
        while len(p["units"]) > 1 and i < len(p["units"]):
            q = dict(p, units=p["units"][:i] + p["units"][i + 1 :])
            runs += 1
            if fails(work, q):
                p, changed = q, True
            else:
                i += 1
        # Whole codes (a conditional with what it covers is one)
        for ci in range(len(p["units"])):
            j = 0
            while len(p["units"][ci][1]) > 1 and j < len(p["units"][ci][1]):
                units = list(p["units"])
                desc, us = units[ci]
                units[ci] = (desc, us[:j] + us[j + 1 :])
                q = dict(p, units=units)
                runs += 1
                if fails(work, q):
                    p, changed = q, True
                else:
                    j += 1
    for field in ("keys", "button"):
        if p[field]:
            q = dict(p, **{field: {}})
            runs += 1
            if fails(work, q):
                p = q
    fails(work, p)  # leaves its run and p["why"] in work
    saved = {
        "name": name,
        "fmt": p["fmt"],
        "keys": p["keys"],
        "button": p["button"],
        "cheats": encode(p["fmt"], p["units"]),
        "note": f"tests/cheat_fuzz.py seed {name}, minimized in {runs} runs: {p['why'][:300]}",
    }
    print(
        f"{name}: minimized in {runs} runs to {sum(len(u) for _, u in p['units'])} codes: {p['why']}"
    )
    return saved


def save(found):
    old = json.loads(SAVED.read_text(encoding="utf-8")) if SAVED.exists() else []
    by_name = {p["name"]: p for p in old}
    for p in found:
        by_name[p["name"]] = p
    SAVED.write_text(
        json.dumps([by_name[k] for k in sorted(by_name)], indent=1) + "\n",
        encoding="utf-8",
    )


def main():
    args = sys.argv[1:]
    if len(args) == 4 and args[0] == "gen":
        gen(Path(args[1]), int(args[2]), int(args[3]))
        return 0
    if len(args) >= 3 and args[0] == "minimize":
        out = Path(args[1])
        found = [s for s in (minimize(out, n) for n in args[2:]) if s]
        if found:
            save(found)
            print(
                f"saved {len(found)} cases to {SAVED.relative_to(ROOT)}: review, commit, and add an [[engine]] entry naming each one's bug to tests/cheat_expected.toml"
            )
        return 0
    if len(args) == 4 and args[0] == "decode-gen":
        out, seed, count = Path(args[1]), int(args[2]), int(args[3])
        out.mkdir(parents=True, exist_ok=True)
        for fmt in FORMATS:
            # A file per 200 cheats: one CodeBreaker key, GameShark seeds
            # and format carry across a file's cheats
            for start in range(0, count, 200):
                rng = random.Random(f"{seed}:{fmt}:{start}")
                cheats = random_cheats(
                    rng, fmt, Space.anywhere(), min(200, count - start), 8
                )
                (out / f"{fmt}-{start // 200}.cht").write_text(
                    cht(cheats) + "\n", encoding="utf-8"
                )
        return 0
    if len(args) == 2 and args[0] == "failing":
        for line in (Path(args[1]) / "report.txt").read_text().splitlines():
            if line.startswith("FAIL: cheat_engine: "):
                print(line.split()[2])
        return 0
    sys.exit(__doc__)


if __name__ == "__main__":
    sys.exit(main())
