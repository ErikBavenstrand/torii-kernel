#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Rebuild the kernel and relaunch the emulated cart whenever source/ changes.

Runs the kernel on the patched mGBA from `just emu`, with the cart files in
dist/cart, at OMEGA_SCALE times the GBA's 240x160 (default 4).
"""

import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WATCH = [ROOT / "source"]
POLL_SECONDS = 0.5


def launch() -> subprocess.Popen:
    env = dict(os.environ, OMEGA_DIR=str(ROOT / "dist" / "cart"))
    mgba = ROOT / "emu" / "build-native" / "sdl" / "mgba"
    rom = ROOT / "dist" / "ezkernelnew.bin"
    scale = os.environ.get("OMEGA_SCALE", "4")
    return subprocess.Popen(
        [str(mgba), "-C", "logLevel.gba.debug=31", f"-{scale}", str(rom)], env=env
    )


def snapshot() -> dict:
    state = {}
    for base in WATCH:
        for path in base.rglob("*"):
            if path.is_file():
                state[path] = path.stat().st_mtime_ns
    return state


def stop(proc):
    if proc and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


def build() -> bool:
    print("\n=== building ===", flush=True)
    ok = subprocess.run(["just", "build"], cwd=ROOT, check=False).returncode == 0
    print(
        "=== build ok ===" if ok else "=== build FAILED; keeping the last ROM ===",
        flush=True,
    )
    return ok


def main() -> int:
    build()
    proc = launch()
    last = snapshot()
    print("watching source/ (Ctrl-C to stop)")
    try:
        while True:
            time.sleep(POLL_SECONDS)
            now = snapshot()
            if now != last:
                last = now
                if build():
                    stop(proc)
                    proc = launch()
    except KeyboardInterrupt:
        stop(proc)
    return 0


if __name__ == "__main__":
    sys.exit(main())
