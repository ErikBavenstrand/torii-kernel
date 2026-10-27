#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Save states across scenes: a game played with seeded random keys, saved,
played on into other scenes (rooms, menus, battles), and loaded must go on
exactly as it went on after the save.

usage: tests/state_trips.py [--seed N] [--quick] [--save FILE] [--cheats FILE] ROM...

For each ROM, on a cart of its own with SAVESTATE on (tests/games.py's
in-game menu cart), launched with BOOT WITH ADDON, tests/state_trip.lua runs
three power-ons:

1. Random play, then round trips: Save state into slot 1 (a); play on;
   Load state, and Load state again at once (a twice); save b, play on
   longer, then load it at the next fade or transition; save c, Reset game,
   play the game's start again, and load c there; save d into slot 2, then
   Back to Torii, whose boot files slot 2 on the card.
2. The game launched into slot 2 (the game popup's Load state: the state put
   back from the card, loaded at the game's start): d's frames again; then
   save e into slot 3, and Back to Torii.
3. The game launched into slot 3: e again; then Load state in game, e again.

After every save the script replays keys the record's name seeds for K
frames, recording each frame's memory digest (EWRAM and IWRAM, the hook's
own words aside) and mgba-shot prints each frame's screen hash; after every
load it replays the same keys, and both must match frame for frame. --quick
runs power-on 1 alone. --cheats puts a cheat file (libretro format, its
picks on) on the card for the game; --save a starting save.

Prints one line per ROM, PASS or FAIL with the first difference. Runs
inside the torii-emu container after `just build` and `just emu-headless`.
tests/games.py calls trips() for its "state trips" check.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import games as G  # noqa: E402

LUA = "tests/state_trip.lua"
K = 120
NAV_TEST_STATE = 10  # source/screens/nav.h

RUN1 = (
    "play:900 save:1:a play:900 load:a load:a "
    "play:400 save:1:b play:1500 load:b:fade "
    "play:200 save:1:c reset play:900 load:c "
    "play:300 save:2:d play:300"
)
RUN2 = "autoload:d play:600 save:3:e play:300"
RUN3 = "autoload:e play:300 load:e"


def run(cart: Path, sd_name: str, steps: str, extra_env: dict, launch: list[str], frames: int):
    """One power-on with the trip script; (lines the script logged, frame -> screen hash, the log)."""
    # The clock stands still: a game reading it after the load reads what it
    # read after the save (the real clock runs on, as it should)
    args = [G.SHOT, G.KERNEL, "-C", "1793109780", "-f", str(frames), "-L", LUA, "-u", "trip done|trip torii>nav: home ready"]
    args += launch + ["-H", f"1:{frames}"] + os.environ.get("TRIP_ARGS", "").split()
    env = dict(os.environ, TZ="UTC", OMEGA_DIR=str(cart), OMEGA_TRACE="1", TRIP_STEPS=steps, TRIP_K=str(K), **extra_env)
    p = subprocess.run(args, env=env, capture_output=True, text=True, errors="replace", check=False)
    if p.returncode not in (0, 3):
        raise G.Failure(f"mgba-shot failed ({p.returncode}): {p.stderr.strip()[-300:]}")
    (cart / f"log-{len(list(cart.glob('log-*')))}.txt").write_text(p.stderr)
    hashes = {int(f): h for f, h in re.findall(r"^hash frame (\d+): (\S+)", p.stdout, re.MULTILINE)}
    lines = re.findall(r"^script: trip (.*)$", p.stderr, re.MULTILINE)
    return lines, hashes, p.stderr


def launch_hook(sd_name: str, action: int) -> list[str]:
    return ["-P", f"200:nav_test_path:/{sd_name}", "-p", f"200:nav_test_action:{0x7E000000 | action}"]


class Trips:
    """What the round trips found: one entry per load, and the run's notes."""

    def __init__(self) -> None:
        self.loads: list[tuple[str, str, str]] = []  # (name, "pass" | "fail", detail)
        self.notes: list[str] = []
        self.failed = ""  # a step that couldn't run
        self.saved: dict[str, tuple[int, dict]] = {}  # name -> (frame, that run's hashes)
        self.info: list[str] = []
        self.mixed: list[tuple[str, int]] = []  # loads that showed the state under other registers

    @property
    def status(self) -> str:
        if self.failed or self.mixed or any(s == "fail" for _, s, _ in self.loads):
            return "fail"
        return "pass" if self.loads else "untested"

    def detail(self) -> str:
        if self.failed:
            return self.failed
        bad = [f"{n}: {d}" for n, s, d in self.loads if s == "fail"]
        bad += [f"{n}: {m} frames of the load showed its VRAM under other display registers" for n, m in self.mixed]
        if bad:
            return "; ".join(bad)
        return f"{len(self.loads)} loads, each {K} frames as after its save ({', '.join(n for n, _, _ in self.loads)})"

    def take(self, lines: list[str], hashes: dict, log: str) -> None:
        crash = [line for line in log.splitlines() if line.startswith("crash at frame")]
        for line in lines:
            words = line.split()
            kind = words[0]
            if kind == "fail" and not self.failed:
                self.failed = " ".join(words[1:])
            elif kind == "saved":
                self.saved[words[1]] = (int(words[2]), hashes)
                self.info.append(f"{words[1]} saved: {' '.join(words[3:])}")
            elif kind == "loaded":
                name, at = words[1], int(words[2])
                self.info.append(f"{name} loaded: {' '.join(words[3:])}")
                self.loads.append((name, "pending", str(at)))
                if name in self.saved:
                    s_at, s_hashes = self.saved[name]
                    diff = next(
                        (
                            j
                            for j in range(K)
                            if s_at + j not in s_hashes or s_hashes.get(s_at + j) != hashes.get(at + j)
                        ),
                        None,
                    )
                    if diff is not None:
                        self.loads[-1] = (name, "fail", f"screen differs {diff} frames after the load")
                        continue
                self.loads[-1] = (name, "screen ok", "")
            elif kind == "mixed":
                self.mixed.append((words[1], int(words[2])))
            elif kind in ("same", "differs"):
                name = words[1]
                i = max(i for i, e in enumerate(self.loads) if e[0] == name)
                screen = self.loads[i]
                if kind == "differs":
                    j, where, count = words[2], words[3], words[4]
                    what = f"memory differs {j[1:]} frames after the load, at {where} ({count} KB blocks)"
                    if screen[1] == "fail":
                        what = f"{screen[2]}, {what}"
                    self.loads[i] = (name, "fail", what)
                elif screen[1] == "fail":
                    self.loads[i] = screen
                else:
                    self.loads[i] = (name, "pass", "")
        for i, (name, s, d) in enumerate(self.loads):
            if s not in ("pass", "fail"):
                self.loads[i] = (name, "fail", "the replay didn't finish")
        if crash and not self.failed:
            self.failed = crash[0]


def trip_cart(cart: Path, rom: Path, sd_name: str, save: Path | None, cheats: Path | None) -> None:
    G.menu_cart(cart, rom, sd_name)
    # and the save copied at power-on without asking (tests/states.sh)
    nor = bytearray((cart / "kernel-nor.bin").read_bytes())
    nor[G.SETTINGS + 28 : G.SETTINGS + 30] = (1).to_bytes(2, "little")
    (cart / "kernel-nor.bin").write_bytes(nor)
    img = str(cart / "sd.img")
    if save:
        for folder in ("torii", "torii/saves", f"torii/saves/{sd_name}"):
            subprocess.run(["mmd", "-i", img, f"::/{folder}"], check=False, capture_output=True)
        G.mtools("mcopy", "-o", "-i", img, str(save), f"::/{G.save_path(sd_name)}")
    if cheats:
        for folder in ("torii", "torii/cheats"):
            subprocess.run(["mmd", "-i", img, f"::/{folder}"], check=False, capture_output=True)
        G.mtools("mcopy", "-o", "-i", img, str(cheats), f"::/torii/cheats/{sd_name.rsplit('.', 1)[0]}.cht")


def trips(
    rom: Path,
    sd_name: str,
    cart: Path,
    seed: int = 1,
    quick: bool = False,
    save: Path | None = None,
    cheats: Path | None = None,
) -> Trips:
    t = Trips()
    trip_cart(cart, rom, sd_name, save, cheats)
    load_frames = 400 + (rom.stat().st_size >> 20) * 150
    records = cart / "trip-records.txt"
    env = {"TRIP_SEED": str(seed), "TRIP_OUT": str(records)}
    steps = RUN1 if quick else RUN1 + " torii"
    frames = load_frames + 16000 + (3000 if not quick else 0)
    lines, hashes, log = run(cart, sd_name, steps, env, launch_hook(sd_name, G.HOOK_ADDON), frames)
    t.take(lines, hashes, log)
    if quick or t.failed:
        return t
    if "states: /torii/states" not in log or "2.state" not in log:
        t.failed = "Back to Torii didn't file slot 2: " + "; ".join(
            line for line in log.splitlines() if "states:" in line
        )[-300:]
        return t
    for slot, steps, bufslot in ((2, RUN2 + " torii", 2), (3, RUN3, 3)):
        env = {
            "TRIP_SEED": str(seed),
            "TRIP_IN": str(records),
            "TRIP_OUT": str(cart / f"trip-records-{slot}.txt"),
            "TRIP_BUFSLOT": str(bufslot),
        }
        lines, hashes, log = run(
            cart,
            sd_name,
            steps,
            env,
            launch_hook(sd_name, NAV_TEST_STATE | slot << 8),
            load_frames + 3000 + 6000,
        )
        t.take(lines, hashes, log)
        if t.failed:
            t.failed = f"launch into slot {slot}: {t.failed}"
            return t
        if slot == 2:
            # the records the next run compares e with
            merged = records.read_text() + (cart / "trip-records-2.txt").read_text()
            records.write_text(merged)
    return t


def main() -> int:
    argv = sys.argv[1:]
    seed, quick, save, cheats, roms = 1, False, None, None, []
    while argv:
        a = argv.pop(0)
        if a == "--seed":
            seed = int(argv.pop(0))
        elif a == "--quick":
            quick = True
        elif a == "--save":
            save = Path(argv.pop(0))
        elif a == "--steps":
            global RUN1
            RUN1 = argv.pop(0)
        elif a == "--cheats":
            cheats = Path(argv.pop(0))
        else:
            roms.append(Path(a))
    failed = 0
    for rom in roms:
        cart = Path(os.environ.get("TRIP_CARTS") or Path(tempfile.gettempdir()) / "torii-trips") / re.sub(r"[^A-Za-z0-9]", "", rom.stem)[:20]
        shutil.rmtree(cart, ignore_errors=True)
        sd_name = "Game." + rom.suffix.lstrip(".")
        try:
            t = trips(rom, sd_name, cart, seed, quick, save, cheats)
        except G.Failure as e:
            print(f"FAIL: state trips {rom.name}: {e}")
            failed += 1
            continue
        if os.environ.get("TRIP_VERBOSE"):
            for line in t.info:
                print(f"  {line}")
        if t.status == "pass":
            print(f"PASS: state trips {rom.name}: {t.detail()}")
            shutil.rmtree(cart, ignore_errors=True)
        else:
            failed += 1
            print(f"FAIL: state trips {rom.name}: {t.detail()} (cart in {cart})")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
