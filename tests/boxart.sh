#!/usr/bin/env bash
# Box art in the game lists' list with art (source/boxart.c): matched art
# by CRC, never by GBA game code (a romhack's), a cover cached across visits, a broken art file, a card
# without /torii/art, and the Box art setting off. (tests/shots/games-art*
# show custom and CRC art.) Runs inside the torii-emu container after `just
# build` and `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-boxart${1:+-$1}"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# id KIND KEY: box art's id for a file (artkey.h: 1 custom, 2 CRC)
id() {
  python3 - "$1" "$2" <<'EOF'
import sys
kind, key = int(sys.argv[1]), int(sys.argv[2], 0)
h = (key ^ (kind * 0x9E3779B9)) & 0xFFFFFFFF
h ^= h >> 16
h = (h * 0x85EBCA6B) & 0xFFFFFFFF
h ^= h >> 13
h = (h * 0xC2B2AE35) & 0xFFFFFFFF
h ^= h >> 16
print(f"{h or 1:08X}")
EOF
}

# new_cart NAME: a card with two GBA games, Code.gba and Zed.gba, in list with art
new_cart() {
  cart=$carts/$1
  rm -rf "$cart" && mkdir -p "$cart"
  mkfs.fat -C -F 32 -n BOXART "$cart/sd.img" 65536 >/dev/null
  mmd -i "$cart/sd.img" ::/torii
  mcopy -i "$cart/sd.img" tests/shots/data/art-night.bin ::/Code.gba
  mcopy -i "$cart/sd.img" tests/shots/data/art-sunset.bin ::/Zed.gba
  # A first power-on makes the cart's kernel NOR, then settings slot 31
  # (game view) 1: list with art
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -f 1 >/dev/null 2>&1 || true
  set_slot 31 1
}
set_slot() {
  printf %b "\\x$(printf %02x $(($2 & 0xFF)))\\x$(printf %02x $(($2 >> 8)))" |
    dd of="$cart/kernel-nor.bin" bs=1 seek=$((0x7B0000 + 2 * $1)) conv=notrunc status=none
}
boot() { # log [mgba-shot options]
  local log=$1
  shift
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" >"$log" 2>&1 || true
  ! grep -qE 'assert failed|^crash at frame' "$log" || fail "a crash or failed assert: $(grep -E 'assert|crash' "$log" | head -2)"
}

crc_of() { python3 -c "import sys, zlib; print(f'{zlib.crc32(open(sys.argv[1], \"rb\").read()):08X}')" "$1"; }
crc=$(crc_of tests/shots/data/art-sunset.bin)     # Zed.gba's
code_crc=$(crc_of tests/shots/data/art-night.bin) # Code.gba's
code=$(id 2 "0x$code_crc")

# Matched art by CRC: it shows; leaving the page and opening it again shows
# it from the cache, not the card
new_cart crc
printf '%s\tAWRE\t3248\t-\t/Code.gba\n' "$code_crc" >"$cart/crc.txt"
mcopy -i "$cart/sd.img" "$cart/crc.txt" ::/torii/crc.txt
mmd -i "$cart/sd.img" ::/torii/art
mcopy -i "$cart/sd.img" tests/shots/data/art-night.bin "::/torii/art/$code_crc.bin"
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/1.log" $(nav_to gba) -w "box art: cover $code shown" -k @8:B -k @12:- \
  -w "nav: home ready" -k @8:A -k @12:- -w "nav: page gba ready" -f 1200
[ "$(grep -c "^kernel: box art: cover $code shown" "$cart/1.log")" = 2 ] ||
  fail "the CRC's art didn't show on both visits: $(grep 'box art\|cover' "$cart/1.log" | tail -4)"
[ "$(grep -c "^kernel: cover $code loaded" "$cart/1.log")" = 1 ] ||
  fail "the second visit read the card again: $(grep -c "cover $code loaded" "$cart/1.log") loads"
grep -q "box art: 1 files listed" "$cart/1.log" || fail "the art wasn't listed once: $(grep 'files listed' "$cart/1.log")"
echo "PASS: matched art by CRC shows, and again from the cache on the next visit"

# A romhack: Code.gba has a listed game's code (AWRE) but a CRC the dats
# don't list. Art by that code (<CODE>.bin, which v0.4.1 read) isn't
# read, so it keeps the placeholder, while Zed.gba's art by CRC shows
new_cart romhack
printf '%s\tAWRE\t3248\t-\t/Code.gba\n%s\t-\t3248\t-\t/Zed.gba\n' "$code_crc" "$crc" >"$cart/crc.txt"
mcopy -i "$cart/sd.img" "$cart/crc.txt" ::/torii/crc.txt
mmd -i "$cart/sd.img" ::/torii/art
mcopy -i "$cart/sd.img" tests/shots/data/art-night.bin ::/torii/art/AWRE.bin
mcopy -i "$cart/sd.img" tests/shots/data/art-sunset.bin "::/torii/art/$crc.bin"
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/5.log" $(nav_game gba 1) -w "box art: cover $(id 2 "0x$crc") shown" -k @4:UP -k @8:- -h @120 -f 3000
grep -q "box art: 1 files listed" "$cart/5.log" || fail "AWRE.bin was listed: $(grep 'files listed' "$cart/5.log")"
[ "$(grep -cE "^kernel: box art: cover [0-9A-F]{8} shown" "$cart/5.log")" = 1 ] ||
  fail "the romhack showed art: $(grep 'box art' "$cart/5.log" | tail -4)"
echo "PASS: a romhack keeps the placeholder: no art by its game code"

# A broken art file (no cover's size) by CRC: the placeholder stays, the
# error is logged, and the next game's cover still loads (both lines, in
# either order: the run goes on past the first)
new_cart broken
printf '%s\t-\t3248\t-\t/Zed.gba\n' "$crc" >"$cart/crc.txt"
mcopy -i "$cart/sd.img" "$cart/crc.txt" ::/torii/crc.txt
mmd -i "$cart/sd.img" ::/torii/art
head -c 1000 tests/shots/data/art-sunset.bin >"$cart/short.bin"
mcopy -i "$cart/sd.img" "$cart/short.bin" "::/torii/art/$crc.bin"
mmd -i "$cart/sd.img" ::/torii/art/custom
mcopy -i "$cart/sd.img" tests/shots/data/art-night.bin ::/torii/art/custom/Code.gba.bin
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/2.log" $(nav_game gba 1) -k @90:UP -k @94:- -f 900
grep -q "kernel error: engine: cover $(id 2 "0x$crc"): the file is shorter than its header says" "$cart/2.log" ||
  fail "the broken file's error wasn't logged: $(grep 'error' "$cart/2.log" | head -3)"
! grep -q "box art: cover $(id 2 "0x$crc") shown" "$cart/2.log" || fail "the broken file showed"
grep -qE "box art: cover [0-9A-F]{8} shown" "$cart/2.log" || fail "Code.gba's custom art didn't show after the broken one"
echo "PASS: a broken art file leaves the placeholder and logs why; other covers still load"

# A card without /torii/art lists nothing and loads nothing
new_cart none
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/3.log" $(nav_to gba) -u "box art: 0 files listed" -f 3000 -k 2900:DOWN
grep -q "box art: 0 files listed" "$cart/3.log" || fail "no listing ended: $(grep 'box art' "$cart/3.log")"
! grep -q "cover-load\|loaded$" "$cart/3.log" || fail "a cover loaded on a card without art"
echo "PASS: a card without /torii/art reads no art"

# Box art off (slot 34 0): no listing, no covers, though the art is there
new_cart off
set_slot 34 0
mmd -i "$cart/sd.img" ::/torii/art
mmd -i "$cart/sd.img" ::/torii/art/custom
mcopy -i "$cart/sd.img" tests/shots/data/art-night.bin ::/torii/art/custom/Code.gba.bin
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/4.log" $(nav_to gba) -f 900
grep -q "nav: page gba ready" "$cart/4.log" || fail "the GBA page didn't open"
! grep -q "box art:" "$cart/4.log" || fail "box art ran while off: $(grep 'box art' "$cart/4.log" | head -2)"
echo "PASS: with Box art off the card's art isn't read"
