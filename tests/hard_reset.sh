#!/usr/bin/env bash
# Hard reset launch test on the emulated cart: CLEAN BOOT with L held,
# so the kernel starts the game through the BIOS HardReset (swi 0x26) instead
# of SoftReset. Checks the game runs and EWRAM is all zero when it starts.
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/idle.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
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

# CLEAN BOOT of Idle.gba with L held.
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Idle.gba hard at=160) \
  -r 400 -m 400:0x02000000:0x40000 >"$cart/out.txt" 2>&1
! grep -i crash "$cart/out.txt" || fail "the launch crashed"
# The game sits in its idle loop at 0x080000C0 (r15 reads 4 bytes ahead).
pc=$(grep -o 'r15=[0-9A-F]*' "$cart/out.txt" | cut -d= -f2)
[ "$pc" = 080000C4 ] || fail "the game is not running (pc ${pc:-unknown})"
[ "$(grep -c '^mem frame 400 ' "$cart/out.txt")" = 16384 ] || fail "EWRAM dump incomplete"
dirty=$(grep '^mem frame 400 ' "$cart/out.txt" | grep -c ': .*[1-9A-F]' || true)
[ "$dirty" = 0 ] || fail "$dirty EWRAM lines still hold data when the game starts"

echo "PASS: a hard reset launch starts the game with EWRAM clear"

# The same from the NOR page (B15): Idle.gba written to the game NOR, then
# A on its row with L held, as v0.3's NOR list did.
cart=$carts/hard-reset-nor
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n HARDRESET "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/idle/idle.gba ::/Idle.gba
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Idle.gba nor at=160) -f 700 \
  >"$cart/write.txt" 2>&1
grep -q 'nor: list saved, 1 games' "$cart/write.txt" || fail "Idle.gba wasn't written to NOR"
# shellcheck disable=SC2046
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_nor_launch_page 0 hard) \
  -w "launch: loading Idle.gba from NOR" -r @240 -m @240:0x02000000:0x40000 >"$cart/out.txt" 2>&1
! grep -i crash "$cart/out.txt" || fail "the NOR launch crashed"
grep -q 'nor: launch 0, hard reset' "$cart/out.txt" || fail "L held with A on the NOR page didn't ask for a hard reset"
pc=$(grep -o 'r15=[0-9A-F]*' "$cart/out.txt" | cut -d= -f2)
[ "$pc" = 080000C4 ] || fail "the NOR game is not running (pc ${pc:-unknown})"
mem=$(grep '^mem frame [0-9]* 02' "$cart/out.txt")
[ "$(printf '%s\n' "$mem" | wc -l)" = 16384 ] || fail "EWRAM dump incomplete (NOR)"
dirty=$(printf '%s\n' "$mem" | grep -c ': .*[1-9A-F]' || true)
[ "$dirty" = 0 ] || fail "$dirty EWRAM lines still hold data when the NOR game starts"

echo "PASS: L held with A on the NOR page starts its game through a hard reset, EWRAM clear"
