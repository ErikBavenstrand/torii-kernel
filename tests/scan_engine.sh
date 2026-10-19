#!/usr/bin/env bash
# BOOT WITH ADDON with the scan engine forced (ENGINE setting, slot 11 = 0):
# the kernel loads the ROM itself, scans it for the IRQ handler's literals
# and writes what it found to PATCH/ (source/ezkernelnew.c, Make_pat_file in
# source/GBApatch.c); the next launch uses that file instead of scanning.
# Both launches must hook the game's VBlank IRQ, so the in-game menu opens
# and closes (tests/hook_watch.lua). Runs inside the torii-emu container
# after `just build`, `just emu-headless` and building tests/roms/hello.
set -euo pipefail

cart=dist/cart-scan
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n SCAN "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba

fail() {
  echo "FAIL: scan engine: $*" >&2
  [ ! -f "$cart/run.log" ] || grep -E '^(kernel|script|crash)' "$cart/run.log" | grep -v ' vblank ' | tail -12 >&2
  exit 1
}

# Settings slots (source/ez_define.h) in the kernel NOR at 0x7B0000, as in
# tests/menu.sh but with the scan engine: English, RESET and SAVESTATE on,
# sleep key L+R+SELECT, menu key L+R+START, scan engine, clock and rumble on.
settings() {
  local slot value
  for slot in "$@"; do
    value=${slot#*=}
    printf %b "\\x$(printf %02x $((value & 0xFF)))\\x$(printf %02x $((value >> 8)))" |
      dd of="$cart/kernel-nor.bin" bs=1 seek=$((0x7B0000 + 2 * ${slot%=*})) conv=notrunc status=none
  done
}
head -c $((8 << 20)) /dev/zero | tr '\0' '\377' >"$cart/kernel-nor.bin"
settings 0=0xE1E1 1=1 2=1 3=0 4=0 5=9 6=8 7=2 8=9 9=8 10=3 11=0 12=0 13=1 25=1

# keys FRAME KEY... -> mgba-shot press/release pairs, one key every 8 frames.
keys() {
  local frame=$1
  shift
  for key in "$@"; do
    printf -- '-k %d:%s -k %d:- ' "$frame" "$key" $((frame + 4))
    frame=$((frame + 8))
  done
}

# Runs the kernel with the hook watcher until it is done; the log goes to run.log
watch() {
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" \
    -L tests/hook_watch.lua -u 'hook watch done' -f 4000 >/dev/null 2>"$cart/run.log" || true
  ! grep -q '^crash at frame' "$cart/run.log" || fail "crashed"
  grep -q '^kernel error:' "$cart/run.log" && fail "errors: $(grep '^kernel error:' "$cart/run.log" | head -3)"
  grep -q '^script: hook [0-9]* patch ' "$cart/run.log" || fail "the kernel did not hook the game"
  grep -q '^script: hook [0-9]* menu open$' "$cart/run.log" || fail "the menu did not open"
  grep -q '^script: hook [0-9]* menu closed$' "$cart/run.log" || fail "B did not close the menu"
  grep -q '^script: hook watch done' "$cart/run.log" || fail "the run ended early"
}

# Power-on 1: Hello.gba is the only entry. Open it, then BOOT WITH ADDON.
# shellcheck disable=SC2046 # $(keys) is a list of options
watch $(keys 100 A) $(keys 160 DOWN A)
grep -qF "kernel: launch: IRQ hook from a scan of the ROM (scan engine)" "$cart/run.log" ||
  fail "the ROM wasn't scanned: $(grep -F 'IRQ hook' "$cart/run.log")"
mdir -i "$cart/sd.img" ::/PATCH/Hello.pat >/dev/null 2>&1 || fail "the scan wasn't written to PATCH/Hello.pat"
echo "PASS: BOOT WITH ADDON with the scan engine scans the ROM, hooks the game and writes PATCH/Hello.pat"

# Power-on 2: copy the save (A at "Save sav?"), then down to the game
# (PATCH, RTS and SAVER sort first) and BOOT WITH ADDON again.
# shellcheck disable=SC2046
watch $(keys 100 A) $(keys 320 DOWN DOWN DOWN A) $(keys 400 DOWN A)
grep -qF "kernel: launch: IRQ hook from PATCH/ (cached scan)" "$cart/run.log" ||
  fail "the second launch didn't use PATCH/Hello.pat: $(grep -F 'IRQ hook' "$cart/run.log")"
echo "PASS: the next BOOT WITH ADDON hooks the game from PATCH/Hello.pat without scanning"
