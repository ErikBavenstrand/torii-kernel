#!/usr/bin/env bash
# Recent on the SD card (source/userdata_store.c), written safely
# (source/safefile.c): the v0.3 list taken over once with its NOR games, the
# time a game was played (0 while the clock isn't set), the boot position
# kept, a copy edited by hand or left broken, a power cut before each SD
# write of a save of the list, and no write of the list reaching the
# kernel's NOR. tests/kernel/test_recent.c covers the favourites.
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/hello.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-userdata${1:+-$1}"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

now=1800000000  # 2027-01-15 08:00 UTC: a clock that is set
unset=946684800 # 2000-01-01: the clock after its battery ran out

# new_cart NAME: a card with /SAVER and Hello.gba
new_cart() {
  cart=$carts/$1
  rm -rf "$cart"
  mkdir -p "$cart"
  mkfs.fat -C -F 32 -n USERDATA "$cart/sd.img" 65536 >/dev/null
  mmd -i "$cart/sd.img" ::/SAVER ::/torii
  mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
}

# put PATH TEXT: the card's file PATH holds TEXT (printf's escapes)
put() {
  printf '%b' "$2" >"$cart/put.txt"
  mcopy -o -i "$cart/sd.img" "$cart/put.txt" "::$1"
}

# sealed TEXT: TEXT (printf's escapes) and its CRC line, as the kernel writes
sealed() {
  printf '%b' "$1" >"$cart/text.txt"
  python3 -c 'import sys, zlib; t = open(sys.argv[1], "rb").read(); sys.stdout.buffer.write(t + b"#crc32 %08x\n" % zlib.crc32(t))' \
    "$cart/text.txt"
}

# copies TEXT: both copies of recent.txt hold TEXT and its CRC line
copies() {
  sealed "$1" >"$cart/want.txt"
  for copy in recent.txt recent.txt.bak; do
    mtype -i "$cart/sd.img" "::/torii/$copy" >"$cart/got.txt" 2>/dev/null || fail "$copy isn't on the card"
    cmp -s "$cart/got.txt" "$cart/want.txt" ||
      fail "$copy holds '$(cat "$cart/got.txt")', expected '$(cat "$cart/want.txt")'"
  done
}

# boot LOG OPTIONS...: one power-on with mgba-shot OPTIONS, logged to
# $cart/LOG; fails if the kernel wrote EZ-Flash's boot loader (status 76)
boot() {
  local log=$cart/$1 status=0
  shift
  TZ=UTC OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" >"$log" 2>&1 || status=$?
  [ $status != 76 ] || fail "the kernel wrote the boot loader: $(tail -3 "$log")"
  return $status
}

# CLEAN BOOT of Recent's first game, as the Recent page will (B13), through
# actions.c and tests/nav.sh's hook. The menu reads Recent as it starts.
launch() { nav_launch "$1" at=170; }

# The v0.3 list is read as it is; the first write (a launch) takes it over,
# with its NOR games, the game's time and its CRC line
new_cart migrate
put "/SAVER/Recently play.txt" '/Hello.gba\nNOR:Old.gba\n'
# shellcheck disable=SC2046 # nav.sh's options
boot 1.log -C $now $(launch /Hello.gba) -f 400
grep -q '^kernel: launch Hello.gba: code' "$cart/1.log" || fail "Hello.gba didn't launch: $(grep '^kernel' "$cart/1.log" | tail -3)"
grep -q 'userdata: Recent: 2 games from /SAVER/Recently play.txt' "$cart/1.log" || fail "the v0.3 list wasn't read"
copies "$now\t/Hello.gba\n0\tNOR:Old.gba\n"
! mtype -i "$cart/sd.img" "::/SAVER/Recently play.txt" >/dev/null 2>&1 || fail "the v0.3 list is still there"
echo "PASS: the v0.3 list and its NOR games move to /torii/recent.txt at the first launch"

# A clock that isn't set gives no time (T4)
new_cart unset
put "/SAVER/Recently play.txt" '/Hello.gba\n'
# shellcheck disable=SC2046
boot 1.log -C $unset $(launch /Hello.gba) -f 400
copies "0\t/Hello.gba\n"
echo "PASS: a game played while the clock isn't set has time 0"

# recent.txt left empty (a write cut short): the backup copy is read
new_cart backup
put /torii/recent.txt ''
sealed '0\t/Gone.gba\n0\t/Hello.gba\n' >"$cart/bak.txt"
mcopy -o -i "$cart/sd.img" "$cart/bak.txt" ::/torii/recent.txt.bak
boot 1.log -f 200
grep -q 'safefile: /torii/recent.txt: read the backup copy' "$cart/1.log" || fail "the backup copy wasn't read"
grep -q 'userdata: Recent: 2 games from /torii/recent.txt' "$cart/1.log" || fail "Recent doesn't hold the backup's games"
echo "PASS: an empty recent.txt (a cut write) gives way to recent.txt.bak"

# Edited by hand on a PC (no CRC line): read as it is, the boot position
# kept (an older kernel's row after its item dropped); the next write puts
# the CRC line on
new_cart hand
put /torii/recent.txt '@boot gba 3\r\n/Hello.gba\r\n'
sealed '0\t/Other.gba\n' >"$cart/bak.txt"
mcopy -o -i "$cart/sd.img" "$cart/bak.txt" ::/torii/recent.txt.bak
# shellcheck disable=SC2046
boot 1.log -C $now $(launch /Hello.gba) -f 400
grep -q 'safefile: /torii/recent.txt: edited by hand' "$cart/1.log" || fail "the hand edit wasn't read as one"
grep -q '^kernel: launch Hello.gba: code' "$cart/1.log" || fail "Hello.gba didn't launch from the edited list"
copies "@boot gba\n$now\t/Hello.gba\n"
echo "PASS: a recent.txt edited by hand is read as it is and sealed by the next write"

# favorites.txt edited by hand and read whole is written back with its CRC
# line at once, so the next boot reads it as the kernel's (the cart said
# "edited by hand" at every boot)
new_cart favhand
put /torii/favorites.txt '/Hello.gba\r\n'
boot 1.log -f 200
grep -q 'safefile: /torii/favorites.txt: edited by hand' "$cart/1.log" || fail "the hand edit wasn't read as one"
grep -q 'userdata: favourites edited by hand: written back' "$cart/1.log" || fail "the favourites weren't written back"
sealed '/Hello.gba\n' >"$cart/want.txt"
for copy in favorites.txt favorites.txt.bak; do
  mtype -i "$cart/sd.img" "::/torii/$copy" >"$cart/got.txt" 2>/dev/null || fail "$copy isn't on the card"
  cmp -s "$cart/got.txt" "$cart/want.txt" || fail "$copy holds '$(cat "$cart/got.txt")'"
done
boot 2.log -f 200
! grep -q 'favorites.txt: edited by hand' "$cart/2.log" || fail "the second boot read the favourites as edited by hand"
echo "PASS: favorites.txt edited by hand is written back once with its CRC line"

# Neither copy holds up (zeros): Recent is empty, and the next write mends it
new_cart broken
printf '\0\0\0\0\0\0\0\0' >"$cart/zero.txt"
mcopy -o -i "$cart/sd.img" "$cart/zero.txt" ::/torii/recent.txt
mcopy -o -i "$cart/sd.img" "$cart/zero.txt" ::/torii/recent.txt.bak
# shellcheck disable=SC2046
boot 1.log -C $now $(launch /Hello.gba) -f 450
grep -q 'safefile: /torii/recent.txt: neither copy holds up' "$cart/1.log" || fail "the broken copies weren't noticed"
copies "$now\t/Hello.gba\n"
echo "PASS: two broken copies read as an empty Recent, and the next launch writes good ones"

# The card stops answering while recent.txt is read (OMEGA_SD_READ_ERROR):
# FatFs sees the error, so the list isn't written back from what was read;
# a read that fails once is tried again and works
read_error() { # N: how many reads fail ('' for all); prints the boot's log
  new_cart "read-error${1:+-$1}"
  sealed '0\t/Other.gba\n0\t/Hello.gba\n' >"$cart/sealed.txt"
  mcopy -o -i "$cart/sd.img" "$cart/sealed.txt" ::/torii/recent.txt
  mcopy -o -i "$cart/sd.img" "$cart/sealed.txt" ::/torii/recent.txt.bak
  local sectors
  sectors=$(sd_sectors "$cart/sd.img" /torii/recent.txt)
  # shellcheck disable=SC2046
  OMEGA_SD_READ_ERROR=$sectors${1:+:$1} boot 1.log -C $now $(launch /Hello.gba) -f 600
  grep -q 'omega: sd read error' "$cart/1.log" || fail "no read of recent.txt failed"
  grep -q '^kernel: launch Hello.gba: code' "$cart/1.log" || fail "Hello.gba didn't launch: $(grep '^kernel' "$cart/1.log" | tail -3)"
}
read_error
grep -q 'kernel error: sd: read of' "$cart/1.log" || fail "disk_read didn't report the error"
grep -q 'userdata: Recent not written: its read failed' "$cart/1.log" ||
  fail "Recent was written after a failed read: $(grep -E 'userdata|safefile' "$cart/1.log")"
copies '0\t/Other.gba\n0\t/Hello.gba\n'
read_error 1
! grep -q 'kernel error: sd: read of' "$cart/1.log" || fail "a read that failed once wasn't tried again"
copies "$now\t/Hello.gba\n0\t/Other.gba\n"
echo "PASS: a card read error keeps Recent from being written back; one tried again works"

# A power cut before each SD write of a save of Recent (the missing
# Gone.gba coming off it): the next boot has the list from before or after,
# never less, and the first write takes the backup first
before='0\t/Gone.gba\n0\t/Hello.gba\n'
new_cart cut
sealed "$before" >"$cart/sealed.txt"
mcopy -o -i "$cart/sd.img" "$cart/sealed.txt" ::/torii/recent.txt
mcopy -o -i "$cart/sd.img" "$cart/sealed.txt" ::/torii/recent.txt.bak
boot 0.log -f 1
cp -r "$cart" "$carts/cut-start"
nor=$(md5sum <"$cart/kernel-nor.bin")
# shellcheck disable=SC2046
OMEGA_TRACE=1 boot 1.log $(launch /Gone.gba) -f 300
grep -q 'recent: forgot /Gone.gba' "$cart/1.log" || fail "Gone.gba didn't come off Recent: $(grep '^kernel' "$cart/1.log" | tail -3)"
[ "$(md5sum <"$cart/kernel-nor.bin")" = "$nor" ] || fail "saving Recent wrote the kernel's NOR"
grep -q 'safefile: writing /torii/recent.txt, the backup first' "$cart/1.log" || fail "the backup wasn't written first"
copies '0\t/Hello.gba\n'
# The SD writes before the save, and the save's own
first=$(sed -n '1,/safefile: writing \/torii\/recent.txt/p' "$cart/1.log" | grep -c '^omega [0-9]*: sd write' || true)
writes=$(sed -n '/safefile: writing \/torii\/recent.txt/,$p' "$cart/1.log" | grep -c '^omega [0-9]*: sd write' || true)
[ "$writes" -ge 4 ] || fail "the save made only $writes SD writes"
results=""
for n in $(seq 1 "$writes"); do
  rm -rf "$cart" && cp -r "$carts/cut-start" "$cart"
  status=0
  # shellcheck disable=SC2046
  OMEGA_POWER_CUT=sd:write:$((first + n)) boot 2.log $(launch /Gone.gba) -f 300 || status=$?
  [ $status = 75 ] || fail "no power cut before SD write $n of the save (exit $status)"
  boot 3.log -f 200
  got=$(grep -o 'userdata: Recent: [0-9]* games' "$cart/3.log" | grep -o '[0-9]*' || echo 0)
  case $got in
  2) results+="before " ;;
  1) results+="after " ;;
  *) fail "after a power cut before SD write $n of $writes, Recent has $got games: $(grep -E 'userdata|safefile' "$cart/3.log")" ;;
  esac
done
[ "$(md5sum <"$cart/kernel-nor.bin")" = "$nor" ] || fail "the power cuts wrote the kernel's NOR"
echo "PASS: a power cut before any of the $writes SD writes of a save of Recent leaves a whole list ($results)"

# A full card: recent.txt is empty (a cut write) and recent.txt.bak holds 32
# long entries. Launching Hello.gba (the last of them) writes recent.txt
# first, which runs out of room: that copy is left empty,
# so the next boot reads recent.txt.bak whole, not the part that fit.
cart=$carts/full
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -s 4 -S 512 "$cart/sd.img" 16384 >/dev/null
mmd -i "$cart/sd.img" ::/torii ::/torii/saves ::/torii/saves/Hello.gba
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
head -c 65536 /dev/zero >"$cart/hello.sav"
mcopy -i "$cart/sd.img" "$cart/hello.sav" ::/torii/saves/Hello.gba/Main.sav
python3 -c '
for i in range(31): print("0\t/%s%02d.gba" % ("L" * 200, i))
print("0\t/Hello.gba")' >"$cart/list.txt"
sealed "$(cat "$cart/list.txt")\n" >"$cart/bak.txt"
mcopy -i "$cart/sd.img" "$cart/bak.txt" ::/torii/recent.txt.bak
put /torii/recent.txt ''
free=$(mdir -i "$cart/sd.img" :: | grep -o '[0-9 ]* bytes free' | tr -dc 0-9)
head -c $((free - 2048)) /dev/zero >"$cart/filler.bin"
mcopy -i "$cart/sd.img" "$cart/filler.bin" ::/filler.bin
# shellcheck disable=SC2046
boot 1.log -C $now $(nav_launch /Hello.gba at=190) -f 450
grep -q 'safefile: writing /torii/recent.txt failed' "$cart/1.log" || fail "the write didn't run out of room"
[ "$(mtype -i "$cart/sd.img" ::/torii/recent.txt | wc -c)" = 0 ] || fail "the failed copy isn't empty"
# B at "Save sav?"; the menu reads Recent as it starts
boot 2.log -k 100:B -k 104:- -f 300
grep -q 'userdata: Recent: 32 games from /torii/recent.txt' "$cart/2.log" ||
  fail "after the full card, Recent has: $(grep 'userdata: Recent' "$cart/2.log")"
echo "PASS: a write the full card can't take leaves its copy empty, and the other is read whole"
