#!/usr/bin/env bash
# The rumble probe ROM (tests/roms/rumble) on the emulated cart: launched CLEAN
# BOOT, it drives the motor four ways, takes an answer after each, goes back
# to the kernel, and the kernel's boot logs the answers. The emulated cart has
# no motor; its trace shows what the real one does.
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and `just test-roms`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-rumble-probe${1:+-$1}"

fail() {
  echo "FAIL: rumble probe: $*" >&2
  exit 1
}

cart=$carts/rumble-probe shots=dist/shots/rumble-probe
rm -rf "$cart" "$shots"
mkdir -p "$cart" "$shots"
mkfs.fat -C -F 32 -n RUMBLE "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/rumble/rumble.gba ::/Probe.gba

# CLEAN BOOT of Probe.gba. Then each try: A runs it (60 frames), and the
# answers are A (yes), B (no), SELECT (skip), A. A goes back to the kernel,
# and A at its "Save sav?" lets the boot go on to the report.
k() { printf -- '-k %d:%s -k %d:- ' "$1" "$2" $(($1 + 4)); }
# shellcheck disable=SC2046
OMEGA_DIR=$cart OMEGA_TRACE=1 emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Probe.gba at=180) \
  -s 420:"$shots/1-try.png" $(k 420 A) -s 500:"$shots/2-answer.png" $(k 500 A) \
  $(k 540 A) $(k 620 B) $(k 660 A) $(k 740 SELECT) $(k 780 A) $(k 860 A) \
  -s 900:"$shots/3-done.png" $(k 900 A) -s 1100:"$shots/4-save.png" $(k 1100 A) -s 1400:"$shots/5-menu.png" -f 1400 >"$cart/out.log" 2>"$cart/kernel.log" || true
log=$cart/kernel.log
! grep -q '^crash at frame' "$log" || fail "the emulator caught a crash"
grep -q "launch: CLEAN BOOT" "$log" || fail "not launched: $(tail -3 "$log")"
grep -q "rumble probe: 4 tries in game mode" "$log" || fail "no report: $(tail -5 "$log")"
for line in "pulsed as Jagoomba does (0xF1 at 0x09E20000, then 2 and 0 at 0x08001000 each frame): yes" \
  "0x08001000 held at 2 (after 0xF1 at 0x09E20000): no" \
  "pulsed as Jagoomba does, with 0x096A0000 at 0 (GPIO port off): skipped" \
  "GPIO pin 3 held on, as Drill Dozer does (enable 1, direction 8, data 8): yes"; do
  grep -qF "rumble probe: $line" "$log" || fail "no line '$line'"
done
# The three tries that write 0x08001000 log what it read.
[ "$(grep -c 'rumble probe: .*; 0x08001000 read [0-9A-F]*, [0-9A-F]* after 2, [0-9A-F]* after 0$' "$log")" = 3 ] ||
  fail "not three tries with 0x08001000 reads"
# What the cart's motor did: 60 pulses (and a last off), a hold, nothing with
# the port off, and GPIO pin 3 on and off.
motor=$(grep -o '^omega [0-9]*: rumble .*' "$log" | cut -d' ' -f3- | sort | uniq -c | tr -s ' ' | paste -sd ,)
[ "$motor" = " 62 rumble off, 1 rumble off (GPIO pin 3), 61 rumble on, 1 rumble on (GPIO pin 3), 121 rumble write ignored: GPIO port off" ] ||
  fail "the cart saw the motor as: $motor"
# The report is cleared once logged.
[ -z "$(od -A n -t x1 -v -j $((0xF1A00)) -N 16 "$cart/sram.bin" | tr -d ' \n0')" ] || fail "the report wasn't cleared"
echo "PASS: rumble probe"
