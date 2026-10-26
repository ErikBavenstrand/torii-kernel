#!/usr/bin/env bash
# Cheats (v0.7) where they could harm the game or its files.
#
#   irq    The cheats run exactly once per VBlank, whatever the game's IRQ
#          handler does (gba_rts_patch.s): tests/roms/cheattest's cheatirq
#          serves one IRQ an entry, HBlank before VBlank, under a cheat set
#          at the interpreter's cycle cap; cheatirq-vblank enables VBlank
#          alone. Neither may freeze, and an add-1 cheat must move its
#          counter by exactly 1 a frame.
#
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and `just test-roms`.
set -euo pipefail

# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh

mode=${1:?usage: cheat_safety.sh irq}
local_carts "dist/cart-cheat-safety-$mode"

fail() {
  echo "FAIL: cheat_safety $mode: $*" >&2
  exit 1
}

boot() { OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@"; }

# new_cart NAME ROM CARD_NAME CHT: a cart with ROM at /CARD_NAME and CHT as its
# cheat file
new_cart() {
  cart=$carts/$1
  rm -rf "$cart" && mkdir -p "$cart"
  mkfs.fat -C -F 32 -n CHEATS "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" "$2" "::/$3"
  mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
  mcopy -i "$cart/sd.img" "$4" "::/torii/cheats/${3%.*}.cht"
}

case $mode in
irq)
  # CodeBreaker: add 1 to the halfword at 0x0203F000; a slide of 550
  # halfwords (the cycle model's 26,892, the set's 27,682 of 28,000)
  cht=$carts/irq.cht
  mkdir -p "$carts"
  printf '%s\n' 'cheats = 2' \
    'cheat0_desc = "Count frames"' 'cheat0_code = "E203F000 0001"' 'cheat0_enable = true' \
    'cheat1_desc = "Heavy slide"' 'cheat1_code = "42010000 0000+00010226 0002"' 'cheat1_enable = true' >"$cht"
  for rom in cheatirq cheatirq-vblank; do
    new_cart "$rom" "tests/roms/cheattest/$rom.gba" CI.gba "$cht"
    # shellcheck disable=SC2046 # nav.sh's options
    boot $(nav_launch /CI.gba addon) -w "launch: starting the game" -u "cheatirq: frame 0600" -f 3000 \
      >"$cart/run.txt" 2>&1 || true
    grep -q "cheats: .*CI.cht: 2 picked, 2 run in game" "$cart/run.txt" ||
      fail "$rom: the launch read: $(grep -a 'cheats:' "$cart/run.txt" | head -3)"
    last=$(grep -a "cheatirq: frame" "$cart/run.txt" | tail -1)
    case $last in
    *"frame 0600 bad 0000"*) ;;
    *) fail "$rom: the game froze or the cheat didn't run once a frame: ${last:-no frame line}" ;;
    esac
    if [ "$rom" = cheatirq ]; then
      h=${last##* hblank }
      [ "$((10#$h))" -gt 100000 ] || fail "$rom: only $h HBlank IRQs in 600 frames"
    fi
  done
  echo "PASS: cheat_safety: cheats at the cycle cap run once per VBlank, with a handler that serves HBlank first and with VBlank alone"
  ;;
*)
  fail "no mode $mode"
  ;;
esac
