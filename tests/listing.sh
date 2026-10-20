#!/usr/bin/env bash
# What the SD list shows: every file and folder whatever its read-only or
# archive bits, but not hidden or system ones; and a name too long for a list
# entry's 100 bytes cut short, where opening it shows an error and touches no
# other file.
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/hello.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
local_carts "dist/cart-listing${1:+-$1}"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# The list's counts, read from memory once the menu is up
sym() { grep " $1\$" dist/ezkernelnew.sym | cut -d' ' -f1; }
counts() { # cart -> "files folders"
  OMEGA_DIR=$1 emu/build-linux/mgba-shot dist/ezkernelnew.bin -m "150:0x$(sym game_total_SD):4" \
    -m "150:0x$(sym folder_total):4" -f 150 2>&1 |
    sed -n 's/^mem frame 150 [0-9A-F]*: //p' | {
    read -r a _
    read -r b _
    echo $((16#$a)) $((16#$b))
  }
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
[ "$(counts "$cart")" = "4 2" ] || fail "FAT32: the list has $(counts "$cart") files and folders, expected 4 2"
echo "PASS: the list shows read-only and no-archive entries, not hidden or system ones"

# Folders first, then the long name, which sorts first: it opens whole,
# launches and gets a save of its own name
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:DOWN -k 104:- -k 116:DOWN -k 120:- \
  -k 132:A -k 136:- -k 180:A -k 184:- -f 400 >"$cart/kernel.log" 2>&1
grep -qF "kernel: launch: loading $long, CLEAN BOOT" "$cart/kernel.log" ||
  fail "FAT32: the long name didn't launch: $(grep '^kernel' "$cart/kernel.log" | tail -2)"
mdir -b -i "$cart/sd.img" ::/SAVER | grep -qF "${long%.gba}.sav" ||
  fail "FAT32: no save for the long name: $(mdir -b -i "$cart/sd.img" ::/SAVER | tr '\n' ' ')"
echo "PASS: a name past 99 bytes opens, launches and saves on FAT32"

# exFAT: a long name whose first 95 bytes match another file's whole name.
# Opening or deleting the long one must touch only it.
cart=$carts/listing-exfat
rm -rf "$cart" && mkdir -p "$cart"
short=$(printf 'A%.0s' $(seq 95)).gba
longer="${short%.gba} and a long tail.gba"
tools/sdimg.sh "$cart/sd.img" mkfs exfat 64
tools/sdimg.sh "$cart/sd.img" put tests/roms/hello/hello.gba "/$short"
tools/sdimg.sh "$cart/sd.img" put tests/roms/hello/hello.gba "/$longer"
[ "$(counts "$cart")" = "2 0" ] || fail "exFAT: the list has $(counts "$cart") files and folders, expected 2 0"
# The long one sorts first (a space before the short one's dot): open it, CLEAN BOOT
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 116:A -k 120:- -k 160:A -k 164:- -f 300 >"$cart/kernel.log" 2>&1
grep -qF "kernel: launch: loading $longer, CLEAN BOOT" "$cart/kernel.log" ||
  fail "exFAT: the long name didn't launch: $(grep '^kernel' "$cart/kernel.log" | tail -2)"
# Hold L, START on the long one, A to confirm the delete (a fresh card)
rm -rf "$cart" && mkdir -p "$cart" # the kernel NOR too: the launch above left a save to copy
tools/sdimg.sh "$cart/sd.img" mkfs exfat 64
tools/sdimg.sh "$cart/sd.img" put tests/roms/hello/hello.gba "/$short"
tools/sdimg.sh "$cart/sd.img" put tests/roms/hello/hello.gba "/$longer"
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 116:L -k 120:L+START -k 124:- -k 160:A -k 164:- \
  -s 150:dist/shots/listing-delete.png -f 300 >/dev/null 2>&1
left=$(tools/sdimg.sh "$cart/sd.img" ls / | cut -f1 | grep -vx 'torii/\?') # the library index's folder
[ "$left" = "$short" ] || fail "exFAT: deleting the long name left: $(echo "$left" | tr '\n' ' ')"
echo "PASS: a name past 99 bytes opens and deletes only itself on exFAT"

# A name whose save name won't fit the pending-save record (over 251 bytes):
# it lists, but launching says the name is too long and writes no save
cart=$carts/listing-toolong
rm -rf "$cart" && mkdir -p "$cart"
toolong=$(printf 'L%.0s' $(seq 249)).gba
mkfs.fat -C -F 32 -n LISTING "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/SAVER
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba "::/$toolong"
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 116:DOWN -k 120:- -k 132:A -k 136:- -k 180:A -k 184:- \
  -f 400 >"$cart/kernel.log" 2>&1
grep -Eq '^kernel( error)?: error 8: The file name is too long' "$cart/kernel.log" ||
  fail "a 253-byte name: no "The file name is too long": $(grep '^kernel' "$cart/kernel.log" | tail -2)"
[ -z "$(mdir -b -i "$cart/sd.img" ::/SAVER)" ] || fail "a 253-byte name: a save was written"
echo "PASS: a name too long for the pending-save record says so and writes no save"
