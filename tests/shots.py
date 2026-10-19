#!/usr/bin/env python3
"""Golden screenshots and the cycle budget, on the emulated cart.

usage: tests/shots.py [--update | --update-perf] [SCENARIO...]

Each scenario is an mgba-shot options file, tests/shots/NAME.opts, run on a
fresh cart (the SD card below, kernel NOR erased) with the clock frozen. It
waits for the kernel's "screen ready: SCREEN" lines with -w and names its key
frames with -s @N:KEY.png; -H @A:@B hashes every frame of an animation.
Lines starting "#! settings SLOT=VALUE..." set kernel settings slots first;
"#! kernel test" runs the test kernel (ezkerneltest.gba, `just test-kernel`'s
build) instead, for scenes only it has, as the cover demo. "#! sd-theme NAME"
copies themes/NAME to /torii/themes/NAME on the scenario's card ("#! sd-theme
NAME DIR" copies DIR there instead), and "#! theme-choice NAME" writes NAME to
/torii/theme.txt, the SD theme the THEME setting's value 2 picks. "#! sd-file
DEST SIZE SRC" copies SRC to DEST on the card, padded with 0xFF to SIZE
bytes: a game large enough for a progress bar to move (boot-progress-big),
or, with SIZE 0 and SRC /dev/null, an empty file in place of one (quote a
DEST with spaces).
"#! sd-fragmented DEST SIZE RUNS SRC" copies SRC, padded with zeros to SIZE
bytes, to DEST in RUNS runs of clusters (tools/sdimg.sh put-fragmented): a
game too fragmented to start (dialog-fragmented). A "kernel error:" line
fails a scenario unless an "#! expect-error TEXT" line expects it (it starts
with TEXT); an expected one that isn't logged fails it too.

Check (the default): every key frame's screen hash, and every animation's
frame hashes, must equal tests/shots/hashes.txt. On a mismatch the actual,
expected and diff PNGs go to dist/shots-failed/. Then every
"frame-cycles: NAME N" line the runs logged is checked against
tests/perf/budget.txt (tests/perf.py).

--update re-records hashes.txt and the PNGs in tests/shots/png/, and prints
what changed; --update-perf re-records tests/perf/budget.txt. Naming
scenarios runs (and updates) only those.

Runs inside the torii-emu container after `just build` and `just
emu-headless` (see `just test`, `just shots-update`, `just perf-update`).
"""

import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import zlib
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import perf
from games import png_lines

SHOTS = Path("tests/shots")
MANIFEST = SHOTS / "hashes.txt"
PNGS = SHOTS / "png"
RUNS = Path("dist/shots-run")
FAILED = Path("dist/shots-failed")
SHOT = "emu/build-linux/mgba-shot"
KERNEL = "dist/ezkernelnew.bin"
TEST_KERNEL = "ezkerneltest.gba"
# 2026-01-01 12:34:56 UTC: the clock stands still there in every scenario
EPOCH = 1767270896
LONG_NAME = (
    "B long name, past the ninety-nine bytes a list entry used to keep,"
    " which now lists, scrolls, opens and saves whole (Rev 1).gba"
)
DEEP_FILES = 300
SETTINGS = 0x7B0000  # settings sector in the kernel NOR (as tests/settings.sh)
KERNEL_NOR_SIZE = 8 << 20

HEADER = """\
# Golden screen hashes for tests/shots.py, written by `just shots-update`.
# SCENARIO/KEY HASH: the key frame tests/shots/png/SCENARIO.KEY.png shows.
# SCENARIO/frames FIRST-LAST HASH: frames of a -H range (counted from 0)
# that all hash the same.
"""


def copy_tree(img: str, src: Path, dest: str) -> None:
    """Copies src's entries (not dot files) into folder dest of img, in
    sorted order. `mcopy -s` would walk folders in the host's directory
    order, which differs between filesystems (APFS, ext4): the card's
    directory entries, and so the kernel's listing and sorting time, would
    differ between machines and shift every golden after it by frames."""
    files: list[str] = []
    for e in sorted(e for e in src.iterdir() if e.name[0] != "."):
        if not e.is_dir():
            files.append(str(e))
            continue
        if files:  # one mcopy for a run of files, in order
            run(["mcopy", "-Q", "-i", img, *files, f"{dest}/"])
            files = []
        run(["mmd", "-i", img, f"{dest}/{e.name}"])
        copy_tree(img, e, f"{dest}/{e.name}")
    if files:
        run(["mcopy", "-Q", "-i", img, *files, f"{dest}/"])


def build_sd(path: Path) -> None:
    """The SD card every scenario starts from."""
    stage = path.parent / "sd"
    shutil.rmtree(stage, ignore_errors=True)
    (stage / "CHEAT").mkdir(parents=True)
    (stage / "Games").mkdir()
    (stage / "Long").mkdir()
    (stage / "SAVER").mkdir()
    shutil.copy("tests/roms/hello/hello.gba", stage / "Hello.gba")
    shutil.copy("tests/roms/idle/idle.gba", stage / "Games" / "Idle.gba")
    # Too long for its row: the SD list scrolls it, an animation to check
    shutil.copy(
        "tests/roms/idle/idle.gba",
        stage / "Long" / "A name too long to fit on its row.gba",
    )
    # Past the 99 bytes a list entry kept before v0.3: it lists, scrolls
    # and opens whole (sd-long)
    shutil.copy("tests/roms/idle/idle.gba", stage / "Long" / LONG_NAME)
    # Enough files to scroll a long way (sd-deep)
    for i in range(1, DEEP_FILES + 1):
        (stage / "Long" / f"Game {i:03d}.gba").write_bytes(bytes(64 * i))
    (stage / "CHEAT" / "Hello.cht").write_bytes(
        b"[Infinite lives]\r\nON=1234,56;\r\n\r\n[Max money]\r\nON=1235,57;\r\n\r\n"
        b"[GameInfo]\r\nName=Hello\r\n"
    )
    # Cheat files for the cheat screen's scenarios: many cheats to scroll
    # through (cheats-many), and names too long for a row (cheats-long)
    shutil.copy("tests/roms/hello/hello.gba", stage / "Games" / "Long cheat.gba")
    shutil.copy("tests/roms/hello/hello.gba", stage / "Games" / "Many cheats.gba")
    (stage / "CHEAT" / "Many cheats.cht").write_bytes(many_cheats())
    (stage / "CHEAT" / "Long cheat.cht").write_bytes(
        b"[InfiniteHealthMagicAndAmmoForEveryoneInTheParty]\r\n"
        b"OnlyWhileTheStartButtonIsHeldDownDuringBattles=1234,56;\r\n"
        b"Always=1234,57;\r\n\r\n[Short]\r\nON=1235,58;\r\n\r\n"
        b"[GameInfo]\r\nName=A game whose name is longer than its title bar\r\n"
    )
    (stage / "SAVER" / "Recently play.txt").write_text(
        "/Hello.gba\n/Games/Idle.gba\n", encoding="utf-8"
    )
    path.unlink(missing_ok=True)
    # A fixed volume ID; mtools stamps files with the host's time, which no
    # screen shows.
    run(
        [
            "mkfs.fat",
            "-C",
            "-F",
            "32",
            "-i",
            "70C0FFEE",
            "-n",
            "TORII",
            str(path),
            "65536",
        ]
    )
    copy_tree(str(path), stage, "::")


def many_cheats() -> bytes:
    """A cheat file of 40 cheats, with one to three options each."""
    words = ["Infinite", "Max", "Unlock", "Walk", "Instant", "Never", "All", "Super"]
    things = ["lives", "money", "items", "through walls", "win", "lose", "maps", "jump"]
    options = [["ON"], ["x2", "x10"], ["Slow", "Normal", "Fast"]]
    out = []
    for i in range(40):
        out.append(f"[{words[i % 8]} {things[i // 5 % 8]} {i + 1}]")
        for j, option in enumerate(options[i % 3]):
            out.append(f"{option}={0x2000 + i:X},{j + 1:02X};")
        out.append("")
    out += ["[GameInfo]", "Name=Many cheats"]
    return "\r\n".join(out).encode() + b"\r\n"


def run(cmd: list[str]) -> None:
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)


class Result:
    def __init__(self, name: str):
        self.name = name
        self.keys: dict[str, tuple[str, int, Path]] = {}  # KEY -> hash, frame, png
        self.frames: list[tuple[int, str]] = []  # -H and -h hashes: frame, hash
        self.cycles: list[str] = []  # frame-cycles lines
        self.error = ""

    def entries(self) -> dict[str, str]:
        """Manifest lines for this run: name -> hash."""
        out = {f"{self.name}/{key}": h for key, (h, _, _) in self.keys.items()}
        for first, last, h in runs_of([h for _, h in self.frames]):
            out[f"{self.name}/frames {first}-{last}"] = h
        return out


def runs_of(hashes: list[str]) -> list[tuple[int, int, str]]:
    out: list[tuple[int, int, str]] = []
    for i, h in enumerate(hashes):
        if out and out[-1][2] == h:
            out[-1] = (out[-1][0], i, h)
        else:
            out.append((i, i, h))
    return out


def boot(
    name: str,
    sd: Path,
    cart: Path,
    out: Path,
    extra: list[str] = (),
    opts: Path | None = None,
):
    """Runs scenario name (or the options file opts) on a new cart, writing
    its key frames to out. tools/theme_check.py runs its previews with it."""
    opts = opts or SHOTS / f"{name}.opts"
    shutil.rmtree(cart, ignore_errors=True)
    cart.mkdir(parents=True)
    shutil.copy(sd, cart / "sd.img")
    kernel = KERNEL
    for line in opts.read_text(encoding="utf-8").splitlines():
        if line.split() == ["#!", "kernel", "test"]:
            kernel = TEST_KERNEL
        elif line.startswith("#! sd-theme "):
            words = line.split(maxsplit=3)
            theme = words[2]
            src = words[3] if len(words) > 3 else f"themes/{theme}"
            img = str(cart / "sd.img")
            for folder in ("::/torii", "::/torii/themes", f"::/torii/themes/{theme}"):
                subprocess.run(
                    ["mmd", "-i", img, folder], capture_output=True, check=False
                )
            # Dot files (.DS_Store and the like) stay behind
            copy_tree(img, Path(src), f"::/torii/themes/{theme}")
        elif line.startswith("#! theme-choice "):
            choice = cart / "theme.txt"
            subprocess.run(
                ["mmd", "-i", str(cart / "sd.img"), "::/torii"],
                capture_output=True,
                check=False,
            )
            choice.write_text(line.split(maxsplit=2)[2] + "\n", encoding="utf-8")
            run(
                [
                    "mcopy",
                    "-Q",
                    "-i",
                    str(cart / "sd.img"),
                    str(choice),
                    "::/torii/theme.txt",
                ]
            )
        elif line.startswith("#! sd-file "):
            _, _, dest, size, src = shlex.split(line)
            data = Path(src).read_bytes()
            padded = cart / Path(dest).name
            padded.write_bytes(data + b"\xff" * (int(size, 0) - len(data)))
            run(
                [
                    "mcopy",
                    "-Q",
                    "-o",
                    "-i",
                    str(cart / "sd.img"),
                    str(padded),
                    f"::{dest}",
                ]
            )
            padded.unlink()
        elif line.startswith("#! sd-fragmented "):
            _, _, dest, size, runs, src = line.split(maxsplit=5)
            data = Path(src).read_bytes()
            padded = cart / Path(dest).name
            padded.write_bytes(data + b"\0" * (int(size, 0) - len(data)))
            run(
                [
                    "tools/sdimg.sh",
                    str(cart / "sd.img"),
                    "put-fragmented",
                    str(padded),
                    dest,
                    runs,
                ]
            )
            padded.unlink()
            # The files that split it up go; its clusters stay where they are
            run(["mdel", "-i", str(cart / "sd.img"), "::/FILL*.BIN"])
        elif line.startswith("#! settings"):
            nor = cart / "kernel-nor.bin"
            if not nor.exists():
                nor.write_bytes(b"\xff" * KERNEL_NOR_SIZE)
            with nor.open("r+b") as f:
                for slot in line.split()[2:]:
                    index, value = (int(x, 0) for x in slot.split("="))
                    f.seek(SETTINGS + 2 * index)
                    f.write(struct.pack("<H", value))
    env = dict(os.environ, OMEGA_DIR=str(cart), TZ="UTC")
    env.pop("OMEGA_TRACE", None)
    cmd = [SHOT, kernel, "-C", str(EPOCH), "-O", str(out), "-i", str(opts), *extra]
    return subprocess.run(cmd, env=env, capture_output=True, text=True, check=False)


def expected_errors(opts: Path) -> list[str]:
    """The "#! expect-error TEXT" lines' TEXTs: kernel error lines starting
    with TEXT the scenario must log, for an error dialog's golden."""
    return [
        line.split(maxsplit=2)[2]
        for line in opts.read_text(encoding="utf-8").splitlines()
        if line.startswith("#! expect-error ")
    ]


def scenario(name: str, sd: Path) -> Result:
    result = Result(name)
    out = RUNS / name
    shutil.rmtree(out, ignore_errors=True)
    proc = boot(name, sd, out / "cart", out)
    (out / "log.txt").write_text(proc.stderr + proc.stdout, encoding="utf-8")
    for line in proc.stdout.splitlines():
        if m := re.fullmatch(r"frame (\d+) -> (\S+) \(hash ([0-9A-F]+)\)", line):
            png = Path(m[2])
            result.keys[png.stem] = (m[3], int(m[1]), png)
        elif m := re.fullmatch(r"hash frame (\d+): ([0-9A-F]+)", line):
            result.frames.append((int(m[1]), m[2]))
    result.cycles = perf.lines(proc.stderr)
    expected = expected_errors(SHOTS / f"{name}.opts")
    errors = [
        line
        for line in proc.stderr.splitlines()
        if line.startswith(("kernel error:", "crash at", "timed out", "cannot"))
        and not any(line.startswith(f"kernel error: {e}") for e in expected)
    ]
    errors += [
        f"no error line: {e}"
        for e in expected
        if f"\nkernel error: {e}" not in "\n" + proc.stderr
    ]
    if proc.returncode or errors:
        result.error = f"mgba-shot exited {proc.returncode}: " + (
            "; ".join(errors[:3]) or proc.stderr.strip()[-200:]
        )
    elif not result.keys and not result.frames:
        result.error = "no -s key frame or -H range"
    return result


def read_manifest() -> dict[str, str]:
    if not MANIFEST.exists():
        return {}
    out = {}
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            name, h = line.rsplit(" ", 1)
            out[name] = h
    return out


def png_rgb(path: Path) -> tuple[int, int, list[bytearray]]:
    rows = [line for _, line in png_lines(path)]
    channels = next(png_lines(path))[0]
    rgb = [
        bytearray(b for i in range(0, len(r), channels) for b in r[i : i + 3])
        for r in rows
    ]
    return len(rgb[0]) // 3, len(rgb), rgb


def write_png(path: Path, width: int, height: int, rows: list[bytes]) -> None:
    raw = b"".join(b"\0" + bytes(r) for r in rows)

    def chunk(kind: bytes, body: bytes) -> bytes:
        return (
            struct.pack(">I", len(body))
            + kind
            + body
            + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
        )

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def diff_png(actual: Path, expected: Path, out: Path) -> int:
    """Writes the expected picture dimmed, with differing pixels in magenta; returns their count."""
    w, h, a = png_rgb(actual)
    w2, h2, e = png_rgb(expected)
    if (w, h) != (w2, h2):
        return -1
    rows, count = [], 0
    for y in range(h):
        row = bytearray()
        for x in range(0, w * 3, 3):
            if a[y][x : x + 3] != e[y][x : x + 3]:
                row += b"\xff\x00\xff"
                count += 1
            else:
                row += bytes(v // 3 for v in e[y][x : x + 3])
        rows.append(row)
    write_png(out, w, h, rows)
    return count


def shot_at(result: Result, frame: int, path: Path, sd: Path) -> None:
    """Runs the scenario again for the screen at an absolute frame (an animation frame)."""
    again = RUNS / result.name / "again"
    boot(result.name, sd, again / "cart", again, ["-s", f"{frame}:{path.resolve()}"])


def check(results: list[Result], want: dict[str, str], sd: Path) -> list[str]:
    failures = []
    FAILED.mkdir(parents=True, exist_ok=True)
    for r in results:
        got = r.entries()
        mine = {k: v for k, v in want.items() if k.split("/")[0] == r.name}
        if got == mine:
            continue
        for key, (h, frame, png) in r.keys.items():
            name = f"{r.name}/{key}"
            if mine.get(name) == h:
                continue
            stem = f"{r.name}.{key}"
            shutil.copy(png, FAILED / f"{stem}.actual.png")
            golden = PNGS / f"{stem}.png"
            if name not in mine:
                failures.append(f"{name}: not recorded (run `just shots-update`)")
                continue
            if golden.exists():
                shutil.copy(golden, FAILED / f"{stem}.expected.png")
                n = diff_png(png, golden, FAILED / f"{stem}.diff.png")
                failures.append(
                    f"{name}: {n} pixels differ at frame {frame} (dist/shots-failed/{stem}.*)"
                )
            else:
                failures.append(
                    f"{name}: hash {h}, expected {mine[name]} (no golden PNG)"
                )
        want_frames = expand([(k, v) for k, v in mine.items() if "/frames " in k])
        got_frames = [h for _, h in r.frames]
        if want_frames != got_frames:
            i = next(
                (
                    i
                    for i, (a, b) in enumerate(
                        zip(got_frames, want_frames, strict=False)
                    )
                    if a != b
                ),
                min(len(got_frames), len(want_frames)),
            )
            what = f"{r.name}/frames: {len(got_frames)} frames hashed, {len(want_frames)} recorded"
            if i < len(r.frames):
                frame = r.frames[i][0]
                png = FAILED / f"{r.name}.frame{i}.actual.png"
                shot_at(r, frame, png, sd)
                what += f"; frame {i} (absolute {frame}) differs first ({png})"
            failures.append(what)
        stale = set(mine) - set(got)
        stale -= {k for k in stale if "/frames " in k}
        failures += [
            f"{k}: recorded, but the run made no such key frame" for k in sorted(stale)
        ]
    return failures


def expand(entries: list[tuple[str, str]]) -> list[str]:
    spans = []
    for name, h in entries:
        first, last = (int(x) for x in name.split(" ")[1].split("-"))
        spans.append((first, last, h))
    return [h for first, last, h in sorted(spans) for _ in range(first, last + 1)]


def entry_order(name: str) -> tuple:
    """Key frames by name, then a scenario's frame runs in frame order."""
    scen, _, rest = name.partition("/")
    if rest.startswith("frames "):
        return (scen, 1, int(rest.split(" ")[1].split("-")[0]), "")
    return (scen, 0, 0, rest)


def update(
    results: list[Result], want: dict[str, str], names: set[str], every: set[str]
) -> None:
    """Records the runs of names; a full run (names is every) drops deleted scenarios."""
    keep = every - names
    new = {k: v for k, v in want.items() if k.split("/")[0] in keep}
    PNGS.mkdir(parents=True, exist_ok=True)
    for r in results:
        new.update(r.entries())
        for key, (_, _, png) in r.keys.items():
            shutil.copy(png, PNGS / f"{r.name}.{key}.png")
    for png in PNGS.glob("*.png"):
        scen, _, key = png.stem.partition(".")
        if scen not in keep and f"{scen}/{key}" not in new:
            png.unlink()
    lines = [
        f"{k} {v}" for k, v in sorted(new.items(), key=lambda kv: entry_order(kv[0]))
    ]
    MANIFEST.write_text(HEADER + "\n".join(lines) + "\n", encoding="utf-8")
    changed = [k for k in sorted(new) if k in want and want[k] != new[k]]
    added = [k for k in sorted(new) if k not in want]
    removed = [k for k in sorted(want) if k not in new]
    for label, keys in (("changed", changed), ("new", added), ("removed", removed)):
        for k in keys:
            print(f"shots: {label}: {k}")
    same = len(new) - len(changed) - len(added)
    print(
        f"shots: {len(changed)} changed, {len(added)} new, {len(removed)} removed, {same} unchanged"
    )


def main() -> int:
    args = sys.argv[1:]
    mode = "check"
    if args and args[0] in ("--update", "--update-perf"):
        mode = args.pop(0)
    if any(a.startswith("-") for a in args):
        sys.exit(__doc__)
    every = sorted(p.stem for p in SHOTS.glob("*.opts"))
    names = args or every
    missing = [n for n in names if n not in every]
    if missing:
        sys.exit(f"shots: no scenario {', '.join(missing)} in {SHOTS}")
    RUNS.mkdir(parents=True, exist_ok=True)
    if mode == "check":
        shutil.rmtree(FAILED, ignore_errors=True)
    sd = RUNS / "sd.img"
    build_sd(sd)
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(lambda n: scenario(n, sd), names))

    errors = [
        f"{r.name}: {r.error} (log: {RUNS / r.name / 'log.txt'})"
        for r in results
        if r.error
    ]
    for e in errors:
        print(f"FAIL: shots: {e}")
    if errors:
        return 1
    cycles = [line for r in results for line in r.cycles]
    if mode == "--update":
        update(results, read_manifest(), set(names), set(every))
        return 0
    if mode == "--update-perf":
        perf.update(cycles, perf.BUDGET, partial=bool(args))
        return 0

    status = 0
    want = read_manifest()
    if not args:
        known = set(every)
        orphans = sorted({k.split("/")[0] for k in want} - known)
        if orphans:
            print(
                f"FAIL: shots: hashes.txt lists scenarios with no .opts file: {', '.join(orphans)}"
            )
            status = 1
    failures = check(results, want, sd)
    for f in failures:
        print(f"FAIL: shots: {f}")
    if failures:
        status = 1
    else:
        total = sum(len(r.entries()) for r in results)
        print(
            f"PASS: golden shots: {len(results)} scenarios, {total} hashes match tests/shots/hashes.txt"
        )
    problems = perf.check(cycles, perf.read_budget(perf.BUDGET), partial=bool(args))
    problems += perf.header_problems(perf.read_budget(perf.BUDGET))
    for p in problems:
        print(f"FAIL: perf: {p}")
    if problems:
        status = 1
    else:
        print(f"PASS: cycle budget: {perf.summary(cycles)}")
    return status


if __name__ == "__main__":
    sys.exit(main())
