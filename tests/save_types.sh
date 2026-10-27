#!/usr/bin/env bash
# Save chip test on the emulated cart: for each save type, launch the matching
# save test ROM with that type picked in the kernel menu, power-cycle so the
# kernel writes the save to SD, and launch again to check the save came back.
# Usage: save_types.sh [flash128|flash64|eeprom8k|eeprom8k-short|eeprom512-shrunk|eeprom8k-to-flash|eeprom512]
# Runs inside the torii-emu container after `just build`,
# `just emu-headless` and building tests/roms/savetest.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-save-types${1:+-$1}"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# keys FRAME KEY... -> mgba-shot press/release pairs, one key every 8 frames.
keys() {
  local frame=$1
  shift
  for key in "$@"; do
    printf -- '-k %d:%s -k %d:- ' "$frame" "$key" $((frame + 4))
    frame=$((frame + 8))
  done
}

hex_at() { # file offset -> 5 bytes as hex
  od -A n -t x1 -j "$2" -N 5 "$1" | tr -d ' \n'
}

# name  save-type  record-offset
run_case() {
  local name=$1 type=$2 offset=$3
  local cart=$carts/save-$name shots=dist/shots/save-$name
  rm -rf "$cart" "$shots"
  mkdir -p "$cart" "$shots"
  mkfs.fat -C -F 32 -n SAVETEST "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" "tests/roms/savetest/$name.gba" ::/Test.gba
  # Errors still show; the kernel's tlog lines don't.
  boot() { OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" 2>&1 >/dev/null | { grep -v '^kernel: ' || true; } >&2; }

  # Power-on 1: CLEAN BOOT of Test.gba with its save type.
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_launch /Test.gba "save=$type" at=190) -s 600:"$shots/1-game.png"

  # Power-on 2: confirm the save prompt so the kernel writes Test.sav.
  boot -k 100:A -k 106:- -s 300:"$shots/2-menu.png" -f 800
  local sav=$cart/test.sav
  mtype -i "$cart/sd.img" ::/torii/saves/Test.gba/Main.sav >"$sav" 2>/dev/null || fail "$name: no torii/saves/Test.gba/Main.sav"
  [ "$(hex_at "$sav" "$offset")" = 4f4d525801 ] ||
    fail "$name: save file has $(hex_at "$sav" "$offset") at $offset, expected the first boot record"

  # Power-on 3: launch it again (its .mde keeps the save type).
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_launch /Test.gba at=190) -s 600:"$shots/3-game.png"
  [ "$(hex_at "$cart/sram.bin" "$offset")" = 4f4d525802 ] ||
    fail "$name: cart SRAM has $(hex_at "$cart/sram.bin" "$offset") at $offset, expected the second boot"

  echo "PASS: $name"
}

# A 512-byte save for a game set to EEPROM 8K (v0.2.1 on the real cart): the
# kernel grows it to 8 KB with FF, keeping its bytes, so the block the game
# writes past 512 bytes reaches the card.
short_eeprom() {
  local cart=$carts/save-eeprom8k-short
  rm -rf "$cart"
  mkdir -p "$cart"
  mkfs.fat -C -F 32 -n SAVETEST "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" tests/roms/savetest/eeprom8k.gba ::/Test.gba
  python3 -c 'import sys; sys.stdout.buffer.write(b"\xff" * 8 + b"OMRX\x05" + b"\xff" * 499)' >"$cart/short.sav"
  mmd -i "$cart/sd.img" ::/torii ::/torii/saves ::/torii/saves/Test.gba
  mcopy -i "$cart/sd.img" "$cart/short.sav" ::/torii/saves/Test.gba/Main.sav
  local log=$cart/kernel.log
  # CLEAN BOOT with SAVE TYPE EEPROM 8K.
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Test.gba save=2 at=270) \
    -f 600 >/dev/null 2>"$log"
  grep -q "save: grew /torii/saves/Test.gba/Main.sav from 512 to 8192 bytes" "$log" || fail "eeprom8k-short: $(grep 'save: ' "$log")"
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:A -k 106:- -f 800 >/dev/null 2>>"$log"
  local sav=$cart/test.sav
  mtype -i "$cart/sd.img" ::/torii/saves/Test.gba/Main.sav >"$sav" || fail "eeprom8k-short: no torii/saves/Test.gba/Main.sav"
  [ "$(stat -c %s "$sav")" = 8192 ] || fail "eeprom8k-short: Main.sav is $(stat -c %s "$sav") bytes"
  [ "$(hex_at "$sav" 8)" = 4f4d525806 ] || fail "eeprom8k-short: block 1 has $(hex_at "$sav" 8), expected boot 6"
  [ "$(hex_at "$sav" 8000)" = 4f4d525806 ] || fail "eeprom8k-short: block 1000 has $(hex_at "$sav" 8000)"
  [ "$(hex_at "$sav" 4096)" = ffffffffff ] || fail "eeprom8k-short: the padding isn't FF"
  echo "PASS: eeprom8k-short"
}

# A save of 8 KB for a game now set to EEPROM 512 would be cut short: it is kept
# whole in the slot's history, never converted, the slot starts blank at the new
# size, and the notice is shown once (a second launch shows none).
shrunk_eeprom() {
  local cart=$carts/save-eeprom512-shrunk
  rm -rf "$cart"
  mkdir -p "$cart"
  mkfs.fat -C -F 32 -n SAVETEST "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" tests/roms/savetest/eeprom512.gba ::/Test.gba
  python3 -c 'import sys; sys.stdout.buffer.write(b"\xff" * 8 + b"OLD8K" + bytes(range(256)) * 31 + b"\xff" * 243)' >"$cart/old.sav"
  [ "$(stat -c %s "$cart/old.sav")" = 8192 ] || fail "eeprom512-shrunk: the old save is not 8192 bytes"
  mmd -i "$cart/sd.img" ::/torii ::/torii/saves ::/torii/saves/Test.gba
  mcopy -i "$cart/sd.img" "$cart/old.sav" ::/torii/saves/Test.gba/Main.sav
  local log=$cart/kernel.log
  # CLEAN BOOT with SAVE TYPE EEPROM 512; the notice waits for a key
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Test.gba save=3 at=270) \
    -w "save: type change notice shown" -k @20:A -k @26:- -f 700 >/dev/null 2>"$log"
  grep -q "save: /torii/saves/Test.gba/Main.sav is 8192 bytes, the save type 21 needs 512" "$log" ||
    fail "eeprom512-shrunk: no type change in the log: $(grep 'save: ' "$log")"
  grep -q "launch: starting the game" "$log" || fail "eeprom512-shrunk: the game didn't start after the notice"
  local sav=$cart/new.sav hist=$cart/hist.sav
  mtype -i "$cart/sd.img" ::/torii/saves/Test.gba/Main.sav >"$sav" || fail "eeprom512-shrunk: no Main.sav"
  [ "$(stat -c %s "$sav")" = 512 ] || fail "eeprom512-shrunk: Main.sav is $(stat -c %s "$sav") bytes, not 512"
  ! cmp -s "$sav" <(head -c 512 "$cart/old.sav") || fail "eeprom512-shrunk: the new save is the old one cut short"
  # The old file, untouched, is the history's first version
  mdir -i "$cart/sd.img" ::/torii/backups/Test.gba/Main/ 2>/dev/null | grep -qi '0001-' || fail "eeprom512-shrunk: no version in the history"
  local name
  name=$(mdir -b -i "$cart/sd.img" ::/torii/backups/Test.gba/Main/ | grep -i '0001-' | head -1)
  mtype -i "$cart/sd.img" "$name" >"$hist"
  cmp -s "$hist" "$cart/old.sav" || fail "eeprom512-shrunk: the history's version isn't the old save"
  # A second power-on (A at "Save sav?" copies the game's save, the old blank
  # file going into the history) and launch: the file fits, no notice
  : >"$log"
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:A -k 106:- $(nav_launch /Test.gba at=270) -f 800 \
    >/dev/null 2>"$log"
  grep -q "launch: starting the game" "$log" || fail "eeprom512-shrunk: the second launch didn't start the game"
  ! grep -q "type change notice" "$log" || fail "eeprom512-shrunk: the notice came again"
  [ "$(mdir -b -i "$cart/sd.img" ::/torii/backups/Test.gba/Main/ | grep -ci '0001-')" = 1 ] ||
    fail "eeprom512-shrunk: version 1 of the history is not the one"
  echo "PASS: eeprom512-shrunk"
}

# An EEPROM 8K save for a game now set to Flash 64K: another chip's, so it
# isn't padded to 64 KB (v0.8's cart did, silently) but kept in the history,
# as a shrunk one is, with the notice naming the game as the lists do
other_chip() {
  local cart=$carts/save-eeprom8k-to-flash
  rm -rf "$cart"
  mkdir -p "$cart"
  mkfs.fat -C -F 32 -n SAVETEST "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" tests/roms/savetest/flash64.gba ::/Test.gba
  python3 -c 'import sys; sys.stdout.buffer.write(b"\xff" * 8 + b"OLD8K" + b"\xff" * 8179)' >"$cart/old.sav"
  mmd -i "$cart/sd.img" ::/torii ::/torii/saves ::/torii/saves/Test.gba
  mcopy -i "$cart/sd.img" "$cart/old.sav" ::/torii/saves/Test.gba/Main.sav
  local log=$cart/kernel.log
  # CLEAN BOOT with SAVE TYPE FLASH 64K; the notice waits for a key
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Test.gba save=4 at=270) \
    -w "save: type change notice shown" -k @20:A -k @26:- -f 700 >/dev/null 2>"$log"
  grep -q "save: /torii/saves/Test.gba/Main.sav is 8192 bytes, the save type 32 needs 65536" "$log" ||
    fail "eeprom8k-to-flash: no type change in the log: $(grep 'save: ' "$log")"
  ! grep -q "save: grew" "$log" || fail "eeprom8k-to-flash: the EEPROM save was padded to Flash's size"
  grep -q "save: type change notice shown for Test$" "$log" ||
    fail "eeprom8k-to-flash: the notice doesn't name the game: $(grep 'notice shown' "$log")"
  grep -q "launch: starting the game" "$log" || fail "eeprom8k-to-flash: the game didn't start after the notice"
  name=$(mdir -b -i "$cart/sd.img" ::/torii/backups/Test.gba/Main/ | grep -i '0001-' | head -1)
  [ -n "$name" ] || fail "eeprom8k-to-flash: no version in the history"
  mtype -i "$cart/sd.img" "$name" >"$cart/hist.sav"
  cmp -s "$cart/hist.sav" "$cart/old.sav" || fail "eeprom8k-to-flash: the history's version isn't the old save"
  echo "PASS: eeprom8k-to-flash"
}

# Save types (nav.sh's save=N): 1 SRAM, 2 EEPROM 8K, 3 EEPROM 512, 4 FLASH 64K, 5 FLASH 128K
# With an argument, run only that case (tests/all.sh runs them in parallel).
case ${1:-all} in
flash128 | all) run_case flash128 5 $((0x10000)) ;;&
flash64 | all) run_case flash64 4 0 ;;&
eeprom8k | all) run_case eeprom8k 2 8 ;;&
eeprom8k-short | all) short_eeprom ;;&
eeprom512-shrunk | all) shrunk_eeprom ;;&
eeprom8k-to-flash | all) other_chip ;;&
eeprom512 | all) run_case eeprom512 3 8 ;;
esac
