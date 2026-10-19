#!/usr/bin/env bash
# The power-on save copy on the emulated cart (Check_save_flag and
# Save_savefile in source/ezkernelnew.c). One part per run, so tests/all.sh
# runs them side by side:
#   copy  a game's save is made, loaded and copied back at the next power-on,
#         and each step logs what it did
#   nearly-full  a copy that just fits the card is written whole, through
#         its progress dialog, and the menu comes back
#   full  a copy the card can't take shows an error dialog and logs how much
#         of it was written; its record stays, so the next power-on offers
#         it again (the cut-short file stays until then: crash-safe saves
#         are v0.5)
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/savetest.
set -euo pipefail

part=$1
cart=dist/cart-saves-$part shots=dist/shots/saves-$part
rm -rf "$cart" "$shots"
mkdir -p "$cart" "$shots"

fail() {
  echo "FAIL: saves $part: $*" >&2
  [ ! -f "$cart/kernel.log" ] || tail -15 "$cart/kernel.log" >&2
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

# Runs the kernel; this power-on's log lines go to run.log, all of them to
# kernel.log. A crash report fails the test.
boot() {
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" >/dev/null 2>"$cart/run.log" || true
  cat "$cart/run.log" >>"$cart/kernel.log"
  ! grep -q '^crash at frame' "$cart/run.log" || fail "the emulator caught a crash"
}

logged() { # this power-on logged the line (fixed text, after "kernel: ")
  grep -qF "kernel: $1" "$cart/run.log" || fail "no log line: $1"
}
logged_error() {
  grep -qF "kernel error: $1" "$cart/run.log" || fail "no error line: $1"
}

# The save's boot record: Flash 128K keeps it at 0x10000 (tests/roms/savetest).
save_record() {
  mtype -i "$cart/sd.img" ::/SAVER/Test.sav >"$cart/test.sav" 2>/dev/null || fail "no SAVER/Test.sav"
  hex_at "$cart/test.sav" $((0x10000))
}

# The pending-save record in the kernel NOR (SAVE_info_offset), first halfword.
pending() {
  od -A n -t x2 -j $((0x790000)) -N 2 "$cart/kernel-nor.bin" | tr -d ' '
}

# Power-on 1: open Test.gba, set SAVE TYPE to FLASH 128K, launch; the game
# writes its first boot record to cart SRAM.
first_launch() {
  mkfs.fat -C -F 32 -n SAVES "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" tests/roms/savetest/flash128.gba ::/Test.gba
  # shellcheck disable=SC2046
  boot $(keys 100 A) $(keys 150 DOWN DOWN DOWN DOWN RIGHT RIGHT RIGHT RIGHT RIGHT UP UP UP UP A) \
    -s 600:"$shots/1-game.png"
  logged "save: made a blank Test.sav, 131072 bytes"
  logged "save: loaded Test.sav into cart SRAM, 131072 bytes"
  logged "nor: wrote the pending-save record, 512 bytes"
  [ "$(pending)" = 0011 ] || fail "no pending-save record after the launch"
}

# Power-on 2: A at "Save sav?" copies the save to the card.
first_copy() {
  boot -k 100:A -k 106:- -s 300:"$shots/2-menu.png"
  logged 'save: "Save sav?" for /SAVER/Test.sav, 131072 bytes, save type 31: A, save'
  logged "save: wrote Test.sav, 131072 bytes, f_close gave 0"
  logged "nor: wrote an empty pending-save record, 512 bytes"
  [ "$(save_record)" = 4f4d525801 ] || fail "the save has $(save_record), expected the first boot record"
  [ "$(pending)" = 0000 ] || fail "the pending-save record is still set after the copy"
}

case $part in
copy)
  first_launch
  first_copy
  # Power-on 3: the game again (SAVER sorts first), then B at "Save sav?".
  # shellcheck disable=SC2046
  boot $(keys 100 DOWN A) $(keys 180 A) -f 600
  logged "save: Test.sav exists, 131072 bytes, save type 31"
  logged "save: loaded Test.sav into cart SRAM, 131072 bytes"
  boot -k 100:B -k 106:- -f 300
  logged 'save: "Save sav?" for /SAVER/Test.sav, 131072 bytes, save type 31: B, not saved'
  [ "$(save_record)" = 4f4d525801 ] || fail "B changed the save"
  [ "$(pending)" = 0000 ] || fail "B left the pending-save record"
  fsck.fat -n "$cart/sd.img" >/dev/null || fail "fsck.fat found errors"
  echo "PASS: saves copy: a save is made, loaded and copied back, and every step is logged"
  ;;

full)
  first_launch
  first_copy
  # Power-on 3: the game again (SAVER sorts first), which sets the record.
  # shellcheck disable=SC2046
  boot $(keys 100 DOWN A) $(keys 180 A) -f 600
  # Then the card loses Test.sav and fills up to 64 KB free: too little for
  # the 128 KB copy.
  mdel -i "$cart/sd.img" ::/SAVER/Test.sav
  free=$(mdir -i "$cart/sd.img" ::/ | sed -n 's/^ *\([0-9 ]*\) bytes free.*/\1/p' | tr -d ' ')
  head -c $((free - 65536)) /dev/urandom >"$cart/filler.bin"
  mcopy -i "$cart/sd.img" "$cart/filler.bin" ::/FILLER.BIN
  # Power-on 4: A at "Save sav?"; the copy runs out of room, and an error
  # dialog says so, not "Done" (v0.3.2); B closes it.
  boot -k 100:A -k 106:- -w "error 3: Can't write the save" -s @20:"$shots/4-error.png" -k @30:B -k @34:- \
    -w "screen ready: sd-list"
  logged 'save: "Save sav?" for /SAVER/Test.sav, 131072 bytes, save type 31: A, save'
  logged_error "save: writing Test.sav: "
  grep -qE "kernel error: save: writing Test.sav: [0-9]+ of 131072 bytes written" "$cart/run.log" ||
    fail "the error doesn't say how much was written"
  logged_error "error 3: Can't write the save"
  logged "dialog: Can't write the save"
  grep -A3 "kernel error: error 3: " "$cart/run.log" | grep -q "^kernel: screen ready: dialog" ||
    fail "no error dialog after the copy ran out of room"
  grep -q "^kernel: screen ready: sd-list" "$cart/run.log" || fail "no menu after the copy ran out of room"
  mtype -i "$cart/sd.img" ::/FILLER.BIN | cmp -s - "$cart/filler.bin" || fail "the other file changed"
  fsck.fat -n "$cart/sd.img" >/dev/null || fail "fsck.fat found errors"
  # The pending-save record stays, so once the card has room again the next
  # power-on offers the copy again and writes it whole.
  [ "$(pending)" = 0011 ] || fail "the failed copy cleared the pending-save record"
  mdel -i "$cart/sd.img" ::/FILLER.BIN
  boot -k 100:A -k 106:- -f 500
  logged 'save: "Save sav?" for /SAVER/Test.sav, 131072 bytes, save type 31: A, save'
  logged "save: wrote Test.sav, 131072 bytes"
  [ "$(save_record)" = 4f4d525802 ] || fail "the copy after freeing room isn't the game's save: $(save_record)"
  [ "$(pending)" = 0000 ] || fail "the pending-save record is still set after the copy"
  fsck.fat -n "$cart/sd.img" >/dev/null || fail "fsck.fat found errors after the second copy"
  echo "PASS: saves full: a copy the card can't take shows an error dialog, logs how much was written, and is offered again"
  ;;

nearly-full)
  first_launch
  first_copy
  # Power-on 3: the game again (SAVER sorts first), which sets the record.
  # shellcheck disable=SC2046
  boot $(keys 100 DOWN A) $(keys 180 A) -f 600
  # Then the card loses Test.sav and fills up to exactly 128 KB free: just
  # room for the copy, through its progress dialog.
  mdel -i "$cart/sd.img" ::/SAVER/Test.sav
  free=$(mdir -i "$cart/sd.img" ::/ | sed -n 's/^ *\([0-9 ]*\) bytes free.*/\1/p' | tr -d ' ')
  head -c $((free - 131072)) /dev/urandom >"$cart/filler.bin"
  mcopy -i "$cart/sd.img" "$cart/filler.bin" ::/FILLER.BIN
  # Power-on 4: A at "Save sav?"; the copy fills the card.
  boot -k 100:A -k 106:- -s 116:"$shots/4-progress.png" -f 400
  logged 'save: "Save sav?" for /SAVER/Test.sav, 131072 bytes, save type 31: A, save'
  grep -q "^kernel: screen ready: progress" "$cart/run.log" || fail "no progress dialog for the copy"
  logged "save: wrote Test.sav, 131072 bytes, f_close gave 0"
  grep -q "^kernel error:" "$cart/run.log" && fail "errors: $(grep '^kernel error:' "$cart/run.log" | head -3)"
  grep -q "^kernel: screen ready: sd-list" "$cart/run.log" || fail "no menu after the copy"
  [ "$(save_record)" = 4f4d525802 ] || fail "the save has $(save_record), expected the second boot record"
  left=$(mdir -i "$cart/sd.img" ::/ | sed -n 's/^ *\([0-9 ]*\) bytes free.*/\1/p' | tr -d ' ')
  [ "$left" = 0 ] || fail "$left bytes free after the copy, expected 0"
  mtype -i "$cart/sd.img" ::/FILLER.BIN | cmp -s - "$cart/filler.bin" || fail "the other file changed"
  fsck.fat -n "$cart/sd.img" >/dev/null || fail "fsck.fat found errors"
  echo "PASS: saves nearly-full: a copy that just fits the card is written whole"
  ;;

*)
  echo "usage: $0 copy|full|nearly-full" >&2
  exit 2
  ;;
esac
