#!/usr/bin/env bash
# Names past a v0.3 list entry's 100 bytes: they launch and save whole on
# FAT32, launching or deleting one touches no other file on exFAT, and one
# too long for the pending-save record says so. Launches and deletes go as
# the pages will (actions.c, through tests/nav.sh's hook). What the SD card
# page lists (read-only and no-archive entries, not hidden or system ones)
# comes back with B14.
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
echo "SKIP (waiting for B14): the SD card page shows read-only and no-archive entries, not hidden or system ones"

# The long name launches whole and gets a save of its own name
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch "/$long" at=180) -f 400 >"$cart/kernel.log" 2>&1
grep -qF "kernel: launch: loading $long, CLEAN BOOT" "$cart/kernel.log" ||
  fail "FAT32: the long name didn't launch: $(grep '^kernel' "$cart/kernel.log" | tail -2)"
mdir -b -i "$cart/sd.img" ::/SAVER | grep -qF "${long%.gba}.sav" ||
  fail "FAT32: no save for the long name: $(mdir -b -i "$cart/sd.img" ::/SAVER | tr '\n' ' ')"
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

# A name whose save name won't fit the pending-save record (over 251 bytes):
# launching it says the name is too long and writes no save
cart=$carts/listing-toolong
rm -rf "$cart" && mkdir -p "$cart"
toolong=$(printf 'L%.0s' $(seq 249)).gba
mkfs.fat -C -F 32 -n LISTING "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/SAVER
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba "::/$toolong"
# shellcheck disable=SC2046
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch "/$toolong" at=180) \
  -f 400 >"$cart/kernel.log" 2>&1
grep -Eq '^kernel( error)?: error 8: The file name is too long' "$cart/kernel.log" ||
  fail "a 253-byte name: no "The file name is too long": $(grep '^kernel' "$cart/kernel.log" | tail -2)"
[ -z "$(mdir -b -i "$cart/sd.img" ::/SAVER)" ] || fail "a 253-byte name: a save was written"
echo "PASS: a name too long for the pending-save record says so and writes no save"
