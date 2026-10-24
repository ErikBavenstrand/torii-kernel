#!/usr/bin/env bash
# Names past a v0.3 list entry's 100 bytes: they launch and save whole on
# FAT32, launching or deleting one touches no other file on exFAT, and one
# too long for the pending-save record says so. The SD card page lists
# read-only and no-archive entries, not hidden or system ones, and starts
# the long name; the other launches and deletes go through tests/nav.sh's
# hook (actions.c), as the pages run them.
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/hello.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-listing${1:+-$1}"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

long="A long name that goes on and on past the hundred bytes a list entry keeps for each file in a folder.gba"
[ ${#long} -gt 100 ] || fail "the long name is only ${#long} bytes"

# FAT32: attributes, and a long name that opens as its 8.3 name
cart=$carts/listing-fat32
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n LISTING "$cart/sd.img" 65536 >/dev/null
for name in Normal NoArchive ReadOnly Hidden System; do
  mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba "::/$name.gba"
done
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba "::/$long"
for dir in Plain RODir HidDir; do mmd -i "$cart/sd.img" "::/$dir"; done
mattrib -i "$cart/sd.img" -a ::/NoArchive.gba
mattrib -i "$cart/sd.img" -a +r ::/ReadOnly.gba
mattrib -i "$cart/sd.img" +h ::/Hidden.gba
mattrib -i "$cart/sd.img" +s ::/System.gba
mattrib -i "$cart/sd.img" +r ::/RODir
mattrib -i "$cart/sd.img" +h ::/HidDir
# The SD card page: Plain and RODir, then the long name, NoArchive, Normal
# and ReadOnly; A on the long name (row 2) starts it. On a copy of the
# card: the launch leaves a save to copy at the next power-on.
rm -rf "$cart-page" && cp -r "$cart" "$cart-page"
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_DIR=$cart-page emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_file "/$long" 2 open) -f 600 >"$cart/page.log" 2>&1
grep -qF "kernel: sdcard: listed /: 2 folders, 4 files" "$cart/page.log" ||
  fail "FAT32: the page listed $(grep 'sdcard: listed' "$cart/page.log")"
grep -qF "kernel: launch: loading $long, CLEAN BOOT" "$cart/page.log" ||
  fail "FAT32: the page didn't start the long name: $(grep '^kernel' "$cart/page.log" | tail -2)"
echo "PASS: the SD card page lists read-only and no-archive entries, not hidden or system ones, and starts a long name"

# The long name launches whole and gets a save of its own name
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch "/$long" at=180) -f 400 >"$cart/kernel.log" 2>&1
grep -qF "kernel: launch: loading $long, CLEAN BOOT" "$cart/kernel.log" ||
  fail "FAT32: the long name didn't launch: $(grep '^kernel' "$cart/kernel.log" | tail -2)"
# (a game missing from the save table gets its file when the chip differs:
# the launch names it in the pending-save record)
grep -qF "save: no /torii/saves/$long/Main.sav yet" "$cart/kernel.log" ||
  fail "FAT32: no save name for the long name: $(grep 'save:' "$cart/kernel.log" | head -3)"
echo "PASS: a name past 99 bytes launches and saves on FAT32"

# exFAT: a long name whose first 95 bytes match another file's whole name.
# Opening or deleting the long one must touch only it.
cart=$carts/listing-exfat
rm -rf "$cart" && mkdir -p "$cart"
short=$(printf 'A%.0s' $(seq 95)).gba
longer="${short%.gba} and a long tail.gba"
tools/sdimg.sh "$cart/sd.img" mkfs exfat 64
tools/sdimg.sh "$cart/sd.img" put tests/roms/hello/hello.gba "/$short"
tools/sdimg.sh "$cart/sd.img" put tests/roms/hello/hello.gba "/$longer"
# shellcheck disable=SC2046
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch "/$longer" at=160) -f 300 >"$cart/kernel.log" 2>&1
grep -qF "kernel: launch: loading $longer, CLEAN BOOT" "$cart/kernel.log" ||
  fail "exFAT: the long name didn't launch: $(grep '^kernel' "$cart/kernel.log" | tail -2)"
# Delete the long one (a fresh card)
rm -rf "$cart" && mkdir -p "$cart" # the kernel NOR too: the launch above left a save to copy
tools/sdimg.sh "$cart/sd.img" mkfs exfat 64
tools/sdimg.sh "$cart/sd.img" put tests/roms/hello/hello.gba "/$short"
tools/sdimg.sh "$cart/sd.img" put tests/roms/hello/hello.gba "/$longer"
# shellcheck disable=SC2046
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_delete "/$longer" at=160) -f 300 >/dev/null 2>&1
left=$(tools/sdimg.sh "$cart/sd.img" ls / | cut -f1 | grep -vx 'torii/\?') # the library index's folder
[ "$left" = "$short" ] || fail "exFAT: deleting the long name left: $(echo "$left" | tr '\n' ' ')"
echo "PASS: a name past 99 bytes launches and deletes only itself on exFAT"

# A name whose save path won't fit the pending-save record (over 251 bytes):
# launching it says the path is too long and writes no save
cart=$carts/listing-toolong
rm -rf "$cart" && mkdir -p "$cart"
toolong=$(printf 'L%.0s' $(seq 249)).gba
mkfs.fat -C -F 32 -n LISTING "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba "::/$toolong"
# shellcheck disable=SC2046
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch "/$toolong" at=180) \
  -f 400 >"$cart/kernel.log" 2>&1
grep -Eq "^kernel( error)?: error 8: The save's path is too long" "$cart/kernel.log" ||
  fail "a 253-byte name: no path-too-long error: $(grep '^kernel' "$cart/kernel.log" | tail -2)"
! mdir -i "$cart/sd.img" ::/torii/saves >/dev/null 2>&1 || fail "a 253-byte name: a save folder was made"
grep -qF "save path is over 251 bytes, the pending-save record's room" "$cart/kernel.log" || fail "a 253-byte name: not logged"
echo "PASS: a name too long for the pending-save record says so and writes no save"

# B held in a deep folder climbs a folder at a time to the card's root and
# stays on the page there (a fresh B leaves it)
cart=$carts/listing-climb
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n LISTING "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/A ::/A/B ::/A/B/C
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/A/B/C/x.gba
# shellcheck disable=SC2046
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_file /A/B/C/x.gba 0 0 0 0 popup) \
  -k @8:B -k @12:- -k @40:B -k @150:- -f 600 >"$cart/kernel.log" 2>&1
for up in /A/B /A /; do
  grep -qF "kernel: sdcard: up to $up" "$cart/kernel.log" || fail "B held: never up to $up: $(grep 'sdcard: up' "$cart/kernel.log")"
done
! grep -q 'nav: home ready' <(sed -n '/sdcard: up to \/$/,$p' "$cart/kernel.log") || fail "B held left the SD page at the root"
echo "PASS: B held climbs to the card's root and stays on the SD page"
