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

Prints PASS, FAIL or SKIP per game. Runs inside the torii-emu container after
`just build` and `just emu-headless`; screenshots go to dist/shots/games.
"""

import hashlib
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
SHOTS = Path("dist/shots/games")

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


def check_launch(trace: str, game: dict, shot: Path) -> None:
    launches = [line for line in trace.splitlines() if ": launch: " in line]
    if not launches:
        raise Failure("the kernel never launched the game")
    mode = int(launches[-1].split("save mode ")[1].split()[0], 16)
    sent = KERNEL_SAVE.get(mode, f"unknown ({mode:#x})")
    if game["save"] != "none" and sent != game["save"]:
        raise Failure(f"save type: kernel sent {sent}, game needs {game['save']}")
    modes = [line for line in trace.splitlines() if ": mode " in line]
    if not modes or ": mode game" not in modes[-1]:
        raise Failure("did not stay in game mode")
    if png_is_blank(shot):
        raise Failure(f"blank screen at the end ({shot})")
    if game["rtc"]:
        if "game uses the clock, but" in trace:
            raise Failure("the game uses the clock, but in-game RTC is off")
        if "game uses the clock" not in trace:
            raise Failure("the game never read the clock")


def sd_save(cart: Path, sd_name: str) -> bytes:
    return mtools(
        "mtype", "-i", str(cart / "sd.img"), f"::/SAVER/{Path(sd_name).stem}.sav"
    )


def run_game(game: dict) -> str:
    rom = ROMS / game["file"]
    if not rom.exists():
        return f"SKIP {game['code']}: no {rom}"
    if hashlib.sha1(rom.read_bytes()).hexdigest() != game["sha1"]:
        return f"SKIP {game['code']}: {rom} is a different dump than listed"
    code = game["code"]
    cart, shots = CARTS / code, SHOTS / code
    shutil.rmtree(shots, ignore_errors=True)
    shots.mkdir(parents=True)
    sd_name = f"{code}.gba"  # short name, so the menu row is predictable
    start_save = rom.with_suffix(".sav")
    key_file = KEYS / f"{code}.keys"
    full = start_save.exists() and key_file.exists()
    make_cart(cart, rom, sd_name, start_save if full else None)
    checks = ["boots", f"save type {game['save']}"] + (["clock"] if game["rtc"] else [])

    if not full:
        trace = power_on(cart, LAUNCH, BOOT_FRAMES, shots / "1-game.png")
        check_launch(trace, game, shots / "1-game.png")
        return f"PASS {code} {game['title']}: {', '.join(checks)} (no starting save or keys: launch only)"

    keys = read_keys(key_file)
    frames = keys[-1][0] + 60
    size = SAVE_SIZE[game["save"]]
    start = start_save.read_bytes()[:size]

    # Power-on 1: launch, continue, save in-game.
    trace = power_on(cart, LAUNCH + keys, frames, shots / "1-play.png")
    check_launch(trace, game, shots / "1-play.png")
    first = (cart / "sram.bin").read_bytes()[:size]
    if first == start:
        raise Failure("the game did not save in-game (cart SRAM unchanged)")

    # Power-on 2: the kernel asks to write the save to SD; confirm.
    power_on(cart, [(100, "A")], 300, shots / "2-menu.png")
    if sd_save(cart, sd_name) != first:
        raise Failure("SAVER/ does not hold what the game saved")

    # Power-on 3: launch again; the game must load that save and save again.
    trace = power_on(cart, LAUNCH + keys, frames, shots / "3-play.png")
    check_launch(trace, game, shots / "3-play.png")
    second = (cart / "sram.bin").read_bytes()[:size]
    if second == first:
        raise Failure("the game did not save after the relaunch")
    counters = [gen3_counter(s) for s in (start, first, second)]
    if None not in counters:
        if not counters[0] < counters[1] < counters[2]:
            raise Failure(f"save counter did not go up: {counters}")
        checks.append(f"save counter {counters[0]}→{counters[1]}→{counters[2]}")
    checks.append("save cycle")
    return f"PASS {code} {game['title']}: {', '.join(checks)}"


def main() -> int:
    games = tomllib.loads((ROMS / "games.toml").read_text()).get("game", [])
    wanted = set(sys.argv[1:])
    failed = False
    for game in games:
        if wanted and game["code"] not in wanted:
            continue
        try:
            print(run_game(game), flush=True)
        except Failure as e:
            print(f"FAIL {game['code']} {game['title']}: {e}", flush=True)
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
