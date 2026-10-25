#!/usr/bin/env bash
# A GB game's save survives a power-off without Jagoomba's menu. The game
# (tests/gb_rom.py) writes a marker to each bank of its battery RAM at start.
# Jagoomba writes it through to the cart's SRAM as the game writes, at SRAM
# E000 for 8 KB and (Torii's patch, torii_raw32) 8000-FFFF for 32 KB; the
# kernel copies the SRAM to /torii/saves at the next power-on, and the next
# launch packs it into Jagoomba's record of the game. Runs inside the
# torii-emu container after `just build` and `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-gb-save${1:+-$1}"

cart=$carts/gb-save
fail() {
  echo "FAIL: gb save: $*" >&2
  [ ! -f "$cart/kernel.log" ] || grep -E '^(kernel|crash)' "$cart/kernel.log" | tail -10 >&2
  exit 1
}

# Whether the SRAM image $1 holds the game's RAM with the marker in each bank
marked() {
  python3 tests/jagoomba_sram.py "$1" SAVETEST >"$cart/ram.bin" || return 1
  python3 -c 'import sys; b = open(sys.argv[1], "rb").read(); sys.exit(0 if b and all(b[i] == 0x5A for i in range(0, len(b), 0x2000)) else 1)' "$cart/ram.bin"
}

# A fresh cart with Save.gb with RAM KB of RAM
new_cart() {
  rm -rf "$cart" && mkdir -p "$cart"
  python3 tests/gb_rom.py "$cart/Save.gb" "$1"
  mkfs.fat -C -F 32 -n GB "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" "$cart/Save.gb" ::/Save.gb
}

# Launch Save.gb and play FRAMES frames
play() {
  local frames=$1
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Save.gb at=100) \
    -w "launch: starting the game" -s "@$frames:$cart/end.png" >/dev/null 2>"$cart/kernel.log" || fail "the emulator failed"
  grep -q '^crash at frame' "$cart/kernel.log" && fail "the emulator caught a crash"
  grep -q '^kernel error:' "$cart/kernel.log" && fail "errors: $(grep '^kernel error:' "$cart/kernel.log" | head -3)"
  return 0
}

# The next power-on copies the cart's SRAM to the card ("Save to SD card?", A)
copy_save() {
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -w "screen ready: dialog" \
    -k @10:A -k @16:- -u "nor: wrote an empty pending-save record" -f 2000 >/dev/null 2>"$cart/kernel.log" || true
  grep -qF "kernel: save: wrote /torii/saves/Save.gb/Main.sav" "$cart/kernel.log" || fail "$1: the save wasn't copied to SD"
  mcopy -o -i "$cart/sd.img" ::/torii/saves/Save.gb/Main.sav "$cart/Main.sav"
}

# 32 KB, off right after the writes: they are in the cart's SRAM at 8000, as
# on a real cart, and on the card after the next power-on
new_cart 32
play 30
python3 -c 'import sys; b = open(sys.argv[1], "rb").read(); sys.exit(0 if all(b[0x8000 + i * 0x2000] == 0x5A for i in range(4)) else 1)' "$cart/sram.bin" ||
  fail "32 KB: the writes aren't in the cart's SRAM at 8000"
copy_save "32 KB"
python3 -c 'import sys; b = open(sys.argv[1], "rb").read(); sys.exit(0 if all(b[0x8000 + i * 0x2000] == 0x5A for i in range(4)) else 1)' "$cart/Main.sav" ||
  fail "32 KB: /torii/saves/Save.gb/Main.sav doesn't hold the save"
play 180
marked "$cart/sram.bin" || fail "32 KB: the next launch didn't find the save"

# 8 KB: the writes go through to the cart's SRAM at once (sram_W2), and
# Jagoomba packs them into the game's record at its next start
new_cart 8
play 180
copy_save "8 KB"
play 180
marked "$cart/sram.bin" || fail "8 KB: the next launch didn't find the save"
echo "PASS: a GB game's save, 32 KB or 8 KB, is on the cart as the game writes it and survives a power-off without Jagoomba's menu"
