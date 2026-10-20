#!/usr/bin/env bash
# Game NOR safety on the emulated cart: power cuts while a game or the NOR list
# is written, deleting the latest game, a lost list, a list from an older
# kernel, a ROM without gbafix's 0x96, and Recent for NOR games. Each
# case writes small test ROMs through WRITE TO NOR (tests/nav.sh's hook) and
# checks where the next write lands: never over a listed game. OMEGA_POWER_CUT
# (emu omega.c) cuts the power between two flash operations. Runs inside the torii-emu container
# after `just build`, `just emu-headless` and the test ROMs. KERNEL picks
# another kernel file, e.g. an older build to see the cases fail.
set -euo pipefail

kernel=${KERNEL:-dist/ezkernelnew.bin}
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
# An argument N/M runs every Mth case from the Nth (shard_init)
shard_init "${1:-}"
local_carts "dist/cart-nor-safety${1:+-${1/\//of}}"
root=$carts

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

# new_cart NAME FILE...: a cart with an erased game NOR (the emulator makes it,
# filling what a test didn't write with 0xFF) and an SD card holding /SAVER
# and the FILEs at the root (cart/FILE paths), in name order as write counts
# them.
new_cart() {
  case=$1
  shift
  mapfile -t files < <(printf '%s\n' "$@" | sort)
  cart=$root/$case
  mkdir -p "$cart"
  mkfs.fat -C -F 32 -n NOR "$cart/sd.img" 65536 >/dev/null
  mmd -i "$cart/sd.img" ::/SAVER
  local f
  for f in "$@"; do
    mcopy -i "$cart/sd.img" "$root/$f" "::/$f"
  done
  runs=0
}

# On a real cart the kernel-mode window's last 256 KB, kernel NOR 0x7C0000
# up, wrap onto EZ-Flash's bootloader (source/ez_define.h); v0.2.2 put the
# NOR list's copy B there and bricked a cart. bootloader_fill fills it like a
# bootloader; outside_data prints a hash of the kernel NOR around the data
# sectors 0x780000-0x7BFFFF, which no power-on may change.
bootloader_fill() {
  python3 - "$cart/kernel-nor.bin" <<'EOF'
import sys
with open(sys.argv[1], "r+b") as f:
    f.seek(0x7C0000)
    f.write(bytes(range(256)) * (0x40000 // 256))
EOF
}
outside_data() {
  # (from 0x300000: the emulator copies the kernel file to 0 at each power-on)
  {
    tail -c +$((0x300000 + 1)) "$cart/kernel-nor.bin" | head -c $((0x480000))
    tail -c +$((0x7C0000 + 1)) "$cart/kernel-nor.bin"
  } | md5sum
}

# boot [-c CUT] OPTIONS...: one power-on, with the cart trace; with -c the
# power is cut at CUT (OMEGA_POWER_CUT), which must happen. Fails if it
# wrote the kernel NOR outside its data sectors.
boot() {
  local cut='' status=0 log before=''
  if [ "$1" = -c ]; then
    cut=$2
    shift 2
  fi
  if [ -f "$cart/kernel-nor.bin" ]; then
    bootloader_fill
    before=$(outside_data)
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
  if [ -n "$before" ] && [ "$(outside_data)" != "$before" ]; then
    fail "power-on $runs wrote the kernel NOR outside 0x780000-0x7BFFFF (0x7C0000 up is the bootloader)"
  fi
}

# write N [CUT]: WRITE TO NOR CLEAN of new_cart's Nth file (1 = the first),
# as a page will (actions.c)
write() {
  # shellcheck disable=SC2046 # nav.sh's options
  boot ${2:+-c "$2"} $(nav_launch "/${files[$1 - 1]}" nor at=130) -f 600
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
if shard_case; then
  new_cart unfixed U.gba B.gba
  write 2
  write 1
  holds 0 U.gba || fail "U.gba is not at 0"
  holds 0x20000 B.gba || fail "B.gba is not at 0x20000 (U.gba wasn't listed)"
  echo "PASS: a ROM without 0x96 at 0xB2 is listed and kept"
fi

# Power cuts in each step of the NOR list update after B's data is written:
# A and B stay, and C goes after them.
for cut in kernel:erase:1@0x7A0000 kernel:program:1@0x7A0000 kernel:program:4240@0x7A0000 \
  kernel:erase:1@0x780000 kernel:program:1@0x780000 kernel:program:4240@0x780000; do
  if shard_case; then
    new_cart "list-cut-${cut//[:@]/-}" A.gba B.gba C.gba
    write 1
    write 2 "$cut"
    write 3
    holds 0 A.gba || fail "A.gba was overwritten"
    holds 0x20000 B.gba || fail "B.gba is not at 0x20000"
    holds 0x40000 C.gba || fail "C.gba is not at 0x40000"
    echo "PASS: a power cut at $cut while saving the NOR list loses no game"
  fi
done

# Delete latest erases the whole game and drops it from the list. A later
# write with the same header, cut after its first block, is not listed.
# (B on the NOR page's question, which keeps the game, comes back with B15.)
if shard_case; then
  echo "SKIP (waiting for B15): B on the NOR page's delete question keeps the game"
  new_cart delete A.gba B.gba B2.gba C.gba
  write 1
  write 2
  # shellcheck disable=SC2046
  boot $(nav_nor_delete at=130) -f 400
  erased 0x20000 0x20000 || fail "delete latest left B.gba's data"
  write 3 game:program:4097
  write 4
  holds 0 A.gba || fail "A.gba was overwritten"
  holds 0x20000 C.gba || fail "C.gba is not at 0x20000: the half-written B2.gba is listed"
  echo "PASS: delete latest erases the game, and a half-written game with its header isn't listed"
fi

# A power cut while a game's data is written: the game isn't listed.
if shard_case; then
  new_cart data-cut A.gba B2.gba C.gba
  write 1
  write 2 game:program:4097
  write 3
  holds 0 A.gba || fail "A.gba was overwritten"
  holds 0x20000 C.gba || fail "C.gba is not at 0x20000: the half-written B2.gba is listed"
  echo "PASS: a game cut off while written isn't listed"
fi

# Both copies of the NOR list lost: the boot rebuilds the list from the games
# in NOR, writes nothing, and the next write goes after them.
if shard_case; then
  new_cart lost-list A.gba B.gba C.gba
  write 1
  write 2
  kernel_wipe 0x7A0000 0x780000
  cp "$cart/kernel-nor.bin" "$cart/wiped.bin"
  boot -f 300
  cmp -s <(tail -c +$((0x780001)) "$cart/kernel-nor.bin" | head -c $((0x40000))) \
    <(tail -c +$((0x780001)) "$cart/wiped.bin" | head -c $((0x40000))) ||
    fail "the boot wrote the NOR list"
  write 3
  holds 0 A.gba || fail "A.gba was overwritten"
  holds 0x20000 B.gba || fail "B.gba was overwritten"
  holds 0x40000 C.gba || fail "C.gba is not at 0x40000"
  echo "PASS: a lost NOR list is rebuilt from the games in NOR, and the boot doesn't write it"
fi

# A list from an older kernel (no copy at 0x780000, no trailer, no
# completion marks in NOR) still counts.
# make_legacy: turn the cart's list into an older kernel's.
make_legacy() {
  python3 - "$cart/kernel-nor.bin" "$cart/game-nor.bin" <<'EOF'
import sys
k = bytearray(open(sys.argv[1], "rb").read())
g = bytearray(open(sys.argv[2], "rb").read())
k[0x7A2100:0x7B0000] = b"\xff" * (0x7B0000 - 0x7A2100)  # the trailer
k[0x780000:0x790000] = b"\xff" * 0x10000                # the second copy
k[0x7A0000 + 0x71] = ord("T")  # stack garbage there can look like Torii's mark flag
g[0x20000 - 0x200:0x20000] = b"\xff" * 0x200            # A's completion mark
open(sys.argv[1], "wb").write(k)
open(sys.argv[2], "wb").write(g)
EOF
}
if shard_case; then
  new_cart legacy A.gba B.gba
  write 1
  make_legacy
  write 2
  holds 0 A.gba || fail "A.gba was overwritten"
  holds 0x20000 B.gba || fail "B.gba is not at 0x20000: the older list was lost"
  echo "PASS: a NOR list from an older kernel is kept"
fi

# The boot saves an older kernel's list in Torii's format once, B first, and
# says so; later boots read it quietly. A power cut in any step of that loses
# no game (one shard case: the power cuts start from its legacy NOR files). (The menu reads the list up to three times a visit; v0.2.1 logged
# "an older kernel's copy A" each time.)
if shard_case; then
  new_cart migrate A.gba B.gba
  write 1
  make_legacy
  cp "$cart/kernel-nor.bin" "$root/legacy-kernel-nor.bin"
  cp "$cart/game-nor.bin" "$root/legacy-game-nor.bin"
  boot -f 300
  grep -q "nor: saving an older kernel's list of 1 games in Torii's format" "$cart/$runs.log" || fail "the list wasn't moved"
  [ "$(grep -c "older kernel's copy A" "$cart/$runs.log")" = 1 ] || fail "the older list was logged more than once"
  magic() { tail -c +$(($1 + 1)) "$cart/kernel-nor.bin" | head -c 4; }
  [ "$(magic 0x782100)" = TNL2 ] || fail "copy B has no trailer"
  [ "$(magic 0x7A2100)" = TNL2 ] || fail "copy A has no trailer"
  boot -f 300
  ! grep -q "older kernel" "$cart/$runs.log" || fail "the next boot read an older kernel's list again"
  [ "$(grep -c "nor: list read from" "$cart/$runs.log")" = 1 ] || fail "the list read was logged more than once"
  write 2
  holds 0x20000 B.gba || fail "B.gba is not at 0x20000 after the move"
  echo "PASS: an older kernel's NOR list is saved in Torii's format once"
  for cut in kernel:erase:1@0x780000 kernel:program:4240@0x780000 kernel:erase:1@0x7A0000 \
    kernel:program:1@0x7A0000 kernel:program:4240@0x7A0000; do
    new_cart "migrate-cut-${cut//[:@]/-}" A.gba B.gba
    cp "$root/legacy-kernel-nor.bin" "$cart/kernel-nor.bin"
    cp "$root/legacy-game-nor.bin" "$cart/game-nor.bin"
    boot -c "$cut" -f 300
    boot -f 300
    write 2
    holds 0 A.gba || fail "A.gba was overwritten"
    holds 0x20000 B.gba || fail "B.gba is not at 0x20000: A.gba was lost from the list"
    echo "PASS: a power cut at $cut while saving an older kernel's list in Torii's format loses no game"
  done
fi

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
if shard_case; then
  new_cart near-full A.gba B2x.gba
  dd if="$root/A.gba" of="$cart/game-nor.bin" conv=notrunc status=none
  head -c $((8 << 20)) /dev/zero | tr '\0' '\377' >"$cart/kernel-nor.bin"
  older_list "$cart/kernel-nor.bin" "$(entry A.gba 0 0x3FC0000 "$root/A.gba")"
  write 2
  holds 0x3FC0000 B2x.gba || fail "B2x.gba wasn't written in the last 256 KB"
  boot -f 200
  grep -q 'nor: 2 games, the next goes at 4000000' "$cart/$runs.log" || fail "B2x.gba isn't listed"
  echo "PASS: a game that fills the game NOR is written without its completion mark"
fi

# A downgrade and back: an older kernel adds C after Torii's A and B, saving
# only copy A. Copy B is whole but older; the next write must go after C.
if shard_case; then
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
fi

# A list whose games fill all 64 MB (here one older kernel's entry): the scan
# used to return without turning interrupts back on, and the menu then hung
# in its first wait for VBlank. DOWN must still move home's cursor.
if shard_case; then
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
  boot -k 200:DOWN -k 204:- -h 190 -h 300 -f 300
  [ "$(grep -c '^hash frame' "$cart/$runs.log")" = 2 ] || fail "no screen hashes"
  [ "$(grep '^hash frame' "$cart/$runs.log" | cut -d: -f2 | sort -u | wc -l)" = 2 ] ||
    fail "DOWN didn't move home's cursor: the menu hung"
  echo "PASS: a NOR list that fills the game NOR leaves interrupts on"
fi

# A game started from NOR goes on Recent, and its entry starts it from NOR
# (as the Recent page will, through actions.c).
if shard_case; then
  new_cart recent A.gba
  write 1
  # shellcheck disable=SC2046
  boot $(nav_nor_launch 0 at=130) -f 400
  grep -q 'mode game from NOR offset 000000$' "$cart/$runs.log" || fail "A.gba didn't start from NOR"
  recent=$(mtype -i "$cart/sd.img" ::/torii/recent.txt 2>/dev/null | grep -v '^[#@]' | cut -f2 || true)
  [ "$recent" = "NOR:A.gba" ] || fail "Recent has '$recent', expected NOR:A.gba"
  # Confirm the save copy, then its Recent entry
  # shellcheck disable=SC2046
  boot -k 100:A -k 104:- $(nav_launch NOR:A.gba at=300) -f 600
  grep -q 'mode game from NOR offset 000000$' "$cart/$runs.log" || fail "its Recent entry didn't start A.gba from NOR"
  echo "PASS: a NOR game goes on Recent and its entry starts it from NOR"
fi
