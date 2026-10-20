#!/usr/bin/env bash
# Save state test: launch the hello test ROM through the kernel, save a state
# in game mode, then load it in a fresh run. The cart must come back in game
# mode showing the same screen. Runs inside the torii-emu container after
# `just build`, `just emu-headless` and `just test-roms`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-state${1:+-$1}"

cart=$carts/state-test
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n STATE "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# shellcheck disable=SC2046 # nav.sh's options
first=$(OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin \
  $(nav_launch /Hello.gba at=160) -x "400:$cart/game.ss" -h 450 2>/dev/null)
grep -q "state frame 400" <<<"$first" || fail "no state written"
hash=$(sed -n 's/^hash frame 450: //p' <<<"$first")

second=$(OMEGA_DIR=$cart OMEGA_TRACE=1 emu/build-linux/mgba-shot dist/ezkernelnew.bin \
  -l "$cart/game.ss" -h 50 2>&1)
grep -q "state loaded: game mode" <<<"$second" || fail "cart state not loaded in game mode"
[ "$(sed -n 's/^hash frame 50: //p' <<<"$second")" = "$hash" ] || fail "screen differs after loading the state"
echo "PASS: save states keep the cart in game mode with the same screen"
