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
# cart, with logging on: the boot's NOR list migration, settings saves
# (settings_commit, SKIP until B16's Settings page), the
# pending-save record and its clearing, WRITE TO NOR, delete latest,
# FORMAT ALL, Diagnostics, power cuts in the middle of those writes and the boot after
# them, and the same writes on the other kernel NOR chip (S29GL064A, buffered
# programming). After each power-on the loader must be byte-identical, and
# each case checks that its write really happened. All cases run; the script
# fails if any did. KERNEL picks another kernel file, e.g.
# dist/release-v0.2.2/ezkernelnew.bin, to see it fail. Runs inside the
# torii-emu container after `just build`, `just emu-headless` and the test ROMs.
set -uo pipefail

kernel=${KERNEL:-dist/ezkernelnew.bin}
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
# An argument N/M runs every Mth case from the Nth (shard_init)
shard_init "${1:-}"
local_carts "dist/cart-loader-safety${1:+-${1/\//of}}"
root=$carts
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
# holding /SAVER, A.gba (Hello), B.gba and Test.gba (Flash 128K save test),
# starting on Home.
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
  # Logging on, and Start on Home: the pages' routes start from home
  settings 26=2 32=2
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
# (OMEGA_POWER_CUT), -g emulates the S29GL064A kernel NOR (KERNEL_NOR picks
# any OMEGA_KERNEL_NOR chip). Then the loader
# must be as seeded.
boot() {
  local cut='' chip=${KERNEL_NOR:-} status=0
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

# The writes, as the pages run them: WRITE TO NOR CLEAN of A.gba or B.gba
# through the SD card page's file popup (rows 0 and 1 of the card's root:
# SAVER and Torii's folder are hidden); through tests/nav.sh's hook
# (actions.c) from frame 200 (logging slows the boot), Test.gba launched with SAVE TYPE Flash 128K, and Diagnostics
# from L+R+SELECT at power-on with its questions skipped; and through the
# NOR page's keys, delete latest (the popup's Delete on B.gba, then Delete)
# and FORMAT ALL (Format all, then Format), each also answered no. The
# Settings page's saves come back with B16: until then their cases say SKIP
# (the settings sector's write still runs, from a first WRITE TO NOR).
write_a=$(nav_file /A.gba 0 nor)
write_b=$(nav_file /B.gba 1 nor)
write_addon=$(nav_file /A.gba 0 nor-addon)
# (each waits for the page to come back after the action, then 30 frames)
delete="$(nav_nor_delete_page 1 yes) $(_nav_file '-w "nav: page nor ready"' '-k @30:-')"
delete_no="$(nav_nor_delete_page 1 no) $(_nav_file '-w "nor: delete 1: cancelled"' '-k @30:-')"
format="$(nav_nor_format_page yes) $(_nav_file '-w "nav: page nor ready"' '-k @30:-')"
format_no="$(nav_nor_format_page no) $(_nav_file '-w "nor: format: cancelled"' '-k @30:-')"
launch=$(nav_launch /Test.gba save=5 at=200)
# (A starts it once its first page is up, and the keys held at power-on,
# let go at frame 60, are up too.)
diag_start="-k 1:L+R+SELECT $(_nav_file '-w "screen ready: diag-start"' '-k @70:A -k @74:-') -k 60:-"
diag="$diag_start $(for f in $(seq 2000 24 4400); do printf -- '-k %d:START -k %d:- ' "$f" $((f + 4)); done)"
# Diagnostics with its questions answered yes, A every 24 frames: the rumble
# step sets the ROM page to PSRAM and back.
diag_rumble="$diag_start $(for f in $(seq 2000 24 3600); do printf -- '-k %d:A -k %d:- ' "$f" $((f + 4)); done)"

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
  if shard_case; then
    new_cart migrate
    older_list
    boot -f 300
    logged "nor: saving an older kernel's list of 1 games"
    boot -f 200
    pass "the boot that moves an older kernel's NOR list"
  fi

  # The Settings page's saves (settings_commit through Save_info): once per
  # visit, only for a change, and kept after a power-on
  if shard_case; then
    echo "SKIP (waiting for B16): settings saves on the Settings page"
  fi

  if shard_case; then
    new_cart pending-save
    boot $launch -f 700
    logged "launch: loading Test.gba"
    logged "nor: wrote the pending-save record"
    boot -k 200:A -k 204:- -f 500
    logged "nor: wrote an empty pending-save record"
    pass "the pending-save record, written and cleared"
  fi

  if shard_case; then
    new_cart nor-write
    boot $write_a -f 900
    logged "nor: list saved, 1 games"
    boot $write_b -f 900
    logged "nor: list saved, 2 games"
    pass "WRITE TO NOR"
  fi

  # WRITE TO NOR with addon (RESET on: the popup offers it), through the page
  if shard_case; then
    new_cart nor-write-addon
    settings 1=1
    boot $write_addon -f 900
    logged "sdcard: write to NOR with addon /A.gba"
    logged "nor: list saved, 1 games"
    pass "WRITE TO NOR with addon"
  fi

  if shard_case; then
    new_cart nor-delete
    boot $write_a -f 900
    boot $write_b -f 900
    boot $delete_no
    [ $ok = 0 ] || ! grep -qE 'nor: deleting|nor: list saved' "$log" || bad "B on the delete question wrote the NOR"
    boot $delete
    logged "nor: deleting B.gba"
    logged "nor: list saved, 1 games"
    pass "delete latest, from the NOR page (and answered no first)"
  fi

  # FORMAT ALL
  if shard_case; then
    new_cart nor-format
    boot $write_a -f 900
    boot $format
    logged "nor: erased the game NOR"
    logged "nor: list saved, 0 games"
    pass "FORMAT ALL, from the NOR page"
  fi

  # FORMAT ALL answered with B: nothing is erased or written
  if shard_case; then
    new_cart nor-format-no
    boot $write_a -f 900
    boot $format_no
    logged "nor: format: cancelled"
    [ $ok = 0 ] || ! grep -qE 'nor: erased|nor: list saved' "$log" || bad "B on the format question wrote the NOR"
    pass "the NOR page's FORMAT ALL question answered no"
  fi

  if shard_case; then
    new_cart diagnostics
    older_list
    boot -f 300
    boot $diag -f 4500
    logged "diag: Diagnostics started"
    logged "diag: unit tests: pass"
    logged "diag: boot loader present: pass"
    logged "not DE.BIN"
    pass "Diagnostics"
  fi

  if shard_case; then
    new_cart diagnostics-rumble
    OMEGA_TRACE=1 boot $diag_rumble -f 3700
    logged "diag: rumble: yes"
    logged "mode game from PSRAM"
    # 60 pulses under the PSRAM page (and a last off), the kernel's page back
    motor=$(grep -o '^omega [0-9]*: rumble .*' "$log" | cut -d' ' -f3- | sort | uniq -c | tr -s ' ' | paste -sd ,)
    [ $ok = 0 ] || [ "$motor" = " 60 rumble off, 60 rumble on" ] || bad "the cart saw the motor as: $motor"
    [ $ok = 0 ] || [ "$(grep -o 'omega [0-9]*: mode .*' "$log" | tail -1 | cut -d' ' -f3-)" = "mode kernel" ] ||
      bad "the kernel's page wasn't put back"
    pass "Diagnostics' rumble step"
  fi

  # Diagnostics reads the loader and says when it's damaged, as v0.2.2 left
  # it: its first 8 KB sector erased.
  if shard_case; then
    new_cart diagnostics-damaged
    head -c 8192 /dev/zero | tr '\0' '\377' | dd of="$cart/kernel-nor.bin" bs=1 seek=$((0x7C0000)) conv=notrunc status=none
    seeded=$(loader)
    boot $diag -f 4500
    logged "diag: boot loader present: FAIL"
    logged "kernel error: diag: the boot loader is DAMAGED"
    pass "Diagnostics finds an erased loader"
  fi

  # ... and as v0.2.2 really left it: erased, then the NOR list programmed
  # over its first 8 KB (the program hung past them)
  if shard_case; then
    new_cart diagnostics-overwritten
    older_list
    python3 - "$cart/kernel-nor.bin" <<'EOF'
import sys
k = bytearray(open(sys.argv[1], "rb").read())
k[0x7C0000:0x7C2000] = k[0x7A0000:0x7A2000]
open(sys.argv[1], "wb").write(k)
EOF
    seeded=$(loader)
    boot $diag -f 4500
    logged "diag: boot loader present: FAIL"
    logged "kernel error: diag: the boot loader is OVERWRITTEN"
    pass "Diagnostics finds a loader the NOR list overwrote"
  fi

  if shard_case; then
    new_cart gl064
    older_list
    boot -g -f 300
    logged "nor: saving an older kernel's list of 1 games"
    boot -g $write_b -f 900
    logged "nor: list saved, 2 games"
    boot -g $launch -f 700
    logged "nor: wrote the pending-save record"
    pass "the same writes on the S29GL064A"
  fi

  # The settings sector on the S29GL064A: the first game written to an empty
  # game NOR saves its save type there (Set_saveMODE through Save_info)
  if shard_case; then
    new_cart gl064-settings
    boot -g $write_a -f 900
    logged "nor: list saved, 1 games"
    logged "nor: wrote the settings"
    pass "a settings save on the S29GL064A (the first WRITE TO NOR's save type)"
  fi

  # The GL064A models whose loader is one 64 KB sector: top boot, uniform
  for chip in gl064top gl064uniform; do
    if shard_case; then
      new_cart "$chip"
      older_list
      KERNEL_NOR=$chip boot -f 300
      logged "nor: saving an older kernel's list of 1 games"
      KERNEL_NOR=$chip boot $write_b -f 900
      logged "nor: list saved, 2 games"
      pass "the boot's migration and WRITE TO NOR on the $chip"
    fi
  done

  # Power cuts before each of the first erases and programs, and in the
  # middle of a sector's programs, then a normal power-on. The kernel NOR
  # writes in a WRITE TO NOR come from the list save; in a launch, from the
  # pending-save record; in the migration, from the list save.
  for cut in kernel:erase:1 kernel:erase:2 kernel:program:1 kernel:program:2000 kernel:program:4300 kernel:program:6400; do
    if shard_case; then
      new_cart "cut-write-${cut//:/-}"
      boot $write_a -f 900
      boot -c "$cut" $write_b -f 900
      boot -f 300
      boot $write_b -f 900
      logged "nor: list saved"
      pass "a power cut at $cut in WRITE TO NOR, and the boots after it"
    fi

    if shard_case; then
      new_cart "cut-migrate-${cut//:/-}"
      older_list
      boot -c "$cut" -f 300
      boot -f 300
      logged "nor: list read from"
      pass "a power cut at $cut in the list migration, and the boot after it"
    fi
  done
  for cut in kernel:erase:1 kernel:program:1 kernel:program:100; do
    if shard_case; then
      new_cart "cut-launch-${cut//:/-}"
      boot -c "$cut" $launch -f 700
      boot -k 200:A -k 204:- -f 500
      pass "a power cut at $cut in the pending-save record, and the boot after it"
    fi

    if shard_case; then
      echo "SKIP (waiting for B16): a power cut at $cut in a settings save"
    fi
  done
}

exit $failed
