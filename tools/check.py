#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Run the checks that the changed files need (`just check`).

usage: check.py [--full] [--staged] [--base REF] [--dry-run]

Changed files are those that differ from where the branch left REF (main by
default), committed or not, plus untracked ones; with --staged, only the
staged changes. Each file selects tiers by the rules below, the first rule
that matches winning; the checks then run in a fixed order, all of them even
after one fails, and a summary lists what failed.

Kernel source is the default for compat: every file in source/ and
tests/kernel/ links into the one kernel binary, so a change anywhere can break
launching (EWRAM data, IWRAM code and the stack share tight limits). Only the
files in NO_COMPAT skip it. A path no rule knows runs everything.

--full is the before-merge tier: compat becomes `compat --all`.
--dry-run prints the plan without running it.
"""

import fnmatch
import os
import subprocess
import sys

LINT, TEST_HOST, TEST, ANALYZE, COMPAT = (
    "lint",
    "test-host",
    "test",
    "analyze",
    "compat",
)
ORDER = [LINT, TEST_HOST, TEST, ANALYZE, COMPAT]
KERNEL = {LINT, TEST, COMPAT}
EVERYTHING = {LINT, TEST, ANALYZE, COMPAT}

# Kernel C files that only draw menus or text and never run while a game
# launches or saves: they skip compat. Headers never skip it, as launching
# code includes them.
NO_COMPAT = {
    "source/charset.c",
    "source/diag.c",
    "source/helpwindow.c",
    "source/lang.c",
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
    (["tests/host/*", "tests/host.sh"], {LINT, TEST_HOST}, "host tests"),
    (
        sorted(NO_COMPAT),
        {LINT, TEST, ANALYZE},
        "menu or text only, listed in NO_COMPAT",
    ),
    (["source/*.c", "source/*.h"], {LINT, TEST, ANALYZE, COMPAT}, "kernel C"),
    (["Makefile"], EVERYTHING, "kernel build flags"),
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
            "tools/make_cart.sh",
            "tools/cart_copy.sh",
            "tools/sdimg.*",
            "tools/fresh.py",
        ],
        {LINT, TEST, COMPAT},
        "builds the kernel image or the cart",
    ),
    (["tests/*"], {LINT, TEST}, "emulated-cart tests"),
    (["justfile"], EVERYTHING, "recipes"),
]


def tiers_for(path: str) -> tuple[set, str]:
    for patterns, tiers, why in RULES:
        if any(fnmatch.fnmatchcase(path, p) for p in patterns):
            return tiers, why
    return EVERYTHING, "no rule for this path: everything"


def git(*args: str) -> list[str]:
    out = subprocess.run(["git", *args], capture_output=True, text=True, check=True)
    return [line for line in out.stdout.splitlines() if line]


def changed_files(base: str, staged: bool) -> tuple[list[str], str]:
    if staged:
        return git("diff", "--cached", "--name-only", "--no-renames"), "staged"
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
    files = git("diff", "--name-only", "--no-renames", fork)
    files += git("ls-files", "--others", "--exclude-standard")
    return sorted(set(files)), f"against {ref}, committed or not"


def main() -> int:
    args = sys.argv[1:]
    full, staged, dry_run = "--full" in args, "--staged" in args, "--dry-run" in args
    base = "main"
    if "--base" in args:
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
