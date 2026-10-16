#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Play the games in roms/games.toml through the kernel on the emulated cart.

usage: tests/games.py [CODE...]    (default: every listed game)

Each game gets a fresh cart in dist/cart-games/<code>. With a starting save
(roms/<name>.sav) and a key script (tests/games/<code>.keys) it runs the whole
cycle: launch and save in-game, power-cycle so the kernel writes the save to
SD, launch and save again. Without them it only launches the game. Checks:

- boots: the cart ends in game mode with a picture on screen
- save type: the kernel sent the save chip the game needs
- clock: a game that uses the clock finds it on
- save cycle: the game saved, the save reached SAVER/ unchanged, and the
  game loaded it back (it saved again, and for Pokémon generation 3 games
  the save counter went up each time)

Runs inside the torii-emu container after
`just build` and `just emu-headless`. Prints a table and writes the report,
with screenshots, to dist/compat/index.html.
"""

import base64
import hashlib
import html
import os
import shutil
import struct
import subprocess
import sys
import tomllib
import zlib
from pathlib import Path

KERNEL = "dist/ezkernelnew.bin"
SHOT = "emu/build-linux/mgba-shot"
ROMS = Path("roms")
KEYS = Path("tests/games")
CARTS = Path("dist/cart-games")
REPORT = Path("dist/compat")

SAVE_SIZE = {
    "sram32k": 0x8000,
    "eeprom512": 0x200,
    "eeprom8k": 0x2000,
    "flash64k": 0x10000,
    "flash128k": 0x20000,
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
# The SAVER folder sorts before the game, so: down, open, CLEAN BOOT.
LAUNCH = [(100, "DOWN"), (120, "A"), (180, "A")]
BOOT_FRAMES = 3000


class Failure(Exception):
    pass


def mtools(*args: str) -> bytes:
    return subprocess.run(args, check=True, capture_output=True).stdout


def make_cart(cart: Path, rom: Path, sd_name: str, start_save: Path | None) -> None:
    shutil.rmtree(cart, ignore_errors=True)
    cart.mkdir(parents=True)
    img = str(cart / "sd.img")
    size_kb = max(65536, (rom.stat().st_size >> 10) * 2 + 16384)
    mtools("mkfs.fat", "-C", "-F", "32", "-n", "GAMES", img, str(size_kb))
    mtools("mcopy", "-i", img, str(rom), f"::/{sd_name}")
    mtools("mmd", "-i", img, "::/SAVER")
    if start_save:
        mtools(
            "mcopy", "-i", img, str(start_save), f"::/SAVER/{Path(sd_name).stem}.sav"
        )


def power_on(cart: Path, keys: list[tuple[int, str]], frames: int, shot: Path) -> str:
    """Run the cart once from power-on; return the cart trace."""
    args = [SHOT, KERNEL, "-f", str(frames), "-s", f"{frames}:{shot}"]
    for frame, key in keys:
        args += ["-k", f"{frame}:{key}"]
        if key != "-":
            args += ["-k", f"{frame + 6}:-"]
    env = dict(os.environ, OMEGA_DIR=str(cart), OMEGA_TRACE="1")
    run = subprocess.run(args, env=env, capture_output=True, text=True, check=False)
    if run.returncode:
        raise Failure(f"mgba-shot failed: {run.stderr.strip()[-300:]}")
    return run.stderr


def read_keys(path: Path) -> list[tuple[int, str]]:
    keys = []
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0].split()
        if line:
            keys.append((int(line[0]), line[1]))
    return keys


def png_is_blank(path: Path) -> bool:
    """True if every pixel of an 8-bit RGB(A) PNG has the same colour."""
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
    pixels = set()
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
        pixels.update(bytes(line[i : i + 3]) for i in range(0, stride, channels))
        if len(pixels) > 1:
            return False
        prev = line
    return True


def gen3_counter(save: bytes) -> int | None:
    """Highest save counter in a Pokémon generation 3 save, if it is one."""
    counters = [
        struct.unpack_from("<I", save, off + 0xFFC)[0]
        for off in range(0, len(save), 0x1000)
        if len(save) >= off + 0x1000
        and struct.unpack_from("<I", save, off + 0xFF8)[0] == 0x08012025
    ]
    return max(counters) if counters else None


CHECKS = ["boots", "save type", "clock", "save cycle"]


class Result:
    """Outcome of each check for one game: pass, fail, n/a or not run."""

    def __init__(self, game: dict):
        self.game = game
        self.checks = {name: ("not run", "") for name in CHECKS}
        self.skipped = ""
        self.note = ""
        self.shots: list[Path] = []

    def set(self, check: str, status: str, detail: str = "") -> None:
        self.checks[check] = (status, detail)

    def fail_next(self, detail: str) -> None:
        """Fail the first check that has not run yet."""
        for name, (status, _) in self.checks.items():
            if status == "not run":
                self.set(name, "fail", detail)
                return

    @property
    def status(self) -> str:
        if self.skipped:
            return "skip"
        if any(status == "fail" for status, _ in self.checks.values()):
            return "fail"
        return "pass"


def launch_problems(trace: str, game: dict, shot: Path) -> dict[str, str]:
    """Problems with a launch, by check; an empty string means it passed."""
    problems = {"boots": "", "save type": "", "clock": ""}
    errors = [line for line in trace.splitlines() if line.startswith("kernel error: ")]
    if errors:
        problems["boots"] = errors[0]
        return problems
    launches = [line for line in trace.splitlines() if ": launch: " in line]
    if not launches:
        problems["boots"] = "the kernel never launched the game"
        return problems
    mode = int(launches[-1].split("save mode ")[1].split()[0], 16)
    sent = KERNEL_SAVE.get(mode, f"unknown ({mode:#x})")
    if game["save"] != "none" and sent != game["save"]:
        problems["save type"] = f"kernel sent {sent}, game needs {game['save']}"
    modes = [line for line in trace.splitlines() if ": mode " in line]
    if not modes or ": mode game" not in modes[-1]:
        problems["boots"] = "did not stay in game mode"
    elif png_is_blank(shot):
        problems["boots"] = "blank screen at the end"
    if game["rtc"]:
        if "game uses the clock, but" in trace:
            problems["clock"] = "the game uses the clock, but in-game RTC is off"
        elif "game uses the clock" not in trace:
            problems["clock"] = "the game never read the clock"
    return problems


def check_launch(result: Result, trace: str, shot: Path) -> bool:
    """Record the launch checks; True if they all passed."""
    problems = launch_problems(trace, result.game, shot)
    for check, problem in problems.items():
        if check == "clock" and not result.game["rtc"]:
            result.set(check, "n/a")
        else:
            result.set(check, "fail" if problem else "pass", problem)
    return not any(problems.values())


def sd_save(cart: Path, sd_name: str) -> bytes:
    return mtools(
        "mtype", "-i", str(cart / "sd.img"), f"::/SAVER/{Path(sd_name).stem}.sav"
    )


def run_game(result: Result) -> None:
    game = result.game
    rom = ROMS / game["file"]
    if not rom.exists():
        result.skipped = f"no {rom}"
        return
    if hashlib.sha1(rom.read_bytes()).hexdigest() != game["sha1"]:
        result.skipped = f"{rom} is a different dump than listed"
        return
    code = game["code"]
    cart, shots = CARTS / code, REPORT / code
    shutil.rmtree(shots, ignore_errors=True)
    shots.mkdir(parents=True)
    sd_name = f"{code}.gba"  # short name, so the menu row is predictable
    start_save = rom.with_suffix(".sav")
    key_file = KEYS / f"{code}.keys"
    full = start_save.exists() and key_file.exists()
    make_cart(cart, rom, sd_name, start_save if full else None)

    def play(name: str, keys: list[tuple[int, str]], frames: int) -> str:
        shot = shots / name
        trace = power_on(cart, keys, frames, shot)
        result.shots.append(shot)
        return trace

    if not full:
        trace = play("1-game.png", LAUNCH, BOOT_FRAMES)
        check_launch(result, trace, shots / "1-game.png")
        missing = [str(p) for p in (start_save, key_file) if not p.exists()]
        result.note = "launch only; no " + " or ".join(missing)
        return

    keys = read_keys(key_file)
    frames = keys[-1][0] + 60
    size = SAVE_SIZE[game["save"]]
    start = start_save.read_bytes()[:size]

    # Power-on 1: launch, continue, save in-game.
    trace = play("1-play.png", LAUNCH + keys, frames)
    if not check_launch(result, trace, shots / "1-play.png"):
        return
    first = (cart / "sram.bin").read_bytes()[:size]
    if first == start:
        raise Failure("the game did not save in-game (cart SRAM unchanged)")

    # Power-on 2: the kernel asks to write the save to SD; confirm.
    play("2-menu.png", [(100, "A")], 300)
    if sd_save(cart, sd_name) != first:
        raise Failure("SAVER/ does not hold what the game saved")

    # Power-on 3: launch again; the game must load that save and save again.
    trace = play("3-play.png", LAUNCH + keys, frames)
    problems = [
        p for p in launch_problems(trace, game, shots / "3-play.png").values() if p
    ]
    if problems:
        raise Failure("relaunch: " + "; ".join(problems))
    second = (cart / "sram.bin").read_bytes()[:size]
    if second == first:
        raise Failure("the game did not save after the relaunch")
    counters = [gen3_counter(s) for s in (start, first, second)]
    detail = ""
    if None not in counters:
        detail = f"save counter {counters[0]}→{counters[1]}→{counters[2]}"
        if not counters[0] < counters[1] < counters[2]:
            raise Failure(f"{detail}, should go up")
    result.set("save cycle", "pass", detail)


def print_table(results: list[Result]) -> None:
    rows = [["", "GAME", *(c.upper() for c in CHECKS)]]
    for r in results:
        name = f"{r.game['code']} {r.game['title']}"
        if r.skipped:
            rows.append(["SKIP", name, *("-" for _ in CHECKS)])
        else:
            rows.append([r.status.upper(), name, *(r.checks[c][0] for c in CHECKS)])
    widths = [max(len(row[i]) for row in rows) for i in range(len(rows[0]))]
    for row in rows:
        print("  ".join(cell.ljust(w) for cell, w in zip(row, widths)).rstrip())
    for r in results:
        details = [f"{c}: {d}" for c, (_, d) in r.checks.items() if d]
        details += [r.skipped] if r.skipped else []
        details += [r.note] if r.note else []
        for d in details:
            print(f"  {r.game['code']}  {d}")


def write_report(results: list[Result]) -> Path:
    rows = []
    for r in results:
        g = r.game
        cells = "".join(
            f'<td class="{"skip" if r.skipped else r.checks[c][0].replace(" ", "-")}"'
            f' title="{html.escape(r.checks[c][1])}">'
            f"{'-' if r.skipped else html.escape(r.checks[c][0])}"
            f"{'<br><small>' + html.escape(r.checks[c][1]) + '</small>' if r.checks[c][1] else ''}</td>"
            for c in CHECKS
        )
        shots = "".join(
            f'<figure><img src="data:image/png;base64,{base64.b64encode(s.read_bytes()).decode()}" width="240" height="160"'
            f' alt=""><figcaption>{html.escape(s.stem)}</figcaption></figure>'
            for s in r.shots
        )
        note = r.skipped or r.note
        rows.append(
            f'<tr><td class="{r.status}">{r.status.upper()}</td>'
            f"<td><b>{html.escape(g['code'])}</b> {html.escape(g['title'])}"
            f"<br><small>{html.escape(g['file'])}</small>"
            f"{'<br><small>' + html.escape(note) + '</small>' if note else ''}</td>"
            f'{cells}<td class="shots">{shots}</td></tr>'
        )
    header = "".join(f"<th>{c}</th>" for c in CHECKS)
    page = REPORT / "index.html"
    page.write_text(
        f"""<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Torii compatibility</title>
<style>
:root {{ color-scheme: light dark; --pass: #1a7f37; --fail: #cf222e; --muted: #6e7781; }}
body {{ font: 14px system-ui, sans-serif; margin: 16px; background: Canvas; color: CanvasText; }}
table {{ border-collapse: collapse; }}
th, td {{ border-bottom: 1px solid #8884; padding: 6px 10px; text-align: left; vertical-align: top; }}
.pass {{ color: var(--pass); font-weight: 600; }}
.fail {{ color: var(--fail); font-weight: 600; }}
.skip, .n\\/a, .not-run, small {{ color: var(--muted); font-weight: normal; }}
.shots {{ display: flex; gap: 8px; flex-wrap: wrap; }}
figure {{ margin: 0; }}
img {{ image-rendering: pixelated; display: block; }}
figcaption {{ font-size: 12px; color: var(--muted); }}
</style>
<h1>Torii compatibility</h1>
<p>{sum(r.status == "pass" for r in results)} passed, {sum(r.status == "fail" for r in results)} failed,
{sum(r.status == "skip" for r in results)} skipped. Made by <code>just compat</code>.</p>
<table>
<tr><th></th><th>Game</th>{header}<th>Screens</th></tr>
{"".join(rows)}
</table>
"""
    )
    return page


def main() -> int:
    games = tomllib.loads((ROMS / "games.toml").read_text()).get("game", [])
    wanted = set(sys.argv[1:])
    results = []
    for game in games:
        if wanted and game["code"] not in wanted:
            continue
        result = Result(game)
        print(f"{game['code']} {game['title']}...", file=sys.stderr, flush=True)
        try:
            run_game(result)
        except Failure as e:
            result.fail_next(str(e))
        results.append(result)
    print_table(results)
    page = write_report(results)
    print(
        f"{sum(r.status == 'pass' for r in results)} passed, "
        f"{sum(r.status == 'fail' for r in results)} failed, "
        f"{sum(r.status == 'skip' for r in results)} skipped; report in {page}"
    )
    return 1 if any(r.status == "fail" for r in results) else 0


if __name__ == "__main__":
    sys.exit(main())
