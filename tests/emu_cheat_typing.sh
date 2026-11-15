#!/usr/bin/env bash
# Typing a code on the cart for a GB, a GBC and a NES game, end to end: the
# game's cheat screen, Edit code on its one cheat (a code that writes where the
# game doesn't look), B over the old code, the new one typed key by key on the
# keypad (source/screens/keypad.c: two rows of hex digits, 0-7 and 8-F, then
# the function row, whose first key types the console's separator), START, the
# name kept with START. The card's cheat file then holds the typed code, and the
# next power-on's launch runs it: the poke games of tests/emu_cheats.sh count
# the write every frame (GB, GBC) or show red (NES). Runs inside the torii-emu
# container after `just build` and `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/pipes.sh
. tests/pipes.sh
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts dist/cart-emu-cheat-typing

cart=$carts/typing
shot=emu/build-linux/mgba-shot
fail() {
  echo "FAIL: emu cheat typing: $*" >&2
  [ ! -f "$cart/kernel.log" ] || grep -E '^(kernel|crash)' "$cart/kernel.log" | tail -12 >&2
  exit 1
}

# keys MOVES...: the keypad presses for a code, as -k options 8 frames apart from @16 after
# "screen ready: keypad" (each MOVE a key: UP, DOWN, LEFT, RIGHT, A, B, START)
keys() {
  local at=16
  for k in "$@"; do
    printf -- '-k @%s:%s -k @%s:- ' "$at" "$k" "$((at + 4))"
    at=$((at + 8))
  done
}

# moves CODE KIND: the presses that type CODE from the '0' key (row 0, column 0), where the
# keypad opens; KIND gb or nes says the separator ('-' or ':', the function row's first key)
moves() {
  python3 - "$1" <<'PY'
import sys
row, col, out = 0, 0, []
for ch in sys.argv[1]:
    if ch in "-:":
        r, c = 2, 0
    else:
        v = int(ch, 16)
        r, c = v // 8, v % 8
    while row < r:
        out.append("DOWN"); row += 1
    while row > r:
        out.append("UP"); row -= 1
    while col < c:
        out.append("RIGHT"); col += 1
    while col > c:
        out.append("LEFT"); col -= 1
    out.append("A")
print(" ".join(out))
PY
}

# typed GAME PAGE OLD NEW: on a cart with GAME (a path from the root) and a cheat file whose
# one cheat is OLD, types NEW over it on PAGE's cheat screen; the file is $cart/typed.cht
typed() {
  local game=$1 page=$2 old=$3 new=$4 n
  mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
  printf 'cheats = 1\n\ncheat0_desc = "Typed"\ncheat0_code = "%s"\ncheat0_enable = true\n' "$old" >"$cart/old.cht"
  mcopy -i "$cart/sd.img" "$cart/old.cht" "::/torii/cheats/${game%.*}.cht"
  python3 tests/setnor.py set "$cart/kernel-nor.bin" 28=1
  local deletes=()
  for ((n = 0; n < ${#old}; n++)); do deletes+=(B); done
  # shellcheck disable=SC2207 # one move a word
  local typing=($(moves "$new"))
  cat >"$cart/typing.opts" <<EOF
-k @16:START -k @20:-
-w "screen ready: game-popup"
-k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @48:A -k @52:-
-w "screen ready: cheats"
-k @8:START -k @12:-
-w "screen ready: cheat-menu"
-k @24:DOWN -k @28:- -k @40:A -k @44:-
-w "screen ready: keypad"
$(keys "${deletes[@]}" "${typing[@]}" START)
-w "screen ready: rename"
-k @16:START -k @20:-
-w "screen ready: cheats"
EOF
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart TZ=UTC $shot dist/ezkernelnew.bin $(NAV_SHOWN=$page nav_to "$page") -i "$cart/typing.opts" >/dev/null 2>"$cart/kernel.log" ||
    fail "$game: typing $new didn't get back to the cheat screen"
  ! grep -q '^kernel error:' "$cart/kernel.log" || fail "$game: errors: $(grep '^kernel error:' "$cart/kernel.log" | drain_head -3)"
  mcopy -o -i "$cart/sd.img" "::/torii/cheats/${game%.*}.cht" "$cart/typed.cht"
  grep -qF "\"$new\"" "$cart/typed.cht" || fail "$game: the cheat file doesn't hold the typed $new: $(grep code "$cart/typed.cht")"
  grep -qF "enable = true" "$cart/typed.cht" || fail "$game: the typed cheat isn't picked"
}

# launch GAME: the next power-on (answering the save dialog the last game's settings make,
# if one comes) launches GAME; out.txt holds WRAM (Jagoomba's XGB_RAM + $100) or the screen
launch() {
  OMEGA_DIR=$cart $shot dist/ezkernelnew.bin -w "screen ready: dialog" \
    -k @10:A -k @16:- -u "nor: wrote an empty pending-save record" -f 600 >/dev/null 2>&1 || true
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart $shot dist/ezkernelnew.bin $(nav_launch "$1" at=100) -w "launch: starting the game" \
    -m "@400:0x$wram100:4" -s "@400:$cart/game.png" >"$cart/out.txt" 2>"$cart/kernel.log" || fail "$1: the launch failed"
  grep -qF "cheats: 1 pokes written to the emulator's table" "$cart/kernel.log" || fail "$1: the typed code wasn't written"
}

xgb=$(python3 tests/elf_sym.py emulators/out/jagoombacolor.elf XGB_RAM)
wram100=$(printf '%08X' $((xgb + 0x100)))
count() { sed -n "s/^mem frame [0-9]* $wram100: [0-9A-F]* [0-9A-F]* \\([0-9A-F]*\\).*/\\1/p" "$cart/out.txt"; }

new() { # GAME MODE: a fresh cart with tests/gb_rom.py's or nes_rom.py's poke game
  rm -rf "$cart" && mkdir -p "$cart"
  case $1 in
  *.nes) python3 tests/nes_rom.py "$cart/game" poke ;;
  *) python3 tests/gb_rom.py "$cart/game" "$2" ;;
  esac
  mkfs.fat -C -F 32 -n TYPING "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" "$cart/game" "::$1"
}

new /Poke.gb poke
typed /Poke.gb gb 015500C2 015500C1
launch /Poke.gb
[ "$(count)" = F0 ] || fail "GB: the game counted $(count), not F0"

new /Poke.gbc pokegbc
typed /Poke.gbc gbc 015500C2 935500D1
launch /Poke.gbc
[ "$(count)" = F0 ] || fail "GBC: the game counted $(count), not F0"

new /Poke.nes
typed /Poke.nes nes 0301:55 0300:55
launch /Poke.nes
python3 - "$cart/game.png" <<'PY' || fail "NES: the screen isn't red: the typed poke didn't run"
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
sys.exit(0 if red > total * 3 // 4 else 1)
PY
echo "PASS: codes typed on the keypad for a GB, a GBC and a NES game are saved picked and run at the next launch"
