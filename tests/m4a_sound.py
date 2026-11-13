#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Sound after the in-game menu, SAVE and LOAD on games with Nintendo's MP2000
(m4a, "Sappy") sound engine.

usage: tests/m4a_sound.py [--steps STEPS] [--verbose] ROM...

The engine mixes each frame's samples into one segment of its PCM buffer
(SoundInfo.pcmBuffer: pcmDmaPeriod segments of pcmSamplesPerVBlank bytes)
while DMA1 (FIFO A; DMA2, FIFO B, reads PCM_DMA_BUF_SIZE further on) plays
the one before, and re-arms both at the buffer's start when its
pcmDmaCounter runs out (m4aSoundVSync). At each frame's end mgba-shot's -a
prints DMA1's internal source, what FIFO A holds and the counter C; in every
frame the game plays on its own, FIFO A then plays PCMBUFFER + (PERIOD - C) *
SAMPLES plus the same few samples (its lead; a game calling the VSync later in
its frame has a segment more). The stub leaves the engine's DMAs off after the
menu's Resume, a SAVE and a LOAD, for the engine to re-arm (m4a_hold): each
frame within PERIOD + 1 frames after it must be silent (DMA1 off) or play
with the game's own lead, give or take SLACK samples (the timers' phase,
which SAVE's measuring and LOAD restart, moves the frame's end in the
stream; a wrong segment is 96 samples off or more), and DMA1 must be on
again by the window's end. Before m4a_hold, the stub turned them on at the
buffer's start: up to PERIOD - 1 frames of the wrong segments.

A game whose own lead varies by more than a segment (an engine calling the
VSync from its main loop, or Camelot's, with its buffer elsewhere) is only
checked for its DMAs coming back.

For each ROM, on a trip cart (tests/state_trips.py), tests/state_trip.lua
plays, saves, loads and resumes (STEPS). A ROM whose word at 0x3007FF0 doesn't
point at a SoundInfo before its first menu isn't on the engine: "not m4a",
and m4a_hold must leave it alone (its sound DMAs on again at once).

tests/m4a_sound.sh runs it on tests/roms/m4a (the engine's DMA as pret's
decomps have it) and its copy with another ident; tests/state_trips.py
judges its first power-on's saves and loads with judge() on every game
`just compat` plays.

Runs inside the torii-emu container after `just build` and `just
emu-headless`. Prints one line per ROM: PASS, or FAIL with the wrong frames.
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
import state_trips as T

PCM_BUF = 1584  # PCM_DMA_BUF_SIZE
PCM_AT = 0x350  # SoundInfo.pcmBuffer
SLACK = 32  # samples a frame's end may move in the stream: the timers' phase
# Saves, loads and resumes at different counters: each after another
# stretch of play
STEPS = (
    "play:900 save:1:a play:301 load:a play:157 load:a "
    "play:211 save:1:b play:173 load:b play:97 resume play:113 "
    "save:1:c play:263 load:c play:300"
)


def parse(stdout: str) -> dict:
    """mgba-shot -a's lines: frame -> (the byte FIFO A plays, DMA1's CNT_H,
    whether DMA2 is a buffer on from DMA1, the m4a fields or None)."""
    sound = {}
    for m in re.finditer(
        r"^sound frame (\d+): dma1 (\S+) (\S+) dma2 (\S+) (\S+) fifo (\d+)"
        r"(?: m4a (\S+) (\d+) (\d+) (\d+))?$",
        stdout,
        re.MULTILINE,
    ):
        m4a = (int(m[7], 16), int(m[8]), int(m[9]), int(m[10])) if m[7] else None
        d1, c1, d2, c2 = (int(m[i], 16) for i in range(2, 6))
        # DMA2 a buffer on from DMA1, when it plays (not in a mono game)
        stereo_ok = c2 & 0xB200 != 0xB200 or d2 - d1 == PCM_BUF
        sound[int(m[1])] = (d1 - int(m[6]), c1, stereo_ok, m4a)
    return sound


def measure(rom: Path, cart: Path, steps: str) -> tuple[list[str], dict]:
    """One power-on: (the trip lines, parse()'s frames)."""
    sd_name = "Game." + rom.suffix.lstrip(".")
    T.trip_cart(cart, rom, sd_name, None, None)
    frames = 400 + (rom.stat().st_size >> 20) * 150 + 9000
    args = [G.SHOT, G.KERNEL, "-C", "1793109780", "-f", str(frames), "-L", T.LUA]
    args += ["-u", "trip done", "-a", f"1:{frames}"]
    args += T.launch_hook(sd_name, G.HOOK_ADDON)
    env = dict(os.environ, TZ="UTC", OMEGA_DIR=str(cart), TRIP_STEPS=steps, TRIP_K="1")
    p = subprocess.run(
        args, env=env, capture_output=True, text=True, errors="replace", check=False
    )
    if p.returncode not in (0, 3):
        raise G.Failure(f"mgba-shot failed ({p.returncode}): {p.stderr.strip()[-300:]}")
    (cart / "log.txt").write_text(p.stderr)
    lines = re.findall(r"^script: trip (.*)$", p.stderr, re.MULTILINE)
    if "done" not in lines and not any(line.startswith("fail") for line in lines):
        raise G.Failure(f"the trip didn't finish: {lines[-3:]}")
    return lines, parse(p.stdout)


def lead(entry) -> int | None:
    """Where FIFO A plays (DMA1's source less what the FIFO holds) less the
    start of the segment the engine has it play; None without a sane
    SoundInfo or with the FIFO DMAs off."""
    d1, c1, _, m4a = entry
    if not m4a or c1 & 0xB200 != 0xB200:
        return None
    ptr, count, period, samples = m4a
    if not (1 <= count <= period and period * samples <= PCM_BUF):
        return None
    return d1 - (ptr + PCM_AT + (period - count) * samples)


def not_m4a(events: list, menus: dict[int, int], sound: dict) -> tuple[bool, str]:
    """A game not on the engine: the stub turns its sound FIFO DMAs on again
    at once, as before m4a_hold. DMA1 on its FIFO as the stub found it (menus:
    the frame the menu showed -> DMA1's CNT_H then; for a load, its save's)
    must be on in the event's frame."""
    held = []
    saved = {name: at for kind, name, at in events if kind == "saved"}
    for kind, name, at in events:
        when = saved.get(name, at) if kind == "loaded" else at
        shown = max((m for m in menus if m < when), default=None)
        if shown is None or at not in sound:
            continue
        if menus[shown] & 0xB200 == 0xB200 and not sound[at][1] & 0x8000:
            held.append(f"{kind} {name}@{at}".replace(" @", "@"))
    if held:
        return False, f"not m4a, but its sound DMA was left off after {', '.join(held)}"
    return True, f"not m4a ({len(events)} saves, loads and resumes)"


def judge(lines: list[str], sound: dict, verbose: bool) -> tuple[bool, str]:
    words = [line.split() for line in lines]
    fails = [line for line in lines if line.startswith("fail")]
    if fails:
        return False, fails[0]
    events = [(w[0], w[1], int(w[2])) for w in words if w[0] in ("saved", "loaded")]
    events += [("resumed", "", int(w[1])) for w in words if w[0] == "resumed"]
    if not events:
        return False, f"no save, load or resume: {lines[-3:]}"
    # The game's own lead: its last 450 frames before the first menu (the
    # menu's own frames have the DMAs off); none there, it isn't on the engine
    # there (Mario & Luigi's engine isn't; its intro may be)
    first = min(at for _, _, at in events)
    own = [
        x
        for f in sorted(sound)
        if first - 450 < f < first and (x := lead(sound[f])) is not None
    ]
    if not own:
        menus = {int(w[1]): int(w[2], 16) for w in words if w[0] == "menu"}
        return not_m4a(events, menus, sound)
    m4a = [f for f, e in sound.items() if e[3]]
    lo, hi = min(own), max(own)
    steady = hi - lo <= SLACK
    ptr, _, period, samples = sound[m4a[-1]][3]
    bad_total, parts = 0, []
    for kind, name, at in sorted(events, key=lambda e: e[2]):
        bad, seen, on = [], [], False
        for f in range(at, at + period + 2):
            e = sound.get(f)
            on = bool(e and e[1] & 0x8000)
            x = lead(e) if on else None
            seen.append(x if on else "off")
            # (a counter of 0: the frame ended inside the engine's VSync,
            # between its re-arm and the counter's reload, Prince of Persia)
            if (
                on
                and steady
                and e[3]
                and e[3][1]
                and (x is None or not lo - SLACK <= x <= hi + SLACK or not e[2])
            ):
                bad.append(f - at)
        if not on:
            bad.append("never on")
        bad_total += len(bad)
        silent = seen.count("off")
        part = f"{kind} {name}@{at}: {silent} silent, {len(bad)} wrong".replace(
            " @", "@"
        )
        if verbose or bad:
            part += f" (leads {seen})"
        parts.append(part)
    own_text = (
        f"lead {lo}..{hi}"
        if steady
        else f"its own lead varies, {lo}..{hi}: checked the DMAs come back"
    )
    text = f"m4a at {ptr:08X}, {period} x {samples} bytes, {own_text}; " + "; ".join(
        parts
    )
    return bad_total == 0, text


def main() -> int:
    argv = sys.argv[1:]
    steps, verbose, roms = STEPS, False, []
    while argv:
        a = argv.pop(0)
        if a == "--steps":
            steps = argv.pop(0)
        elif a == "--verbose":
            verbose = True
        else:
            roms.append(Path(a))
    failed = 0
    base = Path(
        os.environ.get("TRIP_CARTS") or Path(tempfile.gettempdir()) / "torii-m4a"
    )
    for rom in roms:
        cart = base / re.sub(r"[^A-Za-z0-9]", "", rom.stem)
        shutil.rmtree(cart, ignore_errors=True)
        try:
            lines, sound = measure(rom, cart, steps)
            ok, text = judge(lines, sound, verbose)
        except G.Failure as e:
            ok, text = False, str(e)
        print(
            f"{'PASS' if ok else 'FAIL'}: m4a sound after the menu {rom.name}: {text}"
        )
        if ok and not os.environ.get("TRIP_KEEP"):
            shutil.rmtree(cart, ignore_errors=True)
        failed += not ok
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
