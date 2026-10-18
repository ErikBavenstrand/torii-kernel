#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Run the checks that the changed files need (`just check`).

usage: check.py [--full] [--staged] [--base REF] [--dry-run]

Changed files are those that differ from where the branch left REF (main by
default), committed or not, plus untracked ones, and emu/mgba when it has
edits not yet saved to emu/patches (git ignores emu/mgba). With --staged,
only the staged changes count, though the checks run on the working tree.
Each file selects tiers by the rules below, the first rule
that matches winning; the checks then run in a fixed order, all of them even
after one fails, and a summary lists what failed.

Kernel source is the default for compat: every file in source/ and
tests/kernel/ links into the one kernel binary, so a change anywhere can break
launching (EWRAM data, IWRAM code and the stack share tight limits, which
`just size` checks). Only the files in NO_COMPAT skip compat. A path no rule
knows runs everything.

--full is the before-merge tier: compat becomes `compat --all`.
--dry-run prints the plan without running it.
"""

import fnmatch
import hashlib
import os
import subprocess
import sys
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
# engine's); nor does lang.c, whose strings it uses.
NO_COMPAT = {
    "source/charset.c",
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
    "source/helpwindow.c",
    "source/ui/ring.h",
    "source/ui/ui.h",
    "source/ui/ui_core.c",
    "source/ui/ui_list.c",
    "source/ui/ui_priv.h",
    "source/ui/ui_ring.c",
    "source/ui/ui_run.c",
    "source/ui/ui_theme.c",
    "source/ui/ui_widgets.c",
}

# (patterns, tiers, why): fnmatch patterns, where "*" also matches "/"
RULES = [
    (["*.md", "docs/*", "LICENSE", "*.pdf"], {LINT}, "documentation"),
    ([".github/*"], {LINT}, "workflow, checked by actionlint"),
    (
        [".editorconfig", ".gitattributes", ".gitignore", ".git-blame-ignore-revs"],
        {LINT},
        "repository settings",
    ),
    ([".clang-format", ".clang-format-ignore", ".clangd"], {LINT}, "C formatting"),
    (["ruff.toml", "tools/text_check.py", "tools/check.py"], {LINT}, "lint tooling"),
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
        ["emusrc/*", "*.bat", "Link_kernel_image.exe", "ezkernelnew.bin"],
        {LINT},
        "EZ-Flash's files, not built",
    ),
    (
        ["tests/host/gba_base.h"],
        {LINT, TEST_HOST, ANALYZE},
        "host tests, and cppcheck reads it",
    ),
    (["tests/host/*", "tests/host.sh"], {LINT, TEST_HOST}, "host tests"),
    (
        ["tools/font.py", "tests/font_test.py", "assets/fonts/*"],
        {LINT, TEST_HOST},
        "font converter, tested by test-host",
    ),
    (["tools/font_preview.py"], {LINT}, "font preview sheets"),
    (
        ["tools/ease_lut.py"],
        {LINT, TEST_HOST},
        "easing table generator, whose table the host tests check",
    ),
    (
        sorted(NO_COMPAT),
        {LINT, TEST, SIZE, ANALYZE},
        "menu or text only, listed in NO_COMPAT",
    ),
    (["source/*.c", "source/*.h"], EVERYTHING, "kernel C"),
    (["Makefile"], EVERYTHING | {REPRO}, "kernel build flags"),
    (["source/*", "tests/kernel/*", "image.bin"], KERNEL, "kernel"),
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


def mgba_edited() -> bool:
    """emu/mgba differs from the patches it was made or saved from (as emu/fetch.sh checks)."""
    if not Path("emu/mgba").is_dir():
        return False
    subprocess.run(["git", "-C", "emu/mgba", "add", "-A"], check=True)
    diff = subprocess.run(
        ["git", "-C", "emu/mgba", "diff", "--cached"], capture_output=True, check=True
    ).stdout
    stamp = Path("emu/.mgba-patches")
    recorded = stamp.read_text().strip() if stamp.exists() else ""
    return hashlib.sha256(diff).hexdigest() != recorded


def changed_files(base: str, staged: bool) -> tuple[list[str], str]:
    unsaved = ["emu/mgba"] if mgba_edited() else []
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


def main() -> int:
    args = sys.argv[1:]
    full, staged, dry_run = "--full" in args, "--staged" in args, "--dry-run" in args
    base = "main"
    if "--base" in args:
        if args.index("--base") + 1 == len(args):
            sys.exit(__doc__)
        base = args[args.index("--base") + 1]
    unknown = [
        a
        for i, a in enumerate(args)
        if a not in ("--full", "--staged", "--dry-run", "--base")
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

    failed = []
    for cmd in plan:
        name = " ".join(cmd[1:])
        print(f"\ncheck: just {name}", flush=True)
        if subprocess.run(cmd, check=False).returncode != 0:
            failed.append(name)
    print()
    if failed:
        print(f"check: FAILED: {', '.join(failed)}")
        return 1
    print(f"check: passed: {', '.join(' '.join(cmd[1:]) for cmd in plan)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
