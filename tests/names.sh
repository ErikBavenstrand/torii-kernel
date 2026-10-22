#!/usr/bin/env bash
# Non-ASCII file names on FAT32 and exFAT: a save test ROM named in Japanese
# or with accented Latin letters must launch, get its save written to
# /SAVER under its own name, and get that save back on the next launch,
# the second time from the SD card page, which lists the name whole.
# Usage: names.sh [fat32|exfat]
# Runs inside the torii-emu container after `just build`,
# `just emu-headless` and building tests/roms/savetest.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-names${1:+-$1}"

fail() {
  echo "FAIL: $*" >&2
  exit 1
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

  # Power-on 1: CLEAN BOOT with SAVE TYPE FLASH 128K, as a page will
  # (actions.c, through tests/nav.sh's hook: the page has no save type).
  # shellcheck disable=SC2046
  boot $(nav_launch "/$name.gba" save=5 at=250) -s 600:"$shots/1-game.png"
  grep -qF "launch $name.gba:" "$log" || fail "$fs $label: the kernel didn't launch $name.gba"

  # Power-on 2: confirm the save prompt so the kernel writes the save.
  boot -k 100:A -k 106:- -s 300:"$shots/2-menu.png" -f 800
  tools/sdimg.sh "$cart/sd.img" ls /SAVER >"$cart/saver.txt"
  grep -qxF "$name.sav	131072" "$cart/saver.txt" ||
    fail "$fs $label: /SAVER has $(tr '\n' ' ' <"$cart/saver.txt"), expected $name.sav"
  tools/sdimg.sh "$cart/sd.img" get "/SAVER/$name.sav" "$cart/game.sav"
  [ "$(hex_at "$cart/game.sav" $((0x10000)))" = 4f4d525801 ] ||
    fail "$fs $label: the save has $(hex_at "$cart/game.sav" $((0x10000))), expected the first boot record"

  # Power-on 3: launch it again, from the SD card page (its popup's Launch,
  # once the page has logged the popup's path: the name lists whole)
  # shellcheck disable=SC2046
  boot $(nav_file "/$name.gba" 0 launch) -s 800:"$shots/3-game.png"
  grep -qF "sdcard: popup /$name.gba" "$log" || fail "$fs $label: the SD card page didn't list $name.gba"
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
