#!/usr/bin/env bash
# Clean boot test on the emulated cart: launch a ROM that never touches
# memory, then check the kernel's launch reset left EWRAM all zero, as some
# games expect, and WAITCNT at the BIOS's 0 (the menu runs at 0x4018,
# source/waitcnt.h). Runs inside the torii-emu container after `just build`,
# `just emu-headless` and building tests/roms/idle.
set -euo pipefail

cart=dist/cart-clean-boot
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n CLEANBOOT "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/idle/idle.gba ::/Idle.gba

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Idle.gba is the only entry: open it, then pick CLEAN BOOT.
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:A -k 106:- -k 160:A -k 166:- \
  -m 90:0x04000204:2 -m 400:0x04000204:2 -m 400:0x02000000:0x40000 2>/dev/null >"$cart/ewram.txt"
[ "$(grep -c '^mem frame 400 02' "$cart/ewram.txt")" = 16384 ] || fail "EWRAM dump incomplete"
dirty=$(grep '^mem frame 400 02' "$cart/ewram.txt" | grep -c ': .*[1-9A-F]' || true)
[ "$dirty" = 0 ] || fail "$dirty EWRAM lines still hold data when the game starts"

grep -qx 'mem frame 90 04000204: 18 40' "$cart/ewram.txt" || fail "WAITCNT in the menu isn't 0x4018"
grep -qx 'mem frame 400 04000204: 00 00' "$cart/ewram.txt" || fail "the game started with WAITCNT $(grep 'frame 400 04000204' "$cart/ewram.txt")"

echo "PASS: EWRAM is clear and WAITCNT 0 when a game starts"
