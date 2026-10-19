#!/usr/bin/env python3
"""Check an SD theme and preview it on the emulated cart (`just theme-check`),
or start a new one (`just theme-new`). docs/themes.md is the guide.

usage: theme_check.py stage DIR
       theme_check.py run STAGE
       theme_check.py new NAME [dark|light]
       theme_check.py build

stage (on the host): copies the theme folder DIR to dist/theme-check/,
where the container sees it, and prints that folder (the STAGE).

run (in the torii-emu container): checks the staged theme with the kernel's
own parser (tools/themecheck.c, built from source/ui/theme_ini.c), printing
the lines the kernel would log when loading it from /torii/themes/FOLDER,
decodes its wallpaper, then boots the emulated cart with it and writes
preview PNGs: the test kernel's UI demo (list, dialog, grid) and the cheat
viewer, the screens drawn in themes today. Prints their paths; exits 1 if
the kernel would refuse the theme or a preview fails.

new (in the container): writes themes/NAME/theme.ini with every key at the
dark or light theme's value, and checks it.

build (in the container): builds dist/host/themecheck and prints its path.
"""

import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))

STAGES = Path("dist/theme-check")
CLI = Path("dist/host/themecheck")
# Built as tests/host.sh builds the host tests: char unsigned as on ARM,
# under the sanitizers, so a theme.ini that trips the parser fails loudly
CLI_SOURCES = [
    "tools/themecheck.c",
    "source/ui/ui_theme.c",
    "source/engine/font_body.c",
    "source/engine/font_heading.c",
    "source/engine/asset.c",
    "source/engine/budget.c",
    "source/engine/res.c",
    "source/engine/pool.c",
    "source/engine/covercache.c",
    "source/engine/motion.c",
    "source/engine/ease_lut.c",
]
CFLAGS = [
    "-std=gnu11",
    "-g",
    "-O1",
    "-fno-omit-frame-pointer",
    "-funsigned-char",
    "-fsanitize=address,undefined",
    "-fno-sanitize-recover=all",
    "-Wall",
    "-Wextra",
    "-Wshadow",
    "-Wundef",
    "-Wformat=2",
    "-Wstrict-prototypes",
    "-Wmissing-prototypes",
    "-Werror",
    "-I",
    "tests/host",
]

# The folder the previews put the theme in: the test kernel's UI demo loads
# "example" (tests/kernel/run.c), so the real kernel gets the same
PREVIEW_FOLDER = "example"
# Previews: name -> mgba-shot options ({theme} is the staged folder)
PREVIEWS = {
    "demo": """\
#! kernel test
#! sd-theme example {theme}
# The test kernel's UI demo in the theme (as tests/shots/ui-sd-theme.opts)
-k 1:L+SELECT+R
-w "screen ready: ui-demo"
-k @2:-
-s @4:list.png
-k @10:A -k @14:-
-w "screen ready: ui-dialog"
-s @30:dialog.png
-k @40:B -k @44:-
-k @60:SELECT -k @64:-
-w "screen ready: ui-grid"
-s @10:grid.png
""",
    "cheats": """\
#! settings 4=1 28=2
#! sd-theme example {theme}
#! theme-choice example
# The cheat viewer (as tests/shots/cheats.opts), with THEME set to the SD
# theme; the card's torii folder is one more row above Hello.gba
-w "screen ready: sd-list"
-k @8:DOWN -k @12:- -k @16:DOWN -k @20:- -k @24:DOWN -k @28:- -k @32:DOWN -k @36:-
-k @40:DOWN -k @44:-
-k @52:A -k @56:-
-w "screen ready: file-menu"
-k @8:DOWN -k @12:- -k @16:DOWN -k @20:- -k @24:DOWN -k @28:- -k @32:DOWN -k @36:- -k @40:DOWN -k @44:-
-k @52:A -k @56:-
-w "screen ready: cheats"
-s @20:screen.png
""",
}


def build() -> Path:
    CLI.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["cc", *CFLAGS, "-o", str(CLI), *CLI_SOURCES], check=True)
    return CLI


def cli(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(CLI), *args], capture_output=True, text=True, check=False
    )


def stage(src: Path) -> Path:
    """Copies the theme folder src to dist/theme-check/ and returns where."""
    if not src.is_dir():
        sys.exit(f"theme-check: {src} isn't a folder")
    if not (src / "theme.ini").is_file() and not any(
        p.name.lower() == "theme.ini" for p in src.iterdir()
    ):
        sys.exit(f"theme-check: {src} has no theme.ini")
    folder = src.resolve().name
    out = STAGES / (re.sub(r"[^A-Za-z0-9._-]", "_", folder) or "theme")
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    shutil.copytree(src, out / "theme", ignore=shutil.ignore_patterns(".*"))
    (out / "folder.txt").write_text(folder + "\n", encoding="utf-8")
    return out


def check_wallpaper(path: Path) -> list[str]:
    """Decodes the wallpaper as the kernel does when it uploads it."""
    import asset

    try:
        (_, w, h, colours, _), _, _ = asset.read_asset(path.read_bytes())
    except (ValueError, IndexError) as e:
        return [f"FAIL: {path.name}: its tiles don't decode: {e}"]
    line = (
        f"theme-check: {path.name}: {w * 8}x{h * 8} px, {w * h} of "
        f"{asset.WALLPAPER_TILES} tiles, {max(colours - 1, 0)} colours, "
        f"{path.stat().st_size} of {asset.FILE_MAX} bytes; its tiles decode"
    )
    return [line]


def preview(stage_dir: Path, name: str, opts_text: str) -> tuple[list[Path], list[str]]:
    """Runs one preview; returns its PNGs and the log lines worth showing."""
    import shots

    out = stage_dir / name
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    opts = out / "preview.opts"
    opts.write_text(opts_text.format(theme=stage_dir / "theme"), encoding="utf-8")
    proc = shots.boot(name, stage_dir / "sd.img", out / "cart", out, opts=opts)
    (out / "log.txt").write_text(proc.stderr + proc.stdout, encoding="utf-8")
    pngs = [
        Path(m[1])
        for m in re.finditer(r"^frame \d+ -> (\S+)", proc.stdout, re.MULTILINE)
    ]
    notes = [
        f"{name}: {line}"
        for line in proc.stderr.splitlines()
        if line.startswith(("kernel error:", "kernel: theme:", "crash at", "timed out"))
        or "fell back" in line
    ]
    if proc.returncode or not pngs:
        notes.append(
            f"FAIL: {name}: mgba-shot exited {proc.returncode} (log: {out / 'log.txt'})"
        )
    return pngs, notes


def run(stage_dir: Path) -> int:
    import shots

    build()
    folder = (stage_dir / "folder.txt").read_text(encoding="utf-8").strip()
    theme = stage_dir / "theme"
    result = cli("check", str(theme), folder)
    print(f"Loading {folder} as the kernel would:")
    lines = result.stdout.splitlines()
    for line in lines:
        if not line.startswith("theme-check:"):
            print(f"  {line}")
    if result.stderr.strip():
        print(result.stderr.rstrip())
    if result.returncode:
        print(f"FAIL: the kernel would refuse {folder} (see above)")
        return 1
    status = 0
    for line in lines:
        if line.startswith("theme-check: wallpaper "):
            for note in check_wallpaper(Path(line.split(" ", 2)[2])):
                print(note)
                status |= note.startswith("FAIL")
    loaded = next((x for x in lines if x.startswith("kernel: theme: loaded ")), "")
    warnings = re.search(r", (\d+) warnings(?:, a wallpaper)?\)$", loaded)
    shots.build_sd(stage_dir / "sd.img")
    with ThreadPoolExecutor(max_workers=2) as pool:
        results = list(
            pool.map(lambda kv: preview(stage_dir, *kv), sorted(PREVIEWS.items()))
        )
    print("Previews on the emulated cart:")
    for pngs, notes in results:
        for png in pngs:
            print(f"  {png}")
        for note in notes:
            print(f"  {note}" if not note.startswith("FAIL") else note)
            status |= note.startswith("FAIL")
    count = int(warnings[1]) if warnings else 0
    verdict = "FAIL" if status else "PASS"
    print(f"{verdict}: theme {folder}: loads, {count} warnings")
    return status


def new(name: str, base: str) -> int:
    build()
    if base not in ("dark", "light"):
        sys.exit(f"theme-new: the base is dark or light, not {base}")
    if cli("name", name).returncode:
        sys.exit(
            f'theme-new: "{name}" can\'t be a theme folder: 1-23 letters, digits, '
            "spaces, '-', '_' or '.', not starting or ending with '.' or a space"
        )
    out = Path("themes") / name
    if out.exists():
        sys.exit(f"theme-new: {out} is there already")
    text = cli("new", name, base)
    out.mkdir(parents=True)
    (out / "theme.ini").write_text(text.stdout, encoding="utf-8")
    result = cli("check", str(out), name)
    sys.stdout.write(result.stdout)
    if result.returncode or "0 warnings" not in result.stdout:
        print(f"FAIL: the new {out}/theme.ini doesn't load cleanly")
        return 1
    print(
        f"wrote {out}/theme.ini from the {base} theme; `just theme-check {out}` previews it"
    )
    return 0


def main(argv: list[str]) -> int:
    if len(argv) == 2 and argv[0] == "stage":
        print(stage(Path(argv[1])))
        return 0
    if len(argv) == 2 and argv[0] == "run":
        return run(Path(argv[1]))
    if len(argv) in (2, 3) and argv[0] == "new":
        return new(argv[1], argv[2] if len(argv) == 3 else "dark")
    if argv == ["build"]:
        print(build())
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
