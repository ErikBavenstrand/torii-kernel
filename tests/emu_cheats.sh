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
# shellcheck source=tests/pipes.sh
. tests/pipes.sh
local_carts "dist/cart-emu-cheats${1:+-$1}"

cart=$carts/emu-cheats
# Jagoomba's copy of the GB's WRAM (C000 on) and of C100 on, from its symbols
xgb=$(python3 tests/elf_sym.py emulators/out/jagoombacolor.elf XGB_RAM)
wram=$(printf '%08X' $((xgb)))
wram100=$(printf '%08X' $((xgb + 0x100)))
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
  *.gb | *.gbc) python3 tests/gb_rom.py "$cart/game" "${gbmode:-cheat}" ;;
  *.nes) python3 tests/nes_rom.py "$cart/game" "${mode:-cheat}" ;;
  esac
  mkfs.fat -C -F 32 -n EMU "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" "$cart/game" "::$file"
  if [ -n "$cht" ]; then
    mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
    printf '%b' "$cht" >"$cart/game.cht"
    mcopy -i "$cart/sd.img" "$cart/game.cht" "::/torii/cheats/${file%.*}.cht"
  fi
}

# The cheat file of FILE (a path from the root) replaced by CHT on the same cart, as the
# cheat screen rewrites it when a pick changes, after a power-on that answers the
# last game's "Save to SD card?" (the emulator keeps its settings in the cart's SRAM)
re_cht() {
  local file=$1
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -w "screen ready: dialog" \
    -k @10:A -k @16:- -u "nor: wrote an empty pending-save record" -f 2000 >/dev/null 2>&1 || true
  printf '%b' "$2" >"$cart/game.cht"
  mcopy -o -i "$cart/sd.img" "$cart/game.cht" "::/torii/cheats/${file%.*}.cht"
}

# Launches FILE and plays; the screen is $cart/game.png
# With dialog=1, a dialog says picks weren't applied (after the load): A closes it
play() {
  local ack=()
  [ "${dialog:-}" != 1 ] || ack=(-w "screen ready: dialog" -k @10:A -k @14:-)
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch "$1" at=100) "${ack[@]}" \
    -s 700:"$cart/game.png" -m "700:0x$wram:4" -m "700:0x$wram100:4" -f 700 >"$cart/out.txt" 2>"$cart/kernel.log" || fail "the emulator failed"
  grep -q '^crash at frame' "$cart/kernel.log" && fail "the emulator caught a crash"
  grep -q '^kernel error:' "$cart/kernel.log" && fail "errors: $(grep '^kernel error:' "$cart/kernel.log" | drain_head -3)"
  grep -qF "kernel: launch: starting the game" "$cart/kernel.log" || fail "$1 wasn't started"
  if [ "${dialog:-}" = 1 ]; then
    grep -qF "kernel: screen ready: dialog" "$cart/kernel.log" || fail "$1: no dialog said what wasn't applied"
  else
    ! grep -qF "kernel: screen ready: dialog" "$cart/kernel.log" || fail "$1: a dialog said something wasn't applied"
  fi
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
  if [ -s "$cart/out.txt" ] && grep -q "^mem frame 700 $wram: " "$cart/out.txt" && [ "$kind" = gb ]; then
    local byte
    byte=$(sed -n "s/^mem frame 700 $wram: [0-9A-F]* \\([0-9A-F]*\\).*/\\1/p" "$cart/out.txt")
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
re_cht /Cheat.gb "$(cht Red "$(gb 4001 1F E0)" | sed 's/enable = true/enable = false/')"
play /Cheat.gb
expect plain "gb genie turned off, the next launch on the same cart"

new_cart /Cheat.gb "$(cht Red "$(gb 4001 1F)")"
play /Cheat.gb
expect red "gb 6-digit genie (every bank)" "cheats: 1 lines made 3 patches in the game"

new_cart /Cheat.gb "$(cht Wrong "$(gb 4001 1F 99)")"
dialog=1 play /Cheat.gb
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

# a GameShark code to cart RAM reads but isn't run: the game starts all the same
new_cart /Cheat.gb "$(cht Ram 01FF00A1)"
dialog=1 play /Cheat.gb
expect plain "gb gameshark"
grep -qF "cheat 0 (Ram): can't run here, left out" "$cart/kernel.log" || fail "gb gameshark: not left out with its reason"

# --- GB and GBC GameShark (stage 3): the game (gb_rom.py poke, pokegbc) counts, at $C102, how
# often it sees $55 where the cheat writes it, clearing it each time: a write made every
# frame counts up to $F0, so it was applied each frame. WRAM is at XGB_RAM + (address - $C000)
count() { sed -n "s/^mem frame 700 $wram100: [0-9A-F]* [0-9A-F]* \\([0-9A-F]*\\).*/\\1/p" "$cart/out.txt"; }
expect_count() { # NAME WANT
  local got
  got=$(count)
  [ "$got" = "$2" ] || fail "$1: the game counted $got, not $2"
}
gs() { printf 'cheats = 1\n\ncheat0_desc = "Pokes"\ncheat0_code = "%s"\ncheat0_enable = true\n' "$1"; }

gbmode=poke
new_cart /Cheat.gb
play /Cheat.gb
expect_count "gb gameshark game without a cheat" 00

new_cart /Cheat.gb "$(gs 015500C1)"
play /Cheat.gb
expect_count "gb gameshark write to WRAM, every frame" F0
grep -qF "cheats: 1 pokes written to the emulator's table" "$cart/kernel.log" || fail "gb gameshark: no poke written"
# the pick turned off, the next launch on the same cart writes nothing: the emulator's table
# starts empty again, and so does the patched copy (a Game Genie code turned off too)
re_cht /Cheat.gb "$(gs 015500C1 | sed 's/enable = true/enable = false/')"
play /Cheat.gb
expect_count "gb gameshark turned off, the next launch" 00
! grep -qF "pokes written" "$cart/kernel.log" || fail "gb gameshark turned off: a poke was written"

new_cart /Cheat.gb "$(gs 015500C2)"
play /Cheat.gb
expect_count "gb gameshark write elsewhere" 00

# the echo of C100
new_cart /Cheat.gb "$(gs 015500E1)"
play /Cheat.gb
expect_count "gb gameshark write through the echo" F0

# a write to VRAM or the ROM isn't run: the game starts all the same
new_cart /Cheat.gb "$(gs 015500A1)"
dialog=1 play /Cheat.gb
expect_count "gb gameshark write to cart RAM" 00
grep -qF "cheat 0 (Pokes): can't run here, left out" "$cart/kernel.log" || fail "gb gameshark to cart RAM: not left out with its reason"

# GBC: the game maps WRAM bank 3 at D000
gbmode=pokegbc
new_cart /Cheat.gb "$(gs 015500D1)"
play /Cheat.gb
expect_count "gbc gameshark, the mapped bank" F0
new_cart /Cheat.gb "$(gs 935500D1)"
play /Cheat.gb
expect_count "gbc gameshark, bank 3" F0
new_cart /Cheat.gb "$(gs 925500D1)"
play /Cheat.gb
expect_count "gbc gameshark, another bank" 00
new_cart /Cheat.gb "$(gs 975500D1)"
play /Cheat.gb
expect_count "gbc gameshark, bank 7" 00
new_cart /Cheat.gb "$(gs 915500D1)"
play /Cheat.gb
expect_count "gbc gameshark, bank 1" 00
gbmode=cheat

# --- GBC: the same codes in a .gbc game (Jagoomba's GBC mode), and a real GBC game
gbmode=cheatgbc
new_cart /Cheat.gbc "$(cht Red "$(gb 4001 1F E0)")"
play /Cheat.gbc
expect red "gbc genie with its compare" "cheats: 1 lines made 1 patches in the game"
new_cart /Cheat.gbc "$(cht Red "$(gb 4001 1F)")"
play /Cheat.gbc
expect red "gbc 6-digit genie" "cheats: 1 lines made 3 patches in the game"
new_cart /Cheat.gbc "$(cht Wrong "$(gb 4001 1F 99)")"
dialog=1 play /Cheat.gbc
expect plain "gbc genie whose compare matches no bank"
gbmode=cheat

# A real GBC game, if the card of games is here: a code for an address in the switched banks,
# made from the ROM's own bytes (its compare is a byte the address holds in some banks only),
# patches exactly those banks' bytes in the game's copy in PSRAM (the cart's ROM space)
real="roms/Donkey Kong Country.gbc"
if [ -f "$real" ]; then
  emu_size=$(stat -c %s emulators/out/jagoombacolor.gba)
  mapfile -t plan < <(
    python3 - "$real" "$emu_size" <<'PY'
import sys
sys.path.insert(0, "tests")
from emu_cheat_codes import gb_genie

rom = open(sys.argv[1], "rb").read()
emu = int(sys.argv[2])
banks = len(rom) // 0x4000
addr = 0x4123
vals = {b: rom[b * 0x4000 + (addr - 0x4000)] for b in range(1, banks)}
# the byte held by the fewest banks (at least one, so the code does something)
compare = min(set(vals.values()), key=lambda v: list(vals.values()).count(v))
value = compare ^ 0xFF
hit = [b for b, v in vals.items() if v == compare]
miss = [b for b, v in vals.items() if v != compare]
print(gb_genie(addr, value, compare))
print(f"{value:02X}")
print(" ".join(f"{0x08000000 + emu + b * 0x4000 + (addr - 0x4000):X}:{value:02X}" for b in hit[:6]))
print(" ".join(f"{0x08000000 + emu + b * 0x4000 + (addr - 0x4000):X}:{vals[b]:02X}" for b in miss[:3]))
PY
  )
  code=${plan[0]}
  rm -rf "$cart" && mkdir -p "$cart"
  mkfs.fat -C -F 32 -n EMU "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" "$real" "::/Real.gbc"
  mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
  printf '%b' "$(cht Real "$code")" >"$cart/game.cht"
  mcopy -i "$cart/sd.img" "$cart/game.cht" "::/torii/cheats/Real.cht"
  mems=()
  for pair in ${plan[2]} ${plan[3]}; do mems+=(-m "700:0x${pair%%:*}:1"); done
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Real.gbc at=100) \
    "${mems[@]}" -f 700 >"$cart/out.txt" 2>"$cart/kernel.log" || fail "the emulator failed on $real"
  grep -q '^crash at frame' "$cart/kernel.log" && fail "the emulator caught a crash on $real"
  grep -qF "kernel: launch: starting the game" "$cart/kernel.log" || fail "$real wasn't started"
  grep -qE "kernel: cheats: 1 lines made [1-9][0-9]* patches in the game" "$cart/kernel.log" || fail "$real: the code ($code) patched nothing"
  for pair in ${plan[2]} ${plan[3]}; do
    got=$(sed -n "s/^mem frame [0-9]* 0*${pair%%:*}: \([0-9A-F]*\).*/\1/p" "$cart/out.txt" | drain_head -1)
    [ "$got" = "${pair##*:}" ] || fail "$real: the byte at ${pair%%:*} is $got, wanted ${pair##*:}"
  done
fi

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
dialog=1 play /Cheat.nes
expect plain "nes genie with the wrong compare" "cheats: cheat 0 (Wrong): line"

# --- NES raw RAM writes: the game (nes_rom.py poke) clears $0300 each frame and
# shows red only while the cheat writes $55 there every frame
mode=poke
poke() {
  printf 'cheats = %s\n\n' "$#"
  local n=0
  for code in "$@"; do
    printf 'cheat%s_desc = "Poke %s"\ncheat%s_code = "%s"\ncheat%s_enable = true\n\n' "$n" "$n" "$n" "$code" "$n"
    n=$((n + 1))
  done
}

new_cart /Cheat.nes
play /Cheat.nes
expect plain "nes poke game without cheats"

new_cart /Cheat.nes "$(poke 0300:55)"
play /Cheat.nes
expect red "nes raw poke, every frame" "cheats: 1 pokes written to the emulator's table"
re_cht /Cheat.nes "$(poke 0300:55 | sed 's/enable = true/enable = false/')"
play /Cheat.nes
expect plain "nes raw poke turned off, the next launch"

new_cart /Cheat.nes "$(poke 0300:56)"
play /Cheat.nes
expect plain "nes raw poke of another value"

# a mirror of the RAM folds onto it
new_cart /Cheat.nes "$(poke 0B00:55)"
play /Cheat.nes
expect red "nes raw poke at a RAM mirror" "cheats: 1 pokes written to the emulator's table"

# several lines of one cheat and another cheat: all in the table, the last one red
new_cart /Cheat.nes "$(poke '0311:01+0312:02' 0300:55)"
play /Cheat.nes
expect red "nes raw pokes in two cheats" "cheats: 3 pokes written to the emulator's table"

# an address in the ROM, or a register, is no RAM write: the cheat can't run and is left out
new_cart /Cheat.nes "$(poke 8000:55)"
dialog=1 play /Cheat.nes
expect plain "nes raw poke to the ROM"
grep -qF "cheat 0 (Poke 0): can't run here, left out" "$cart/kernel.log" || fail "nes raw ROM poke: not left out as can't run"

mode=cheat

# A file edited past the budget (the cheat screen refuses a pick that wouldn't fit, but a file can
# pick them all): at launch the first cheat is in, the second is left out whole, and a dialog says so
forty() { python3 -c "print('+'.join('%04X:01' % (0x$1 + i) for i in range(40)))"; }
new_cart /Cheat.nes "$(printf 'cheats = 2\n\ncheat0_desc = "A"\ncheat0_code = "%s"\ncheat0_enable = true\n\ncheat1_desc = "B"\ncheat1_code = "%s"\ncheat1_enable = true\n' "$(forty 400)" "$(forty 500)")"
dialog=1 play /Cheat.nes
grep -qF "cheats: 40 pokes written to the emulator's table" "$cart/kernel.log" || fail "the budget: not the first cheat's 40 pokes alone"
grep -qF "cheat 1 (B): its 3200 bytes" "$cart/kernel.log" || fail "the budget: the second cheat wasn't left out for its bytes"
echo "PASS: Game Genie patches and RAM writes reach GB, GBC and NES games"
