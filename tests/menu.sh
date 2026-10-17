#!/usr/bin/env bash
# In-game menu test on the emulated cart: save once with the hello test ROM,
# then launch it with BOOT WITH ADDON (SAVESTATE and RESET on) and a FLASH
# 64K save chip, open the in-game menu with L+R+START and leave it with B
# (tests/hook_watch.lua). The kernel must hook the ROM's VBlank IRQ, the menu
# must keep the screen in cart SRAM past the save chip and hand it back, the
# game must go on, and loading the save-state buffer into cart SRAM must
# leave the game's save alone. Runs inside the torii-emu container.
set -euo pipefail

cart=dist/cart-menu
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n MENU "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba

boot() {
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@"
}

fail() {
  echo "FAIL: $*" >&2
  [ -z "${log:-}" ] || grep -E '^(script|crash)' <<<"$log" | grep -v ' vblank ' | tail -20 >&2
  exit 1
}

# Power-on 1 and 2: CLEAN BOOT writes the first save, then the kernel copies
# it to SAVER/ (as in tests/smoke.sh).
boot -k 100:A -k 106:- -k 160:A -k 166:- -f 400 >/dev/null 2>&1
boot -k 100:A -k 106:- -f 300 >/dev/null 2>&1
[ "$(mtype -i "$cart/sd.img" ::/SAVER/Hello.sav | head -c 5 | od -A n -t x1 | tr -d ' \n')" = 4f4d525801 ] ||
  fail "no first save to start from"

# Settings slots (source/ez_define.h) in the kernel NOR at 0x7B0000: English,
# RESET and SAVESTATE on, sleep key L+R+SELECT, menu key L+R+START, fast patch
# engine, clock and rumble on.
settings() {
  local slot value
  for slot in "$@"; do
    value=${slot#*=}
    printf %b "\\x$(printf %02x $((value & 0xFF)))\\x$(printf %02x $((value >> 8)))" |
      dd of="$cart/kernel-nor.bin" bs=1 seek=$((0x7B0000 + 2 * ${slot%=*})) conv=notrunc status=none
  done
}
settings 0=0xE1E1 1=1 2=1 3=0 4=0 5=9 6=8 7=2 8=9 9=8 10=3 11=1 12=0 13=1 25=1
rm -rf "$cart-state"
cp -r "$cart" "$cart-state"  # for the save-state run below

# keys FRAME KEY... -> mgba-shot press/release pairs, one key every 8 frames.
keys() {
  local frame=$1
  shift
  for key in "$@"; do
    printf -- '-k %d:%s -k %d:- ' "$frame" "$key" $((frame + 4))
    frame=$((frame + 8))
  done
}

# Power-on 3: down to the game (SAVER sorts first), open it, SAVE TYPE FLASH
# 64K (four steps right of AUTO), up to BOOT WITH ADDON. A blue background
# pixel, dumped every 10 frames, shows the menu gave the screen back.
pixel=$((0x06000000 + (80 * 240 + 120) * 2))
dumps=$(for f in $(seq 10 10 4000); do printf -- '-m %d:0x%x:2 -h %d ' "$f" "$pixel" "$f"; done)
# Memory dumps and screen hashes go to stdout, the script's lines to stderr.
# shellcheck disable=SC2046,SC2086 # $(keys) and $dumps are lists of options
out=$(boot $(keys 100 DOWN A) $(keys 180 DOWN DOWN DOWN DOWN RIGHT RIGHT RIGHT RIGHT UP UP UP A) \
  -L tests/hook_watch.lua -u 'hook watch done' -f 4000 $dumps 2>"$cart/log")
log=$(cat "$cart/log")

grep -q '^crash at frame' <<<"$log" && fail "crashed"
grep -q '^script: hook [0-9]* patch ' <<<"$log" || fail "the kernel did not hook the game"
opened=$(sed -n 's/^script: hook \([0-9]*\) menu open$/\1/p' <<<"$log" | head -1)
[ -n "$opened" ] || fail "the menu did not open"
closed=$(awk -v o="$opened" '$1 == "script:" && $4 == "menu" && $5 == "closed" && $3 > o { print $3; exit }' <<<"$log")
[ -n "$closed" ] || fail "B did not close the menu"
grep -q '^script: hook watch done' <<<"$log" || fail "the run ended early"

# The background is back, and the marker still moves.
after=$(awk -v c="$closed" '$1 == "mem" && $3 > c + 10 { print $5 $6; exit }' <<<"$out")
[ "$after" = C250 ] || fail "screen not restored after the menu (pixel $after, want C250)"
screens=$(awk -v c="$closed" '$1 == "hash" && $3 + 0 > c + 10 { print $4 }' <<<"$out" | sort -u | wc -l)
[ "$screens" -gt 1 ] || fail "the game did not go on after the menu"

# The menu kept the screen in cart SRAM from page 0x20 (0x20000), past the
# save memory, not in the game's save chip.
backup=$(od -A n -t x1 -j $((0x20000 + pixel - 0x06000000)) -N 2 "$cart/sram.bin" | tr -d ' \n')
[ "$backup" = c250 ] || fail "the menu's screen backup is not in cart SRAM page 0x20 (got $backup)"

# The save came back intact through the save-state buffer load. (The flash
# chip ignores the ROM's plain SRAM writes, so this boot isn't counted.)
od -A n -t x1 -N 5 "$cart/sram.bin" | tr -d ' \n' | grep -q '4f4d525801' ||
  fail "the game's save did not survive the addon launch (SRAM $(od -A n -t x1 -N 5 "$cart/sram.bin"))"

# A save state round trip on a copy of the cart from before power-on 3:
# the same launch, then from the menu SAVE (down one, A), and later LOAD
# (down two, A). The state holds the menu's IME = 0, so loading must turn
# IME back on, or the game stops at its next VBlankIntrWait.
key=$(sed -n 's/^script: hook \([0-9]*\) key L+R+START$/\1/p' <<<"$log" | head -1)
load=$((key + 300))
hashes=$(for f in $(seq $((load + 100)) 20 $((load + 400))); do printf -- '-h %d ' "$f"; done)
# shellcheck disable=SC2046,SC2086 # $(keys) and $hashes are lists of options
out=$(OMEGA_DIR=$cart-state emu/build-linux/mgba-shot dist/ezkernelnew.bin \
  $(keys 100 DOWN A) $(keys 180 DOWN DOWN DOWN DOWN RIGHT RIGHT RIGHT RIGHT UP UP UP A) \
  -k "$key:L+R+START" -k "$((key + 6)):-" $(keys $((key + 40)) DOWN A) \
  -k "$load:L+R+START" -k "$((load + 6)):-" $(keys $((load + 40)) DOWN DOWN A) \
  -m $((load + 400)):0x04000208:2 $hashes -f $((load + 400)) 2>"$cart/log")
log=$(cat "$cart/log")
grep -q '^crash at frame' <<<"$log" && fail "crashed in the save state run"
[ "$(awk '$1 == "mem" { print $5 $6 }' <<<"$out")" = 0100 ] || fail "IME is off after loading a state"
[ "$(awk '$1 == "hash" { print $4 }' <<<"$out" | sort -u | wc -l)" -gt 1 ] ||
  fail "the game did not go on after loading a state"
rm -rf "$cart-state"

echo "PASS: in-game menu opens on L+R+START, B returns to the game with its screen, a state saves and loads, and the save survives"
