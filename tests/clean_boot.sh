#!/usr/bin/env bash
# Clean boot test on the emulated cart: launch a ROM that never touches
# memory, then check the kernel's launch reset left EWRAM and IWRAM all
# zero, as some games expect, and WAITCNT at the BIOS's 0 (the menu runs at
# 0x4018, source/waitcnt.h). Runs inside the torii-emu container after `just build`,
# `just emu-headless` and building tests/roms/idle.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-clean-boot${1:+-$1}"

cart=$carts/clean-boot
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n CLEANBOOT "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/idle/idle.gba ::/Idle.gba

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# CLEAN BOOT of Idle.gba, once the menu is up.
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Idle.gba at=160) \
  -m 90:0x04000204:2 -m 400:0x04000204:2 -m 400:0x02000000:0x40000 -m 400:0x03000000:0x8000 2>/dev/null >"$cart/ram.txt"
grep '^mem frame 400 020' "$cart/ram.txt" >"$cart/ewram.txt" || true
grep '^mem frame 400 030' "$cart/ram.txt" >"$cart/iwram.txt" || true
[ "$(wc -l <"$cart/ewram.txt")" = 16384 ] || fail "EWRAM dump incomplete"
[ "$(wc -l <"$cart/iwram.txt")" = 2048 ] || fail "IWRAM dump incomplete"
dirty=$(grep -c ': .*[1-9A-F]' "$cart/ewram.txt" || true)
[ "$dirty" = 0 ] || fail "$dirty EWRAM lines still hold data when the game starts"
# The kernel's reset code used to stay behind in IWRAM.
dirty=$(grep -c ': .*[1-9A-F]' "$cart/iwram.txt" || true)
[ "$dirty" = 0 ] || fail "$dirty IWRAM lines still hold data when the game starts: $(grep -m 3 ': .*[1-9A-F]' "$cart/iwram.txt")"

grep -qx 'mem frame 90 04000204: 18 40' "$cart/ram.txt" || fail "WAITCNT in the menu isn't 0x4018"
grep -qx 'mem frame 400 04000204: 00 00' "$cart/ram.txt" || fail "the game started with WAITCNT $(grep 'frame 400 04000204' "$cart/ram.txt")"

echo "PASS: EWRAM and IWRAM are clear, and WAITCNT 0, when a game starts"
