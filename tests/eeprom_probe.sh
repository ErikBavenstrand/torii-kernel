#!/usr/bin/env bash
# The EEPROM probe ROM (tests/roms/eeprom) on the emulated cart: launched with
# SAVE TYPE EEPROM 512 or 8K, it runs with 6-bit (A) or 14-bit (B) addresses,
# goes back to the kernel, and the kernel's boot logs its report.
# Usage: eeprom_probe.sh [512|8k]
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and `just test-roms`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
local_carts "dist/cart-eeprom-probe${1:+-$1}"

fail() {
  echo "FAIL: eeprom probe $*" >&2
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

# type  save-type-menu-steps  width-key  width
run_case() {
  local type=$1 rights=$2 key=$3 width=$4
  local cart=$carts/eeprom-$type-$width shots=dist/shots/eeprom-$type-$width
  rm -rf "$cart" "$shots"
  mkdir -p "$cart" "$shots"
  mkfs.fat -C -F 32 -n EEPROM "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" tests/roms/eeprom/eeprom.gba ::/Probe.gba
  local right_keys=()
  for _ in $(seq "$rights"); do right_keys+=(RIGHT); done
  # Open Probe.gba, set SAVE TYPE, back up to CLEAN BOOT, launch; pick the
  # width, then A goes back to the kernel, whose boot logs the report. A at
  # its "Save sav?" keeps the probe's EEPROM.
  # shellcheck disable=SC2046
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(keys 100 A) \
    $(keys 150 DOWN DOWN DOWN DOWN "${right_keys[@]}" UP UP UP UP A) \
    -s 600:"$shots/1-probe.png" $(keys 640 "$key") -s 700:"$shots/2-verdict.png" $(keys 720 A) \
    -s 1000:"$shots/3-save.png" $(keys 1010 A) -s 1300:"$shots/4-menu.png" -f 1300 >"$cart/out.log" 2>"$cart/kernel.log" || true
  ! grep -q '^crash at frame' "$cart/kernel.log" || fail "$type $width-bit: the emulator caught a crash"
  local log=$cart/kernel.log
  grep -q "save mode $5" "$log" || fail "$type $width-bit: not launched with save mode $5"
  grep -q "eeprom probe: a $width-bit run" "$log" || fail "$type $width-bit: no report: $(tail -5 "$log")"
  grep -q "eeprom probe: $width-bit addresses work" "$log" ||
    fail "$type $width-bit: $(grep 'eeprom probe: [0-9]*-bit' "$log")"
  ! grep -q "never finished" "$log" || fail "$type $width-bit: a write never finished"
  # Each way to write (blocks 1-4) reads back whole a frame later.
  grep -q "eeprom probe: *[0-9]*: waited a frame" "$log" || fail "$type $width-bit: no frame's wait"
  for way in "polled at once" "waited, then polled" "no poll, waited 10 ms" "an extra 0 bit, waited, then polled"; do
    grep -q "read block [1-4]: .*, from step [0-9]*'s write (block [1-4], $way)\$" "$log" ||
      fail "$type $width-bit: the write that $way didn't read back"
  done
  if [ "$width" = 14 ]; then
    # mGBA's EEPROM holds 8 KB whatever the type says.
    grep -q "1024 blocks: an 8 KB EEPROM" "$log" || fail "$type 14-bit: $(grep 'eeprom probe: 14-bit' "$log")"
  fi
  # The report is cleared once logged, and only page 0xF0 has it.
  [ -z "$(od -A n -t x1 -v -j $((0xF1800)) -N 16 "$cart/sram.bin" | tr -d ' \n0')" ] || fail "$type $width-bit: the report wasn't cleared"
  echo "PASS: eeprom probe, EEPROM $type, $width-bit addresses"
}

# Menu SAVE TYPE steps from AUTO: 2 EEPROM 8K, 3 EEPROM 512.
case ${1:-all} in
512 | all)
  run_case 512 3 A 6 21
  run_case 512 3 B 14 21
  ;;&
8k | all)
  run_case 8k 2 A 6 22
  run_case 8k 2 B 14 22
  ;;
esac
