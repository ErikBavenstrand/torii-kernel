#!/usr/bin/env bash
# What the SD list shows: every file and folder whatever its read-only or
# archive bits, but not hidden or system ones; and a name too long for a list
# entry's 100 bytes as its 8.3 name, which opens (FAT32), or cut short (exFAT,
# which has no 8.3 names), where opening it shows an error.
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

# Folders first, then the long name's 8.3 name sorts first: open it, CLEAN BOOT
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:DOWN -k 104:- -k 116:DOWN -k 120:- \
  -k 132:A -k 136:- -k 180:A -k 184:- -f 400 >"$cart/kernel.log" 2>&1
grep -q '^kernel: launch ALONGN~1.GBA:' "$cart/kernel.log" ||
  fail "FAT32: the long name didn't launch as its 8.3 name: $(grep '^kernel: ' "$cart/kernel.log" | tail -2)"
od -A n -t x1 -N 5 "$cart/sram.bin" | tr -d ' \n' | grep -q '4f4d525801' || fail "FAT32: the long-named game didn't boot"
echo "PASS: a name past 100 bytes opens as its 8.3 name on FAT32"

# exFAT: no 8.3 name, so the entry is cut short, keeping .gba, and opening it shows an error
cart=dist/cart-listing-exfat
rm -rf "$cart" && mkdir -p "$cart"
tools/sdimg.sh "$cart/sd.img" mkfs exfat 64
tools/sdimg.sh "$cart/sd.img" put tests/roms/hello/hello.gba "/$long"
[ "$(counts "$cart")" = "1 0" ] || fail "exFAT: the list has $(counts "$cart") files and folders, expected 1 0"
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:A -k 104:- -k 150:A -k 154:- -f 300 \
  >"$cart/kernel.log" 2>&1
grep -qF "kernel: launch ${long:0:95}.gba: can't open it" "$cart/kernel.log" ||
  fail "exFAT: no error for the cut-short name: $(grep '^kernel: ' "$cart/kernel.log" | tail -2)"
echo "PASS: a name past 100 bytes lists cut short on exFAT, and shows an error"
