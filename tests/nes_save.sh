#!/usr/bin/env bash
# A NES game's battery save survives a power-off without PocketNES's menu.
# The game (tests/nes_rom.py save) writes $5A to $6000 at start; PocketNES
# writes its battery RAM through to the cart's SRAM (E000-FFFF) as the game
# writes it, the kernel copies the SRAM to /torii/saves at the next
# power-on, and the next launch gives the RAM back: the game then writes
# $A5 to $6001. Runs inside the torii-emu container after `just build` and
# `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-nes-save${1:+-$1}"

cart=$carts/nes-save
fail() {
  echo "FAIL: nes save: $*" >&2
  [ ! -f "$cart/kernel.log" ] || grep -E '^(kernel|crash)' "$cart/kernel.log" | tail -10 >&2
  exit 1
}

# Whether byte $2 of the SRAM image $1 is $3
byte_is() {
  python3 -c 'import sys; b = open(sys.argv[1], "rb").read(); sys.exit(0 if b[int(sys.argv[2], 16)] == int(sys.argv[3], 16) else 1)' "$@"
}

# Launch Save.nes and play FRAMES frames
play() {
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Save.nes at=100) \
    -w "launch: starting the game" -s "@$1:$cart/end.png" >/dev/null 2>"$cart/kernel.log" || fail "the emulator failed"
  grep -q '^crash at frame' "$cart/kernel.log" && fail "the emulator caught a crash"
  grep -q '^kernel error:' "$cart/kernel.log" && fail "errors: $(grep '^kernel error:' "$cart/kernel.log" | head -3)"
  return 0
}

rm -rf "$cart" && mkdir -p "$cart"
python3 tests/nes_rom.py "$cart/Save.nes" save
mkfs.fat -C -F 32 -n NES "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" "$cart/Save.nes" ::/Save.nes

# Off a few seconds after the write: it is in the cart's SRAM, as on a real
# cart (PocketNES has no autosave: it got there by writing through)
play 300
byte_is "$cart/sram.bin" E000 5A || fail "the write isn't in the cart's SRAM at E000"

# The next power-on copies it to the card ("Save to SD card?", A)
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -w "screen ready: dialog" \
  -k @10:A -k @16:- -u "nor: wrote an empty pending-save record" -f 2000 >/dev/null 2>"$cart/kernel.log" || true
grep -qF "kernel: save: wrote /torii/saves/Save.nes/Main.sav" "$cart/kernel.log" || fail "the save wasn't copied to SD"
mcopy -o -i "$cart/sd.img" ::/torii/saves/Save.nes/Main.sav "$cart/Main.sav"
byte_is "$cart/Main.sav" E000 5A || fail "/torii/saves/Save.nes/Main.sav doesn't hold the save"

# The next launch gives the game its RAM back
play 300
byte_is "$cart/sram.bin" E001 A5 || fail "the next launch didn't give the game its save"
echo "PASS: a NES game's battery save is on the cart as the game writes it and comes back at the next launch"
