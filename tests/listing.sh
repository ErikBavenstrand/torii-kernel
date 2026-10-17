#!/usr/bin/env bash
# What the SD list shows: every file and folder whatever its read-only or
# archive bits, but not hidden or system ones; and a name too long for a list
# entry's 100 bytes cut short, where opening it shows an error and touches no
# other file.
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/hello.
set -euo pipefail

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# The list's counts, read from memory once the menu is up
sym() { grep " $1\$" dist/ezkernelnew.sym | cut -d' ' -f1; }
counts() { # cart -> "files folders"
  OMEGA_DIR=$1 emu/build-linux/mgba-shot dist/ezkernelnew.bin -m "150:0x$(sym game_total_SD):8" -f 150 2>&1 |
    sed -n 's/^mem frame 150 [0-9A-F]*: //p' | {
    read -r a _ _ _ b _
    echo $((16#$a)) $((16#$b))
  }
}

long="A long name that goes on and on past the hundred bytes a list entry keeps for each file in a folder.gba"
[ ${#long} -gt 100 ] || fail "the long name is only ${#long} bytes"

# FAT32: attributes, and a long name that opens as its 8.3 name
cart=dist/cart-listing-fat32
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

# Folders first, then the long name, which sorts first: opening it must say
# the name is too long and launch nothing
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:DOWN -k 104:- -k 116:DOWN -k 120:- \
  -k 132:A -k 136:- -k 180:A -k 184:- -f 400 >"$cart/kernel.log" 2>&1
grep -q '^kernel: error 8: Name too long' "$cart/kernel.log" ||
  fail "FAT32: no error for the long name: $(grep '^kernel: ' "$cart/kernel.log" | tail -2)"
grep -q '^kernel: launch' "$cart/kernel.log" && fail "FAT32: the long name launched something"
echo "PASS: a name past 99 bytes shows Name too long on FAT32"

# exFAT: a long name whose first 95 bytes match another file's whole name.
# Opening or deleting the long one must not touch the other.
cart=dist/cart-listing-exfat
rm -rf "$cart" && mkdir -p "$cart"
short=$(printf 'A%.0s' $(seq 95)).gba
tools/sdimg.sh "$cart/sd.img" mkfs exfat 64
tools/sdimg.sh "$cart/sd.img" put tests/roms/hello/hello.gba "/$short"
tools/sdimg.sh "$cart/sd.img" put tests/roms/hello/hello.gba "/${short%.gba} and a long tail.gba"
[ "$(counts "$cart")" = "2 0" ] || fail "exFAT: the list has $(counts "$cart") files and folders, expected 2 0"
# The long one sorts first (a space before the short one's dot): open it, CLEAN BOOT
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 116:A -k 120:- -k 160:A -k 164:- -f 300 >"$cart/kernel.log" 2>&1
grep -q '^kernel: error 8: Name too long' "$cart/kernel.log" ||
  fail "exFAT: no error for the long name: $(grep '^kernel: ' "$cart/kernel.log" | tail -2)"
grep -q '^kernel: launch' "$cart/kernel.log" && fail "exFAT: the long name launched the other file"
# Hold L, START on the long one, A to confirm the delete
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 116:L -k 120:L+START -k 124:- -k 160:A -k 164:- \
  -s 150:dist/shots/listing-delete.png -f 300 >/dev/null 2>&1
[ "$(tools/sdimg.sh "$cart/sd.img" ls / | wc -l)" = 2 ] ||
  fail "exFAT: deleting the long name removed a file: $(tools/sdimg.sh "$cart/sd.img" ls / | tr '\n' ' ')"
echo "PASS: a name past 99 bytes shows Name too long on exFAT, and opens or deletes nothing"
