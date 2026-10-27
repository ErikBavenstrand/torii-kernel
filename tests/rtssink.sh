#!/usr/bin/env bash
# /torii/rts.bin, the file every launch's table names for the FPGA's
# write-back of cart SRAM's state buffer (source/rtssink.h), on the emulated
# cart with the hello ROM:
#
#   truncated  rts.bin's cluster chain cut short in the FAT (its size says
#              448 KB, its chain covers 10 clusters): the launch makes it
#              again and starts the game, its new chain whole.
#   full       a card with 448 KB free but not 128 KB more (the room a
#              save made at the next boot may need), then 256 KB free: the
#              launch stops with a message that says what's missing, and
#              leaves no rts.bin.
#   coldboot   a cold boot (the cart's table all zeros) with a state and its
#              flag in cart SRAM, and the emulated cart writing the buffer
#              back on kernel writes too (OMEGA_RTS_KERNEL_WRITES): a launch
#              writes nothing to the card outside rts.bin.
#
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and `just test-roms`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh

mode=${1:?usage: rtssink.sh truncated|full|coldboot}
local_carts "dist/cart-rtssink-$mode"

fail() {
  echo "FAIL: rtssink $mode: $*" >&2
  exit 1
}

cart=$carts/rs shots=dist/shots/rtssink
rm -rf "$cart"
mkdir -p "$cart" "$shots"
mkfs.fat -C -F 32 -n SINK "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Game.gba

status=0
boot() {
  status=0
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -c 1767225600 "$@" >"$cart/out.log" \
    2>"$cart/run.log" || status=$?
  ! grep -q '^crash at frame' "$cart/run.log" || fail "the emulator caught a crash"
}
logged() { grep -qF -- "$1" "$cart/run.log" || fail "no line '$1': $(grep -E 'kernel|omega' "$cart/run.log" | tail -5)"; }

# The clusters of rts.bin's chain (mshowfat's <a-b> runs), one a line
clusters() {
  mshowfat -i "$cart/sd.img" ::/torii/rts.bin | grep -o '<[0-9-]*>' | tr -d '<>' |
    awk -F- '{ last = $2 == "" ? $1 : $2; for (c = $1; c <= last; c++) print c }'
}

case $mode in
truncated)
  rts_sink "$cart/sd.img"
  first=$(clusters | head -1)
  [ "$(clusters | wc -l)" = 896 ] || fail "the test's rts.bin isn't 896 clusters of 512 bytes"
  # The 10th cluster's FAT entry made the chain's end, in both FATs
  python3 - "$cart/sd.img" $((first + 9)) <<'PY'
import struct, sys
path, cluster = sys.argv[1], int(sys.argv[2])
with open(path, "r+b") as f:
    boot = f.read(512)
    per_sector, _, reserved, fats = struct.unpack_from("<HBHB", boot, 11)
    fat_sectors = struct.unpack_from("<I", boot, 36)[0]
    for k in range(fats):
        f.seek((reserved + k * fat_sectors) * per_sector + cluster * 4)
        f.write(struct.pack("<I", 0x0FFFFFFF))
PY
  [ "$(clusters | wc -l)" = 10 ] || fail "the chain wasn't cut short"
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_launch /Game.gba at=180) -f 600
  logged "kernel error: launch: /torii/rts.bin's cluster chain covers 10 sectors, not 896: made again"
  logged "kernel: launch: made /torii/rts.bin, 458752 bytes"
  logged "kernel: launch: loading Game.gba, CLEAN BOOT"
  [ "$(clusters | wc -l)" = 896 ] || fail "the new rts.bin's chain is $(clusters | wc -l) clusters"
  echo "PASS: rtssink truncated: a cluster chain cut short is made again before the table names it"
  ;;
full)
  free=$(mdir -i "$cart/sd.img" ::/ | sed -n 's/^ *\([0-9 ]*\) bytes free.*/\1/p' | tr -d ' ')
  head -c $((free - 458752 - 65536)) /dev/urandom >"$cart/filler.bin"
  mcopy -i "$cart/sd.img" "$cart/filler.bin" ::/Filler.bin
  for left in save-room 256k; do
    if [ $left = 256k ]; then
      mdel -i "$cart/sd.img" ::/Filler.bin
      free=$(mdir -i "$cart/sd.img" ::/ | sed -n 's/^ *\([0-9 ]*\) bytes free.*/\1/p' | tr -d ' ')
      head -c $((free - 262144)) /dev/urandom >"$cart/filler.bin"
      mcopy -i "$cart/sd.img" "$cart/filler.bin" ::/Filler.bin
    fi
    # shellcheck disable=SC2046 # nav.sh's options
    boot $(nav_launch /Game.gba at=180) -w "error 10:" -w "screen ready: dialog" -s @4:"$shots/full-$left.png" -f 600
    logged "kernel error: error 10: Torii needs 448 KB free in one piece on the card for in-game states (/torii/rts.bin). Free some space."
    logged "kernel: screen ready: dialog"
    ! grep -q "launch: loading Game.gba" "$cart/run.log" || fail "$left: the game was launched"
    ! mtype -i "$cart/sd.img" ::/torii/rts.bin >/dev/null 2>&1 || fail "$left: a part of rts.bin is left"
    mtype -i "$cart/sd.img" ::/Filler.bin | cmp -s - "$cart/filler.bin" || fail "$left: the other file changed"
    fsck.fat -n "$cart/sd.img" >/dev/null || fail "$left: fsck.fat found errors"
  done
  echo "PASS: rtssink full: no room for rts.bin and a save after it stops the launch with its own message"
  ;;
coldboot)
  # A state with its flag, as a game saved in game and the cart was then
  # turned off; the buffer's bytes 0xA3, as Emerald's EWRAM heap starts
  python3 - "$cart/sram.bin" <<'PY'
import sys
b = bytearray(0x100000)
for i in range(0x40000, 0xAFFF0):
    b[i] = 0xA3
b[0xAFFF0:0xB0000] = b"EZ-OmegaRTCFILE."
open(sys.argv[1], "wb").write(b)
PY
  cp "$cart/sd.img" "$cart/before.img"
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_RTS_KERNEL_WRITES=1 boot $(nav_launch /Game.gba at=180) -f 600
  [ "$status" != 81 ] || fail "the state buffer was written back outside rts.bin: $(grep -m2 'omega: ' "$cart/run.log")"
  logged "kernel: launch: loading Game.gba, CLEAN BOOT"
  # Any write-back went to rts.bin's sectors
  range=$(sd_sectors "$cart/sd.img" /torii/rts.bin)
  sed -n 's/^omega: state buffer written back to sectors \([0-9]*\)-\([0-9]*\).*/\1 \2/p' "$cart/run.log" |
    while read -r lo hi; do
      [ "$lo" -ge "${range%-*}" ] && [ "$hi" -le "${range#*-}" ] || fail "written back to $lo-$hi, rts.bin is $range"
    done
  mtype -i "$cart/sd.img" ::/Game.gba | cmp -s - tests/roms/hello/hello.gba || fail "/Game.gba changed"
  cmp -s -n 512 "$cart/sd.img" "$cart/before.img" || fail "the card's first sector changed"
  fsck.fat -n "$cart/sd.img" >/dev/null || fail "fsck.fat found errors"
  echo "PASS: rtssink coldboot: a cold boot's launch with a flagged state writes nothing outside rts.bin"
  ;;
*) fail "unknown mode $mode" ;;
esac
