#!/usr/bin/env bash
# The in-game menu under each save type, and its stub failing safe
# (docs/design/v0.8/ingame-menu.md). Runs inside the torii-emu container.
#
# tests/menu_safety.sh TYPE: hello launched with BOOT WITH ADDON and save
# type TYPE (sram, eeprom8k, eeprom512, flash64k, flash128k, auto); L+R+START
# must open the menu, B close it, and the game come back as it was
# (tests/hook_watch.lua), then a save state round trip.
#
# tests/menu_safety.sh FAULT: the same at FLASH 128K, with cart SRAM failing
# in game mode (OMEGA_GAME_SRAM in the cart model): drop (writes past the
# save memory lost), flip-image (a byte of the overlay image reads wrong) or
# chip (the save chip on every page: the page register ignored while the
# FPGA presents a flash chip, unproven either way on the real cart).
# The stub must not open the menu and must give the game back untouched,
# never a black screen or a hang (Pokémon Emerald on the real cart, v0.8):
# the game goes on, its screen as before, and with flip-image the stub's
# record (struct ingame_refused) reaches the kernel's next boot's log.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
mode=${1:?usage: tests/menu_safety.sh TYPE|FAULT}
local_carts "dist/cart-menu-safety-$mode"

fault=
case $mode in
auto) save=0 ;;
sram) save=1 ;;
eeprom8k) save=2 ;;
eeprom512) save=3 ;;
flash64k) save=4 ;;
flash128k) save=5 ;;
drop | flip-image | chip) save=5 fault=$mode ;;
*)
  echo "unknown mode $mode" >&2
  exit 2
  ;;
esac

cart=$carts/menu
mkdir -p "$cart"
mkfs.fat -C -F 32 -n MENU "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba

fail() {
  echo "FAIL ($mode): $*" >&2
  [ -z "${log:-}" ] || grep -E '^(script|crash|omega:)' <<<"$log" | grep -v ' vblank ' | tail -20 >&2
  exit 1
}

# Power-on 1 makes the cart; then the settings (as tests/menu.sh): English,
# RESET and SAVESTATE on, sleep key L+R+SELECT, menu key L+R+START
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -f 100 >/dev/null 2>&1
python3 tests/setnor.py set "$cart/kernel-nor.bin" 0=0xE1E1 1=1 2=1 3=0 4=0 5=9 6=8 7=2 8=9 9=8 10=3 11=1 12=0 13=1 25=1

# Power-on 2: the launch, the menu key, B (hook_watch.lua), the screen's
# hash every frame, and the EWRAM the overlay borrows (its window, 0x1FC0
# bytes) every 10 frames
ewram=$(for f in $(seq 500 10 1500); do printf -- '-m %d:0x02000000:0x1FC0 ' "$f"; done)
# shellcheck disable=SC2046,SC2086 # $(nav_launch) and $ewram are lists of options
out=$(OMEGA_DIR=$cart OMEGA_GAME_SRAM=$fault emu/build-linux/mgba-shot dist/ezkernelnew.bin \
  $(nav_launch /Hello.gba addon save=$save at=270) -L tests/hook_watch.lua -u 'hook watch done' -f 4000 \
  -H 1:4000 $ewram 2>"$cart/log") || fail "the run stopped with status $?"
log=$(cat "$cart/log")
grep -q '^crash at frame' <<<"$log" && fail "crashed"
grep -q '^script: hook [0-9]* patch ' <<<"$log" || fail "the kernel did not hook the game"
grep -q '^script: hook watch done' <<<"$log" || fail "the run ended early"
key=$(sed -n 's/^script: hook \([0-9]*\) key L+R+START$/\1/p' <<<"$log" | head -1)
[ -n "$key" ] || fail "no menu key pressed"
hash_at() { awk -v f="$1" '$1 == "hash" && $3 == f ":" { print $4 }' <<<"$out"; }
screens_after() { awk -v f="$1" '$1 == "hash" && $3 + 0 > f { print $4 }' <<<"$out" | sort -u | wc -l; }

if [ -z "$fault" ]; then
  grep -q '^script: hook [0-9]* menu open$' <<<"$log" || fail "the menu did not open"
  grep -q '^script: hook [0-9]* menu left the game as it was$' <<<"$log" ||
    fail "the menu didn't leave the game as it was: $(grep -o 'menu changed .*' <<<"$log")"
  [ "$(screens_after $((key + 100)))" -gt 1 ] || fail "the game did not go on after the menu"
  [ "$(grep -cx 'kernel: screen ready: ingame' <<<"$log")" -ge 3 ] ||
    fail "the menu didn't open for the save state round trip"
  echo "PASS: the in-game menu at save type $mode opens, gives the game back as it was and saves and loads a state"
  exit 0
fi

# Faults: the menu never shows, the screen is the game's
grep -q '^script: hook [0-9]* menu open$' <<<"$log" && fail "the menu opened from cart SRAM that failed"
grep -q 'screen ready: ingame' <<<"$log" && fail "the overlay ran from cart SRAM that failed"
[ -n "$(hash_at $((key - 1)))" ] || fail "no screen hash before the menu key"
[ "$(screens_after "$key")" -gt 1 ] || fail "the game did not go on after the menu key"
# The EWRAM window as it was: the last dump before the key against the one
# 10 to 20 frames after it (hello leaves its EWRAM alone)
ewram_at() { awk -v f="$1" '$1 == "mem" && $3 + 0 == f { $1 = $2 = $3 = ""; print; exit }' <<<"$out"; }
before=$(ewram_at $(((key - 1) / 10 * 10)))
after=$(ewram_at $(((key + 19) / 10 * 10)))
[ -n "$before" ] && [ "$before" = "$after" ] || fail "the game's EWRAM window changed under the refused menu"

if [ "$mode" = flip-image ]; then
  # The stub's record in page 0xF0, read by the kernel's next boot (after a
  # soft reset: page 0xF0 has no battery)
  rec=$(od -A n -t x1 -j $((0xF1C00)) -N 10 "$cart/sram.bin" | tr -d ' \n')
  [ "$rec" = 54494e52464544310300 ] || fail "no refusal record (\"TINRFED1\", reason 3) in cart SRAM: $rec"
  boot_log=$(OMEGA_DIR=$cart OMEGA_SOFT_RESET=1 emu/build-linux/mgba-shot dist/ezkernelnew.bin \
    -k 100:A -k 106:- -f 400 2>&1 >/dev/null) # A: the save dialog first
  grep -q "in-game menu: the stub didn't open it, the overlay image loaded from cart SRAM didn't check" <<<"$boot_log" ||
    fail "the kernel's next boot didn't log the refusal"
fi
echo "PASS: with cart SRAM failing in game mode ($mode) the stub leaves the menu shut and the game goes on"
