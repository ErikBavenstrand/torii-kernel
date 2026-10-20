#!/usr/bin/env bash
# A game file in many pieces on SD: the cart loads it through a table of the
# file's cluster runs, which holds 61. A game in fewer runs must still boot; a
# game in more must show an error instead of overrunning the table. Also a
# FAT16 card whose chains end in 0xFFF8.
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/hello.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-fragmented${1:+-$1}"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# label  file-system  runs: the runs of clusters the 1 MB game is written in
run_case() {
  local label=$1 fs=$2 runs=$3
  local cart=$carts/fragmented-$label shots=dist/shots/fragmented-$label log
  rm -rf "$cart" "$shots"
  mkdir -p "$cart" "$shots"
  log=$cart/kernel.log
  tools/sdimg.sh "$cart/sd.img" mkfs "$fs" 64
  head -c 1048576 /dev/zero >"$cart/game.gba"
  dd if=tests/roms/hello/hello.gba of="$cart/game.gba" conv=notrunc status=none
  tools/sdimg.sh "$cart/sd.img" put-fragmented "$cart/game.gba" /AGAME.GBA "$runs"
  if [ "$fs" = fat16 ]; then
    python3 tools/fat16_eoc.py "$cart/sd.img" >/dev/null
  fi

  # CLEAN BOOT of the game
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /AGAME.GBA at=160) \
    -s 400:"$shots/game.png" -f 400 >"$log" 2>&1 || fail "$label: the emulator failed: $(tail -3 "$log")"
}

run_case boots fat32 61
od -A n -t x1 -N 5 "$carts/fragmented-boots/sram.bin" | tr -d ' \n' | grep -q '4f4d525801' ||
  fail "a game in 61 pieces did not boot"
echo "PASS: a game in 61 pieces boots"

run_case refused fat32 62
grep -qF "AGAME.GBA has more runs of clusters than the cart's table holds" "$carts/fragmented-refused/kernel.log" ||
  fail "a game in 62 pieces was not refused: $(grep '^kernel: ' "$carts/fragmented-refused/kernel.log" | tail -3)"
if od -A n -t x1 -N 5 "$carts/fragmented-refused/sram.bin" | tr -d ' \n' | grep -q '4f4d5258'; then
  fail "a game in 62 pieces booted"
fi
mdir -i "$carts/fragmented-refused/sd.img" ::/SAVER >/dev/null 2>&1 &&
  fail "the refused game still got a save or a Recently Played entry"
echo "PASS: a game in 62 pieces shows an error"

# FAT16 chains can end in any of 0xFFF8-0xFFFF; the kernel stopped only at
# 0xFFFF and looped forever on the others
run_case fat16-eoc fat16 3
od -A n -t x1 -N 5 "$carts/fragmented-fat16-eoc/sram.bin" | tr -d ' \n' | grep -q '4f4d525801' ||
  fail "a game on FAT16 whose chain ends in 0xFFF8 did not boot"
echo "PASS: a game on FAT16 whose chain ends in 0xFFF8 boots"
