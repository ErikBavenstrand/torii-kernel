#!/usr/bin/env bash
# The power-on save copy on the emulated cart (Check_save_flag and
# Save_savefile in source/ezkernelnew.c). One part per run, so tests/all.sh
# runs them side by side:
#   copy  a game's save is made, loaded and copied back at the next power-on,
#         and each step logs what it did
#   nearly-full  a copy that just fits the card is written whole, through
#         its progress dialog, and the menu comes back
#   read-error  a save the card can't read (OMEGA_SD_READ_ERROR) stops the
#         launch before its pending-save record, so nothing copies over it
#   delete  Delete in a game list's popup removes the ROM and its save
#         stays (T11): the game copied back starts with it
#   rename-cut, rename-cut-split  a power cut before each of a rename's SD
#         writes leaves one game with its save; -split fills the folders'
#         first sectors, so a cut inside f_rename leaves two names on a file
#   rename-cut-case  the same for a rename that changes only case
#   rename  Rename in the SD card page's popup renames the ROM, its save
#         and its save type, Recent follows, and the game starts with its
#         save under the new name
#   full  a copy the card can't take shows an error dialog and logs how much
#         of it was written; its record stays, and a launch tries the copy
#         again first, asking before it loses the save (the cut-short file
#         stays until then: crash-safe saves are v0.5); B keeps it waiting,
#         A launches another game, and the next power-on copies that game's
#         save to its own file, not the lost one's
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/savetest.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-saves${1:+-$1}"

part=$1
cart=$carts/saves-$part shots=dist/shots/saves-$part
rm -rf "$cart" "$shots"
mkdir -p "$cart" "$shots"

fail() {
  echo "FAIL: saves $part: $*" >&2
  [ ! -f "$cart/kernel.log" ] || tail -15 "$cart/kernel.log" >&2
  exit 1
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

# Power-on 1: CLEAN BOOT of Test.gba with SAVE TYPE FLASH 128K; the game
# writes its first boot record to cart SRAM.
first_launch() {
  mkfs.fat -C -F 32 -n SAVES "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" tests/roms/savetest/flash128.gba ::/Test.gba
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_launch /Test.gba save=5 at=250) -s 600:"$shots/1-game.png"
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
read-error)
  first_launch
  first_copy
  cp "$cart/test.sav" "$cart/kept.sav"
  # Power-on 3: the card stops answering while Test.sav loads: no launch,
  # no pending-save record, so power-on 4 copies nothing over the file
  sectors=$(sd_sectors "$cart/sd.img" /SAVER/Test.sav)
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_SD_READ_ERROR=$sectors boot $(nav_launch /Test.gba at=180) -f 900
  logged_error "save: loading Test.sav into cart SRAM"
  ! grep -q 'launch: starting the game' "$cart/run.log" || fail "the game started after its save's read failed"
  ! grep -qF 'nor: wrote the pending-save record' "$cart/run.log" || fail "a pending-save record was written"
  [ "$(pending)" = 0000 ] || fail "the pending-save record is set"
  boot -k 100:A -k 106:- -f 300
  ! grep -qF 'Save sav?' "$cart/run.log" || fail "the next power-on asked to copy the save"
  mtype -i "$cart/sd.img" ::/SAVER/Test.sav | cmp -s - "$cart/kept.sav" || fail "Test.sav changed"
  echo "PASS: saves read-error: a save the card can't read stops the launch, and nothing is copied over it"
  ;;

delete)
  first_launch
  first_copy
  cp "$cart/test.sav" "$cart/kept.sav"
  # Power-on 3: Delete in Test's popup on the GBA page, Delete answered
  NAV_SHOWN="recent gba"
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_game_delete gba 0) -w "delete: /Test.gba" -f 900
  logged "delete: /Test.gba"
  ! mtype -i "$cart/sd.img" ::/Test.gba >/dev/null 2>&1 || fail "Test.gba is still on the card"
  mtype -i "$cart/sd.img" ::/SAVER/Test.sav | cmp -s - "$cart/kept.sav" || fail "the save changed or went"
  # Power-on 4: the game copied back starts with its save
  mcopy -i "$cart/sd.img" tests/roms/savetest/flash128.gba ::/Test.gba
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_launch /Test.gba at=180) -f 600
  logged "save: Test.sav exists, 131072 bytes"
  logged "save: loaded Test.sav into cart SRAM, 131072 bytes"
  echo "PASS: saves delete: Delete removes the ROM, its save stays and loads when the game comes back"
  ;;

rename-cut | rename-cut-split)
  first_launch
  first_copy
  cp "$cart/test.sav" "$cart/kept.sav"
  if [ "$part" = rename-cut-split ]; then
    # Folders whose first sector is full: each new name's entries land in
    # another sector than the old one's, so a cut inside f_rename can leave
    # both names on one cluster chain
    : >"$cart/pad.txt"
    for i in $(seq -w 1 20); do
      mcopy -i "$cart/sd.img" "$cart/pad.txt" "::/PAD$i.TXT"
      mcopy -i "$cart/sd.img" "$cart/pad.txt" "::/SAVER/PAD$i.TXT"
    done
  fi
  rm -rf "$carts/cut-start-$part"
  cp -r "$cart" "$carts/cut-start-$part"
  : >"$carts/aliases-$part.txt"
  # The rename through the keys, as rename's power-on 3; the SD writes
  # before it and its own, counted from the cart trace
  rename_keys=$(nav_file /Test.gba 0)
  # shellcheck disable=SC2086 # nav.sh's options
  OMEGA_TRACE=1 boot $rename_keys -k @12:UP -k @16:- -k @20:UP -k @24:- -k @28:UP -k @32:- -k @40:A -k @44:- \
    -w "screen ready: rename" -k @10:A -k @14:- -k @24:START -k @28:- -w "rename: kept" -f 2400
  logged "rename: /Test.gba to Testq.gba"
  first=$(sed -n '1,/safefile: writing \/torii\/rename.txt/p' "$cart/run.log" | grep -c '^omega [0-9]*: sd write' || true)
  writes=$(sed -n '/safefile: writing \/torii\/rename.txt/,/rename: kept/p' "$cart/run.log" | grep -c '^omega [0-9]*: sd write' || true)
  [ "$writes" -ge 6 ] || fail "the rename made only $writes SD writes"
  results=""
  for n in $(seq 1 "$writes"); do
    rm -rf "$cart" && cp -r "$carts/cut-start-$part" "$cart"
    status=0
    # shellcheck disable=SC2086 # nav.sh's options
    OMEGA_POWER_CUT=sd:write:$((first + n)) OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin \
      $rename_keys -k @12:UP -k @16:- -k @20:UP -k @24:- -k @28:UP -k @32:- -k @40:A -k @44:- \
      -w "screen ready: rename" -k @10:A -k @14:- -k @24:START -k @28:- -f 2400 >"$cart/cut.log" 2>&1 || status=$?
    [ $status = 75 ] || fail "no power cut before SD write $n of the rename (exit $status)"
    # The next power-on finishes or undoes it: one game, its save and save
    # type beside it, whole, Recent naming it, no journal left
    boot -f 300
    grep -h 'second name' "$cart/run.log" >>"$carts/aliases-$part.txt" || true
    if mtype -i "$cart/sd.img" ::/Testq.gba >/dev/null 2>&1; then to=Testq other=Test results+="done "; else to=Test other=Testq results+="undone "; fi
    ! mtype -i "$cart/sd.img" "::/$other.gba" >/dev/null 2>&1 || fail "cut before write $n: both names are on the card"
    mtype -i "$cart/sd.img" "::/SAVER/$to.sav" | cmp -s - "$cart/kept.sav" || fail "cut before write $n: $to.sav isn't the save"
    ! mtype -i "$cart/sd.img" "::/SAVER/$other.sav" >/dev/null 2>&1 || fail "cut before write $n: $other.sav is left"
    mtype -i "$cart/sd.img" "::/SAVER/$to.mde" >/dev/null 2>&1 || fail "cut before write $n: $to.mde is missing"
    ! mtype -i "$cart/sd.img" ::/torii/rename.txt >/dev/null 2>&1 || fail "cut before write $n: the journal is left"
    # No damage (two names on one chain, a broken one); clusters a cut write
    # left allocated are only lost room
    fsck.fat -n "$cart/sd.img" >"$cart/fsck.txt" 2>&1 || true
    ! grep -vE '^(fsck\.fat |Reclaimed [0-9]+ unused clusters? |Free cluster summary |  (Auto-)?correct|Leaving filesystem unchanged|.*: [0-9]+ files, |$)' \
      "$cart/fsck.txt" >"$cart/fsck-bad.txt" || fail "cut before write $n: fsck.fat: $(head -4 "$cart/fsck-bad.txt")"
    # (the copy a reader takes: recent.txt, or its backup when the cut broke it)
    {
      mtype -i "$cart/sd.img" ::/torii/recent.txt
      mtype -i "$cart/sd.img" ::/torii/recent.txt.bak
    } 2>/dev/null |
      grep "	/$to.gba$" >/dev/null || fail "cut before write $n: Recent doesn't name /$to.gba"
  done
  if [ "$part" = rename-cut-split ]; then
    # Some cuts came inside an f_rename: the next power-on dropped the
    # second name, of the game and of its save
    grep -qF '/Test.gba was a second name of /Testq.gba' "$carts/aliases-$part.txt" ||
      fail "no cut left the game under two names: $(cat "$carts/aliases-$part.txt")"
    grep -qF '/SAVER/Test.sav was a second name of /SAVER/Testq.sav' "$carts/aliases-$part.txt" ||
      fail "no cut left the save under two names: $(cat "$carts/aliases-$part.txt")"
  fi
  echo "PASS: saves $part: a power cut before any of the rename's $writes SD writes leaves one game with its save ($results)"
  ;;

rename-cut-case)
  # A case-only rename (Test to TesT: B deletes the t, SELECT shifts, t),
  # cut before each SD write, folders' first sectors full as in -split: FatFs
  # writes the new entry before it removes the old, so a cut between leaves
  # two names a path can't tell apart, on one chain
  first_launch
  first_copy
  cp "$cart/test.sav" "$cart/kept.sav"
  : >"$cart/pad.txt"
  for i in $(seq -w 1 20); do
    mcopy -i "$cart/sd.img" "$cart/pad.txt" "::/PAD$i.TXT"
    mcopy -i "$cart/sd.img" "$cart/pad.txt" "::/SAVER/PAD$i.TXT"
  done
  rm -rf "$carts/cut-start-$part"
  cp -r "$cart" "$carts/cut-start-$part"
  : >"$carts/aliases-$part.txt"
  rename_keys=$(nav_file /Test.gba 0)
  case_keys=(-k @12:UP -k @16:- -k @20:UP -k @24:- -k @28:UP -k @32:- -k @40:A -k @44:- -w "screen ready: rename"
    -k @10:B -k @14:- -k @22:SELECT -k @26:- -k @34:RIGHT -k @38:- -k @46:RIGHT -k @50:- -k @58:RIGHT -k @62:-
    -k @70:RIGHT -k @74:- -k @82:A -k @86:- -k @96:START -k @100:-)
  # shellcheck disable=SC2086 # nav.sh's options
  OMEGA_TRACE=1 boot $rename_keys "${case_keys[@]}" -w "rename: kept" -f 2400
  logged "rename: /Test.gba to TesT.gba"
  first=$(sed -n '1,/safefile: writing \/torii\/rename.txt/p' "$cart/run.log" | grep -c '^omega [0-9]*: sd write' || true)
  writes=$(sed -n '/safefile: writing \/torii\/rename.txt/,/rename: kept/p' "$cart/run.log" | grep -c '^omega [0-9]*: sd write' || true)
  [ "$writes" -ge 6 ] || fail "the rename made only $writes SD writes"
  for n in $(seq 1 "$writes"); do
    rm -rf "$cart" && cp -r "$carts/cut-start-$part" "$cart"
    status=0
    # shellcheck disable=SC2086 # nav.sh's options
    OMEGA_POWER_CUT=sd:write:$((first + n)) OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin \
      $rename_keys "${case_keys[@]}" -f 2400 >"$cart/cut.log" 2>&1 || status=$?
    [ $status = 75 ] || fail "no power cut before SD write $n of the rename (exit $status)"
    boot -f 300
    grep -h 'second name' "$cart/run.log" >>"$carts/aliases-$part.txt" || true
    # One entry for the game and one for its save, whatever their case
    [ "$(mdir -b -i "$cart/sd.img" ::/ | grep -ci '^::/test\.gba$')" = 1 ] ||
      fail "cut before write $n: the game's names: $(mdir -b -i "$cart/sd.img" ::/ | grep -i test)"
    [ "$(mdir -b -i "$cart/sd.img" ::/SAVER | grep -ci '^::/SAVER/test\.sav$')" = 1 ] ||
      fail "cut before write $n: the save's names: $(mdir -b -i "$cart/sd.img" ::/SAVER | grep -i test)"
    mtype -i "$cart/sd.img" ::/SAVER/Test.sav | cmp -s - "$cart/kept.sav" || fail "cut before write $n: the save changed"
    ! mtype -i "$cart/sd.img" ::/torii/rename.txt >/dev/null 2>&1 || fail "cut before write $n: the journal is left"
    fsck.fat -n "$cart/sd.img" >"$cart/fsck.txt" 2>&1 || true
    ! grep -vE '^(fsck\.fat |Reclaimed [0-9]+ unused clusters? |Free cluster summary |  (Auto-)?correct|Leaving filesystem unchanged|.*: [0-9]+ files, |$)' \
      "$cart/fsck.txt" >"$cart/fsck-bad.txt" || fail "cut before write $n: fsck.fat: $(head -4 "$cart/fsck-bad.txt")"
  done
  grep -qF 'another case of /SAVER/TesT.sav was a second name of it' "$carts/aliases-$part.txt" ||
    fail "no cut left the save under two cases: $(cat "$carts/aliases-$part.txt")"
  echo "PASS: saves $part: a power cut before any of a case-only rename's $writes SD writes leaves one game and one save"
  ;;

rename)
  first_launch
  first_copy
  cp "$cart/test.sav" "$cart/kept.sav"
  # Power-on 3: the SD card page's Rename on Test.gba (UP three times wraps
  # to it from Launch), "q" typed after the name, START: the file, its save
  # and its save type are renamed, Recent follows
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_file /Test.gba 0) -k @12:UP -k @16:- -k @20:UP -k @24:- -k @28:UP -k @32:- -k @40:A -k @44:- \
    -w "screen ready: rename" -k @10:A -k @14:- -k @24:START -k @28:- \
    -w "rename: kept" -w "sdcard: ready /" -s @1:"$shots/3-renamed.png" -f 2400
  logged "rename: /Test.gba to Testq.gba"
  logged "rename: /SAVER/Test.sav to /SAVER/Testq.sav (FatFs 0)"
  logged "rename: /SAVER/Test.mde to /SAVER/Testq.mde (FatFs 0)"
  ! mtype -i "$cart/sd.img" ::/Test.gba >/dev/null 2>&1 || fail "Test.gba is still on the card"
  ! mtype -i "$cart/sd.img" ::/SAVER/Test.sav >/dev/null 2>&1 || fail "SAVER/Test.sav is still on the card"
  mtype -i "$cart/sd.img" ::/Testq.gba | cmp -s - tests/roms/savetest/flash128.gba || fail "Testq.gba isn't the game"
  mtype -i "$cart/sd.img" ::/SAVER/Testq.sav | cmp -s - "$cart/kept.sav" || fail "the save didn't follow the game"
  mtype -i "$cart/sd.img" ::/SAVER/Testq.mde >/dev/null 2>&1 || fail "the save type didn't follow the game"
  mtype -i "$cart/sd.img" ::/torii/recent.txt | grep -q "	/Testq.gba$" || fail "Recent didn't follow the game"
  # Power-on 4: the game under its new name starts with its save
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_launch /Testq.gba at=180) -f 600
  logged "save: Testq.sav exists, 131072 bytes"
  logged "save: loaded Testq.sav into cart SRAM, 131072 bytes"
  echo "PASS: saves rename: the SD card page's Rename moves the save and save type with the game, which starts with them"
  ;;

copy)
  first_launch
  first_copy
  # Power-on 3: the game again, then B at "Save sav?".
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_launch /Test.gba at=180) -f 600
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
  # Power-on 3: the game again, which sets the record.
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_launch /Test.gba at=180) -f 600
  # Then the card loses Test.sav and fills up to 64 KB free: too little for
  # the 128 KB copy. The filler is a .gba, which the kernel deletes below.
  mdel -i "$cart/sd.img" ::/SAVER/Test.sav
  free=$(mdir -i "$cart/sd.img" ::/ | sed -n 's/^ *\([0-9 ]*\) bytes free.*/\1/p' | tr -d ' ')
  head -c $((free - 65536)) /dev/urandom >"$cart/filler.bin"
  mcopy -i "$cart/sd.img" "$cart/filler.bin" ::/Filler.gba
  # Power-on 4: A at "Save sav?"; the copy runs out of room, and an error
  # dialog says so, not "Done" (v0.3.2); B closes it.
  boot -k 100:A -k 106:- -w "error 3: Can't write the save" -s @20:"$shots/4-error.png" -k @30:B -k @34:- \
    -w "nav: home ready"
  logged 'save: "Save sav?" for /SAVER/Test.sav, 131072 bytes, save type 31: A, save'
  logged_error "save: writing Test.sav: "
  grep -qE "kernel error: save: writing Test.sav: [0-9]+ of 131072 bytes written" "$cart/run.log" ||
    fail "the error doesn't say how much was written"
  logged_error "error 3: Can't write the save"
  logged "dialog: Can't write the save"
  grep -A20 "kernel error: error 3: " "$cart/run.log" | grep -q "^kernel: screen ready: dialog" ||
    fail "no error dialog after the copy ran out of room"
  grep -q "^kernel: nav: home ready" "$cart/run.log" || fail "no menu after the copy ran out of room"
  mtype -i "$cart/sd.img" ::/Filler.gba | cmp -s - "$cart/filler.bin" || fail "the other file changed"
  fsck.fat -n "$cart/sd.img" >/dev/null || fail "fsck.fat found errors"
  # The pending-save record stays: the next power-on or launch tries again.
  [ "$(pending)" = 0011 ] || fail "the failed copy cleared the pending-save record"
  mtype -i "$cart/sd.img" ::/SAVER/Test.sav >"$cart/short.sav"

  # Power-on 5: the copy fails again; then CLEAN BOOT of Test.gba would hand
  # cart SRAM to a game, so the launch tries the copy again, fails, and
  # asks; B keeps the save waiting and launches nothing.
  # shellcheck disable=SC2046 # nav.sh's options
  boot -k 100:A -k 106:- -w "error 3: Can't write the save" -k @30:B -k @34:- \
    -w "nav: home ready" $(nav_launch /Test.gba at=@10) \
    -w "still waits to be copied" -w "screen ready: dialog" -s @20:"$shots/5-launch-anyway.png" -k @30:B -k @34:- \
    -w "nav: home ready"
  logged "save: /SAVER/Test.sav still waits to be copied; trying again"
  logged "save: Test.sav isn't on the card: B, kept"
  ! grep -q "kernel: launch: loading" "$cart/run.log" || fail "B on the question still launched"
  [ "$(pending)" = 0011 ] || fail "B on the question cleared the pending-save record"
  mtype -i "$cart/sd.img" ::/SAVER/Test.sav | cmp -s - "$cart/short.sav" || fail "Test.sav changed though B kept it"

  # A on the same question, on a copy of this cart, launching another game:
  # Other.gba (tests/roms/hello, its save on the card already, so launching
  # it needs no new room). Filler.gba goes, Other.gba and its save come, and
  # a smaller filler leaves 16 KB free: the copy still can't fit.
  main=$cart
  cart=$main-anyway
  rm -rf "$cart"
  cp -r "$main" "$cart"
  : >"$cart/kernel.log"
  mdel -i "$cart/sd.img" ::/Filler.gba
  mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Other.gba
  head -c 65536 /dev/zero | tr '\0' '\377' >"$cart/other.sav"
  mcopy -i "$cart/sd.img" "$cart/other.sav" ::/SAVER/Other.sav
  free=$(mdir -i "$cart/sd.img" ::/ | sed -n 's/^ *\([0-9 ]*\) bytes free.*/\1/p' | tr -d ' ')
  head -c $((free - 16384)) /dev/urandom >"$cart/filler.bin"
  mcopy -i "$cart/sd.img" "$cart/filler.bin" ::/Filler.gba
  # Power-on 5a: B on the power-on copy's error, then CLEAN BOOT of
  # Other.gba; the copy fails again, and A on the question launches it: the
  # record goes first.
  # shellcheck disable=SC2046 # nav.sh's options
  boot -k 100:A -k 106:- -w "error 3: Can't write the save" -k @30:B -k @34:- \
    -w "nav: home ready" $(nav_launch /Other.gba at=@10) \
    -w "still waits to be copied" -w "screen ready: dialog" -k @30:A -k @34:- \
    -w "launch: starting the game"
  logged "save: Test.sav isn't on the card: A, launch anyway, it is lost"
  grep -A20 "isn't on the card: A, launch anyway" "$cart/run.log" | grep -q "kernel: nor: wrote an empty pending-save record" ||
    fail "A on the question didn't clear the pending-save record"
  logged "launch: loading Other.gba, CLEAN BOOT"
  logged "launch: starting the game"
  mtype -i "$cart/sd.img" ::/SAVER/Test.sav >"$cart/lost.sav"
  # Power-on 6a: the record now names Other.gba: A at "Save sav?" copies
  # cart SRAM to Other.sav, and Test.sav stays as it was (before v0.3.2 the
  # record still named Test.sav, which got the other game's save).
  boot -k 100:A -k 106:- -w "nav: home ready"
  logged 'save: "Save sav?" for /SAVER/Other.sav'
  logged "save: wrote Other.sav, "
  ! grep -q '"Save sav?" for /SAVER/Test.sav' "$cart/run.log" || fail "the record still named Test.sav"
  mtype -i "$cart/sd.img" ::/SAVER/Test.sav | cmp -s - "$cart/lost.sav" || fail "Test.sav changed after A launched another game"
  [ "$(pending)" = 0000 ] || fail "the pending-save record is still set after Other.sav's copy"
  mtype -i "$cart/sd.img" ::/Filler.gba | cmp -s - "$cart/filler.bin" || fail "the other file changed"
  fsck.fat -n "$cart/sd.img" >/dev/null || fail "fsck.fat found errors after launching anyway"
  cart=$main

  # Power-on 6: the copy fails again; deleting Filler.gba (the action the
  # SD card page's Delete runs after its question, through the hook) makes room,
  # and CLEAN BOOT of Test.gba tries the copy again: it is written whole,
  # the record goes, and the game starts.
  # shellcheck disable=SC2046 # nav.sh's options
  boot -k 100:A -k 106:- -w "error 3: Can't write the save" -k @30:B -k @34:- \
    -w "nav: home ready" $(nav_delete /Filler.gba at=@10) \
    -w "delete: /Filler.gba" $(nav_launch /Test.gba at=@10) \
    -w "launch: loading Test.gba"
  logged "delete: /Filler.gba"
  logged "save: /SAVER/Test.sav still waits to be copied; trying again"
  logged "save: wrote Test.sav, 131072 bytes"
  ! grep -q "isn't on the card" "$cart/run.log" || fail "the question came though the copy was written"
  [ "$(save_record)" = 4f4d525802 ] || fail "the copy at the launch isn't the game's save: $(save_record)"
  fsck.fat -n "$cart/sd.img" >/dev/null || fail "fsck.fat found errors after the second copy"
  echo "PASS: saves full: a copy the card can't take shows an error and stays waiting; a launch tries it again, and asks before losing it"
  ;;

nearly-full)
  first_launch
  first_copy
  # Power-on 3: the game again, which sets the record.
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_launch /Test.gba at=180) -f 600
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
  grep -q "^kernel: nav: home ready" "$cart/run.log" || fail "no menu after the copy"
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
