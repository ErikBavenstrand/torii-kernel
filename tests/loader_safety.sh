#!/usr/bin/env bash
# The kernel must never write EZ-Flash's boot loader. On the cart's 8 MB
# kernel NOR the loader sits at chip 0, and the kernel's window (ROM page
# 0x8002) starts at chip 0x40000, so window offsets from 0x7C0000 wrap onto
# it; v0.2.1 and v0.2.2 wrote their NOR list's copy B there and bricked a
# cart. The emulated cart models the chip (emu omega.c): kernel-nor.bin holds
# it in window order, the loader at 0x7C0000, seeded with a stand-in image,
# and a write there stops the emulator with status 76.
#
# Every path that erases or programs the kernel NOR runs here, each on its own
# cart, with logging on: the boot's NOR list migration, a settings save, the
# pending-save record and its clearing, WRITE TO NOR, delete latest,
# Diagnostics, power cuts in the middle of those writes and the boot after
# them, and the same writes on the other kernel NOR chip (S29GL064A, buffered
# programming). After each power-on the loader must be byte-identical, and
# each case checks that its write really happened. All cases run; the script
# fails if any did. KERNEL picks another kernel file, e.g.
# dist/release-v0.2.2/ezkernelnew.bin, to see it fail. Runs inside the
# torii-emu container after `just build`, `just emu-headless` and the test ROMs.
set -uo pipefail

kernel=${KERNEL:-dist/ezkernelnew.bin}
root=dist/cart-loader-safety
rm -rf "$root"
mkdir -p "$root"
failed=0

# rom OUT SOURCE: SOURCE with its header complement fixed
rom() {
  python3 - "$@" <<'EOF'
import sys
rom = bytearray(open(sys.argv[2], "rb").read())
rom[0xBD] = -(sum(rom[0xA0:0xBD]) + 0x19) & 0xFF
open(sys.argv[1], "wb").write(rom)
EOF
}
rom "$root/A.gba" tests/roms/hello/hello.gba
rom "$root/B.gba" tests/roms/savetest/flash64.gba

# The loader and the rest of the chip below the kernel (256 KB), as a hash.
loader() { tail -c +$((0x7C0000 + 1)) "$cart/kernel-nor.bin" | md5sum | cut -c1-32; }

# new_cart NAME: an erased cart with logging on (LOGGING Info) and a card
# holding /SAVER, A.gba (Hello), B.gba and Test.gba (Flash 128K save test).
# The first power-on, one frame long, makes the cart's files and seeds the
# loader.
new_cart() {
  case=$1
  cart=$root/$case
  ok=1
  runs=0
  mkdir -p "$cart"
  mkfs.fat -C -F 32 -n LOADER "$cart/sd.img" 65536 >/dev/null
  mmd -i "$cart/sd.img" ::/SAVER
  mcopy -i "$cart/sd.img" "$root/A.gba" ::/A.gba
  mcopy -i "$cart/sd.img" "$root/B.gba" ::/B.gba
  mcopy -i "$cart/sd.img" tests/roms/savetest/flash128.gba ::/Test.gba
  OMEGA_DIR=$cart emu/build-linux/mgba-shot "$kernel" -f 1 >/dev/null 2>&1
  [ "$(tail -c +$((0x7C00A0 + 1)) "$cart/kernel-nor.bin" | head -c 12)" = "TORII LOADER" ] ||
    bad "the emulator didn't seed the loader"
  settings 26=2
  seeded=$(loader)
}

bad() {
  echo "FAIL: $case: $*" >&2
  ok=0
  failed=1
}
pass() { [ $ok = 0 ] || echo "PASS: $case: $*"; }

# settings SLOT=VALUE...: write settings slots (source/ez_define.h) directly
settings() {
  local slot value
  for slot in "$@"; do
    value=${slot#*=}
    printf %b "\\x$(printf %02x $((value & 0xFF)))\\x$(printf %02x $((value >> 8)))" |
      dd of="$cart/kernel-nor.bin" bs=1 seek=$((0x7B0000 + 2 * ${slot%=*})) conv=notrunc status=none
  done
}

# boot [-c CUT] [-g] OPTIONS...: one power-on; -c cuts the power at CUT
# (OMEGA_POWER_CUT), -g emulates the S29GL064A kernel NOR. Then the loader
# must be as seeded.
boot() {
  local cut='' chip='' status=0
  [ $ok = 1 ] || return 0 # the case failed: the rest would only repeat it
  while :; do
    case $1 in
    -c) cut=$2 && shift 2 ;;
    -g) chip=gl064 && shift ;;
    *) break ;;
    esac
  done
  runs=$((runs + 1))
  log=$cart/$runs.log
  OMEGA_KERNEL_NOR=$chip OMEGA_POWER_CUT=$cut OMEGA_DIR=$cart emu/build-linux/mgba-shot "$kernel" "$@" >"$log" 2>&1 ||
    status=$?
  if [ $status = 76 ] || grep -q 'BOOT LOADER WRITTEN' "$log"; then
    bad "power-on $runs wrote the boot loader: $(grep -m1 'BOOT LOADER WRITTEN' "$log")"
  elif [ "$(loader)" != "$seeded" ]; then
    bad "power-on $runs changed the boot loader"
  elif [ -n "$cut" ] && [ $status != 75 ]; then
    bad "power-on $runs: no power cut at $cut (exit $status)"
  elif [ -z "$cut" ] && [ $status != 0 ]; then
    bad "power-on $runs: mgba-shot exited with $status"
  fi
  if grep -E 'assert failed|^crash at frame' "$log" >&2; then
    bad "power-on $runs: the kernel crashed or failed an assert"
  fi
}

# logged TEXT: this power-on logged TEXT (the path under test ran)
logged() { [ $ok = 0 ] || grep -qF -- "$1" "$log" || bad "power-on $runs didn't log '$1'"; }

# keys FRAME KEY... -> mgba-shot press/release pairs, one key every 8 frames.
keys() {
  local frame=$1
  shift
  for key in "$@"; do
    printf -- '-k %d:%s -k %d:- ' "$frame" "$key" $((frame + 4))
    frame=$((frame + 8))
  done
}

# Key sequences, from frame 200 (logging slows the boot). WRITE TO NOR CLEAN
# for A.gba or B.gba (after the folders SAVER and torii), delete latest in the NOR list, LANGUAGE saved in SETTINGS,
# Test.gba launched with SAVE TYPE Flash 128K, and Diagnostics from
# L+R+SELECT at power-on with its questions skipped.
# shellcheck disable=SC2046 # $(keys) is a list of options
write_a=$(keys 200 DOWN DOWN A DOWN DOWN A)
write_b=$(keys 200 DOWN DOWN DOWN A DOWN DOWN A)
delete=$(keys 200 R DOWN A DOWN A)
save_setting="-k 200:R -k 206:- -k 220:R -k 226:- -k 270:DOWN -k 276:- -k 285:DOWN -k 291:- -k 300:A -k 306:- -k 340:A -k 346:-"
launch="$(keys 200 DOWN DOWN DOWN DOWN A) $(keys 250 DOWN DOWN DOWN DOWN RIGHT RIGHT RIGHT RIGHT RIGHT UP UP UP UP A)"
diag="-k 1:L+R+SELECT -k 60:- -k 120:A -k 124:- $(for f in $(seq 2000 24 4400); do printf -- '-k %d:START -k %d:- ' "$f" $((f + 4)); done)"

# older_list: the official kernel's NOR list (no trailer, no copy B) with
# A.gba at 0 in the game NOR, as on a cart coming from EZ-Flash's 1.06
older_list() {
  dd if="$root/A.gba" of="$cart/game-nor.bin" conv=notrunc status=none
  python3 - "$cart/kernel-nor.bin" "$root/A.gba" <<'EOF'
import struct, sys
k = bytearray(open(sys.argv[1], "rb").read())
rom = open(sys.argv[2], "rb").read()
entry = struct.pack("<100sHHHHIBBH16s", b"A.gba", 0, 0, 0, 0, 0x20000, 0, 0, 0, rom[0xA0:0xB0])
k[0x7A0000:0x7B0000] = b"\xff" * 0x10000
k[0x7A0000:0x7A2100] = entry + bytes(0x2100 - len(entry))
open(sys.argv[1], "wb").write(k)
EOF
}

# shellcheck disable=SC2086 # the key sequences are lists of options
{
  new_cart migrate
  older_list
  boot -f 300
  logged "nor: saving an older kernel's list of 1 games"
  boot -f 200
  pass "the boot that moves an older kernel's NOR list"

  new_cart settings
  boot $save_setting -f 420
  logged "nor: wrote the settings"
  pass "a settings save"

  new_cart pending-save
  boot $launch -f 700
  logged "launch: loading Test.gba"
  logged "nor: wrote the pending-save record"
  boot -k 200:A -k 204:- -f 500
  logged "nor: wrote an empty pending-save record"
  pass "the pending-save record, written and cleared"

  new_cart nor-write
  boot $write_a -f 700
  logged "nor: list saved, 1 games"
  boot $write_b -f 700
  logged "nor: list saved, 2 games"
  pass "WRITE TO NOR"

  new_cart nor-delete
  boot $write_a -f 700
  boot $write_b -f 700
  boot $delete -f 500
  logged "nor: deleting B.gba"
  logged "nor: list saved, 1 games"
  pass "delete latest"

  new_cart diagnostics
  older_list
  boot -f 300
  boot $diag -f 4500
  logged "diag: Diagnostics started"
  logged "diag: unit tests: pass"
  logged "diag: boot loader present: pass"
  pass "Diagnostics"

  # Diagnostics reads the loader and says when it's damaged, as v0.2.2 left
  # it: its first 8 KB sector erased.
  new_cart diagnostics-damaged
  head -c 8192 /dev/zero | tr '\0' '\377' | dd of="$cart/kernel-nor.bin" bs=1 seek=$((0x7C0000)) conv=notrunc status=none
  seeded=$(loader)
  boot $diag -f 4500
  logged "diag: boot loader present: FAIL"
  logged "kernel error: diag: the boot loader is DAMAGED"
  pass "Diagnostics finds a damaged loader"

  new_cart gl064
  older_list
  boot -g -f 300
  logged "nor: saving an older kernel's list of 1 games"
  boot -g $write_b -f 700
  logged "nor: list saved, 2 games"
  boot -g $save_setting -f 420
  logged "nor: wrote the settings"
  boot -g $launch -f 700
  logged "nor: wrote the pending-save record"
  pass "the same writes on the S29GL064A"

  # Power cuts before each of the first erases and programs, and in the
  # middle of a sector's programs, then a normal power-on. The kernel NOR
  # writes in a WRITE TO NOR come from the list save; in a launch, from the
  # pending-save record; in the migration, from the list save.
  for cut in kernel:erase:1 kernel:erase:2 kernel:program:1 kernel:program:2000 kernel:program:4300 kernel:program:6400; do
    new_cart "cut-write-${cut//:/-}"
    boot $write_a -f 700
    boot -c "$cut" $write_b -f 700
    boot -f 300
    boot $write_b -f 700
    logged "nor: list saved"
    pass "a power cut at $cut in WRITE TO NOR, and the boots after it"

    new_cart "cut-migrate-${cut//:/-}"
    older_list
    boot -c "$cut" -f 300
    boot -f 300
    logged "nor: list read from"
    pass "a power cut at $cut in the list migration, and the boot after it"
  done
  for cut in kernel:erase:1 kernel:program:1 kernel:program:100; do
    new_cart "cut-launch-${cut//:/-}"
    boot -c "$cut" $launch -f 700
    boot -k 200:A -k 204:- -f 500
    pass "a power cut at $cut in the pending-save record, and the boot after it"

    new_cart "cut-settings-${cut//:/-}"
    boot -c "$cut" $save_setting -f 420
    boot -f 200
    pass "a power cut at $cut in a settings save, and the boot after it"
  done
}

exit $failed
