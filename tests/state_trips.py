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
import games as G

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


def run(
    cart: Path,
    sd_name: str,
    steps: str,
    extra_env: dict,
    launch: list[str],
    frames: int,
):
    """One power-on with the trip script; (lines the script logged, frame -> screen hash, the log)."""
    # The clock stands still: a game reading it after the load reads what it
    # read after the save (the real clock runs on, as it should)
    args = [
        G.SHOT,
        G.KERNEL,
        "-C",
        "1793109780",
        "-f",
        str(frames),
        "-L",
        LUA,
        "-u",
        "trip done|trip torii>nav: home ready",
    ]
    args += launch + ["-H", f"1:{frames}"] + os.environ.get("TRIP_ARGS", "").split()
    env = dict(
        os.environ,
        TZ="UTC",
        OMEGA_DIR=str(cart),
        OMEGA_TRACE="1",
        TRIP_STEPS=steps,
        TRIP_K=str(K),
        **extra_env,
    )
    p = subprocess.run(
        args, env=env, capture_output=True, text=True, errors="replace", check=False
    )
    if p.returncode not in (0, 3):
        raise G.Failure(f"mgba-shot failed ({p.returncode}): {p.stderr.strip()[-300:]}")
    (cart / f"log-{len(list(cart.glob('log-*')))}.txt").write_text(p.stderr)
    hashes = {
        int(f): h
        for f, h in re.findall(r"^hash frame (\d+): (\S+)", p.stdout, re.MULTILINE)
    }
    lines = re.findall(r"^script: trip (.*)$", p.stderr, re.MULTILINE)
    return lines, hashes, p.stderr


def launch_hook(sd_name: str, action: int) -> list[str]:
    return [
        "-P",
        f"200:nav_test_path:/{sd_name}",
        "-p",
        f"200:nav_test_action:{0x7E000000 | action}",
    ]


class Load:
    """One load's replay against its save's: the first frame whose screen
    differs and the first whose memory digest does (None: all K matched)."""

    def __init__(self, name: str) -> None:
        self.name = name
        self.screen: int | None = None
        self.memory: int | None = None
        self.where = ""
        self.screen_frames = 0
        self.video: int | None = (
            None  # the first frame VRAM, palette, OAM or a readable display register differ
        )
        self.replayed = False

    @property
    def kind(self) -> str:
        """exact; drift: the memory went otherwise first and the screen, if at
        all, only later (the game's timing after the load: a sound DMA's place,
        which no load can give back); display: the screen went otherwise while
        the memory and everything of the display a CPU can read were the
        save's (write-only display registers, scroll, windows, BLDY, that the
        game keeps no copy of: no load can give them back); fail: otherwise
        (the picture's memory or the game went otherwise first, or the replay
        didn't run)."""
        if not self.replayed:
            return "fail"
        if self.screen is None and self.memory is None and self.video is None:
            return "exact"
        first = min(x for x in (self.screen, self.memory, self.video) if x is not None)
        if self.memory == first and (self.video is None or self.video >= first):
            return "drift"
        if self.screen is None and self.memory is None:
            return "drift"  # VRAM or OAM the screen doesn't show went otherwise
        if self.video == first and self.screen_frames <= K // 4:
            # VRAM first, the screen hardly: a renderer drawing over frames
            # (mode 4's page flips in Need for Speed) got as far as its
            # timing took it by each frame's end
            return "drift"
        if self.screen == first and self.memory != first and self.video != first:
            return "display"
        return "fail"

    def text(self) -> str:
        if not self.replayed:
            return f"{self.name}: the replay didn't finish"
        parts = []
        if self.screen is not None:
            parts.append(
                f"screen differs from {self.screen} frames after the load ({self.screen_frames} of {K})"
            )
        if self.video is not None:
            parts.append(f"VRAM, palette, OAM or display registers from {self.video}")
        if self.memory is not None:
            parts.append(f"memory {self.memory} frames after, at {self.where}")
        return f"{self.name}: {', '.join(parts)}"


class Trips:
    """What the round trips found: one entry per load, and the run's notes."""

    def __init__(self) -> None:
        self.loads: list[Load] = []
        self.failed = ""  # a step that couldn't run
        self.saved: dict[
            str, tuple[int, dict]
        ] = {}  # name -> (frame, that run's hashes)
        self.info: list[str] = []
        self.mixed: list[
            tuple[str, int]
        ] = []  # loads that showed the state under other registers

    @property
    def status(self) -> str:
        """pass, fail, or untested: the game put its own IRQ handler over the
        hook (the in-game menu's limit, not the states'), the loads before
        it all right"""
        bad = self.mixed or any(load.kind == "fail" for load in self.loads)
        if "unhooked:" in self.failed and not bad:
            return "untested"
        if self.failed or bad:
            return "fail"
        return "pass" if self.loads else "untested"

    def counts(self) -> dict:
        kinds = [load.kind for load in self.loads]
        return {k: kinds.count(k) for k in ("exact", "drift", "display", "fail")}

    def detail(self) -> str:
        bad = [self.failed] if self.failed else []
        bad += [load.text() for load in self.loads if load.kind == "fail"]
        bad += [
            f"{n}: {m} frames of the load showed its VRAM under other display registers"
            for n, m in self.mixed
        ]
        if bad and self.status == "fail":
            return "; ".join(bad)
        if bad:  # unhooked: what ran before
            return f"{bad[0]}; before it: {self.summary()}"
        return self.summary()

    def summary(self) -> str:
        c = self.counts()
        text = f"{len(self.loads)} loads, {c['exact']} frame for frame as after the save for {K} frames"
        drift = [load.text() for load in self.loads if load.kind == "drift"]
        if drift:
            text += f"; {c['drift']} went otherwise in memory first (timing: {'; '.join(drift)})"
        display = [load.text() for load in self.loads if load.kind == "display"]
        if display:
            text = (
                f"write-only display registers the game keeps no copy of: {c['display']} loads showed "
                f"otherwise ({'; '.join(display)}); {text}"
            )
        return text

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
                load = Load(name)
                self.loads.append(load)
                if name in self.saved:
                    s_at, s_hashes = self.saved[name]
                    differ = [
                        j
                        for j in range(K)
                        if s_at + j not in s_hashes
                        or s_hashes.get(s_at + j) != hashes.get(at + j)
                    ]
                    load.screen = differ[0] if differ else None
                    load.screen_frames = len(differ)
            elif kind == "mixed":
                self.mixed.append((words[1], int(words[2])))
            elif kind == "vdiffers":
                load = next(
                    load for load in reversed(self.loads) if load.name == words[1]
                )
                load.video = int(words[2][1:])
            elif kind in ("same", "differs"):
                load = next(
                    load for load in reversed(self.loads) if load.name == words[1]
                )
                load.replayed = True
                if kind == "differs":
                    load.memory = int(words[2][1:])
                    load.where = f"{words[3]} ({words[4]} KB blocks)"
        if crash and not self.failed:
            self.failed = crash[0]


def trip_cart(
    cart: Path, rom: Path, sd_name: str, save: Path | None, cheats: Path | None
) -> None:
    G.menu_cart(cart, rom, sd_name)
    # and the save copied at power-on without asking (tests/states.sh)
    nor = bytearray((cart / "kernel-nor.bin").read_bytes())
    nor[G.SETTINGS + 28 : G.SETTINGS + 30] = (1).to_bytes(2, "little")
    (cart / "kernel-nor.bin").write_bytes(nor)
    img = str(cart / "sd.img")
    if save:
        for folder in ("torii", "torii/saves", f"torii/saves/{sd_name}"):
            subprocess.run(
                ["mmd", "-i", img, f"::/{folder}"], check=False, capture_output=True
            )
        G.mtools("mcopy", "-o", "-i", img, str(save), f"::/{G.save_path(sd_name)}")
    if cheats:
        for folder in ("torii", "torii/cheats"):
            subprocess.run(
                ["mmd", "-i", img, f"::/{folder}"], check=False, capture_output=True
            )
        G.mtools(
            "mcopy",
            "-o",
            "-i",
            img,
            str(cheats),
            f"::/torii/cheats/{sd_name.rsplit('.', 1)[0]}.cht",
        )


def trips(
    rom: Path,
    sd_name: str,
    cart: Path,
    seed: int = 1,
    quick: bool = False,
    save: Path | None = None,
    cheats: Path | None = None,
    run1: str = RUN1,
) -> Trips:
    t = Trips()
    trip_cart(cart, rom, sd_name, save, cheats)
    load_frames = 400 + (rom.stat().st_size >> 20) * 150
    records = cart / "trip-records.txt"
    env = {"TRIP_SEED": str(seed), "TRIP_OUT": str(records)}
    steps = run1 if quick else run1 + " torii"
    frames = load_frames + 16000 + (3000 if not quick else 0)
    lines, hashes, log = run(
        cart, sd_name, steps, env, launch_hook(sd_name, G.HOOK_ADDON), frames
    )
    t.take(lines, hashes, log)
    if quick or t.failed:
        return t
    if "states: /torii/states" not in log or "2.state" not in log:
        t.failed = (
            "Back to Torii didn't file slot 2: "
            + "; ".join(line for line in log.splitlines() if "states:" in line)[-300:]
        )
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


INFO = 0x6FF80  # INGAME_SRAM_STATE_INFO in the buffer
SPEND = 0x68400  # the stub's ST_SPEND
VECTOR = 0x47FFC  # IWRAM's 0x3007FFC


def refuse(rom: Path, other: Path, cart: Path) -> list[str]:
    """Load's checks of a state from the card: what failed (none: all held).

    Slot 2 saved in game and filed, then launched into: with another ROM
    (other) in the game's place; from another path's folder (another
    rom_crc); with its format made newer; and made as v0.8.0 left one (no
    info, the IRQ vector at another launch's hook, no Supervisor registers),
    which must load as it was saved."""
    import statefile

    bad = []
    sd_name = "Game.gba"
    trip_cart(cart, rom, sd_name, None, None)
    img = str(cart / "sd.img")
    records = cart / "trip-records.txt"
    env = {"TRIP_OUT": str(records)}
    steps = "play:300 save:2:d play:120 torii"
    lines, _, log = run(
        cart, sd_name, steps, env, launch_hook(sd_name, G.HOOK_ADDON), 6000
    )
    if "2.state" not in log:
        return [f"slot 2 wasn't filed: {lines}"]
    state = cart / "2.state"
    G.mtools(
        "mcopy", "-o", "-i", img, "::/torii/states/Game.gba/Main/2.state", str(state)
    )
    raw = bytearray(statefile.unpack(str(state)))
    # every launch's patch area is at the same place: the info's hook
    hook = int.from_bytes(raw[INFO + 20 : INFO + 24], "little")

    def refused(path: str, why: str, case: str) -> None:
        # The question the launch asks, B: not started
        extra = [
            "-w",
            "screen ready: dialog",
            "-k",
            "@12:B",
            "-k",
            "@16:-",
            "-u",
            "not started",
        ]
        _, _, log = run(
            cart, path, "", {}, launch_hook(path, NAV_TEST_STATE | 2 << 8) + extra, 4000
        )
        if (
            f"is {why}" not in log
            or "not started" not in log
            or "launch: starting the game" in log
        ):
            found = [
                line
                for line in log.splitlines()
                if "states:" in line or "launch:" in line
            ]
            bad.append(f"{case}: not refused as {why}: {found[-3:]}")

    def put(data: bytes) -> None:
        state.write_bytes(data)
        G.mtools(
            "mcopy",
            "-o",
            "-i",
            img,
            str(state),
            "::/torii/states/Game.gba/Main/2.state",
        )
        subprocess.run(
            ["mdel", "-i", img, "::/torii/states/Game.gba/Main/2.state.bak"],
            capture_output=True,
            check=False,
        )

    original = state.read_bytes()
    # Another ROM at the game's path: its rom_crc is the same, its header not
    G.mtools("mcopy", "-o", "-i", img, str(other), f"::/{sd_name}")
    refused(sd_name, "another game's", "another ROM in its place")
    G.mtools("mcopy", "-o", "-i", img, str(rom), f"::/{sd_name}")
    # The same ROM at another path, the state copied to that path's folder
    G.mtools("mcopy", "-o", "-i", img, str(rom), "::/Copy.gba")
    for folder in ("torii/states/Copy.gba", "torii/states/Copy.gba/Main"):
        subprocess.run(
            ["mmd", "-i", img, f"::/{folder}"], capture_output=True, check=False
        )
    G.mtools(
        "mcopy", "-o", "-i", img, str(state), "::/torii/states/Copy.gba/Main/2.state"
    )
    refused("Copy.gba", "another game's", "another path's state")
    # A newer format
    newer = bytearray(raw)
    newer[INFO + 4 : INFO + 8] = (99 | 32 << 16).to_bytes(4, "little")
    put(statefile.pack(str(state), bytes(newer)))
    refused(sd_name, "of a newer format", "a newer format")
    # As v0.8.0 left it: loads, into the frames that followed the save
    old = bytearray(raw)
    old[INFO : INFO + 4] = bytes(4)
    old[VECTOR : VECTOR + 4] = (hook - 0x2000).to_bytes(4, "little")
    old[SPEND + 0x60 : SPEND + 0x64] = (0x31564F54).to_bytes(
        4, "little"
    )  # the overlay's header
    put(statefile.pack(str(state), bytes(old)))
    lines, _, log = run(
        cart,
        sd_name,
        "autoload:d",
        {"TRIP_IN": str(records), "TRIP_BUFSLOT": "2"},
        launch_hook(sd_name, NAV_TEST_STATE | 2 << 8),
        4000,
    )
    if "same d 120" not in lines:
        bad.append(f"a state as v0.8.0 left one didn't load as saved: {lines}")
    put(original)
    return bad


def main() -> int:
    argv = sys.argv[1:]
    seed, quick, save, cheats, roms, run1 = 1, False, None, None, [], RUN1
    while argv:
        a = argv.pop(0)
        if a == "--refuse":
            other = Path(argv.pop(0))
            rom = Path(argv.pop(0))
            cart = (
                Path(
                    os.environ.get("TRIP_CARTS")
                    or Path(tempfile.gettempdir()) / "torii-trips"
                )
                / "refuse"
            )
            shutil.rmtree(cart, ignore_errors=True)
            bad = refuse(rom, other, cart)
            for b in bad:
                print(f"FAIL: state refusals: {b} (cart in {cart})")
            if not bad:
                print(
                    "PASS: state refusals: a state of another ROM, path or newer format is refused; "
                    "one as v0.8.0 left it loads"
                )
                shutil.rmtree(cart, ignore_errors=True)
            return 1 if bad else 0
        if a == "--seed":
            seed = int(argv.pop(0))
        elif a == "--quick":
            quick = True
        elif a == "--save":
            save = Path(argv.pop(0))
        elif a == "--steps":
            run1 = argv.pop(0)
        elif a == "--cheats":
            cheats = Path(argv.pop(0))
        else:
            roms.append(Path(a))
    failed = 0
    for rom in roms:
        cart = (
            Path(
                os.environ.get("TRIP_CARTS")
                or Path(tempfile.gettempdir()) / "torii-trips"
            )
            / re.sub(r"[^A-Za-z0-9]", "", rom.stem)[:20]
        )
        shutil.rmtree(cart, ignore_errors=True)
        sd_name = "Game." + rom.suffix.lstrip(".")
        try:
            t = trips(rom, sd_name, cart, seed, quick, save, cheats, run1)
        except G.Failure as e:
            print(f"FAIL: state trips {rom.name}: {e}")
            failed += 1
            continue
        if os.environ.get("TRIP_VERBOSE"):
            for line in t.info:
                print(f"  {line}")
        if t.status != "fail":
            print(
                f"{'PASS' if t.status == 'pass' else 'SKIP'}: state trips {rom.name}: {t.detail()}"
            )
            if not os.environ.get("TRIP_KEEP"):
                shutil.rmtree(cart, ignore_errors=True)
        else:
            failed += 1
            print(f"FAIL: state trips {rom.name}: {t.detail()} (cart in {cart})")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
