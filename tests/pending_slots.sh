#!/usr/bin/env bash
# The pending-save record's 128 slots (Save_sav_info, Read_sav_info in
# source/Ezcard_OP.c) in states a power cut, an older kernel or a damaged
# sector leave. One part per run:
#   cut-new    a power cut at each of a launch's record programs: the record
#              is never pending with part of a path; the next launch takes
#              the next slot, and its save copies to its own file
#   cut-clear  a power cut at each of a copy's clear programs: the save is
#              copied again or not at all, never to another path
#   half       a slot a cut left half programmed (flag still FFFF) reads as
#              no record, and the next launch uses it up and takes the next
#   garbage    a slot past the record with flag 0011 and another game's path
#              never becomes the record when the slots before it are used
#   last       the last slot live is copied, and the next launch erases the
#              sector and takes slot 0
#   old        an older kernel's record (slot 0 live, the rest erased) is
#              copied, and the next launch takes slot 1
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/savetest.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
part=$1
local_carts "dist/cart-pending-slots-$part"

savep=/torii/saves/Test.gba/Main.sav
cart=$carts/$part
mkdir -p "$cart"
base=$((0x790000))

fail() {
  echo "FAIL: pending-slots $part: $*" >&2
  [ ! -f "$cart/run.log" ] || tail -15 "$cart/run.log" >&2
  exit 1
}

boot() {
  local status=0
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" >/dev/null 2>"$cart/run.log" || status=$?
  cat "$cart/run.log" >>"$cart/kernel.log"
  ! grep -q '^crash at frame' "$cart/run.log" || fail "the emulator caught a crash"
  [ "$status" != 76 ] || fail "the boot loader was written"
  ! grep -q '^kernel error: assert' "$cart/run.log" || fail "an assert failed"
  return 0
}
logged() { grep -qF "kernel: $1" "$cart/run.log" || fail "no log line: $1"; }
pending() { pending_flag "$cart/kernel-nor.bin"; }
slot_flag() { od -A n -t x2 -j $((base + $1 * 512)) -N 2 "$cart/kernel-nor.bin" | tr -d ' '; }

# nor_edit PYTHON: runs PYTHON with d, the kernel NOR as a bytearray, and
# B, the record sector's start, then writes d back
nor_edit() {
  python3 - "$cart/kernel-nor.bin" "$1" <<'PY'
import sys
p = sys.argv[1]
d = bytearray(open(p, "rb").read())
B = 0x790000
exec(sys.argv[2])
open(p, "wb").write(d)
PY
}

# A record as the kernel writes it: flag, size in 512-byte units, path
record_py='
def record(path, size=256):
    r = bytearray(512)
    r[0:2] = (0x11).to_bytes(2, "little")
    r[2:4] = size.to_bytes(2, "little")
    r[4:4 + len(path)] = path.encode()
    return bytes(r)
'

# Power-on 1: Test.gba launched with FLASH 128K: its save is blank on the
# card, the game writes a boot record into SRAM, the pending record is set
launch() {
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_launch /Test.gba "$@" at=250) -f 600
}
setup() {
  mkfs.fat -C -F 32 -n SAVES "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" tests/roms/savetest/flash128.gba ::/Test.gba
  launch save=5
  logged "nor: wrote the pending-save record, 512 bytes"
  [ "$(pending)" = 0011 ] || fail "no pending-save record after the first launch"
}
# A at "Save to SD card?"
copy() { boot -k 100:A -k 106:- -f 800; }
copied() {
  logged "save: wrote $savep, 131072 bytes, verified"
  [ "$(pending)" = 0000 ] || fail "the record is still set after the copy"
  mtype -i "$cart/sd.img" ::$savep >"$cart/test.sav" || fail "no save"
  [ "$(od -A n -t x1 -j $((0x10000)) -N 4 "$cart/test.sav" | tr -d ' ')" = 4f4d5258 ] || fail "the save lacks the game's boot record"
}
# Only Test.gba's save on the card under /torii/saves
only_test_save() {
  local files
  files=$(mdir -i "$cart/sd.img" -/ -b ::/torii/saves 2>/dev/null | grep -v '/$' | grep -iv '/Test.gba/' || true)
  [ -z "$files" ] || fail "other files under /torii/saves: $files"
}

case $part in
cut-new)
  setup
  copy && copied
  cp -a "$cart" "$cart.start"
  # The second launch's record goes into slot 1: count its programs
  launch
  logged "nor: the pending-save record in slot 1"
  progs=$(python3 -c "
import sys
d = open(sys.argv[1], 'rb').read()
print(sum(1 for i in range(0x790200, 0x790400, 2) if d[i:i+2] != b'\xff\xff'))" "$cart/kernel-nor.bin")
  [ "$progs" -gt 2 ] || fail "the record took only $progs programs"
  for n in 1 2 3 $((progs / 2)) $((progs - 1)) "$progs"; do
    cart_restore "$cart.start" "$cart"
    # The programs before this launch's record (the launch's own other NOR
    # writes are outside the sector, so the count is the record's)
    status=0
    # shellcheck disable=SC2046 # nav.sh's options
    OMEGA_POWER_CUT=kernel:program:$n@0x790000 OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin \
      $(nav_launch /Test.gba at=250) -f 600 >/dev/null 2>"$cart/run.log" || status=$?
    [ $status = 75 ] || fail "cut $n of $progs: no power cut (exit $status)"
    [ "$(slot_flag 1)" = ffff ] || fail "cut $n of $progs: slot 1's flag is $(slot_flag 1) before its last program"
    [ "$(pending)" = 0000 ] || fail "cut $n of $progs: a record is pending"
    # The next power-on asks nothing; the next launch takes slot 2, and its
    # save copies to Test's file
    boot -k 100:A -k 106:- -f 400
    ! grep -qF 'Save to SD card?' "$cart/run.log" || fail "cut $n of $progs: the next power-on asked to copy a save"
    # Cut before its first program, slot 1 is still erased and taken
    want=2
    [ "$n" != 1 ] || want=1
    launch
    logged "nor: the pending-save record in slot $want"
    [ "$want" = 1 ] || [ "$(slot_flag 1)" = 0000 ] || fail "cut $n of $progs: the half record in slot 1 wasn't used up"
    copy && copied
    only_test_save
  done
  echo "PASS: pending-slots cut-new: a cut at any of a record's programs leaves no record, and the next launch takes the next slot"
  ;;

cut-clear)
  setup
  cp -a "$cart" "$cart.start"
  for n in 1 2 3 10; do
    cart_restore "$cart.start" "$cart"
    status=0
    OMEGA_POWER_CUT=kernel:program:$n@0x790000 OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin \
      -k 100:A -k 106:- -f 800 >/dev/null 2>"$cart/run.log" || status=$?
    [ $status = 75 ] || fail "cut $n: no power cut (exit $status)"
    grep -qF "kernel: save: wrote $savep, 131072 bytes, verified" "$cart/run.log" || fail "cut $n: the copy wasn't written first"
    if [ "$n" = 1 ]; then
      [ "$(pending)" = 0011 ] || fail "cut 1: the flag was zeroed before the clear's first program"
    else
      [ "$(pending)" = 0000 ] || fail "cut $n: the record still reads as pending after its flag's program"
    fi
    # The next power-on copies it again (cut 1) or asks nothing
    boot -k 100:A -k 106:- -f 800
    if [ "$n" = 1 ]; then
      logged "save: the chip is identical to $savep, nothing to copy"
      [ "$(pending)" = 0000 ] || fail "cut 1: the record is still set after the second power-on"
    else ! grep -qF 'Save to SD card?' "$cart/run.log" || fail "cut $n: asked again"; fi
    launch
    logged "nor: the pending-save record in slot 1"
    copy && copied
    only_test_save
  done
  echo "PASS: pending-slots cut-clear: a cut at any of a clear's programs copies the save again or not at all"
  ;;

half)
  setup
  copy && copied
  # Slot 1 half programmed: path bytes in, flag still FFFF
  nor_edit "$record_py"'
r = record("/torii/saves/Other.gba/Main.sav")
d[B + 0x200 + 2:B + 0x200 + 0x80] = r[2:0x80]'
  [ "$(slot_flag 1)" = ffff ] && [ "$(pending)" = 0000 ] || fail "the half slot reads as pending"
  boot -k 100:A -k 106:- -f 400
  ! grep -qF 'Save to SD card?' "$cart/run.log" || fail "a half-programmed slot asked to copy a save"
  launch
  logged "nor: the pending-save record in slot 2"
  [ "$(slot_flag 1)" = 0000 ] || fail "the half slot wasn't used up"
  copy && copied
  only_test_save
  echo "PASS: pending-slots half: a half-programmed slot is no record, and the next launch takes the slot after it"
  ;;

garbage)
  setup
  copy && copied
  # Slot 0 used, slot 1 erased: slot 2 holds a stray live record naming
  # another game's save (as a cut erase or a stray program could leave)
  nor_edit "$record_py"'
d[B + 0x400:B + 0x600] = record("/torii/saves/Other.gba/Main.sav")'
  [ "$(pending)" = 0000 ] || fail "the stray slot reads as pending at once"
  launch
  logged "nor: the pending-save record in slot 1"
  copy && copied
  # With slot 1 used up, the stray slot 2 must not become the record
  [ "$(pending)" = 0000 ] || fail "after the copy, stray slot 2 reads as the pending record ($(pending))"
  boot -k 100:A -k 106:- -f 800
  ! grep -qF 'Other.gba' "$cart/run.log" || fail "the next power-on offered to copy the stray record's save: $(grep -F Other.gba "$cart/run.log" | head -2)"
  only_test_save
  echo "PASS: pending-slots garbage: a stray live slot past the record never becomes it"
  ;;

last)
  setup
  # Move the live record into slot 127, every slot before it used
  nor_edit '
r = bytes(d[B:B + 0x200])
d[B:B + 0x10000] = bytes(0x10000)
d[B + 127 * 0x200:B + 0x10000] = r'
  [ "$(pending)" = 0011 ] || fail "the last slot isn't the record"
  copy && copied
  [ "$(slot_flag 127)" = 0000 ] || fail "slot 127 wasn't used up"
  launch
  logged "nor: the pending-save record's 128 slots are used: the sector is erased"
  [ "$(slot_flag 0)" = 0011 ] && [ "$(slot_flag 1)" = ffff ] && [ "$(slot_flag 127)" = ffff ] ||
    fail "after the erase: slots $(slot_flag 0) $(slot_flag 1) $(slot_flag 127)"
  copy && copied
  echo "PASS: pending-slots last: the last slot's record copies, and the next launch erases the sector"
  ;;

old)
  setup
  # An older kernel's layout: slot 0 live, every other slot erased
  nor_edit '
r = bytes(d[B + 0x200:B + 0x400]) if d[B:B+2] == bytes(2) else bytes(d[B:B + 0x200])
d[B:B + 0x10000] = b"\xff" * 0x10000
d[B:B + 0x200] = r'
  [ "$(slot_flag 0)" = 0011 ] && [ "$(slot_flag 1)" = ffff ] || fail "not the old layout"
  copy && copied
  launch
  logged "nor: the pending-save record in slot 1"
  copy && copied
  echo "PASS: pending-slots old: an older kernel's record copies, and the next launch takes slot 1"
  ;;

*)
  echo "usage: $0 cut-new|cut-clear|half|garbage|last|old" >&2
  exit 2
  ;;
esac
