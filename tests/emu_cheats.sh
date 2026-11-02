#!/usr/bin/env bash
# Game Genie cheats for GB, GBC and NES games on the emulated cart: the picks in
# /torii/cheats/<game>.cht patch the game's copy in PSRAM before Jagoomba or
# PocketNES starts it (cheatstore_launch_emu). The games (tests/gb_rom.py cheat,
# tests/nes_rom.py cheat) read a byte from their ROM and show it: the NES's
# backdrop is green, and red once a Game Genie code patches that byte; the GB
# game stores its byte in WRAM, which the test reads from the emulator's memory. Codes come from
# tests/emu_cheat_codes.py. Runs inside the torii-emu container after
# `just build` and `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-emu-cheats${1:+-$1}"

cart=$carts/emu-cheats
fail() {
  echo "FAIL: emu cheats: $*" >&2
  [ ! -f "$cart/kernel.log" ] || grep -E '^(kernel|crash)' "$cart/kernel.log" | tail -12 >&2
  exit 1
}

# A fresh cart with the game FILE (a path from the root) and, if CHT is set, its cheat file
new_cart() {
  local file=$1 cht=${2:-}
  rm -rf "$cart" && mkdir -p "$cart"
  case $file in
    *.gb) python3 tests/gb_rom.py "$cart/game" cheat ;;
    *.nes) python3 tests/nes_rom.py "$cart/game" cheat ;;
  esac
  mkfs.fat -C -F 32 -n EMU "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" "$cart/game" "::$file"
  if [ -n "$cht" ]; then
    mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
    printf '%b' "$cht" >"$cart/game.cht"
    mcopy -i "$cart/sd.img" "$cart/game.cht" "::/torii/cheats/${file%.*}.cht"
  fi
}

# Launches FILE and plays; the screen is $cart/game.png
play() {
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch "$1" at=100) \
    -s 700:"$cart/game.png" -m 700:0x03005890:4 -f 700 >"$cart/out.txt" 2>"$cart/kernel.log" || fail "the emulator failed"
  grep -q '^crash at frame' "$cart/kernel.log" && fail "the emulator caught a crash"
  grep -q '^kernel error:' "$cart/kernel.log" && fail "errors: $(grep '^kernel error:' "$cart/kernel.log" | head -3)"
  grep -qF "kernel: launch: starting the game" "$cart/kernel.log" || fail "$1 wasn't started"
  return 0
}

# Whether most of the screen is red
red() {
  python3 - "$cart/game.png" <<'PY'
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
PY
}

# expect red|plain NAME LOG: what the last play showed (a NES game's screen, or
# the byte a GB game stored: $1F patched, $E0 not), and a line of its log
expect() {
  local want=$1 name=$2 log=${3:-}
  if [ -s "$cart/out.txt" ] && grep -q "^mem frame 700 03005890: " "$cart/out.txt" && [ "$kind" = gb ]; then
    local byte
    byte=$(sed -n 's/^mem frame 700 03005890: [0-9A-F]* \([0-9A-F]*\).*/\1/p' "$cart/out.txt")
    if [ "$want" = red ]; then
      [ "$byte" = 1F ] || fail "$name: the game read $byte, not the patched 1F"
    else
      [ "$byte" = E0 ] || fail "$name: the game read $byte, not its own E0"
    fi
  elif [ "$want" = red ]; then
    red 2>/dev/null || fail "$name: the screen isn't red: the patch didn't reach the game"
  else
    ! red 2>/dev/null || fail "$name: the screen is red: something patched the game"
  fi
  [ -z "$log" ] || grep -qF "kernel: $log" "$cart/kernel.log" || fail "$name: no log line '$log'"
}

cht() { # DESC CODE: a picked cheat
  printf 'cheats = 1\n\ncheat0_desc = "%s"\ncheat0_code = "%s"\ncheat0_enable = true\n' "$1" "$2"
}

# --- GB (GBC-flagged, MBC5, 4 banks): bank 1's byte at 4001 is $E0 (dark green), bank 2's $77
kind=gb
gb() { python3 tests/emu_cheat_codes.py gb "$@"; }

new_cart /Cheat.gb
play /Cheat.gb
expect plain "gb without a cheat file"

new_cart /Cheat.gb "$(cht Red "$(gb 4001 1F E0)")"
play /Cheat.gb
expect red "gb genie with its compare" "cheats: 1 lines made 1 patches in the game"

new_cart /Cheat.gb "$(cht Red "$(gb 4001 1F)")"
play /Cheat.gb
expect red "gb 6-digit genie (every bank)" "cheats: 1 lines made 3 patches in the game"

new_cart /Cheat.gb "$(cht Wrong "$(gb 4001 1F 99)")"
play /Cheat.gb
expect plain "gb genie whose compare matches no bank" "cheats: cheat 0 (Wrong): line 4001 patches nothing"

# the compare $77 is bank 2's: bank 1 is left alone, so the screen stays green
new_cart /Cheat.gb "$(cht Other "$(gb 4001 1F 77)")"
play /Cheat.gb
expect plain "gb genie for another bank" "cheats: 1 lines made 1 patches in the game"

# not picked: nothing
new_cart /Cheat.gb "$(cht Off "$(gb 4001 1F E0)" | sed 's/enable = true/enable = false/')"
play /Cheat.gb
expect plain "gb genie not picked"
! grep -q "lines made" "$cart/kernel.log" || fail "an unpicked cheat patched the game"

# a GameShark code reads but doesn't run yet: the game starts all the same
new_cart /Cheat.gb "$(cht Ram 01FF00C1)"
play /Cheat.gb
expect plain "gb gameshark"
grep -qF "cheat 0 (Ram): can't run here, left out" "$cart/kernel.log" || fail "gb gameshark: not left out with its reason"

# --- NES (NROM): the backdrop's byte is $2A (green)
kind=nes
nes() { python3 tests/emu_cheat_codes.py nes "$@"; }
at=$(python3 -c 'import sys; sys.path.insert(0, "tests"); import nes_rom; print("%X" % nes_rom.cheat_at())')

new_cart /Cheat.nes
play /Cheat.nes
expect plain "nes without a cheat file"

new_cart /Cheat.nes "$(cht Red "$(nes "$at" 16 2A)")"
play /Cheat.nes
expect red "nes 8-letter genie" "cheats: 1 lines made 1 patches in the game"

new_cart /Cheat.nes "$(cht Red "$(nes "$at" 16)")"
play /Cheat.nes
expect red "nes 6-letter genie" "cheats: 1 lines made 1 patches in the game"

new_cart /Cheat.nes "$(cht Wrong "$(nes "$at" 16 33)")"
play /Cheat.nes
expect plain "nes genie with the wrong compare" "cheats: cheat 0 (Wrong): line"

new_cart /Cheat.nes "$(cht Ram 0754:00)"
play /Cheat.nes
expect plain "nes raw code"
grep -qF "cheat 0 (Ram): can't run here, left out" "$cart/kernel.log" || fail "nes raw: not left out with its reason"

echo "PASS: Game Genie patches reach GB, GBC and NES games"
