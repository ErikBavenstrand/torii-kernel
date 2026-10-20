#!/usr/bin/env bash
# Non-ASCII file names on FAT32 and exFAT: a save test ROM named in Japanese
# or with accented Latin letters must list, launch, get its save written to
# /SAVER under its own name, and get that save back on the next launch.
# Usage: names.sh [fat32|exfat]
# Runs inside the torii-emu container after `just build`,
# `just emu-headless` and building tests/roms/savetest.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
local_carts "dist/cart-names${1:+-$1}"

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

# fs  label  game-name-without-extension
run_case() {
  local fs=$1 label=$2 name=$3
  local cart=$carts/names-$fs-$label shots=dist/shots/names-$fs-$label log
  rm -rf "$cart" "$shots"
  mkdir -p "$cart" "$shots"
  log=$cart/kernel.log
  tools/sdimg.sh "$cart/sd.img" mkfs "$fs" 64
  tools/sdimg.sh "$cart/sd.img" put tests/roms/savetest/flash128.gba "/$name.gba"
  boot() { OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" 2>&1 >/dev/null | tee -a "$log" | { grep -v '^kernel: ' || true; } >&2; }

  # Power-on 1: open the game, set SAVE TYPE to FLASH 128K, launch.
  # shellcheck disable=SC2046
  boot -s 90:"$shots/0-list.png" $(keys 100 A) \
    $(keys 150 DOWN DOWN DOWN DOWN RIGHT RIGHT RIGHT RIGHT RIGHT UP UP UP UP A) -s 600:"$shots/1-game.png"
  grep -qF "launch $name.gba:" "$log" || fail "$fs $label: the kernel didn't launch $name.gba"

  # Power-on 2: confirm the save prompt so the kernel writes the save.
  boot -k 100:A -k 106:- -s 300:"$shots/2-menu.png"
  tools/sdimg.sh "$cart/sd.img" ls /SAVER >"$cart/saver.txt"
  grep -qxF "$name.sav	131072" "$cart/saver.txt" ||
    fail "$fs $label: /SAVER has $(tr '\n' ' ' <"$cart/saver.txt"), expected $name.sav"
  tools/sdimg.sh "$cart/sd.img" get "/SAVER/$name.sav" "$cart/game.sav"
  [ "$(hex_at "$cart/game.sav" $((0x10000)))" = 4f4d525801 ] ||
    fail "$fs $label: the save has $(hex_at "$cart/game.sav" $((0x10000))), expected the first boot record"

  # Power-on 3: SAVER and torii (the first launch's Recent) sort first; move
  # down to the game, open it, launch.
  # shellcheck disable=SC2046
  boot $(keys 100 DOWN DOWN A) $(keys 180 A) -s 600:"$shots/3-game.png"
  [ "$(hex_at "$cart/sram.bin" $((0x10000)))" = 4f4d525802 ] ||
    fail "$fs $label: cart SRAM has $(hex_at "$cart/sram.bin" $((0x10000))), expected the second boot"

  echo "PASS: $fs $label name ($name)"
}

# With an argument, run only that file system (tests/all.sh runs them in parallel).
for fs in fat32 exfat; do
  case ${1:-all} in
  "$fs" | all)
    run_case "$fs" japanese "テスト ゲーム"
    run_case "$fs" latin "Pokémon Ñandú Æsir"
    ;;
  esac
done
