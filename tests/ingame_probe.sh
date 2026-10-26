#!/usr/bin/env bash
# The in-game probe ROM (tests/roms/ingame, v0.8 P0) on the emulated cart:
# launched CLEAN BOOT, it tries cart SRAM, the page register, a flash bank,
# a PSRAM write, the SD card and the clock in game mode, goes back to the
# kernel on A, and the kernel's boot logs what it saw. The emulated cart
# answers as it models the real one; the point is that the probe runs, puts
# everything back and reports. The emulator lets its PSRAM write and SD
# access through (OMEGA_GAME_PSRAM_WRITES, OMEGA_GAME_SD), which it stops
# from Torii's own in-game code.
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and `just test-roms`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-ingame-probe${1:+-$1}"

fail() {
  echo "FAIL: in-game probe: $*" >&2
  exit 1
}

cart=$carts/ingame-probe shots=dist/shots/ingame-probe
rm -rf "$cart" "$shots"
mkdir -p "$cart" "$shots"
mkfs.fat -C -F 32 -n PROBE "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/ingame/ingame.gba ::/Probe.gba

# CLEAN BOOT of Probe.gba; it runs by itself, then A goes back to the
# kernel, and A at its "Save sav?" lets the boot go on to the report.
k() { printf -- '-k %d:%s -k %d:- ' "$1" "$2" $(($1 + 4)); }
# shellcheck disable=SC2046
OMEGA_DIR=$cart OMEGA_GAME_PSRAM_WRITES=allow OMEGA_GAME_SD=allow emu/build-linux/mgba-shot dist/ezkernelnew.bin \
  -c 1767225600 $(nav_launch /Probe.gba at=180) -s 600:"$shots/1-done.png" $(k 620 A) $(k 900 A) \
  -s 1300:"$shots/2-menu.png" -f 1300 >"$cart/out.log" 2>"$cart/kernel.log" || true
log=$cart/kernel.log
! grep -q '^crash at frame' "$log" || fail "the emulator caught a crash"
grep -q "launch: CLEAN BOOT" "$log" || fail "not launched: $(tail -3 "$log")"
lba=$(sed -n "s/^kernel: launch: the in-game probe's scratch sector: \([0-9]*\)$/\1/p" "$log")
[ -n "$lba" ] && [ "$lba" != 0 ] || fail "the kernel gave no scratch sector"
has() { grep -qF "in-game probe: $1" "$log" || fail "no line '$1': $(grep 'in-game probe' "$log" | head -8)"; }
# The emulated cart: plain RAM on every page, page register write-only,
# no flash save (the banks alike), PSRAM read-only in game mode, the SD
# card and the clock as in kernel mode
has "cart SRAM pages 20-EF: 208 of 208 read back (first bad 00)"
has "SRAM wait states read back at 4 yes, 3 yes, 2 yes, 8 cycles yes"
has "flash bank: "
has "PSRAM 0x08010000 read 0000, 0000 after writing A55A, 0000 put back (PSRAM writes don't land)"
has "SD sector 0 read with its signature; scratch sector $lba: wrote and read back, the kernel's bytes put back"
has "clock with 0x096A0000 at 1: 26-01-01"
# The scratch file holds the kernel's bytes again
mcopy -n -i "$cart/sd.img" ::/torii/probe/sd.bin "$cart/scratch.bin"
python3 -c 'import sys; sys.stdout.buffer.write(bytes(((i * 7 + 0x5A) ^ (i >> 3)) & 0xFF for i in range(512)))' >"$cart/want.bin"
cmp -s "$cart/scratch.bin" "$cart/want.bin" || fail "the scratch file wasn't put back"
# The report is cleared once logged.
[ -z "$(od -A n -t x1 -v -j $((0xF1B00)) -N 16 "$cart/sram.bin" | tr -d ' \n0')" ] || fail "the report wasn't cleared"
echo "PASS: in-game probe"
