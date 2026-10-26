#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Play the games in roms/games.toml through the kernel on the emulated cart.

usage: tests/games.py [--all] [FILTER...]

Games share requirements: Ruby and Sapphire need exactly the same of the
kernel. So by default one game runs per set of requirements (system, save
chip, kernel save mode, ROM size class, GB mapper, clock, other hardware),
preferring one with a key script or a starting save; the report lists the
games each one stands for. A GB mapper roms/ has no game for is named and
skipped. --all runs every game. A FILTER keeps games whose code,
system (gba, gbc, gb), save chip or file name contains it, or, written as
system=NAME, whose system is NAME. Games marked
`skip` in games.toml (ROM hacks, kernel plugins) never run. Each game gets a
fresh cart and three power-ons:

1. Launch it from the kernel menu, press START and A now and then for about
   40 seconds of game time, and watch the screen.
2. If the game wrote its save chip, power on again: the kernel must copy
   the save to /torii/saves.
3. Launch it again: the kernel must load that save back into the cart.

A GBA game that boots also gets a cart of its own with SAVESTATE and RESET
on, is launched with BOOT WITH ADDON, and tests/hook_watch.lua opens and
leaves the in-game menu (L+R+START, then B) once the game has run a while.

Checks, each pass, fail, untested (the run didn't get that far) or n/a:

- boots: the game left the kernel and its screen changed; a crash, kernel
  error, blank or frozen screen fails
- save type: the kernel sent the save chip the ROM's SDK strings ask for
- clock: a game that uses the clock read it, with the cart's clock on
- save cycle: what the game wrote reached /torii/saves and came back on relaunch;
  for a game that didn't save during the run, a test pattern put in the
  cart's save memory makes the same trip
- in-game menu: the kernel hooked the game's IRQ, the menu opened, and the
  game went on after it; a failure says why (no literal for the kernel to
  patch, the game set its own handler, a crash); n/a for a game that never
  enables the VBlank IRQ, which the menu needs. Metrics
  give the hook: how the kernel found the game's IRQ handler stores
  (built-in table or ROM scan), the IRQ vector during the game and how many
  frames had the VBlank IRQ enabled

A game with a starting save (roms/<name>.sav, .esv for GB/GBC) and a key
script (tests/games/<code>.keys) plays that script instead, saves in-game and
is relaunched to save again; Pokémon generation 3 save counters must go up.

Results go to dist/compat/runs/<time>.json and the report to
dist/compat/index.html (tests/compat_report.py); a failed game's carts are
kept in dist/cart-games/. Runs inside the torii-emu
container and reads ROMs only from roms/, where `just game-add` copies them.
"""

import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import tomllib
import zlib
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

KERNEL = "dist/ezkernelnew.bin"
SHOT = "emu/build-linux/mgba-shot"
ROMS = Path("roms")
KEYS = Path("tests/games")
# The games' carts, on the container's own disk: the emulator syncs the
# memory-mapped cart files at each power-off, which takes about 0.5 s on the
# virtiofs mount (tests/carts.sh). A failed game's carts are copied to KEPT.
CARTS = Path(tempfile.gettempdir()) / "torii-compat-carts"
KEPT = Path("dist/cart-games")
REPORT = Path("dist/compat")
INDEX = Path("dist/.fresh/rom-index.json")

SAVE_SIZE = {
    "sram32k": 0x8000,
    "eeprom512": 0x200,
    "eeprom8k": 0x2000,
    "flash64k": 0x10000,
    "flash128k": 0x20000,
    "battery": 0x10000,  # GB/GBC saves: the kernel gives its emulator 64 KB
}
# Kernel save modes (FAT table word 0x1FC) by the names in games.toml.
KERNEL_SAVE = {
    0x11: "sram32k",
    0x21: "eeprom512",
    0x22: "eeprom8k",
    0x23: "eeprom8k",
    0x31: "flash128k",
    0x32: "flash64k",
    0x33: "flash64k",
}
CHECKS = ["boots", "save type", "clock", "save cycle", "in-game menu"]
# Launches go as the menu's pages will run them, through actions.c and the
# menu's test hook (source/screens/nav.h, tests/nav.sh): a "hook" key pokes
# the game's path and the action (1 CLEAN BOOT, 2 BOOT WITH ADDON), which
# the menu runs once it is up. GB, GBC and NES games start clean.
HOOK_CLEAN = 1
HOOK_ADDON = 2


def launch_keys(sd_name: str, action: int, frame: int) -> list[tuple[int, str]]:
    return [(frame, f"hook:{action}:/{sd_name}")]


# The kernel has handed over to the game by now: with the menu's transitions
# (v0.3 F2) it finishes loading a 32 MB game and its save about 320 frames in
GAME_START = 360
RUN_FRAMES = 2400
SHOTS = (600, 1500, RUN_FRAMES)

# The in-game menu run: SAVESTATE on alone (it puts the menu on the reset
# keys, whatever RESET says), menu key L+R+START, English, the fast patch
# engine, clock and rumble on: settings slots in the kernel NOR
# (source/ez_define.h), written before power-on.
MENU_SETTINGS = {0: 0xE1E1, 1: 0, 2: 1, 3: 0, 4: 0, 5: 9, 6: 8, 7: 2, 8: 9, 9: 8}
MENU_SETTINGS |= {10: 3, 11: 1, 12: 0, 13: 1, 25: 1}
KERNEL_NOR_SIZE = 0x800000
SETTINGS = 0x7B0000
HOOK_WATCH = "tests/hook_watch.lua"
IRQ_LITERALS = (0x03007FFC, 0x03FFFFFC)  # what the kernel's ROM scan patches
PATCH_SLOTS = 32  # EMax in source/GBApatch.h: literals past this stay unpatched
PATCH_SIZE = 0x2000  # the largest patch area, as in hook_watch.lua


class Failure(Exception):
    pass


# --- Finding ROMs and saves ---------------------------------------------------


def library_roots() -> list[Path]:
    # Only the copies in roms/: tests never read the folders games came from.
    return [ROMS]


def rom_index() -> dict[str, Path]:
    """SHA-1 to path for every ROM under roms/ (cached)."""
    cache = json.loads(INDEX.read_text()) if INDEX.exists() else {}
    fresh, index = {}, {}
    for root in library_roots():
        for path in root.rglob("*"):
            if path.suffix.lower() not in (".gba", ".gbc", ".gb") or not path.is_file():
                continue
            st = path.stat()
            key = str(path)
            entry = cache.get(key)
            if not entry or entry[:2] != [st.st_size, st.st_mtime_ns]:
                sha1 = hashlib.sha1(path.read_bytes()).hexdigest()
                entry = [st.st_size, st.st_mtime_ns, sha1]
            fresh[key] = entry
            index.setdefault(entry[2], path)
    INDEX.parent.mkdir(parents=True, exist_ok=True)
    INDEX.write_text(json.dumps(fresh))
    return index


def save_index() -> dict[str, Path]:
    """Starting saves by file stem, from roms/ only."""
    saves = {}
    for root in library_roots():
        for path in [*root.rglob("*.sav"), *root.rglob("*.esv")]:
            stem = re.sub(r"\(\d+\)$", "", path.stem).strip()
            saves.setdefault(stem, path)
    return saves


# --- Running the cart ----------------------------------------------------------


def mtools(*args: str) -> bytes:
    return subprocess.run(args, check=True, capture_output=True).stdout


def make_cart(cart: Path, rom: Path, sd_name: str, start_save: Path | None) -> None:
    shutil.rmtree(cart, ignore_errors=True)
    cart.mkdir(parents=True)
    img = str(cart / "sd.img")
    size_kb = max(65536, (rom.stat().st_size >> 10) * 2 + 16384)
    mtools("mkfs.fat", "-C", "-F", "32", "-n", "GAMES", img, str(size_kb))
    mtools("mcopy", "-i", img, str(rom), f"::/{sd_name}")
    if start_save:
        for folder in ("torii", "torii/saves", f"torii/saves/{sd_name}"):
            mtools("mmd", "-i", img, f"::/{folder}")
        mtools("mcopy", "-i", img, str(start_save), f"::/{save_path(sd_name)}")


def power_on(cart: Path, keys, frames: int, extra: list[str] = ()) -> tuple[str, str]:
    """Run the cart once from power-on; return (stdout, cart trace)."""
    args = [SHOT, KERNEL, "-f", str(frames), *extra]
    for frame, key in keys:
        if key.startswith("hook:"):
            _, action, path = key.split(":", 2)
            args += ["-P", f"{frame}:nav_test_path:{path}"]
            args += ["-p", f"{frame}:nav_test_action:{0x7E000000 | int(action)}"]
            continue
        args += ["-k", f"{frame}:{key}"]
        if key != "-":
            args += ["-k", f"{frame + 6}:-"]
    env = dict(os.environ, OMEGA_DIR=str(cart), OMEGA_TRACE="1")
    run = subprocess.run(
        args, env=env, capture_output=True, text=True, errors="replace", check=False
    )
    if run.returncode not in (0, 3):  # 3: crashed, the report is in the trace
        raise Failure(f"mgba-shot failed: {run.stderr.strip()[-300:]}")
    return run.stdout, run.stderr


def mash() -> list[tuple[int, str]]:
    """START past title screens, then A and START now and then."""
    keys, frame = [], 600
    while frame < RUN_FRAMES - 200:
        keys.append((frame, "START" if (frame // 120) % 3 == 0 else "A"))
        frame += 120
    return keys


def read_keys(path: Path) -> list[tuple[int, str]]:
    keys = []
    for line in path.read_text().splitlines():
        words = line.split("#", 1)[0].split()
        if words:
            keys.append((int(words[0]), words[1]))
    return keys


def save_path(sd_name: str) -> str:
    """Where the kernel keeps the game's first slot (a game in the root)."""
    return f"torii/saves/{sd_name}/Main.sav"


def sd_file(cart: Path, path: str) -> bytes | None:
    try:
        return mtools("mtype", "-i", str(cart / "sd.img"), f"::/{path}")
    except subprocess.CalledProcessError:
        return None


def sd_save(cart: Path, sd_name: str) -> bytes | None:
    return sd_file(cart, save_path(sd_name))


# --- Reading results -------------------------------------------------------------


def png_lines(path: Path):
    """Yield (channels, pixels of one line) for an 8-bit RGB(A) PNG, top down."""
    data = path.read_bytes()
    pos, idat = 8, b""
    width = height = channels = 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos : pos + 8])
        body = data[pos + 8 : pos + 8 + length]
        if kind == b"IHDR":
            width, height, _, colour = struct.unpack(">IIBB", body[:10])
            channels = {2: 3, 6: 4}[colour]
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * channels
    prev = bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1 : (y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if kind == 1:
                line[i] = (line[i] + a) & 0xFF
            elif kind == 2:
                line[i] = (line[i] + b) & 0xFF
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else b if pb <= pc else c
                line[i] = (line[i] + pred) & 0xFF
        yield channels, line
        prev = line


def png_is_blank(path: Path) -> bool:
    """True if every pixel of an 8-bit RGB(A) PNG has the same colour."""
    pixels = set()
    for channels, line in png_lines(path):
        pixels.update(bytes(line[i : i + 3]) for i in range(0, len(line), channels))
        if len(pixels) > 1:
            return False
    return True


def png_pixel(path: Path, x: int, y: int) -> str:
    """The RGB colour at x, y of an 8-bit RGB(A) PNG, as hex."""
    for row, (channels, line) in enumerate(png_lines(path)):
        if row == y:
            return line[x * channels : x * channels + 3].hex()
    raise ValueError(f"{path} has no row {y}")


def gen3_counter(save: bytes) -> int | None:
    """Highest save counter in a Pokémon generation 3 save, if it is one."""
    counters = [
        struct.unpack_from("<I", save, off + 0xFFC)[0]
        for off in range(0, len(save), 0x1000)
        if len(save) >= off + 0x1000
        and struct.unpack_from("<I", save, off + 0xFF8)[0] == 0x08012025
    ]
    return max(counters) if counters else None


def lines(trace: str, marker: str) -> list[str]:
    return [line for line in trace.splitlines() if marker in line]


# --- One game ----------------------------------------------------------------------


class Result:
    def __init__(self, game: dict):
        self.game = game
        self.checks = {name: ["untested", ""] for name in CHECKS}
        self.skipped = ""
        self.notes: list[str] = []
        self.shots: list[str] = []
        self.metrics: dict = {}
        self.trace_tail = ""
        self.requirements = ""
        self.alike: list[str] = []

    def set(self, check: str, status: str, detail: str = "") -> None:
        self.checks[check] = [status, detail]

    @property
    def status(self) -> str:
        if self.skipped:
            return "skip"
        states = [s for s, _ in self.checks.values()]
        if "fail" in states:
            return "fail"
        return "partial" if "untested" in states else "pass"

    def to_json(self) -> dict:
        g = self.game
        return {
            "key": key_of(g),
            "file": g["file"],
            "title": g["title"],
            "code": g["code"],
            "system": g.get("system", "gba"),
            "save": g["save"],
            "rtc": g["rtc"],
            "hardware": g.get("hardware", []),
            "size": self.metrics.get("size"),
            "status": self.status,
            "skipped": self.skipped,
            "checks": self.checks,
            "notes": self.notes,
            "metrics": self.metrics,
            "shots": self.shots,
            "trace": self.trace_tail,
            "requirements": self.requirements,
            "alike": self.alike,
        }


def key_of(game: dict) -> str:
    return game["sha1"][:10]


def screen_hashes(out: str) -> list[tuple[int, str]]:
    """(frame, hash) for each -h line of mgba-shot's output."""
    return [
        (int(m[0]), m[1])
        for m in re.findall(r"^hash frame (\d+): (\S+)", out, re.MULTILINE)
    ]


def check_boot(result: Result, trace: str, out: str, shots: list[Path]) -> bool:
    """Record boots, save type and clock from the first power-on."""
    g = result.game
    crash = lines(trace, "crash at frame ")
    errors = lines(trace, "kernel error: ")
    # The cart model's trace ("omega FRAME: launch: ..."), not the kernel's log
    launches = [
        line for line in lines(trace, ": launch: ") if line.startswith("omega ")
    ]
    hashes = [
        line.split(": ")[1]
        for line in out.splitlines()
        if line.startswith("hash frame ")
    ]
    frames = [
        int(line.split()[2].rstrip(":"))
        for line in out.splitlines()
        if line.startswith("hash frame ")
    ]
    in_game = [h for f, h in zip(frames, hashes, strict=True) if f >= GAME_START]
    result.metrics["distinct screens"] = len(set(in_game))
    modes = lines(trace, ": mode ")
    if crash:
        result.set("boots", "fail", crash[0])
        result.trace_tail = "\n".join(trace.splitlines()[-40:])
    elif errors:
        result.set("boots", "fail", errors[0])
    elif not launches:
        result.set("boots", "fail", "the kernel never launched the game")
    elif not modes or ": mode game" not in modes[-1]:
        result.set("boots", "fail", "never left kernel mode")
    elif all(png_is_blank(s) for s in shots):
        result.set("boots", "fail", "blank screen throughout")
    elif len(set(in_game)) <= 1:
        result.set("boots", "fail", "frozen: the screen never changed")
    else:
        result.set("boots", "pass", f"{len(set(in_game))} different screens")

    if launches:
        mode = int(launches[-1].split("save mode ")[1].split()[0], 16)
        result.metrics["kernel save mode"] = f"{mode:02X}"
        if g.get("system", "gba") != "gba" or g["save"] == "none":
            result.set("save type", "n/a")
        else:
            sent = KERNEL_SAVE.get(mode, f"unknown ({mode:#x})")
            ok = sent == g["save"]
            result.set(
                "save type",
                "pass" if ok else "fail",
                "" if ok else f"kernel sent {sent}, game needs {g['save']}",
            )

    if not g["rtc"]:
        result.set("clock", "n/a")
    elif "game uses the clock, but" in trace:
        result.set("clock", "fail", "the game uses the clock, but in-game RTC is off")
    elif "game uses the clock" in trace:
        result.set("clock", "pass")
    else:
        result.set("clock", "untested", "the game didn't read the clock in this run")
    return result.checks["boots"][0] == "pass"


def irq_literals(rom: bytes) -> int:
    """Words the kernel's ROM scan would patch (PatchInternal)."""
    words = struct.unpack_from(f"<{len(rom) // 4}I", rom)
    return sum(words.count(v) for v in IRQ_LITERALS)


def hook_table() -> dict[bytes, str]:
    """How the kernel finds each known game's IRQ handler stores, by game code:
    its built-in table of literals (reset_table.h) and the code patches for
    games that compute the address (Patch_SpecialROM_sleepmode)."""
    text = Path("source/reset_table.h").read_text()
    words = [int(w, 16) for w in re.findall(r"0x([0-9A-Fa-f]{8})", text)]
    table, i = {}, 0
    while i + 1 < len(words):
        table.setdefault(
            struct.pack("<I", words[i]), f"built-in table, {words[i + 1]} patched"
        )
        i += words[i + 1] + 2
    source = Path("source/GBApatch.c").read_text()
    special = source.split("void Patch_SpecialROM_sleepmode(void)")[1].split("\n}")[0]
    # Cases can share a body (case A: case B: ...), so each runs to its break.
    for case in re.finditer(r"\bcase 0x([0-9A-Fa-f]{8})", special):
        key = struct.pack("<I", int(case[1], 16))
        body = special[case.end() : special.find("break;", case.end())]
        patches = len(re.findall(r"Add2(?:_if)?\(", body))
        what = f"{patches} code patch{'es' if patches != 1 else ''}"
        table[key] = f"{table[key]}, {what}" if key in table else what
    # Code patches for exact ROMs (source/irqpatch.c), keyed by code, revision
    # and header check.
    patches = Path("source/irqpatch.c").read_text()
    for m in re.finditer(
        r"\{\{'(.)', '(.)', '(.)', '(.)'\}, (0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+),",
        patches,
    ):
        key = "".join(m.groups()[:4]).encode()
        what = "exact-ROM patch"
        table[key] = f"{table[key]}, {what}" if key in table else what
    return table


def menu_cart(cart: Path, rom: Path, sd_name: str) -> None:
    make_cart(cart, rom, sd_name, None)
    nor = bytearray(b"\xff") * KERNEL_NOR_SIZE
    for slot, value in MENU_SETTINGS.items():
        struct.pack_into("<H", nor, SETTINGS + 2 * slot, value)
    (cart / "kernel-nor.bin").write_bytes(nor)


def check_menu(result: Result, rom: Path, sd_name: str, table: dict) -> None:
    """Launch with addons, open the in-game menu and leave it (hook_watch.lua)."""
    data = rom.read_bytes()
    found = irq_literals(data)
    code = data[0xAC:0xB0]
    in_table = table.get(code)
    result.metrics["IRQ literals"] = found
    cart = CARTS / f"{key_of(result.game)}-menu"
    menu_cart(cart, rom, sd_name)
    # Loading with patches takes about 90 frames per MB.
    frames = 400 + (len(data) >> 20) * 150 + 1800
    hashes = [a for f in range(30, frames + 1, 30) for a in ("-h", str(f))]
    out, trace = power_on(
        cart,
        launch_keys(sd_name, HOOK_ADDON, 200),
        frames,
        ["-L", HOOK_WATCH, "-u", "hook watch done", *hashes],
    )
    events = [
        (int(f), text)
        for f, text in re.findall(r"^script: hook (\d+) (.*)$", trace, re.MULTILINE)
    ]
    pat = sd_file(cart, f"PATCH/{sd_name.rsplit('.', 1)[0]}.pat")
    if pat and len(pat) >= 64:
        # Patch slots, then 16 words; the slot count is the 6th.
        patched = struct.unpack_from("<I", pat, len(pat) - 64 + 20)[0]
        result.metrics["hook from"] = f"ROM scan, {patched} patched"
    elif in_table is not None:
        result.metrics["hook from"] = in_table
    note = (
        f"the ROM has no {' or '.join(f'{v:#x}' for v in IRQ_LITERALS)} literal"
        if not found
        else f"{found} literals, over the kernel's {PATCH_SLOTS} patch slots"
        if found > PATCH_SLOTS
        else ""
    )

    def first(prefix: str) -> tuple[int, str] | None:
        return next(((f, t) for f, t in events if t.startswith(prefix)), None)

    crash = lines(trace, "crash at frame ")
    start = first("game ")
    if not start:
        result.set(
            "in-game menu",
            "fail",
            crash[0] if crash else "the addon launch never started the game",
        )
        return
    patch = first("patch ")
    if not patch:
        detail = "the kernel did not hook the game" + (f": {note}" if note else "")
        result.metrics["IRQ vector"] = "not hooked"
        result.set("in-game menu", "fail", detail)
        return
    base = int(patch[1].split()[1], 16)
    key_at = next((f for f, t in events if t == "key L+R+START"), None)
    vectors = [(f, int(t.split()[1], 16)) for f, t in events if t.startswith("vector ")]
    before = [
        (f, v) for f, v in vectors if f >= start[0] and (key_at is None or f <= key_at)
    ]
    # The first value is what the kernel left; the hook goes in after that,
    # and only a change after the hook counts as the game's.
    hooked_at = [f for f, v in before if base <= v < base + PATCH_SIZE]
    own = [
        (f, v)
        for f, v in before
        if hooked_at and f > hooked_at[0] and not base <= v < base + PATCH_SIZE
    ]
    vblank = first("vblank ")
    if vblank:
        on, total = (int(w) for w in vblank[1].split()[1::2])
        result.metrics["VBlank IRQ"] = f"on {on} of {total} frames"
    if not hooked_at:
        stayed = before[-1][1] if before else 0
        result.metrics["IRQ vector"] = f"never hooked, {stayed:08X}"
        unhooked = f"the hook was never seen at 0x3007FFC: it held {stayed:08X} from the game's first frame (the entry code failed, or the game overwrote it at once)"
    elif own and own[-1][0] > hooked_at[-1]:
        result.metrics["IRQ vector"] = f"the game's own, {own[-1][1]:08X}"
        unhooked = f"the game overwrote the hook at 0x3007FFC with {own[-1][1]:08X}, {own[-1][0] - start[0]} frames in"
    else:
        result.metrics["IRQ vector"] = "hooked"
        unhooked = ""
    opened = next((f for f, t in events if t == "menu open"), None)
    closed = next(
        (f for f, t in events if t == "menu closed" and opened and f > opened), None
    )
    if crash and (key_at is None or int(crash[0].split()[3].rstrip(":")) < key_at):
        why = f"crashed before the menu key: {crash[0]}"
        result.set(
            "in-game menu", "fail", f"{unhooked}, then {why}" if unhooked else why
        )
        return
    if opened is None:
        if unhooked:
            why = unhooked + (f"; {note}" if note else "")
        elif crash:
            why = f"crashed after the menu key: {crash[0]}"
        elif key_at is None or "hook watch done" not in trace:
            why = "the run ended before the menu was tried (the game started late)"
        elif vblank and vblank[1].startswith("vblank 0 "):
            # The menu opens from the VBlank IRQ; nothing the kernel can fix.
            result.set(
                "in-game menu",
                "n/a",
                "the game never enables the VBlank IRQ, which the menu needs",
            )
            return
        else:
            why = "the menu did not open"
        result.set("in-game menu", "fail", why)
        return
    if crash:
        result.set("in-game menu", "fail", f"crashed after the menu: {crash[0]}")
        return
    if closed is None:
        result.set("in-game menu", "fail", "B did not close the menu")
        return
    if any(t == "kernel back" for _, t in events):
        result.set(
            "in-game menu", "fail", "the game went back to the kernel after the menu"
        )
        return
    after = {h for f, h in screen_hashes(out) if f > closed + 30}
    if len(after) <= 1:
        result.set(
            "in-game menu",
            "fail",
            "the screen did not change after the menu, even with keys pressed",
        )
        return
    result.set(
        "in-game menu",
        "pass",
        f"opened {opened - key_at} frames after the key; the game went on ({len(after)} screens)",
    )


def run_game(result: Result, roms: dict, saves: dict, table: dict) -> None:
    g = result.game
    rom = roms.get(g["sha1"])
    if not rom:
        result.skipped = f"no file with this game's SHA-1 ({g['file']})"
        return
    result.metrics["size"] = rom.stat().st_size
    key = key_of(g)
    cart, shots_dir = CARTS / key, REPORT / "shots" / key
    shutil.rmtree(shots_dir, ignore_errors=True)
    shots_dir.mkdir(parents=True)
    system = g.get("system", "gba")
    sd_name = f"{re.sub(r'[^A-Za-z0-9]', '', g['code'])[:8] or 'GAME'}.{system}"
    start_save = (
        rom.with_suffix(".sav")
        if rom.with_suffix(".sav").exists()
        else saves.get(rom.stem)
    )
    key_file = KEYS / f"{g['code']}.keys"
    scripted = key_file.exists() and start_save is not None
    make_cart(cart, rom, sd_name, start_save)
    if start_save:
        result.notes.append(f"starts from {start_save.name}")
    for hw in g.get("hardware", []):
        result.notes.append(f"{hw} not emulated")
    size = SAVE_SIZE.get(g["save"])
    launch = launch_keys(sd_name, HOOK_CLEAN, 180 if system == "gba" else 120)
    started = time.time()

    # Power-on 1: launch, then play (a key script, or mashing).
    keys = read_keys(key_file) if scripted else mash()
    frames = max(keys[-1][0] + 60, RUN_FRAMES) if scripted else RUN_FRAMES
    shot_frames = (SHOTS[0], frames // 2, frames) if scripted else SHOTS
    shots = [shots_dir / f"{i + 1}.png" for i in range(len(shot_frames))]
    extra = [
        a for f, p in zip(shot_frames, shots, strict=True) for a in ("-s", f"{f}:{p}")
    ]
    extra += [a for f in range(60, frames + 1, 60) for a in ("-h", str(f))]
    out, trace = power_on(cart, launch + keys, frames, extra)
    result.shots = [str(p.relative_to(REPORT)) for p in shots if p.exists()]
    if not check_boot(result, trace, out, [p for p in shots if p.exists()]):
        result.metrics["seconds"] = round(time.time() - started, 1)
        return
    if system == "gba":
        check_menu(result, rom, sd_name, table)
    else:
        result.set("in-game menu", "n/a")
    if size is None:
        result.set("save cycle", "n/a")
        result.metrics["seconds"] = round(time.time() - started, 1)
        return

    loaded = sd_save(cart, sd_name)
    sram = cart / "sram.bin"
    first = sram.read_bytes()[:size]
    game_saved = loaded is None or first[: len(loaded)] != loaded[:size]
    if not game_saved:
        if scripted:
            result.notes.append("the key script didn't make this game save")
            scripted = False
        # The game didn't save, so test the kernel's copy path with a pattern
        # in the cart's save memory, as if the game had written it.
        # It changes a part of the loaded save (the kernel asks before
        # writing a chip that looks like another game's over a file)
        first = bytes((i * 37 + 11) & 0xFF for i in range(size))
        if loaded is not None:
            keep = bytearray(loaded[:size].ljust(size, b"\xff"))
            keep[:256] = first[:256]
            first = bytes(keep)
        sram.write_bytes(first + sram.read_bytes()[size:])

    # Power-on 2: the kernel asks to write the save to SD; confirm.
    power_on(cart, [(100, "A")], 800)
    written = sd_save(cart, sd_name)
    if written is None or written[:size] != first[: len(written)]:
        result.set(
            "save cycle", "fail", "the save folder does not hold what the game saved"
        )
        result.metrics["seconds"] = round(time.time() - started, 1)
        return

    # Power-on 3: launch again; the kernel must load the save back.
    if scripted:
        _, trace = power_on(cart, launch + keys, frames)
        second = (cart / "sram.bin").read_bytes()[:size]
        counters = [gen3_counter(s) for s in (loaded or b"", first, second)]
        if second == first:
            result.set("save cycle", "fail", "the game did not save after the relaunch")
        elif None not in counters and not counters[0] < counters[1] < counters[2]:
            result.set("save cycle", "fail", f"save counter {counters}, should go up")
        else:
            detail = (
                f"save counter {counters[0]}→{counters[1]}→{counters[2]}"
                if None not in counters
                else "key script saved twice"
            )
            result.set("save cycle", "pass", detail)
    else:
        _, trace = power_on(cart, launch, GAME_START + 20)
        # The cart logs a checksum of the save the kernel loaded as the game
        # takes over, before the game can change it.
        loads = re.findall(
            r"game save: mode \w+, (\d+) bytes, crc32 ([0-9A-F]{8})", trace
        )
        if loads:
            length, crc = int(loads[0][0]), int(loads[0][1], 16)
            came_back = zlib.crc32(written[:length]) == crc
        else:
            back = (cart / "sram.bin").read_bytes()[:size]
            came_back = back[: len(written)] == written[:size]
        if not came_back:
            result.set("save cycle", "fail", "the save was not loaded back on relaunch")
        else:
            what = "the game's save" if game_saved else "a test pattern"
            amount = (
                f"{len(written) // 1024} KB"
                if len(written) >= 1024
                else f"{len(written)} bytes"
            )
            result.set(
                "save cycle",
                "pass",
                f"{what} ({amount}) reached /torii/saves and came back",
            )
    result.metrics["seconds"] = round(time.time() - started, 1)


# --- Output --------------------------------------------------------------------------


def kernel_table() -> dict[str, int]:
    text = Path("source/saveMODE.h").read_text()
    return {
        c: int(m, 16) for c, m in re.findall(r'\{"(\w{4})",\s*(0x[0-9a-fA-F]+)\}', text)
    }


def size_class(size: int) -> str:
    for limit, name in ((4 << 20, "≤ 4 MB"), (8 << 20, "8 MB"), (16 << 20, "16 MB")):
        if size <= limit:
            return name
    return "32 MB"


# GB cartridge types (header byte 0x147) by mapper. `just compat` runs one
# game per mapper; it names the GB_MAPPERS_WANTED that roms/ has no game for.
GB_MAPPER = {
    **dict.fromkeys((0x00, 0x08, 0x09), "no mapper"),
    **dict.fromkeys((0x01, 0x02, 0x03), "MBC1"),
    **dict.fromkeys((0x05, 0x06), "MBC2"),
    **dict.fromkeys((0x0B, 0x0C, 0x0D), "MMM01"),
    **dict.fromkeys((0x0F, 0x10), "MBC3+clock"),
    **dict.fromkeys((0x11, 0x12, 0x13), "MBC3"),
    **dict.fromkeys((0x19, 0x1A, 0x1B), "MBC5"),
    **dict.fromkeys((0x1C, 0x1D, 0x1E), "MBC5+rumble"),
    0x20: "MBC6",
    0x22: "MBC7",
    0xFC: "Pocket Camera",
    0xFD: "TAMA5",
    0xFE: "HuC3",
    0xFF: "HuC1",
}
GB_MAPPERS_WANTED = ("MBC1", "MBC2", "MBC3+clock", "MBC5", "MBC5+rumble")


def gb_mapper(rom: Path | None) -> str:
    """The mapper a GB/GBC ROM's header names (cartridge type, byte 0x147)."""
    if rom is None:
        return "mapper unknown"
    with rom.open("rb") as f:
        header = f.read(0x148)
    if len(header) < 0x148:
        return "mapper unknown"
    return GB_MAPPER.get(header[0x147], f"cartridge type {header[0x147]:02X}")


def requirements(game: dict, rom: Path | None, table: dict) -> str:
    """What the game needs of the kernel; games with the same text are alike."""
    size = rom.stat().st_size if rom else 0
    parts = [game.get("system", "gba"), game["save"], size_class(size)]
    if game.get("system", "gba") != "gba":
        parts.append(gb_mapper(rom))
    if game.get("system", "gba") == "gba":
        mode = table.get(game["code"])
        parts.append(f"save mode {mode:02X}" if mode else "not in the save table")
        if game["save"].startswith("eeprom") and size > 0x1200000:
            parts.append("EEPROM over 18 MB")
    if game["rtc"]:
        parts.append("clock")
    parts += game.get("hardware", [])
    return " · ".join(parts)


def pick(games: list[dict], roms: dict, table: dict) -> list[tuple[dict, list[dict]]]:
    """One game per set of requirements, with the games it stands for.

    Copies of the same game (same system, code and title, e.g. one padded to
    32 MB) count once, before grouping by requirements."""

    def size(game: dict) -> int:
        rom = roms.get(game["sha1"])
        return rom.stat().st_size if rom else 0

    def preference(game: dict) -> tuple:
        rom = roms.get(game["sha1"])
        return (
            not (KEYS / f"{game['code']}.keys").exists(),
            not (rom and rom.with_suffix(".sav").exists()),
            rom is None,
            size(game),
            game["file"],
        )

    copies: dict[tuple, list[dict]] = {}
    for game in games:
        copies.setdefault(
            (game.get("system", "gba"), game["code"], game["title"]), []
        ).append(game)
    groups: dict[str, list[tuple[dict, list[dict]]]] = {}
    for same in copies.values():
        same.sort(key=preference)
        kept = same[0]
        groups.setdefault(requirements(kept, roms.get(kept["sha1"]), table), []).append(
            (kept, same[1:])
        )

    picked = []
    for group in groups.values():
        group.sort(key=lambda entry: preference(entry[0]))
        chosen, chosen_copies = group[0]
        alike = chosen_copies + [g for kept, extra in group[1:] for g in (kept, *extra)]
        picked.append((chosen, alike))
    return picked


def matches(game: dict, filters: list[str]) -> bool:
    if not filters:
        return True
    fields = [
        game["code"],
        game.get("system", "gba"),
        game["save"],
        game["file"],
        game["title"],
    ]
    system = game.get("system", "gba")
    return any(
        system == f[len("system=") :]
        if f.startswith("system=")
        else any(f.lower() in field.lower() for field in fields)
        for f in filters
    )


def print_summary(results: list[Result], run_file: Path) -> None:
    counts = {
        s: sum(r.status == s for r in results)
        for s in ("pass", "partial", "fail", "skip")
    }
    for r in results:
        if r.status in ("fail", "skip"):
            reasons = r.skipped or "; ".join(
                f"{c}: {d}" for c, (s, d) in r.checks.items() if s == "fail"
            )
            print(
                f"{r.status.upper():7} {r.game['code']:8} {r.game['file'][:48]:48} {reasons}"
            )
    print(
        f"{counts['pass']} passed, {counts['partial']} partly tested, {counts['fail']} failed, "
        f"{counts['skip']} skipped; results in {run_file}, report in {REPORT / 'index.html'}"
    )


def main() -> int:
    if not (ROMS / "games.toml").exists():
        print("no roms/games.toml yet: list your games with `just game-add PATH...`")
        return 1
    games = tomllib.loads((ROMS / "games.toml").read_text()).get("game", [])
    every = "--all" in sys.argv[1:]
    filters = [a for a in sys.argv[1:] if a != "--all"]
    roms, saves = rom_index(), save_index()
    table = kernel_table()
    hooks = hook_table()
    wanted = [g for g in games if not g.get("skip") and matches(g, filters)]
    chosen = [(g, []) for g in wanted] if every else pick(wanted, roms, table)
    results = []
    for game, alike in chosen:
        result = Result(game)
        rom = roms.get(game["sha1"])
        result.requirements = requirements(game, rom, table)
        result.alike = [g["file"] for g in alike]
        results.append(result)
    left_out = sum(1 for g in games if g.get("skip"))
    # One GB/GBC game per mapper: name the mappers roms/ has no game for.
    gb_games = [g for g in wanted if g.get("system", "gba") != "gba"]
    if gb_games and not every:
        have = {gb_mapper(roms.get(g["sha1"])) for g in gb_games}
        missing = [m for m in GB_MAPPERS_WANTED if m not in have]
        if missing:
            print(f"skipped GB mappers (no game in roms/): {', '.join(missing)}")

    def play(result: Result) -> None:
        key = key_of(result.game)
        carts = [CARTS / key, CARTS / f"{key}-menu"]
        for cart in carts:
            shutil.rmtree(KEPT / cart.name, ignore_errors=True)
        try:
            run_game(result, roms, saves, hooks)
        except Exception as e:  # noqa: BLE001 - one game going wrong must not stop the run
            for name in CHECKS:
                if result.checks[name][0] == "untested":
                    result.set(
                        name,
                        "fail",
                        str(e) if isinstance(e, Failure) else f"runner error: {e!r}",
                    )
                    break
        for cart in carts:
            if cart.exists() and result.status == "fail":
                shutil.copytree(cart, KEPT / cart.name)
            shutil.rmtree(cart, ignore_errors=True)
        print(
            f"{result.status:7} {result.game['file'][:60]}", file=sys.stderr, flush=True
        )

    # Each game has its own cart, so they run side by side.
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        list(pool.map(play, results))

    runs = REPORT / "runs"
    runs.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    run_file = runs / f"{stamp}.json"
    run_file.write_text(
        json.dumps(
            {
                "time": stamp,
                "filters": filters + (["--all"] if every else []),
                "listed": len(games),
                "left out": left_out,
                "kernel": hashlib.sha1(Path(KERNEL).read_bytes()).hexdigest()[:10],
                "games": [r.to_json() for r in results],
            }
        )
    )
    subprocess.run([sys.executable, "tests/compat_report.py"], check=True)
    print_summary(results, run_file)
    covered = len(results) + sum(len(r.alike) for r in results)
    print(
        f"{len(results)} games tested stand for {covered} of {len(games)} listed; {left_out} left out (skip)"
    )
    return 1 if any(r.status == "fail" for r in results) else 0


if __name__ == "__main__":
    sys.exit(main())
