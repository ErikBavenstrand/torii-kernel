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
# cart, with logging on: the boot's NOR list migration, settings saves on both pages, the
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

# slot SLOT: a settings slot's value, as the kernel NOR holds it
slot() {
  od -An -tu2 -j $((0x7B0000 + 2 * $1)) -N 2 "$cart/kernel-nor.bin" | tr -d ' '
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

# keys FRAME KEY... -> mgba-shot press/release pairs, one key every 8 frames.
# A key _ is a pause: A on a menu or question still opening does nothing,
# so an A right after the key that opened one waits a slot.
keys() {
  local frame=$1
  shift
  for key in "$@"; do
    [ "$key" = _ ] || printf -- '-k %d:%s -k %d:- ' "$frame" "$key" $((frame + 4))
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
format=$(keys 200 R A DOWN DOWN A _ A)
format_no=$(keys 200 R A DOWN DOWN A B)
# SETTINGS writes the kernel NOR as it closes, so each sequence ends by
# leaving it: L on page 2, then L on page 1 to the NOR list.
# THEME on SETTINGS page 2 set to LIGHT and saved, through save_set2_info
save_theme="$(keys 200 R) $(keys 220 R) $(keys 270 R) $(keys 320 DOWN DOWN DOWN DOWN DOWN A A RIGHT A) $(keys 410 L) $(keys 450 L)"
# ACCENT on SETTINGS page 2 set to Blue and saved
save_accent="$(keys 200 R) $(keys 220 R) $(keys 270 R) $(keys 320 DOWN DOWN DOWN DOWN DOWN DOWN A A RIGHT A) $(keys 410 L) $(keys 450 L)"
save_setting="-k 200:R -k 206:- -k 220:R -k 226:- -k 270:DOWN -k 276:- -k 285:DOWN -k 291:- -k 300:A -k 306:- -k 340:A -k 346:- -k 370:L -k 376:-"
# AUTO SAVE on page 2 turned over and saved COUNT times (A, LEFT, A, RIGHT,
# A on OK), then SETTINGS left: toggles COUNT
toggles() {
  local seq=''
  for _ in $(seq "$1"); do seq="$seq A LEFT A RIGHT A"; done
  # shellcheck disable=SC2086 # seq is a list of keys
  echo "$(keys 200 R) $(keys 220 R) $(keys 270 R) $(keys 320 $seq) $(keys $((330 + 40 * $1)) L) $(keys $((370 + 40 * $1)) L)"
}
toggles5=$(toggles 5)
toggles2=$(toggles 2)
# SETTINGS opened and left, nothing saved
settings_out="$(keys 200 R) $(keys 220 R) $(keys 270 R) $(keys 320 L) $(keys 360 L)"
launch="$(keys 200 DOWN DOWN DOWN DOWN A) $(keys 250 DOWN DOWN DOWN DOWN RIGHT RIGHT RIGHT RIGHT RIGHT UP UP UP UP A)"
diag="-k 1:L+R+SELECT -k 60:- -k 120:A -k 124:- $(for f in $(seq 2000 24 4400); do printf -- '-k %d:START -k %d:- ' "$f" $((f + 4)); done)"
# Diagnostics with its questions answered yes, A every 24 frames: the rumble
# step sets the ROM page to PSRAM and back.
diag_rumble="-k 1:L+R+SELECT -k 60:- -k 120:A -k 124:- $(for f in $(seq 2000 24 3600); do printf -- '-k %d:A -k %d:- ' "$f" $((f + 4)); done)"

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
  boot $save_setting -f 460
  logged "nor: wrote the settings"
  pass "a settings save"

  new_cart settings-page2
  boot $save_theme -f 540
  logged "nor: wrote the settings"
  logged "settings: THEME LIGHT"
  [ "$(slot 28)" = 1 ] || bad "THEME wasn't saved: slot 28 holds $(slot 28)"
  [ "$(slot 26)" = 2 ] || bad "the save lost LOGGING: slot 26 holds $(slot 26)"
  [ "$(slot 27)" = 65535 ] || bad "DIM was written: slot 27 holds $(slot 27)"
  [ "$(slot 29)" = 65535 ] || bad "ACCENT was written: slot 29 holds $(slot 29)"
  boot -f 200
  [ "$(slot 28)" = 1 ] || bad "THEME didn't survive a power-on: slot 28 holds $(slot 28)"
  pass "a settings save on page 2 (THEME), kept after a power-on"

  new_cart settings-accent
  boot $save_accent -f 540
  logged "nor: wrote the settings"
  logged "settings: THEME DARK, ACCENT BLUE"
  [ "$(slot 29)" = 1 ] || bad "ACCENT wasn't saved as Blue: slot 29 holds $(slot 29)"
  [ "$(slot 28)" = 65535 ] || bad "THEME was written: slot 28 holds $(slot 28)"
  [ "$(slot 27)" = 65535 ] || bad "DIM was written: slot 27 holds $(slot 27)"
  pass "an ACCENT save on page 2 writes only its slot"

  # Five saves on page 2 write the kernel NOR once, as SETTINGS closes
  # (v0.3.2: each save erased the sector); saves that change nothing, and
  # SETTINGS left without a save, don't write it
  new_cart settings-once
  boot $toggles5 -f 640
  logged "nor: wrote the settings"
  [ $ok = 0 ] || [ "$(grep -c 'nor: wrote the settings' "$log")" = 1 ] ||
    bad "five saves wrote the settings $(grep -c 'nor: wrote the settings' "$log") times"
  auto=$(slot 14)
  [ "$auto" != 65535 ] || bad "AUTO SAVE wasn't saved: slot 14 holds $auto"
  boot $toggles2 -f 520
  logged "settings: staged"
  [ $ok = 0 ] || ! grep -q 'nor: wrote the settings' "$log" || bad "two saves that change nothing wrote the settings"
  [ "$(slot 14)" = "$auto" ] || bad "AUTO SAVE changed: slot 14 holds $(slot 14), not $auto"
  boot $settings_out -f 420
  logged "screen ready: settings2"
  [ $ok = 0 ] || ! grep -q 'nor: wrote the settings' "$log" || bad "SETTINGS left without a save wrote the settings"
  pass "SETTINGS writes the kernel NOR once as it closes, and only for a change"

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

  # FORMAT ALL: the NOR list's first game, its menu's third row, and A on
  # the question
  new_cart nor-format
  boot $write_a -f 700
  boot $format -f 900
  logged "nor: erased the game NOR"
  logged "nor: list saved, 0 games"
  pass "FORMAT ALL"

  # FORMAT ALL answered with B: nothing is erased or written
  new_cart nor-format-no
  boot $write_a -f 700
  boot $format_no -f 600
  [ $ok = 0 ] || ! grep -q 'nor: erasing\|nor: list saved' "$log" || bad "B on FORMAT ALL's question wrote the NOR"
  pass "FORMAT ALL answered no"

  new_cart diagnostics
  older_list
  boot -f 300
  boot $diag -f 4500
  logged "diag: Diagnostics started"
  logged "diag: unit tests: pass"
  logged "diag: boot loader present: pass"
  logged "not DE.BIN"
  pass "Diagnostics"

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

  # Diagnostics reads the loader and says when it's damaged, as v0.2.2 left
  # it: its first 8 KB sector erased.
  new_cart diagnostics-damaged
  head -c 8192 /dev/zero | tr '\0' '\377' | dd of="$cart/kernel-nor.bin" bs=1 seek=$((0x7C0000)) conv=notrunc status=none
  seeded=$(loader)
  boot $diag -f 4500
  logged "diag: boot loader present: FAIL"
  logged "kernel error: diag: the boot loader is DAMAGED"
  pass "Diagnostics finds an erased loader"

  # ... and as v0.2.2 really left it: erased, then the NOR list programmed
  # over its first 8 KB (the program hung past them)
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

  new_cart gl064
  older_list
  boot -g -f 300
  logged "nor: saving an older kernel's list of 1 games"
  boot -g $write_b -f 700
  logged "nor: list saved, 2 games"
  boot -g $save_setting -f 460
  logged "nor: wrote the settings"
  boot -g $launch -f 700
  logged "nor: wrote the pending-save record"
  pass "the same writes on the S29GL064A"

  # The GL064A models whose loader is one 64 KB sector: top boot, uniform
  for chip in gl064top gl064uniform; do
    new_cart "$chip"
    older_list
    KERNEL_NOR=$chip boot -f 300
    logged "nor: saving an older kernel's list of 1 games"
    KERNEL_NOR=$chip boot $write_b -f 700
    logged "nor: list saved, 2 games"
    pass "the boot's migration and WRITE TO NOR on the $chip"
  done

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
    boot -c "$cut" $save_setting -f 460
    boot -f 200
    pass "a power cut at $cut in a settings save, and the boot after it"
  done
}

exit $failed
