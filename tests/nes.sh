#!/usr/bin/env bash
# A NES launch on the emulated cart: opening a .nes file loads it with the
# kernel's PocketNES (LoadEMU2PSRAM in source/ezkernelnew.c) and starts it.
# The game is tests/nes_rom.py's, which turns the screen red, so a red
# screen means PocketNES runs it. Runs inside the torii-emu container after
# `just build` and `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-nes${1:+-$1}"

cart=$carts/nes
rm -rf "$cart"
mkdir -p "$cart"

fail() {
  echo "FAIL: nes: $*" >&2
  [ ! -f "$cart/kernel.log" ] || grep -E '^(kernel|crash)' "$cart/kernel.log" | tail -10 >&2
  exit 1
}

python3 tests/nes_rom.py "$cart/Red.nes"
mkfs.fat -C -F 32 -n NES "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" "$cart/Red.nes" ::/Red.nes

# Red.nes, launched as a page will (a NES game starts clean)
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Red.nes at=100) \
  -s 700:"$cart/game.png" -f 700 >/dev/null 2>"$cart/kernel.log" || fail "the emulator failed"
grep -q '^crash at frame' "$cart/kernel.log" && fail "the emulator caught a crash"
grep -q '^kernel error:' "$cart/kernel.log" && fail "errors: $(grep '^kernel error:' "$cart/kernel.log" | head -3)"
grep -qF "kernel: launch: loading Red.nes with PocketNES" "$cart/kernel.log" || fail "Red.nes wasn't loaded with PocketNES"
grep -qF "kernel: launch: starting the game" "$cart/kernel.log" || fail "the game wasn't started"

# Most of the screen is the game's red backdrop
python3 - "$cart/game.png" <<'EOF' || fail "the screen isn't red: PocketNES doesn't run the game"
import sys
from pathlib import Path
sys.path.insert(0, "tests")
from games import png_lines

red = total = 0
for channels, row in png_lines(Path(sys.argv[1])):
    for i in range(0, len(row), channels):
        r, g, b = row[i : i + 3]
        total += 1
        red += r > 120 and g < r // 2 and b < r // 2
print(f"{red} of {total} pixels red", file=sys.stderr)
sys.exit(0 if red > total * 3 // 4 else 1)
EOF
echo "PASS: a NES game loads with PocketNES and runs"
