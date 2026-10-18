#!/usr/bin/env bash
# Clean boot test on the emulated cart: launch a ROM that never touches
# memory, then check the kernel's launch reset left EWRAM and IWRAM all
# zero, as some games expect. Runs inside the torii-emu container after `just build`,
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
  -m 400:0x02000000:0x40000 -m 400:0x03000000:0x8000 2>/dev/null >"$cart/ram.txt"
grep '^mem frame 400 020' "$cart/ram.txt" >"$cart/ewram.txt" || true
grep '^mem frame 400 030' "$cart/ram.txt" >"$cart/iwram.txt" || true
[ "$(wc -l <"$cart/ewram.txt")" = 16384 ] || fail "EWRAM dump incomplete"
[ "$(wc -l <"$cart/iwram.txt")" = 2048 ] || fail "IWRAM dump incomplete"
dirty=$(grep -c ': .*[1-9A-F]' "$cart/ewram.txt" || true)
[ "$dirty" = 0 ] || fail "$dirty EWRAM lines still hold data when the game starts"
# The kernel's reset code used to stay behind in IWRAM.
dirty=$(grep -c ': .*[1-9A-F]' "$cart/iwram.txt" || true)
[ "$dirty" = 0 ] || fail "$dirty IWRAM lines still hold data when the game starts: $(grep -m 3 ': .*[1-9A-F]' "$cart/iwram.txt")"

echo "PASS: EWRAM and IWRAM are clear when a game starts"
