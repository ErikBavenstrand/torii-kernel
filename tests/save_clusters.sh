#!/usr/bin/env bash
# The save copy and new folders on cards of other cluster sizes, with their
# free space dirty (a deleted file's bytes): a new folder's cluster is
# cleared in one call (Write_SD_fill), the blank save is written 8 KB at a
# time and the copy 8 KB at a time (Save_savefile). One part per card:
#   f32s1   FAT32, 512-byte clusters
#   f16s8   FAT16, 4 KB clusters
#   f16s64  FAT16, 32 KB clusters
# Each: the first launch's new folders read empty and fsck.fat finds nothing;
# then the second copy is cut at each cutpoint (tests/cutsweep.sh), and the
# next power-on leaves the game's newer save whole and the card clean.
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/savetest.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
# shellcheck source=tests/cutsweep.sh
. tests/cutsweep.sh
part=$1
local_carts "dist/cart-save-clusters-$part"
cart=$carts/$part
mkdir -p "$cart"
savep=/torii/saves/Test.gba/Main.sav

fail() {
  echo "FAIL: save-clusters $part: $*" >&2
  [ ! -f "$cart/run.log" ] || tail -15 "$cart/run.log" >&2
  exit 1
}
logged() { grep -qF "kernel: $1" "$cart/run.log" || fail "no log line: $1"; }
boot() {
  cut_run "$@"
  [ "$cut_status" = 0 ] || fail "the emulator stopped with status $cut_status"
  ! grep -q '^crash at frame' "$cart/run.log" || fail "the emulator caught a crash"
}
clean_card() { # fsck.fat -n finds nothing ($1: when)
  local out
  out=$(fsck.fat -n "$cart/sd.img" 2>&1) || fail "$1: fsck.fat: $out"
  ! grep -qiE 'free cluster summary|differences|dirty|lost|unused|wrong|invalid|bad' <<<"$out" || fail "$1: fsck.fat: $out"
}
record() { # the save's boot record count byte (Flash 128K: at 0x10000)
  mtype -i "$cart/sd.img" ::$savep >"$cart/test.sav" 2>/dev/null || {
    echo none
    return
  }
  [ "$(stat -c %s "$cart/test.sav")" = 131072 ] || {
    echo "size $(stat -c %s "$cart/test.sav")"
    return
  }
  od -A n -t x1 -j $((0x10000)) -N 5 "$cart/test.sav" | tr -d ' \n'
}

case $part in
f32s1) mkfs.fat -C -F 32 -s 1 -n SAVES "$cart/sd.img" 65536 >/dev/null ;;
f16s8) mkfs.fat -C -F 16 -s 8 -n SAVES "$cart/sd.img" 65536 >/dev/null ;;
f16s64) mkfs.fat -C -F 16 -s 64 -n SAVES "$cart/sd.img" 163840 >/dev/null ;;
*)
  echo "usage: $0 f32s1|f16s8|f16s64" >&2
  exit 2
  ;;
esac
# Dirty free space: a deleted file of 0xA5, then the game
head -c $((8 * 1024 * 1024)) /dev/zero | tr '\0' '\245' >"$carts/dirt.bin"
mcopy -i "$cart/sd.img" "$carts/dirt.bin" ::/DIRT.BIN
mdel -i "$cart/sd.img" ::/DIRT.BIN
mcopy -i "$cart/sd.img" tests/roms/savetest/flash128.gba ::/Test.gba

# Power-on 1: the launch makes /torii/saves/Test.gba and a blank save
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_launch /Test.gba save=5 at=250) -f 600
logged "save: made a blank $savep, 131072 bytes"
clean_card "after the first launch"
# Every folder under /torii reads as only its own entries: no 0xA5 junk
junk=$(mdir -i "$cart/sd.img" -/ -a ::/torii 2>&1 | grep -c $'\xa5' || true)
[ "$junk" = 0 ] || fail "junk entries in a new folder: $(mdir -i "$cart/sd.img" -/ -a ::/torii | grep -a $'\xa5' | head -3)"
names=$(mdir -i "$cart/sd.img" -b ::/torii/saves/Test.gba | sed 's|.*/||' | sort | tr '\n' ' ')
[ "$names" = "Main.sav game.ini " ] || fail "/torii/saves/Test.gba holds: $names"
[ "$(od -A n -t x1 -N 4 -j 0 <(mtype -i "$cart/sd.img" ::$savep) | tr -d ' ')" = ffffffff ] || fail "the blank save isn't 0xFF"

# Power-on 2: the first copy; power-on 3: the second launch, a newer record
boot -k 100:A -k 106:- -f 800
[ "$(record)" = 4f4d525801 ] || fail "the first copy holds $(record)"
clean_card "after the first copy"
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_launch /Test.gba at=250) -f 600

# Power-on 4, the second copy, cut at each cutpoint; the next power-on
# (A at "Save to SD card?" if it asks again) must leave the newer save whole
scenario() { cut_run -k 100:A -k 106:- -f 800 "$@"; }
check() {
  local n=$1 name=$2 r
  boot -k 100:A -k 106:- -f 800
  r=$(record)
  [ "$r" = 4f4d525802 ] || fail "cut $n ($name): the save holds $r after the next power-on"
  [ "$(pending_flag "$cart/kernel-nor.bin")" = 0000 ] || fail "cut $n ($name): the record still waits"
  clean_card "cut $n ($name)"
  ! mdir -i "$cart/sd.img" -b ::/torii/saves/Test.gba | grep -qE '\.(tmp|bak)$' || fail "cut $n ($name): a .tmp or .bak is left"
}
cut_sweep scenario check
echo "PASS: save-clusters $part: new folders read empty, and the copy survives a cut at each cutpoint"
