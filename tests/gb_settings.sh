#!/usr/bin/env bash
# Jagoomba keeps its VSync setting (jagoombacolor#36): VSync turned off in
# its menu (L+R, Other Settings) is written to its config record in the
# cart's SRAM, and after a power-off the next launch shows and uses it.
# Before Torii's patch, run(1) set it back to on at every start. Runs inside
# the torii-emu container after `just build` and `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-gb-settings${1:+-$1}"

cart=$carts/gb-settings
fail() {
  echo "FAIL: gb settings: $*" >&2
  [ ! -f "$cart/kernel.log" ] || grep -E '^(kernel|crash)' "$cart/kernel.log" | tail -10 >&2
  exit 1
}

rm -rf "$cart" && mkdir -p "$cart"
python3 tests/gb_rom.py "$cart/Save.gb" 8
mkfs.fat -C -F 32 -n GB "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" "$cart/Save.gb" ::/Save.gb

# Launch Save.gb, open Jagoomba's menu and its Other Settings (DOWN three
# times, A), screenshot it as $1, then the extra options
settings() {
  local shot=$1
  shift
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Save.gb at=100) \
    -w "launch: starting the game" -k @120:L+R -k @126:- \
    -k @170:DOWN -k @176:- -k @184:DOWN -k @190:- -k @198:DOWN -k @204:- -k @212:A -k @218:- \
    -s "@240:$cart/$shot" "$@" >/dev/null 2>"$cart/kernel.log" || fail "the emulator failed"
  grep -q '^crash at frame' "$cart/kernel.log" && fail "the emulator caught a crash"
  grep -q '^kernel error:' "$cart/kernel.log" && fail "errors: $(grep '^kernel error:' "$cart/kernel.log" | head -3)"
  return 0
}

# VSync is on, A turns it off, and B twice leaves the menu, which writes the config
settings on.png -k @250:A -k @256:- -s "@280:$cart/off.png" -k @300:B -k @306:- -k @330:B -k @336:- -f 3000
cmp -s "$cart/on.png" "$cart/off.png" && fail "A on VSync didn't change it"

# The next power-on copies the cart's SRAM to the card ("Save to SD card?", A)
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -w "screen ready: dialog" \
  -k @10:A -k @16:- -u "nor: wrote an empty pending-save record" -f 2000 >/dev/null 2>"$cart/kernel.log" || true
grep -qF "kernel: save: wrote /torii/saves/Save.gb/Main.sav" "$cart/kernel.log" || fail "the save wasn't copied to SD"

# Launched again, the menu shows VSync off
settings again.png -f 600
cmp -s "$cart/off.png" "$cart/again.png" || fail "VSync is back on after a power-off ($cart/again.png)"
echo "PASS: Jagoomba's VSync setting is kept over a power-off"
