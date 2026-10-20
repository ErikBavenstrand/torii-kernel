#!/usr/bin/env bash
# Game NOR test on the emulated cart: write two test ROMs to the game NOR
# through WRITE TO NOR, power-cycle, and boot each from the NOR list. The NOR
# starts full of random bytes, so writes only work if erasing does, and the
# second game is padded past the first 8 MB ROM page of the 0x09000000
# window. Both games must run and keep their saves. Runs inside the torii-emu
# container after `just build`, `just emu-headless` and the test ROMs.
set -euo pipefail

shots=dist/shots/nor
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts dist/cart-nor
cart=$carts
hello=tests/roms/hello/hello.gba
world=$cart/World.gba
rm -rf "$shots"
mkdir -p "$shots"
# flash128 padded to 8.25 MB with words that encode their offset, so it
# lands at 0x20000 and ends past 0x800000 with no two blocks alike.
python3 - tests/roms/savetest/flash128.gba "$world" <<'EOF'
import struct, sys
rom = bytearray(open(sys.argv[1], "rb").read())
rom += bytes(-len(rom) % 4)
rom += b"".join(struct.pack("<I", o ^ 0x5A5A5A5A) for o in range(len(rom), 0x840000, 4))
open(sys.argv[2], "wb").write(rom)
EOF
head -c $((64 << 20)) /dev/urandom >"$cart/game-nor.bin"
mkfs.fat -C -F 32 -n NOR "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" "$hello" ::/Hello.gba
mcopy -i "$cart/sd.img" "$world" ::/World.gba

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# boot NAME OPTIONS...: run with the cart trace, keep the log as $cart/NAME.log
# and fail on a crash or a failed kernel assert.
boot() {
  local name=$1
  shift
  OMEGA_TRACE=1 OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" >"$cart/$name.log" 2>&1 ||
    fail "$name: mgba-shot exited with $?"
  if grep -E 'assert failed|^crash at frame' "$cart/$name.log" >&2; then
    fail "$name: the kernel crashed or failed an assert"
  fi
}

# keys FRAME KEY... -> mgba-shot press/release pairs, one key every 8 frames.
# A key _ is a pause: A on a menu or question still opening does nothing,
# so an A right after the key that opened one waits a slot.
keys() {
  local frame=$1
  shift
  for key in "$@"; do
    [ "$key" = _ ] || printf -- '-k %d:%s -k %d:- ' "$frame" "$key" $((frame + 4))
    frame=$((frame + 8))
  done
}

# nor_holds OFFSET ROM: the game NOR holds ROM at OFFSET
nor_holds() {
  cmp -s -n "$(stat -c %s "$2")" <(tail -c +$(($1 + 1)) "$cart/game-nor.bin") "$2"
}

# rom_seen LOG ROM OFFSET...: the -m dumps of 0x08000000+OFFSET in LOG show ROM
rom_seen() {
  local log=$1 rom=$2 offset want got
  shift 2
  for offset in "$@"; do
    want=$(od -A n -t x1 -j $((offset)) -N 16 "$rom" | tr -d ' \n')
    got=$(grep "^mem frame [0-9]* $(printf %08X $((0x08000000 + offset))):" "$log" | cut -d: -f2 | tr -d ' \n' | tr A-F a-f)
    [ "$want" = "$got" ] || return 1
  done
}

hex_at() { # file offset -> 5 bytes as hex
  od -A n -t x1 -j $(($2)) -N 5 "$1" | tr -d ' \n'
}

# Power-on 1: WRITE TO NOR CLEAN of Hello.gba, as a page will (actions.c).
# shellcheck disable=SC2046 # nav.sh's options
boot 1-write $(nav_launch /Hello.gba nor at=130) -s 400:$shots/1-nor-list.png -f 400
nor_holds 0 "$hello" || fail "Hello.gba was not written to NOR at 0"

# Power-on 2: boot Hello, the NOR's first game.
# shellcheck disable=SC2046
boot 2-hello $(nav_nor_launch 0 at=130) -s 500:$shots/2-hello.png -f 500
grep -q 'mode game from NOR offset 000000$' "$cart/2-hello.log" || fail "Hello did not boot from NOR at 0"
[ "$(hex_at "$cart/sram.bin" 0)" = 4f4d525801 ] || fail "Hello did not run from NOR (SRAM $(hex_at "$cart/sram.bin" 0))"

# Power-on 3: confirm the save copy to SAVER/Hello.sav, then write World.gba
# to NOR after Hello with SAVE TYPE FLASH 128K.
# shellcheck disable=SC2046
boot 3-write -k 100:A -k 104:- $(nav_launch /World.gba nor save=5 at=400) -s 3800:$shots/3-nor-list.png -f 3800
[ "$(mtype -i "$cart/sd.img" ::/SAVER/Hello.sav | head -c 5 | od -A n -t x1 | tr -d ' \n')" = 4f4d525801 ] ||
  fail "Hello's NOR save was not written back to SD"
nor_holds 0x20000 "$world" || fail "World.gba was not written to NOR at 0x20000"
nor_holds 0 "$hello" || fail "writing World.gba changed Hello.gba"

# Power-on 4: World from NOR, reading its ROM on both sides of the 8 MB page.
# shellcheck disable=SC2046
boot 4-world $(nav_nor_launch 1 at=130) -m 500:0x080000A0:16 -m 500:0x087DFFF0:16 -m 500:0x087E0000:16 \
  -m 500:0x0883FFF0:16 -s 500:$shots/4-world.png -f 500
grep -q 'mode game from NOR offset 020000$' "$cart/4-world.log" || fail "World did not boot from NOR at 0x20000"
rom_seen "$cart/4-world.log" "$world" 0xA0 0x7DFFF0 0x7E0000 0x83FFF0 || fail "World's ROM reads wrong from NOR"

# Power-on 5: confirm World's save copy; Hello from NOR again gets its save
# back and adds a boot. World's flash save (128 KB, copied by about frame
# 260) holds its first boot at 0x10000.
# shellcheck disable=SC2046
boot 5-hello -k 100:A -k 104:- $(nav_nor_launch 0 at=430) -s 800:$shots/5-hello.png -f 800
[ "$(mtype -i "$cart/sd.img" ::/SAVER/World.sav | od -A n -t x1 -j $((0x10000)) -N 5 | tr -d ' \n')" = 4f4d525801 ] ||
  fail "World did not run from NOR (no first boot in SAVER/World.sav)"
grep -q 'mode game from NOR offset 000000$' "$cart/5-hello.log" || fail "Hello did not boot from NOR again"
[ "$(hex_at "$cart/sram.bin" 0)" = 4f4d525802 ] || fail "Hello's save did not come back on a NOR boot (SRAM $(hex_at "$cart/sram.bin" 0))"

echo "PASS: two games written over a used game NOR boot from it after power cycles, and keep their saves"
