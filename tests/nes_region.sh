#!/usr/bin/env bash
# PocketNES's region (Dwedit/PocketNES#3): a NES 2.0 header's region picks
# it (tests/nes_rom.py pal), and a region picked in PocketNES's menu (L+R,
# Other Settings) is kept in its config record in the cart's SRAM, so the
# game starts with it again after a power-off. Runs inside the torii-emu
# container after `just build` and `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-nes-region${1:+-$1}"

cart=$carts/nes-region
fail() {
  echo "FAIL: nes region: $*" >&2
  [ ! -f "$cart/kernel.log" ] || grep -E '^(kernel|crash)' "$cart/kernel.log" | tail -10 >&2
  exit 1
}

shots=$carts/nes-region-shots
rm -rf "$shots" && mkdir -p "$shots"
# A fresh cart with ROM, made by tests/nes_rom.py with OPTION
new_cart() {
  rm -rf "$cart" && mkdir -p "$cart"
  python3 tests/nes_rom.py "$cart/$1" "$2"
  mkfs.fat -C -F 32 -n NES "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" "$cart/$1" "::/$1"
}

# Launch ROM, open PocketNES's menu, its Other Settings (DOWN five times,
# A) and the Region row (DOWN six times), screenshot it as SHOT, then the
# extra options
region_row() {
  local rom=$1 shot=$2 keys=() f=170
  shift 2
  for _ in 1 2 3 4 5; do
    keys+=(-k "@$f:DOWN" -k "@$((f + 6)):-")
    f=$((f + 14))
  done
  keys+=(-k "@$f:A" -k "@$((f + 6)):-")
  f=$((f + 30))
  for _ in 1 2 3 4 5 6; do
    keys+=(-k "@$f:DOWN" -k "@$((f + 6)):-")
    f=$((f + 14))
  done
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch "/$rom" at=100) \
    -w "launch: starting the game" -k @120:L+R -k @126:- "${keys[@]}" \
    -s "@$((f + 20)):$shots/$shot" "$@" >/dev/null 2>"$cart/kernel.log" || fail "the emulator failed"
  grep -q '^crash at frame' "$cart/kernel.log" && fail "the emulator caught a crash"
  grep -q '^kernel error:' "$cart/kernel.log" && fail "errors: $(grep '^kernel error:' "$cart/kernel.log" | head -3)"
  return 0
}
# 20 frames after region_row's screenshot
after=$((170 + 5 * 14 + 30 + 6 * 14 + 40))

# A NES 2.0 PAL game starts in PAL
new_cart Pal.nes pal
region_row Pal.nes pal.png -f 3000

# An iNES game starts in NTSC; A picks PAL, as the NES 2.0 game showed, and
# B twice leaves the menu, which writes the config
new_cart Save.nes save
region_row Save.nes ntsc.png -k "@$after:A" -k "@$((after + 6)):-" -s "@$((after + 30)):$shots/picked.png" \
  -k "@$((after + 50)):B" -k "@$((after + 56)):-" -k "@$((after + 80)):B" -k "@$((after + 86)):-" -f 3000
cmp -s "$shots/ntsc.png" "$shots/pal.png" && fail "the NES 2.0 header's PAL didn't change the region"
cmp -s "$shots/picked.png" "$shots/pal.png" || fail "A on Region didn't pick PAL ($shots/picked.png)"

# The next power-on copies the cart's SRAM to the card ("Save to SD card?", A)
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -w "screen ready: dialog" \
  -k @10:A -k @16:- -u "nor: wrote an empty pending-save record" -f 2000 >/dev/null 2>"$cart/kernel.log" || true
grep -qF "kernel: save: wrote /torii/saves/Save.nes/Main.sav" "$cart/kernel.log" || fail "the save wasn't copied to SD"

# Launched again, the game is in PAL
region_row Save.nes again.png -f 3000
cmp -s "$shots/again.png" "$shots/pal.png" || fail "the picked region is lost after a power-off ($shots/again.png)"
echo "PASS: PocketNES takes the region from a NES 2.0 header and keeps a picked one over a power-off"
