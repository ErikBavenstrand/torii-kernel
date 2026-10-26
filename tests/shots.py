#!/usr/bin/env python3
"""Golden screenshots and the cycle budget, on the emulated cart.

usage: tests/shots.py [--update | --update-perf] [SCENARIO...]

Each scenario is an mgba-shot options file, tests/shots/NAME.opts, run on a
fresh cart (the SD card below, kernel NOR erased) with the clock frozen. It
waits for the kernel's "screen ready: SCREEN" lines with -w and names its key
frames with -s @N:KEY.png; -H @A:@B hashes every frame of an animation.
Lines starting "#! settings SLOT=VALUE..." set kernel settings slots first;
"#! kernel test" runs the test kernel (ezkerneltest.gba, `just test-kernel`'s
build) instead, for scenes only it has, as the cover demo. "#! sd-file
DEST SIZE SRC" copies SRC to DEST on the card, padded with 0xFF to SIZE
bytes: a game large enough for a progress bar to move (boot-progress-big),
or, with SIZE 0 and SRC /dev/null, an empty file in place of one (quote a
DEST with spaces).
"#! sd-rm PATH" deletes PATH (a file or a whole folder) from the card first,
so a later sd-file can put a file in its place (nor-launch-fail).
"#! sd-fragmented DEST SIZE RUNS SRC" copies SRC, padded with zeros to SIZE
bytes, to DEST in RUNS runs of clusters (tools/sdimg.sh put-fragmented): a
game too fragmented to start (dialog-fragmented). A "kernel error:" line
fails a scenario unless an "#! expect-error TEXT" line expects it (it starts
with TEXT); an expected one that isn't logged fails it too.
"#! nor-game NAME SIZE ROM" puts a game on the game NOR as an older kernel
lists it (the boot saves the list in Torii's format): ROM's bytes at the
next free 128 KB block, its area SIZE bytes rounded up to whole blocks, and
an entry named NAME, so a NOR page shows games of any size without writing
them through the menu (nor-page).
"#! sd-name DEST SRC NAME" names the card's file DEST (a copy of SRC) NAME
in a names.db and crc.txt made for the scenario, as `just names-update`
would (games-names). "#! sd-fsinfo-unknown" marks the card's free cluster
count as not recorded (info-no-free).
"#! sd-many FOLDER COUNT" makes FOLDER on the card with COUNT small games,
"Game 0001.gba" on, in that order (sd-many: a folder past the SD card page's
1,024 entries).
"#! env NAME=VALUE" sets an environment variable for the emulated cart, as
OMEGA_COPY_RATE=BYTES (bytes the cart's ROM copy moves per frame), which
makes a launch's copy take as long as the cart's (boot-copy). A scenario with
-T FROM:TO runs mgba-shot's tear watch over those frames: any "tear:" line
(a register, palette or on-screen VRAM write while lines 0-159 scan out)
fails it.

Check (the default): every key frame's screen hash, and every animation's
frame hashes, must equal tests/shots/hashes.txt. On a mismatch the actual,
expected and diff PNGs go to dist/shots-failed/; an animation's expected
frame comes from dist/shots-frames, where every run keeps each animation
frame it hashed by its hash (none on a fresh checkout). Then every
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
import tempfile
import zlib
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(1, str(Path(__file__).parent.parent / "tools"))
import names_db
import perf
import setnor
from games import png_lines

SHOTS = Path("tests/shots")
MANIFEST = SHOTS / "hashes.txt"
PNGS = SHOTS / "png"
RUNS = Path("dist/shots-run")
FAILED = Path("dist/shots-failed")
# Every animation frame's screen by its hash (mgba-shot -D), kept between
# runs, so a frame that changes has its expected picture from an earlier run
FRAMES = Path("dist/shots-frames")
# The scenarios' carts, on the container's own disk: the emulator syncs the
# memory-mapped cart files at each power-off, which takes about 0.5 s on the
# virtiofs mount (tests/carts.sh)
CARTS = Path(tempfile.gettempdir()) / "torii-shots-carts"
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
KERNEL_NOR_SIZE = 8 << 20
GAME_NOR_SIZE = 64 << 20
NOR_LIST_A = 0x7A0000  # the game NOR's list, as the official kernel keeps it
NOR_BLOCK = 0x20000

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
    (stage / "Games").mkdir(parents=True)
    (stage / "Long").mkdir()
    (stage / "SAVER").mkdir()
    shutil.copy("tests/roms/hello/hello.gba", stage / "Hello.gba")
    shutil.copy("tests/roms/idle/idle.gba", stage / "Games" / "Idle.gba")
    # Too long for its row: the SD card page scrolls it, an animation to check
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
    # Cheat files in libretro's format (source/cheatfile.h), for the cheat
    # screen: Hello's with a master code (hidden), a pick, a conditional,
    # a placeholder (greyed) and a ROM patch; many cheats to scroll
    # through (cheats-many), and names too long for a row (cheats-long)
    cheats = stage / "torii" / "cheats"
    (cheats / "Games").mkdir(parents=True)
    (cheats / "Hello.cht").write_bytes(
        libretro_cht(
            [
                ("(M) Must Be On", "0000ABCD 0007+100F68A4 0007", False),
                ("Infinite lives", "32001000 0063", False),
                ("Max money", "82001002 270F", True),
                ("Moon jump (hold A)", "74000130 03FE+32001004 0001", False),
                ("Pick a level", "3300075C+00??", False),
                ("Skip the intro", "08001000:46C0", False),
            ]
        )
    )
    shutil.copy("tests/roms/hello/hello.gba", stage / "Games" / "Long cheat.gba")
    shutil.copy("tests/roms/hello/hello.gba", stage / "Games" / "Many cheats.gba")
    (cheats / "Games" / "Many cheats.cht").write_bytes(many_cheats())
    (cheats / "Games" / "Long cheat.cht").write_bytes(
        libretro_cht(
            [
                (
                    "Infinite health, magic and ammo for everyone in the party",
                    "32001000 0063",
                    False,
                ),
                ("Short", "32001001 0001", True),
            ]
        )
    )
    (stage / "SAVER" / "Recently play.txt").write_text(
        "/Hello.gba\n/Games/Idle.gba\n", encoding="utf-8"
    )
    path.unlink(missing_ok=True)
    # A fixed volume ID; mtools stamps files with EPOCH's time (run)
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


def libretro_cht(cheats: list[tuple[str, str, bool]]) -> bytes:
    """A cheat file as tools/cheats.py writes one, CRLF ends as RetroArch's."""
    lines = [f"cheats = {len(cheats)}", ""]
    for n, (desc, code, on) in enumerate(cheats):
        lines += [
            f'cheat{n}_desc = "{desc}"',
            f'cheat{n}_code = "{code}"',
            f"cheat{n}_enable = {'true' if on else 'false'}",
            "",
        ]
    return "\r\n".join(lines).encode()


def many_cheats() -> bytes:
    """A cheat file of 40 cheats."""
    words = ["Infinite", "Max", "Unlock", "Walk", "Instant", "Never", "All", "Super"]
    things = ["lives", "money", "items", "through walls", "win", "lose", "maps", "jump"]
    return libretro_cht(
        [
            (
                f"{words[i % 8]} {things[i // 5 % 8]} {i + 1}",
                f"3200{0x1000 + i:04X} 00{i:02X}",
                i % 7 == 0,
            )
            for i in range(40)
        ]
    )


def run(cmd: list[str]) -> None:
    # mtools stamps what it writes with SOURCE_DATE_EPOCH's time, so File
    # info's date is the same on every machine
    env = dict(os.environ, SOURCE_DATE_EPOCH=str(EPOCH), TZ="UTC")
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, env=env)


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


def boot(name: str, sd: Path, cart: Path, out: Path, extra: list[str] = ()):
    """Runs scenario name on a new cart, writing its key frames to out."""
    opts = SHOTS / f"{name}.opts"
    shutil.rmtree(cart, ignore_errors=True)
    cart.mkdir(parents=True)
    shutil.copy(sd, cart / "sd.img")
    kernel = KERNEL
    cart_env: dict[str, str] = {}
    names: list[tuple[str, bytes, str]] = []
    for line in opts.read_text(encoding="utf-8").splitlines():
        if line.split() == ["#!", "kernel", "test"]:
            kernel = TEST_KERNEL
        elif line.startswith("#! sd-rm "):
            target = "::" + line.split(maxsplit=2)[2]
            run(["mdeltree", "-i", str(cart / "sd.img"), target])
        elif line.startswith("#! sd-file "):
            _, _, dest, size, src = shlex.split(line)
            data = Path(src).read_bytes()
            padded = cart / Path(dest).name
            padded.write_bytes(data + b"\xff" * (int(size, 0) - len(data)))
            # Its folders first, if the card doesn't hold them (/torii)
            parts = dest.strip("/").split("/")[:-1]
            for i in range(len(parts)):
                folder = "::/" + "/".join(parts[: i + 1])
                if subprocess.run(
                    ["mdir", "-i", str(cart / "sd.img"), folder],
                    capture_output=True,
                    check=False,
                ).returncode:
                    run(["mmd", "-i", str(cart / "sd.img"), folder])
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
        elif line.startswith("#! sd-name "):
            _, _, dest, src, title = shlex.split(line)
            names.append((dest, Path(src).read_bytes(), title))
        elif line.split() == ["#!", "sd-fsinfo-unknown"]:
            fsinfo_unknown(cart / "sd.img")
        elif line.startswith("#! nor-game "):
            _, _, name, size, src = shlex.split(line)
            nor_game(cart, name, int(size, 0), Path(src).read_bytes())
        elif line.startswith("#! sd-many "):
            _, _, folder, count = shlex.split(line)
            stage = cart / "many"
            stage.mkdir()
            files = []
            for i in range(1, int(count) + 1):
                f = stage / f"Game {i:04d}.gba"
                f.write_bytes(bytes(32))
                files.append(str(f))
            run(["mmd", "-i", str(cart / "sd.img"), f"::{folder}"])
            run(["mcopy", "-Q", "-i", str(cart / "sd.img"), *files, f"::{folder}/"])
            shutil.rmtree(stage)
        elif line.startswith("#! env "):
            name, _, value = line.split(maxsplit=2)[2].partition("=")
            cart_env[name] = value
        elif line.startswith("#! settings"):
            nor = cart / "kernel-nor.bin"
            image = bytearray(
                nor.read_bytes() if nor.exists() else b"\xff" * KERNEL_NOR_SIZE
            )
            setnor.write(
                image,
                {
                    int(s, 0): int(v, 0)
                    for s, v in (a.split("=") for a in line.split()[2:])
                },
            )
            nor.write_bytes(image)
    if names:
        sd_names(cart, names)
    env = dict(os.environ, OMEGA_DIR=str(cart), TZ="UTC")
    env.pop("OMEGA_TRACE", None)
    env.update(cart_env)
    FRAMES.mkdir(parents=True, exist_ok=True)
    cmd = [SHOT, kernel, "-C", str(EPOCH), "-O", str(out), "-D", str(FRAMES)]
    cmd += ["-i", str(opts), *extra]
    return subprocess.run(cmd, env=env, capture_output=True, text=True, check=False)


def fsinfo_unknown(img: Path) -> None:
    """Marks the FAT32 card's free cluster count (FSInfo) as not recorded,
    as a card some PCs leave: the kernel then shows no free space."""
    with img.open("r+b") as f:
        f.seek(48)
        sector = struct.unpack("<H", f.read(2))[0]
        f.seek(sector * 512 + 488)
        f.write(b"\xff" * 8)  # the free count and the next free cluster


def sd_names(cart: Path, names: list[tuple[str, bytes, str]]) -> None:
    """/torii/names.db naming each game by its CRC, and /torii/crc.txt
    giving the games' CRCs, as `just names-update` writes them."""
    tables: list[dict[int, str]] = [{} for _ in names_db.CONSOLES]
    lines = [names_db.CRC_HEADER]
    for dest, data, title in names:
        console = names_db.console_of(Path(dest).name)
        crc = zlib.crc32(data)
        tables[console][crc] = title
        lines.append(f"{crc:08X}\t{len(data)}\t-\t{dest}\n")
    stage = cart / "names"
    stage.mkdir()
    (stage / "names.db").write_bytes(names_db.build_db(tables, "2026.01.02"))
    names_db.write_safe(stage / "crc.txt", "".join(lines))
    files = [str(stage / n) for n in ("names.db", "crc.txt", "crc.txt.bak")]
    img = str(cart / "sd.img")
    if subprocess.run(
        ["mdir", "-i", img, "::/torii"], capture_output=True, check=False
    ).returncode:
        run(["mmd", "-i", img, "::/torii"])
    run(["mcopy", "-Q", "-o", "-i", str(cart / "sd.img"), *files, "::/torii/"])
    shutil.rmtree(stage)


def nor_game(cart: Path, name: str, size: int, rom: bytes) -> None:
    """Adds a game to the cart's game NOR and an older kernel's list entry
    for it (FM_NOR_FS, source/ezkernel.h) after the entries already there."""
    kernel, game = cart / "kernel-nor.bin", cart / "game-nor.bin"
    for path, length in ((kernel, KERNEL_NOR_SIZE), (game, GAME_NOR_SIZE)):
        if not path.exists():
            path.write_bytes(b"\xff" * length)
    area = -(-max(size, len(rom)) // NOR_BLOCK) * NOR_BLOCK
    with kernel.open("r+b") as k, game.open("r+b") as g:
        k.seek(NOR_LIST_A)
        offset, index = 0, 0
        while True:
            entry = k.read(132)
            filesize = struct.unpack_from("<I", entry, 108)[0]
            if filesize in (0, 0xFFFFFFFF):
                break
            offset += filesize
            index += 1
        g.seek(offset)
        g.write(rom)
        k.seek(NOR_LIST_A + 132 * index)
        k.write(
            struct.pack(
                "<100sHHHHIBBH16s",
                name.encode(),
                offset >> 17,
                0,
                0,
                0,
                area,
                0x11,
                0,
                0,
                rom[0xA0:0xB0],
            )
        )


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
    out.mkdir(parents=True)
    proc = boot(name, sd, CARTS / name, out)
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
    # The tear watch (-T in the opts): a store that changed the screen while
    # it scanned out
    errors += [line for line in proc.stdout.splitlines() if line.startswith("tear:")]
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
    again.mkdir(parents=True, exist_ok=True)
    boot(
        result.name,
        sd,
        CARTS / f"{result.name}-again",
        again,
        ["-s", f"{frame}:{path.resolve()}"],
    )


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
                stem = FAILED / f"{r.name}.frame{i}"
                png = Path(f"{stem}.actual.png")
                seen = FRAMES / f"{got_frames[i]}.png"
                if seen.exists():
                    shutil.copy(seen, png)
                else:
                    shot_at(r, frame, png, sd)
                what += f"; frame {i} (absolute {frame}) differs first ({stem}.*)"
                golden = (
                    FRAMES / f"{want_frames[i]}.png" if i < len(want_frames) else None
                )
                if golden and golden.exists() and png.exists():
                    shutil.copy(golden, f"{stem}.expected.png")
                    n = diff_png(png, golden, Path(f"{stem}.diff.png"))
                    what += f", {n} pixels"
                elif golden:
                    what += "; no expected picture (dist/shots-frames has none from an earlier run)"
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


def binaries() -> list[int]:
    """CRCs of the kernels and the emulator the scenarios run."""
    out = []
    for path in (KERNEL, TEST_KERNEL, SHOT):
        try:
            out.append(zlib.crc32(Path(path).read_bytes()))
        except OSError:
            out.append(-1)
    return out


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
    before = binaries()
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(lambda n: scenario(n, sd), names))
    if binaries() != before:
        # A build during the runs (or a late view of one through the colima
        # VM's virtiofs mount) gives some scenarios another kernel: their
        # screens would fail for no fault of their own
        print("FAIL: shots: the kernel or emulator changed during the runs: run again")
        return 1

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
