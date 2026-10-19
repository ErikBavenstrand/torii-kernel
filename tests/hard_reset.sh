#!/usr/bin/env bash
# Hard reset launch test on the emulated cart: hold L while picking CLEAN BOOT,
# so the kernel starts the game through the BIOS HardReset (swi 0x26) instead
# of SoftReset. Checks the game runs and EWRAM is all zero when it starts.
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/idle.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
local_carts "dist/cart-hard-reset${1:+-$1}"

cart=$carts/hard-reset
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n HARDRESET "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/idle/idle.gba ::/Idle.gba

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Idle.gba is the only entry: open it, then pick CLEAN BOOT with L held.
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:A -k 106:- \
  -k 150:L -k 160:L+A -k 166:L -k 170:- \
  -r 400 -m 400:0x02000000:0x40000 >"$cart/out.txt" 2>&1
! grep -i crash "$cart/out.txt" || fail "the launch crashed"
# The game sits in its idle loop at 0x080000C0 (r15 reads 4 bytes ahead).
pc=$(grep -o 'r15=[0-9A-F]*' "$cart/out.txt" | cut -d= -f2)
[ "$pc" = 080000C4 ] || fail "the game is not running (pc ${pc:-unknown})"
[ "$(grep -c '^mem frame 400 ' "$cart/out.txt")" = 16384 ] || fail "EWRAM dump incomplete"
dirty=$(grep '^mem frame 400 ' "$cart/out.txt" | grep -c ': .*[1-9A-F]' || true)
[ "$dirty" = 0 ] || fail "$dirty EWRAM lines still hold data when the game starts"

echo "PASS: a hard reset launch starts the game with EWRAM clear"
