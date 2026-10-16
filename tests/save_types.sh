#!/usr/bin/env bash
# Save chip test on the emulated cart: for each save type, launch the matching
# save test ROM with that type picked in the kernel menu, power-cycle so the
# kernel writes the save to SD, and launch again to check the save came back.
# Runs inside the torii-emu container after `just build`,
# `just emu-headless` and building tests/roms/savetest.
set -euo pipefail

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

# name  save-type-menu-steps  record-offset
run_case() {
  local name=$1 rights=$2 offset=$3
  local cart=dist/cart-save-$name shots=dist/shots/save-$name
  rm -rf "$cart" "$shots"
  mkdir -p "$cart" "$shots"
  mkfs.fat -C -F 32 -n SAVETEST "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" "tests/roms/savetest/$name.gba" ::/Test.gba
  boot() { OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" >/dev/null; }

  # Power-on 1: open Test.gba, set SAVE TYPE, go back to CLEAN BOOT, launch.
  local right_keys=()
  for _ in $(seq "$rights"); do right_keys+=(RIGHT); done
  # shellcheck disable=SC2046
  boot $(keys 100 A) $(keys 150 DOWN DOWN DOWN DOWN "${right_keys[@]}" UP UP UP UP A) \
    -s 600:"$shots/1-game.png"

  # Power-on 2: confirm the save prompt so the kernel writes Test.sav.
  boot -k 100:A -k 106:- -s 300:"$shots/2-menu.png"
  local sav=$cart/test.sav
  mtype -i "$cart/sd.img" ::/SAVER/Test.sav >"$sav" 2>/dev/null || fail "$name: no SAVER/Test.sav"
  [ "$(hex_at "$sav" "$offset")" = 4f4d525801 ] ||
    fail "$name: save file has $(hex_at "$sav" "$offset") at $offset, expected the first boot record"

  # Power-on 3: SAVER sorts first; move down to the game, open it, launch.
  # shellcheck disable=SC2046
  boot $(keys 100 DOWN A) $(keys 180 A) -s 600:"$shots/3-game.png"
  [ "$(hex_at "$cart/sram.bin" "$offset")" = 4f4d525802 ] ||
    fail "$name: cart SRAM has $(hex_at "$cart/sram.bin" "$offset") at $offset, expected the second boot"

  echo "PASS: $name"
}

# Menu SAVE TYPE steps from AUTO: 1 SRAM, 2 EEPROM 8K, 3 EEPROM 512, 4 FLASH 64K, 5 FLASH 128K
run_case flash128 5 $((0x10000))
run_case flash64 4 0
run_case eeprom8k 2 8
run_case eeprom512 3 8
