#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# ///
"""Run the checks that the changed files need (`just check`).

usage: check.py [--full] [--staged] [--base REF] [--no-cache] [--dry-run]

Changed files are those that differ from where the branch left REF (main by
default), committed or not, plus untracked ones, and emu/mgba when it has
edits not yet saved to emu/patches (git ignores emu/mgba). With --staged,
only the staged changes count, though the checks run on the working tree.
Each file selects tiers by the rules below, the first rule
that matches winning. The tiers then run side by side, all of them even
after one fails: lint at once, the rest after one `just` call builds what
they need (PREPARE), with test, compat and repro, which each fill the VM,
one after another (VM_HEAVY). Each tier's output goes to dist/.check/TIER.log and is
printed whole when the tier ends; a summary lists what failed.

A tier that passed before with the same inputs is skipped: the hash of every
file whose rules select it (PassCache), recorded in dist/.check/passed.json
when it passes. Re-running `just check` after committing, or after a change
to docs only, then runs nothing again. --no-cache runs every tier.

Kernel source is the default for compat: every file in source/ and
tests/kernel/ links into the one kernel binary, so a change anywhere can break
launching (EWRAM data, IWRAM code and the stack share tight limits, which
`just size` checks). Only the files in NO_COMPAT skip compat. A path no rule
knows runs everything.

`just test` runs only the jobs of tests/all.sh that the change touches
(TestJobs): every run records the kernel functions each job executes, and a
job runs again only when one of them, its own script or the files every job
reads (the harness) changed since it last passed, or when it never passed.
tools/testmap.py explains the fingerprints that say a function changed.

--full is the before-merge tier: compat becomes `compat --all`, and test
runs every job (a change can also break code it didn't touch, by moving
data).
--dry-run prints the plan without running it.
"""

import fnmatch
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

LINT, TEST_HOST, TEST, SIZE, REPRO, ANALYZE, COMPAT = (
    "lint",
    "test-host",
    "test",
    "size",
    "repro",
    "analyze",
    "compat",
)
ORDER = [LINT, TEST_HOST, TEST, SIZE, REPRO, ANALYZE, COMPAT]
KERNEL = {LINT, TEST, SIZE, COMPAT}
EVERYTHING = {LINT, TEST, SIZE, ANALYZE, COMPAT}

# Kernel C files that only draw menus or text and never run while a game
# launches or saves: they skip compat (not size). Other headers never skip
# it, as launching code includes them (only menu code includes the text
# engine's); nor does lang.c, whose strings it uses. The menu pages a player
# opens (games, SD card, NOR, cheats, saves, settings) are here too: compat
# launches through the tests' hook in screens/nav.c, straight into
# actions.c, so their code never runs there; `just test` drives them.
# Pages a launch itself can show (slot picker, slot flow, save page) and
# headers nav.c or actions.c include are not.
NO_COMPAT = {
    "source/artkey.c",
    "source/artkey.h",
    "source/artpack.c",
    "source/artpack.h",
    "source/boxart.c",
    "source/boxart.h",
    "source/charset.c",
    "source/diag_render.c",
    "source/engine/affine.c",
    "source/engine/affine.h",
    "source/engine/ease_lut.c",
    "source/engine/font.h",
    "source/engine/font_body.c",
    "source/engine/font_heading.c",
    "source/engine/input.c",
    "source/engine/input_hw.c",
    "source/engine/motion.c",
    "source/engine/text.c",
    "source/engine/text.h",
    "source/engine/text_blit.s",
    "source/engine/text_layout.c",
    "source/gamelist.c",
    "source/gamelist.h",
    "source/gamename.c",
    "source/gamename.h",
    "source/keypad_edit.c",
    "source/keypad_edit.h",
    "source/rename_edit.c",
    "source/rename_edit.h",
    "source/screens/carousel.c",
    "source/screens/carousel_ring.c",
    "source/screens/cheats.c",
    "source/screens/cheats.h",
    "source/screens/game_saves.c",
    "source/screens/game_saves.h",
    "source/screens/games.c",
    "source/screens/home.c",
    "source/screens/info.c",
    "source/screens/info.h",
    "source/screens/keypad.c",
    "source/screens/keypad.h",
    "source/screens/nor.c",
    "source/screens/registry.c",
    "source/screens/rename.c",
    "source/screens/rename.h",
    "source/screens/saves.c",
    "source/screens/saves.h",
    "source/screens/sdcard.c",
    "source/screens/settings_layout.c",
    "source/screens/settings_layout.h",
    "source/screens/settings_page.c",
    "source/screens/slot_list.c",
    "source/screens/slot_list.h",
    "source/sdlist.c",
    "source/sdlist.h",
    "source/ui/art.c",
    "source/ui/art.h",
    "source/ui/art_data.c",
    "source/ui/art_ids.h",
    "source/ui/letter.c",
    "source/ui/letter.h",
    "source/ui/paint.h",
    "source/ui/palette.c",
    "source/ui/palette.h",
    "source/ui/ring.h",
    "source/ui/rowcache.c",
    "source/ui/rowcache.h",
    "source/ui/ui.h",
    "source/ui/ui_core.c",
    "source/ui/ui_list.c",
    "source/ui/ui_paper.c",
    "source/ui/ui_popup.c",
    "source/ui/ui_priv.h",
    "source/ui/ui_reading.c",
    "source/ui/ui_reading.h",
    "source/ui/ui_ring.c",
    "source/ui/ui_run.c",
    "source/ui/ui_theme.c",
    "source/ui/ui_widgets.c",
}

# (patterns, tiers, why): fnmatch patterns, where "*" also matches "/"
RULES = [
    (
        ["docs/design/v0.4/palette.json", "tools/palette.py"],
        {LINT, TEST_HOST},
        "palette source, whose generated C test-host checks",
    ),
    (["*.md", "docs/*", "LICENSE", "*.pdf"], {LINT}, "documentation"),
    ([".github/*"], {LINT}, "workflow, checked by actionlint"),
    (
        [".editorconfig", ".gitattributes", ".gitignore", ".git-blame-ignore-revs"],
        {LINT},
        "repository settings",
    ),
    ([".clang-format", ".clang-format-ignore", ".clangd"], {LINT}, "C formatting"),
    (
        ["ruff.toml", "tools/text_check.py", "tools/check.py", "tools/testmap.py"],
        {LINT},
        "lint and check tooling",
    ),
    (
        ["tools/release_notes.sh", "tools/logs.py", "tools/stack.py", "tools/size.sh"],
        {LINT},
        "tool no test or game runs",
    ),
    (
        [
            ".clang-tidy",
            "docker/analyze.Dockerfile",
            "tools/analyze*",
            "tools/compile_commands.py",
        ],
        {LINT, ANALYZE},
        "static analysis",
    ),
    (["roms/games.example.toml"], {LINT}, "example config"),
    (
        ["*.bat", "Link_kernel_image.exe", "ezkernelnew.bin"],
        {LINT},
        "EZ-Flash's files, not built",
    ),
    (
        ["tests/host/gba_base.h"],
        {LINT, TEST_HOST, ANALYZE},
        "host tests, and cppcheck reads it",
    ),
    (
        ["tests/host/*", "tests/host.sh", "tools/hostcc.py"],
        {LINT, TEST_HOST},
        "host tests",
    ),
    (
        ["tools/font.py", "tests/font_test.py", "assets/fonts/*"],
        {LINT, TEST_HOST},
        "font converter, tested by test-host",
    ),
    (["tools/font_preview.py"], {LINT}, "font preview sheets"),
    (["tools/mockup/*"], {LINT}, "design mockups (docs/design/v0.4)"),
    (
        ["assets/ui/*"],
        {LINT, TEST_HOST, TEST},
        "menu art: test-host checks source/ui/art_data.c is current, the art-sheet shot shows it",
    ),
    (
        ["tests/asset_test.py"],
        {LINT, TEST_HOST},
        "asset converter tests, run by test-host",
    ),
    (
        ["tools/asset.py"],
        {LINT, TEST_HOST, TEST},
        "asset converter: test-host checks its test assets, the kernel tests read them",
    ),
    (
        ["tools/art.py", "tests/art_test.py", "tests/art/*"],
        {LINT, TEST_HOST, TEST},
        "box art converter: test-host runs its tests, the games-art shots show its covers",
    ),
    (
        ["tools/ease_lut.py"],
        {LINT, TEST_HOST},
        "easing table generator, whose table the host tests check",
    ),
    (
        ["tools/names_db.py", "tests/names_db_test.py", "tests/names/*"],
        {LINT, TEST_HOST, TEST},
        "names database tool: test-host runs its tests, tests/names_db.sh reads its files on the cart",
    ),
    (
        ["tools/art_scrape.py", "tests/art_scrape_test.py"],
        {LINT, TEST_HOST},
        "box art scraper: test-host runs its tests against a local server",
    ),
    (
        [
            "tools/cheat_oracle.c",
            "tools/cheat_corpus.c",
            "tools/cheat_diff.py",
            "tools/cheat_gen.py",
            "tools/cheat_sample.py",
            "tools/cheat_corpus_fetch.sh",
            "tools/mgba_cheats.sh",
            "tools/mgba_cheats/*",
            "tests/cheat_corpus.*",
            "tests/cheat_expected.toml",
            "tests/cheat_fuzz*",
            "tests/cheat_engine.*",
            "tests/cheats/sample/*",
        ],
        {LINT, TEST_HOST, TEST},
        (
            "cheat validation suite: test-host decodes tests/cheats/sample with Torii's and mGBA's decoders, "
            "tests/cheat_engine.sh runs the type matrix and the saved fuzz cases in both engines"
        ),
    ),
    (
        [
            "tools/cheats.py",
            "tools/cheats_harvest.js",
            "tools/cheat_patches.c",
            "tests/cheats_test.py",
            "tests/cheats/*",
        ],
        {LINT, TEST_HOST},
        "cheat file writer, its browser harvester and patch lister: test-host runs its tests on a fixture card",
    ),
    (
        ["tools/cheat_encode.py", "tests/cheat_encode_test.py"],
        {LINT, TEST_HOST, TEST},
        "cheat encoders: test-host runs their tests, tests/cheat_engine.sh makes its cases with them",
    ),
    (
        ["tests/cheat_codes.*"],
        {LINT, COMPAT},
        "real cheat codes in real games, run by `just compat`",
    ),
    (
        ["tools/art_pack.py", "tests/art_pack_test.py"],
        {LINT, TEST_HOST},
        "art pack builder: test-host runs its tests against a local server",
    ),
    (
        ["tools/library_report.py", "tests/library_report_test.py"],
        {LINT, TEST_HOST},
        "library report: test-host runs its tests on a fixture card",
    ),
    (
        ["tools/card.py", "tests/card_test.py"],
        {LINT, TEST_HOST},
        "one-step card setup: test-host runs its tests on a fixture card",
    ),
    (
        ["tools/artpack.py"],
        {LINT, TEST_HOST, TEST},
        "art pack format: test-host checks it, tests/boxart.sh writes packs with it",
    ),
    (
        ["tests/library_idx.py"],
        {LINT, TEST_HOST, TEST},
        "library index reader: the library tests and art-scrape's tests use it",
    ),
    (
        sorted(NO_COMPAT),
        {LINT, TEST, SIZE, ANALYZE},
        "menu or text only, listed in NO_COMPAT",
    ),
    (["source/*.c", "source/*.h"], EVERYTHING, "kernel C"),
    (["Makefile"], EVERYTHING | {REPRO}, "kernel build flags"),
    (["source/*", "tests/kernel/*", "image.bin"], KERNEL, "kernel"),
    (["emulators/*"], KERNEL | {REPRO}, "built-in emulators, in every kernel"),
    (
        ["emu/*", "docker/emu.Dockerfile"],
        {LINT, TEST, COMPAT},
        "emulated cart or its build",
    ),
    (
        [
            "tests/games*",
            "tests/compat_report.py",
            "tests/hook_watch.lua",
            "tools/games.py",
        ],
        {LINT, COMPAT},
        "compat runner",
    ),
    (
        [
            "tools/omega_image.py",
            "tools/nm_to_sym.py",
            "tools/build_id.py",
            "tools/repro.sh",
            "tools/make_cart.sh",
            "tools/cart_copy.sh",
            "tools/sdimg.*",
            "tools/fresh.py",
        ],
        {LINT, TEST, REPRO, COMPAT},
        "builds the kernel image or the cart",
    ),
    (
        ["tests/shots*", "tests/perf*"],
        {LINT, TEST},
        "golden screens or cycle budget, checked by `just test`",
    ),
    (["tests/*"], {LINT, TEST}, "emulated-cart tests"),
    (["justfile"], EVERYTHING | {REPRO}, "recipes"),
]


def tiers_for(path: str) -> tuple[set, str]:
    for patterns, tiers, why in RULES:
        if any(fnmatch.fnmatchcase(path, p) for p in patterns):
            return tiers, why
    return EVERYTHING, "no rule for this path: everything"


def git(*args: str, cwd: str = ".") -> list[str]:
    out = subprocess.run(
        ["git", "-c", "core.quotePath=false", *args],
        capture_output=True,
        text=True,
        check=True,
        cwd=cwd,
    )
    return [line for line in out.stdout.splitlines() if line]


def checkout_edited(checkout: str, stamp: str, binary: bool = False) -> bool:
    """A checkout differs from the patches it was made or saved from (as emu/fetch.sh and emulators/fetch.sh check)."""
    if not Path(checkout).is_dir():
        return False
    subprocess.run(["git", "-C", checkout, "add", "-A"], check=True)
    diff = subprocess.run(
        ["git", "-C", checkout, "diff", "--cached"] + (["--binary"] if binary else []),
        capture_output=True,
        check=True,
    ).stdout
    recorded = Path(stamp).read_text().strip() if Path(stamp).exists() else ""
    return hashlib.sha256(diff).hexdigest() != recorded


def changed_files(base: str, staged: bool) -> tuple[list[str], str]:
    checkouts = [("emu/mgba", "emu/.mgba-patches", False)] + [
        (f"emulators/{name}", f"emulators/.{name}-patch", True)
        for name in ("jagoombacolor", "pocketnes")
    ]
    unsaved = [
        path
        for path, stamp, binary in checkouts
        if checkout_edited(path, stamp, binary)
    ]
    if staged:
        files = git("diff", "--cached", "--name-only", "--no-renames")
        return sorted(set(files + unsaved)), "staged"
    for ref in (base, f"origin/{base}"):
        found = subprocess.run(
            ["git", "rev-parse", "--verify", "-q", ref],
            capture_output=True,
            check=False,
        )
        if found.returncode == 0:
            fork = git("merge-base", ref, "HEAD")[0]
            break
    else:
        sys.exit(f"check: no branch {base} or origin/{base} to compare with")
    if fork == git("rev-parse", "HEAD")[0]:
        print(
            f"check: HEAD is {ref} itself, so only uncommitted changes count; "
            "use --base to compare with an older commit"
        )
    files = git("diff", "--name-only", "--no-renames", fork)
    files += git("ls-files", "--others", "--exclude-standard")
    return sorted(set(files + unsaved)), f"against {ref}, committed or not"


# What each tier's recipe builds first. `just check` builds them all in one
# `just` call before the tiers start, so tiers running side by side never
# build the same thing at once; their own dependencies are then no-ops.
PREPARE = {
    TEST_HOST: ["emu-image"],
    TEST: ["build", "emu-headless", "test-roms", "_test-kernel-build"],
    SIZE: ["build", "_test-kernel-build"],
    REPRO: ["build"],
    ANALYZE: ["build", "compile-commands", "_analyze-image"],
    COMPAT: ["build", "emu-headless"],
}
# Tiers that need no container and none of PREPARE: they start at once,
# alongside the preparation.
HOST_ONLY = {LINT}
# Tiers that fill the VM's CPUs (and memory) by themselves: they run one
# after another, beside the others. Run at once, test and compat each took
# about 2.5 times as long as alone (about 160 emulators on 8 CPUs).
VM_HEAVY = [TEST, COMPAT, REPRO]


def tree_state() -> dict[str, str]:
    """Every file git knows or would add, with a hash of its content.

    Index blobs for files the work tree hasn't changed; the rest are hashed from
    disk by git hash-object. Edits in emu/mgba and the emulators' checkouts not
    yet saved to their patches count as files of their own (their diff's hash),
    and so does the list of roms/ (names, sizes, times) and roms/games.toml."""
    state = {}
    for line in git("ls-files", "-s"):
        meta, path = line.split("\t", 1)
        state[path] = meta.split()[1]
    dirty = git("diff", "--name-only", "--no-renames") + git(
        "ls-files", "--others", "--exclude-standard"
    )
    present = [f for f in dirty if Path(f).is_file()]
    for path in set(dirty) - set(present):
        state.pop(path, None)
    if present:
        hashes = subprocess.run(
            ["git", "hash-object", "--stdin-paths"],
            input="\n".join(present),
            capture_output=True,
            text=True,
            check=True,
        ).stdout.split()
        state.update(zip(present, hashes, strict=True))
    for checkout, binary in [
        ("emu/mgba", False),
        ("emulators/jagoombacolor", True),
        ("emulators/pocketnes", True),
    ]:
        if Path(checkout).is_dir():
            diff = subprocess.run(
                ["git", "-C", checkout, "diff", "--cached"]
                + (["--binary"] if binary else []),
                capture_output=True,
                check=True,
            ).stdout
            state[checkout + "/"] = hashlib.sha256(diff).hexdigest()
    roms = hashlib.sha256()
    for rom in sorted(Path("roms").glob("**/*")):
        if rom.is_file():
            info = rom.stat()
            roms.update(f"{rom} {info.st_size} {info.st_mtime_ns}\n".encode())
    state["roms/"] = roms.hexdigest()
    return state


class PassCache:
    """The input hashes of tiers that passed: a tier whose inputs hash the same
    as in a passing run is skipped.

    A tier's inputs are the files whose rules select it (the rules that decide
    whether a change needs the tier at all), with test also taking test-host's,
    and repro and anything else every file. compat also reads roms/. The hash
    covers the command too, so `compat` and `compat --all` pass separately."""

    KEEP = 64

    def __init__(self, path: Path):
        self.path = path
        try:
            self.hashes = json.loads(path.read_text())
        except (OSError, ValueError):
            self.hashes = []

    def key(self, cmd: list[str], state: dict[str, str]) -> str:
        tier = cmd[1]
        wanted = {tier, TEST_HOST} if tier == TEST else {tier}
        digest = hashlib.sha256(" ".join(cmd[1:]).encode() + b"\0")
        for path in sorted(state):
            if path == "roms/" and tier != COMPAT:
                continue
            tiers = tiers_for(path.rstrip("/"))[0]
            if tier in (LINT, TEST, SIZE, ANALYZE, COMPAT, TEST_HOST) and not (
                tiers & wanted
            ):
                continue
            digest.update(f"{path}\0{state[path]}\n".encode())
        return digest.hexdigest()

    def passed(self, key: str) -> bool:
        return key in self.hashes

    def add(self, key: str) -> None:
        if key not in self.hashes:
            self.hashes.append(key)
        self.hashes = self.hashes[-self.KEEP :]

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(self.hashes))


# Files that reach the test jobs only through the kernel's code and data,
# which the jobs' recorded functions cover
THROUGH_KERNEL = ["source/*", "tests/kernel/*", "assets/ui/*"]


class TestJobs:
    """Which jobs `just test` runs, recording each job's kernel functions as
    it goes (read by tools/testmap.py into dist/.check/testmap.json).

    The jobs are tests/all.sh's (TORII_JOBS, TORII_COVERAGE_DIR) and, apart,
    each of tests/shots.py's scenarios, as shots:NAME (TORII_SHOTS,
    TORII_SHOTS_COVERAGE)."""

    MAP = "dist/.check/testmap.json"
    COVERAGE = "dist/.check/coverage"
    HARNESS = "dist/.check/harness.json"
    SHOTS_JOB = "python3 tests/shots.py"
    SHOTS_FILES = ("tests/shots.py", "tests/perf.py", "tests/perf/*")
    # Every scenario's expected screens: each counts its own lines (harness)
    SHOTS_SHARED = ("tests/shots/hashes.txt", "tests/shots/png/*")

    def __init__(self, full: bool):
        self.full = full

    @classmethod
    def own_files(cls, job: str) -> list[str]:
        """The files of a job's own: its script, and files named after it."""
        if job.startswith("shots:"):
            return [*cls.SHOTS_FILES, f"tests/shots/{job[6:]}.opts"]
        script = next(word for word in job.split() if word.startswith("tests/"))
        stem = Path(script).stem
        return [script, f"tests/{stem}.*", f"tests/{stem}/*"]

    def harness(self, jobs: list[str]) -> dict[str, str]:
        """Per job, a hash of the files it reads other than through the
        kernel: its own, and those every job reads."""
        state = tree_state()
        common = hashlib.sha256()
        own = {job: hashlib.sha256() for job in jobs}
        for path in sorted(state):
            name = path.rstrip("/")
            if name == "roms" or TEST not in tiers_for(name)[0]:
                continue
            if any(
                fnmatch.fnmatchcase(name, p)
                for p in [*THROUGH_KERNEL, *self.SHOTS_SHARED]
            ):
                continue
            entry = f"{path}\0{state[path]}\n".encode()
            owners = [
                job
                for job in jobs
                if any(fnmatch.fnmatchcase(name, p) for p in self.own_files(job))
            ]
            for job in owners:
                own[job].update(entry)
            if not owners:
                common.update(entry)
        expected = Path("tests/shots/hashes.txt").read_text().splitlines()
        for job in jobs:
            if job.startswith("shots:"):
                own[job].update(
                    "\n".join(
                        line for line in expected if line.startswith(job[6:] + "/")
                    ).encode()
                )
        return {
            job: hashlib.sha256(common.digest() + own[job].digest()).hexdigest()
            for job in jobs
        }

    def env(self) -> dict[str, str]:
        """Pick the jobs, once the kernels are built (their objects give the fingerprints)."""
        jobs = subprocess.run(
            ["bash", "tests/all.sh", "--list"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.splitlines()
        if self.SHOTS_JOB in jobs:
            jobs.remove(self.SHOTS_JOB)
            jobs += [
                f"shots:{p.stem}" for p in sorted(Path("tests/shots").glob("*.opts"))
            ]
        Path(self.HARNESS).parent.mkdir(parents=True, exist_ok=True)
        Path(self.HARNESS).write_text(json.dumps(self.harness(jobs)))
        if self.full:
            chosen, why = jobs, ""
        else:
            picked = subprocess.run(
                [sys.executable, "tools/testmap.py", "select", self.MAP, self.HARNESS],
                input="\n".join(jobs),
                capture_output=True,
                text=True,
                check=True,
            )
            chosen, why = picked.stdout.splitlines(), picked.stderr
        print(
            f"check: test runs {len(chosen)} of {len(jobs)} jobs (tests/all.sh's, and tests/shots.py's scenarios)",
            flush=True,
        )
        print(why, end="", flush=True)
        subprocess.run(["rm", "-rf", self.COVERAGE], check=True)
        shots = [job[6:] for job in chosen if job.startswith("shots:")]
        scripts = [job for job in chosen if not job.startswith("shots:")]
        if shots:
            scripts.append(self.SHOTS_JOB)
        return {
            "TORII_JOBS": "\n".join(scripts) or "-",  # "-": none
            "TORII_COVERAGE_DIR": self.COVERAGE,
            "TORII_SHOTS": "" if self.full else " ".join(shots),
            "TORII_SHOTS_COVERAGE": f"{self.COVERAGE}/shots",
        }

    def record(self) -> None:
        if Path(self.COVERAGE).is_dir():
            subprocess.run(
                [
                    sys.executable,
                    "tools/testmap.py",
                    "record",
                    self.MAP,
                    self.COVERAGE,
                    self.HARNESS,
                ],
                check=False,
            )


def run(plan: list[list[str]], just: str, test_jobs: TestJobs) -> dict[str, bool]:
    """Run the plan's tiers side by side, each into its own log in
    dist/.check, and print each log whole as its tier ends.

    Host-only tiers start at once; the others after one `just` call builds
    what they all need (PREPARE), the VM_HEAVY ones one at a time. Returns
    each tier's name and whether it passed, in plan order."""
    logs = Path("dist/.check")
    logs.mkdir(parents=True, exist_ok=True)
    names = {id(cmd): " ".join(cmd[1:]) for cmd in plan}
    results: dict[str, bool] = {}
    procs: dict[int, tuple[list[str], subprocess.Popen, float, Path]] = {}

    def start(cmd: list[str]) -> None:
        log = logs / (names[id(cmd)].replace(" ", "_") + ".log")
        env = {**os.environ, **test_jobs.env()} if cmd[1] == TEST else None
        out = log.open("wb")
        proc = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, env=env)
        out.close()
        procs[id(cmd)] = (cmd, proc, time.monotonic(), log)

    def finish(key: int) -> None:
        cmd, proc, began, log = procs.pop(key)
        name = names[key]
        ok = proc.returncode == 0
        if cmd[1] == TEST:
            test_jobs.record()
        results[name] = ok
        print(f"\ncheck: just {name}", flush=True)
        sys.stdout.buffer.write(log.read_bytes())
        print(
            f"check: just {name}: {'passed' if ok else 'FAILED'} in {time.monotonic() - began:.0f} s",
            flush=True,
        )

    def wait_any() -> None:
        while True:
            for key, (_, proc, _, _) in list(procs.items()):
                if proc.poll() is not None:
                    finish(key)
                    return
            time.sleep(0.1)

    later = [cmd for cmd in plan if cmd[1] not in HOST_ONLY]
    for cmd in plan:
        if cmd[1] in HOST_ONLY:
            start(cmd)
    recipes = []
    for cmd in later:
        for recipe in PREPARE.get(cmd[1], []):
            if recipe not in recipes:
                recipes.append(recipe)
    prepared = True
    if recipes:
        began = time.monotonic()
        log = logs / "prepare.log"
        with log.open("wb") as out:
            prepared = (
                subprocess.run(
                    [just, *recipes],
                    stdout=out,
                    stderr=subprocess.STDOUT,
                    check=False,
                ).returncode
                == 0
            )
        print(
            f"check: built what the tiers need ({' '.join(recipes)}) in {time.monotonic() - began:.0f} s",
            flush=True,
        )
        if not prepared:
            sys.stdout.buffer.write(log.read_bytes())
            print("check: the build failed, so the tiers that need it didn't run")
            for cmd in later:
                results[names[id(cmd)]] = False
            later = []
    lane = [cmd for cmd in later if cmd[1] in VM_HEAVY]
    lane.sort(key=lambda cmd: VM_HEAVY.index(cmd[1]))
    for cmd in later:
        if cmd not in lane:
            start(cmd)
    try:
        while procs or lane:
            if lane and not any(c[1] in VM_HEAVY for c, *_ in procs.values()):
                start(lane.pop(0))
            wait_any()
    except KeyboardInterrupt:
        for _, proc, _, _ in procs.values():
            proc.terminate()
        raise
    return {names[id(cmd)]: results[names[id(cmd)]] for cmd in plan}


def main() -> int:
    args = sys.argv[1:]
    full, staged, dry_run = "--full" in args, "--staged" in args, "--dry-run" in args
    use_cache = "--no-cache" not in args
    base = "main"
    if "--base" in args:
        if args.index("--base") + 1 == len(args):
            sys.exit(__doc__)
        base = args[args.index("--base") + 1]
    unknown = [
        a
        for i, a in enumerate(args)
        if a not in ("--full", "--staged", "--dry-run", "--no-cache", "--base")
        and not (i > 0 and args[i - 1] == "--base")
    ]
    if unknown:
        sys.exit(__doc__)

    files, what = changed_files(base, staged)
    if not files:
        print(f"check: no changed files ({what}), nothing to run")
        return 0
    selected = set()
    print(f"check: {len(files)} changed file(s) ({what})")
    width = min(max(len(f) for f in files), 48)
    for path in files:
        tiers, why = tiers_for(path)
        selected |= tiers
        print(
            f"  {path:<{width}}  {', '.join(t for t in ORDER if t in tiers)}  ({why})"
        )
    if TEST in selected:
        selected.discard(TEST_HOST)  # `just test` runs the host tests first

    just = os.environ.get("JUST", "just")
    plan = []
    for tier in ORDER:
        if tier not in selected:
            continue
        if tier == COMPAT and full:
            plan.append([just, "compat", "--all"])
        else:
            plan.append([just, tier])
    if full and COMPAT not in selected:
        print("check: --full: no change needs compat, so no compat --all")
    print("check: running " + ", ".join(" ".join(cmd[1:]) for cmd in plan))
    if dry_run:
        return 0

    started = time.monotonic()
    cache = PassCache(Path("dist/.check/passed.json"))
    keys = {}
    if use_cache:
        state = tree_state()
        for cmd in plan:
            keys[cmd[1]] = cache.key(cmd, state)
        cached = [cmd for cmd in plan if cache.passed(keys[cmd[1]])]
        if cached:
            print(
                "check: passed before with the same inputs, so skipped: "
                + ", ".join(" ".join(cmd[1:]) for cmd in cached)
                + " (--no-cache runs them)"
            )
        plan = [cmd for cmd in plan if cmd not in cached]
    results = run(plan, just, TestJobs(full))
    if use_cache:
        # A file edited while the tiers ran may not be what they checked
        state = tree_state()
        for cmd in plan:
            if results[" ".join(cmd[1:])] and cache.key(cmd, state) == keys[cmd[1]]:
                cache.add(keys[cmd[1]])
        cache.save()
    failed = [name for name, ok in results.items() if not ok]
    took = f"in {time.monotonic() - started:.0f} s"
    print()
    if failed:
        print(f"check: FAILED: {', '.join(failed)} ({took})")
        return 1
    print(f"check: passed: {', '.join(results) or 'nothing left to run'} ({took})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
