#!/usr/bin/env bash
# Game NOR safety on the emulated cart: power cuts while a game or the NOR list
# is written, deleting the latest game, a lost list, a list from an older
# kernel, a ROM without gbafix's 0x96, and Recently Played for NOR games. Each
# case writes small test ROMs through WRITE TO NOR and checks where the next
# write lands: never over a listed game. OMEGA_POWER_CUT (emu omega.c) cuts
# the power between two flash operations. Runs inside the torii-emu container
# after `just build`, `just emu-headless` and the test ROMs. KERNEL picks
# another kernel file, e.g. an older build to see the cases fail.
set -euo pipefail

kernel=${KERNEL:-dist/ezkernelnew.bin}
root=dist/cart-nor-safety
rm -rf "$root"
mkdir -p "$root"

fail() {
  echo "FAIL: $case: $*" >&2
  exit 1
}

# rom OUT SOURCE [BLOCKS [FILL]]: SOURCE padded to BLOCKS 128 KB blocks of
# FILL bytes, its header complement fixed, so a test can change the header.
rom() {
  python3 - "$@" <<'EOF'
import sys
out, src = sys.argv[1], sys.argv[2]
blocks = int(sys.argv[3]) if len(sys.argv) > 3 else 0
fill = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0
rom = bytearray(open(src, "rb").read())
if blocks:
    rom += bytes([fill]) * (blocks * 0x20000 - len(rom))
rom[0xBD] = -(sum(rom[0xA0:0xBD]) + 0x19) & 0xFF
open(out, "wb").write(rom)
EOF
}

# new_cart NAME FILE...: a cart with an erased game NOR and an SD card holding
# /SAVER and the FILEs at the root (cart/FILE paths), listed as SAVER, A, B, C.
new_cart() {
  case=$1
  shift
  cart=$root/$case
  mkdir -p "$cart"
  head -c $((64 << 20)) /dev/zero | tr '\0' '\377' >"$cart/game-nor.bin"
  mkfs.fat -C -F 32 -n NOR "$cart/sd.img" 65536 >/dev/null
  mmd -i "$cart/sd.img" ::/SAVER
  local f
  for f in "$@"; do
    mcopy -i "$cart/sd.img" "$root/$f" "::/$f"
  done
  runs=0
}

# boot [-c CUT] OPTIONS...: one power-on, with the cart trace; with -c the
# power is cut at CUT (OMEGA_POWER_CUT), which must happen.
boot() {
  local cut='' status=0 log
  if [ "$1" = -c ]; then
    cut=$2
    shift 2
  fi
  runs=$((runs + 1))
  log=$cart/$runs.log
  OMEGA_POWER_CUT=$cut OMEGA_TRACE=1 OMEGA_DIR=$cart emu/build-linux/mgba-shot "$kernel" "$@" >"$log" 2>&1 ||
    status=$?
  if [ -n "$cut" ]; then
    [ $status = 75 ] || fail "power-on $runs: no power cut at $cut (exit $status)"
  elif [ $status != 0 ]; then
    fail "power-on $runs: mgba-shot exited with $status"
  fi
  if grep -E 'assert failed|^crash at frame' "$log" >&2; then
    fail "power-on $runs: the kernel crashed or failed an assert"
  fi
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

# write N [CUT]: WRITE TO NOR CLEAN for the Nth file in the root (1 = A).
write() {
  local downs=() i
  for ((i = 0; i < $1; i++)); do downs+=(DOWN); done
  # shellcheck disable=SC2046 # $(keys) is a list of options
  boot ${2:+-c "$2"} $(keys 100 "${downs[@]}" A DOWN DOWN A) -f 600
}

# holds OFFSET FILE: the game NOR holds root FILE at OFFSET
holds() {
  cmp -s -n "$(stat -c %s "$root/$2")" <(tail -c +$(($1 + 1)) "$cart/game-nor.bin") "$root/$2"
}

# erased OFFSET LENGTH: the game NOR reads 0xFF there
erased() {
  [ -z "$(tail -c +$(($1 + 1)) "$cart/game-nor.bin" | head -c $(($2)) | tr -d '\377' | head -c 1)" ]
}

# kernel_wipe OFFSET...: erase the 64 KB kernel NOR sectors at OFFSET
kernel_wipe() {
  local o
  for o in "$@"; do
    head -c 65536 /dev/zero | tr '\0' '\377' |
      dd of="$cart/kernel-nor.bin" bs=65536 seek=$((o / 65536)) conv=notrunc status=none
  done
}

rom "$root/A.gba" tests/roms/hello/hello.gba
rom "$root/B.gba" tests/roms/savetest/flash128.gba
rom "$root/C.gba" tests/roms/savetest/eeprom8k.gba
# B2 holds B's header but other bytes, over three blocks; B2x over two
rom "$root/B2.gba" tests/roms/savetest/flash128.gba 3 0x5A
rom "$root/B2x.gba" tests/roms/savetest/flash128.gba 2 0x5A
rom "$root/D.gba" tests/roms/savetest/flash64.gba
# A without gbafix's fixed byte 0x96 at 0xB2, as homebrew can be
python3 - "$root/A.gba" "$root/U.gba" <<'EOF'
import sys
rom = bytearray(open(sys.argv[1], "rb").read())
rom[0xB2] = 0
rom[0xBD] = -(sum(rom[0xA0:0xBD]) + 0x19) & 0xFF
open(sys.argv[2], "wb").write(rom)
EOF

# A game whose header lacks 0x96 is listed, so the next write goes after it.
new_cart unfixed U.gba B.gba
write 2
write 1
holds 0 U.gba || fail "U.gba is not at 0"
holds 0x20000 B.gba || fail "B.gba is not at 0x20000 (U.gba wasn't listed)"
echo "PASS: a ROM without 0x96 at 0xB2 is listed and kept"

# Power cuts in each step of the NOR list update after B's data is written:
# A and B stay, and C goes after them.
for cut in kernel:erase:1@0x7A0000 kernel:program:1@0x7A0000 kernel:program:4240@0x7A0000 \
  kernel:erase:1@0x7C0000 kernel:program:1@0x7C0000 kernel:program:4240@0x7C0000; do
  new_cart "list-cut-${cut//[:@]/-}" A.gba B.gba C.gba
  write 1
  write 2 "$cut"
  write 3
  holds 0 A.gba || fail "A.gba was overwritten"
  holds 0x20000 B.gba || fail "B.gba is not at 0x20000"
  holds 0x40000 C.gba || fail "C.gba is not at 0x40000"
  echo "PASS: a power cut at $cut while saving the NOR list loses no game"
done

# Delete latest erases the whole game and drops it from the list. A later
# write with the same header, cut after its first block, is not listed.
new_cart delete A.gba B.gba B2.gba C.gba
write 1
write 2
# shellcheck disable=SC2046
boot $(keys 100 R DOWN A DOWN A) -f 400
erased 0x20000 0x20000 || fail "delete latest left B.gba's data"
write 3 game:program:4097
write 4
holds 0 A.gba || fail "A.gba was overwritten"
holds 0x20000 C.gba || fail "C.gba is not at 0x20000: the half-written B2.gba is listed"
echo "PASS: delete latest erases the game, and a half-written game with its header isn't listed"

# A power cut while a game's data is written: the game isn't listed.
new_cart data-cut A.gba B2.gba C.gba
write 1
write 2 game:program:4097
write 3
holds 0 A.gba || fail "A.gba was overwritten"
holds 0x20000 C.gba || fail "C.gba is not at 0x20000: the half-written B2.gba is listed"
echo "PASS: a game cut off while written isn't listed"

# Both copies of the NOR list lost: the boot rebuilds the list from the games
# in NOR, writes nothing, and the next write goes after them.
new_cart lost-list A.gba B.gba C.gba
write 1
write 2
kernel_wipe 0x7A0000 0x7C0000
cp "$cart/kernel-nor.bin" "$cart/wiped.bin"
boot -f 300
cmp -s <(tail -c +$((0x7A0001)) "$cart/kernel-nor.bin" | head -c $((0x30000))) \
  <(tail -c +$((0x7A0001)) "$cart/wiped.bin" | head -c $((0x30000))) ||
  fail "the boot wrote the NOR list"
write 3
holds 0 A.gba || fail "A.gba was overwritten"
holds 0x20000 B.gba || fail "B.gba was overwritten"
holds 0x40000 C.gba || fail "C.gba is not at 0x40000"
echo "PASS: a lost NOR list is rebuilt from the games in NOR, and the boot doesn't write it"

# A list from an older kernel (no copy at 0x7C0000, no trailer, no
# completion marks in NOR) still counts.
new_cart legacy A.gba B.gba
write 1
python3 - "$cart/kernel-nor.bin" "$cart/game-nor.bin" <<'EOF'
import sys
k = bytearray(open(sys.argv[1], "rb").read())
g = bytearray(open(sys.argv[2], "rb").read())
k[0x7A2100:0x7B0000] = b"\xff" * (0x7B0000 - 0x7A2100)  # the trailer
k[0x7C0000:0x7D0000] = b"\xff" * 0x10000                # the second copy
k[0x7A0000 + 0x71] = ord("T")  # stack garbage there can look like Torii's mark flag
g[0x20000 - 0x200:0x20000] = b"\xff" * 0x200            # A's completion mark
open(sys.argv[1], "wb").write(k)
open(sys.argv[2], "wb").write(g)
EOF
write 2
holds 0 A.gba || fail "A.gba was overwritten"
holds 0x20000 B.gba || fail "B.gba is not at 0x20000: the older list was lost"
echo "PASS: a NOR list from an older kernel is kept"

# entry NAME OFFSET SIZE ROM: an older kernel's list entry, as hex
entry() {
  python3 - "$@" <<'EOF2'
import struct, sys
name, offset, size, rom = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0), sys.argv[4]
title = open(rom, "rb").read()[0xA0:0xB0]
print(struct.pack("<100sHHHHIBBH16s", name.encode(), offset >> 17, 0, 0, 0, size, 0, 0, 0, title).hex())
EOF2
}

# older_list KERNEL_NOR HEX...: copy A as an older kernel saves it (the
# sector erased, then the entries), copy B left alone
older_list() {
  python3 - "$@" <<'EOF2'
import sys
k = bytearray(open(sys.argv[1], "rb").read())
entries = b"".join(bytes.fromhex(h) for h in sys.argv[2:])
k[0x7A0000:0x7B0000] = b"\xff" * 0x10000
k[0x7A0000:0x7A2100] = entries + bytes(0x2100 - len(entries))
open(sys.argv[1], "wb").write(k)
EOF2
}

# Nearly full: an older kernel's game up to 256 KB before the end, then a
# 256 KB ROM, which leaves no room for a block for its mark. It is written
# without one rather than refused.
new_cart near-full A.gba B2x.gba
dd if="$root/A.gba" of="$cart/game-nor.bin" conv=notrunc status=none
head -c $((8 << 20)) /dev/zero | tr '\0' '\377' >"$cart/kernel-nor.bin"
older_list "$cart/kernel-nor.bin" "$(entry A.gba 0 0x3FC0000 "$root/A.gba")"
write 2
holds 0x3FC0000 B2x.gba || fail "B2x.gba wasn't written in the last 256 KB"
boot -f 200
grep -q 'nor: 2 games, the next goes at 4000000' "$cart/$runs.log" || fail "B2x.gba isn't listed"
echo "PASS: a game that fills the game NOR is written without its completion mark"

# A downgrade and back: an older kernel adds C after Torii's A and B, saving
# only copy A. Copy B is whole but older; the next write must go after C.
new_cart downgrade A.gba B.gba C.gba D.gba
write 1
write 2
dd if="$root/C.gba" of="$cart/game-nor.bin" bs=128K seek=2 conv=notrunc status=none
older_list "$cart/kernel-nor.bin" "$(head -c 132 <(tail -c +$((0x7A0001)) "$cart/kernel-nor.bin") | od -v -A n -t x1 | tr -d ' \n')" \
  "$(tail -c +$((0x7A0001 + 132)) "$cart/kernel-nor.bin" | head -c 132 | od -v -A n -t x1 | tr -d ' \n')" \
  "$(entry C.gba 0x40000 0x20000 "$root/C.gba")"
write 4
holds 0x40000 C.gba || fail "C.gba, added by an older kernel, was overwritten"
holds 0x60000 D.gba || fail "D.gba is not at 0x60000"
echo "PASS: games an older kernel added after a downgrade are kept"

# A list whose games fill all 64 MB (here one older kernel's entry): the scan
# used to return without turning interrupts back on, and the menu then hung
# in its first wait for VBlank. R must still switch to the NOR list.
new_cart full A.gba
dd if="$root/A.gba" of="$cart/game-nor.bin" conv=notrunc status=none
head -c $((8 << 20)) /dev/zero | tr '\0' '\377' >"$cart/kernel-nor.bin"
python3 - "$cart/kernel-nor.bin" "$root/A.gba" <<'EOF'
import struct, sys
k = bytearray(open(sys.argv[1], "rb").read())
title = open(sys.argv[2], "rb").read()[0xA0:0xB0]
entry = struct.pack("<100sHHHHIBBH16s", b"A.gba", 0, 0, 0, 0, 0x4000000, 0, 0, 0, title)
k[0x7A0000:0x7A0000 + len(entry)] = entry
open(sys.argv[1], "wb").write(k)
EOF
boot -k 200:R -k 204:- -h 190 -h 300 -f 300
[ "$(grep -c '^hash frame' "$cart/$runs.log")" = 2 ] || fail "no screen hashes"
[ "$(grep '^hash frame' "$cart/$runs.log" | cut -d: -f2 | sort -u | wc -l)" = 2 ] ||
  fail "R didn't switch to the NOR list: the menu hung"
echo "PASS: a NOR list that fills the game NOR leaves interrupts on"

# A game started from NOR goes on Recently Played, and opens from NOR there.
new_cart recent A.gba
write 1
# shellcheck disable=SC2046
boot $(keys 100 R A A) -f 400
grep -q 'mode game from NOR offset 000000$' "$cart/$runs.log" || fail "A.gba didn't start from NOR"
recent=$(mtype -i "$cart/sd.img" "::/SAVER/Recently play.txt" 2>/dev/null || true)
[ "$recent" = "NOR:A.gba" ] || fail "Recently Played has '$recent', expected NOR:A.gba"
# Confirm the save copy, then START and the first entry
# shellcheck disable=SC2046
boot -k 100:A -k 104:- $(keys 300 START A) -f 600
grep -q 'mode game from NOR offset 000000$' "$cart/$runs.log" || fail "Recently Played didn't start A.gba from NOR"
echo "PASS: a NOR game goes on Recently Played and starts from NOR there"
