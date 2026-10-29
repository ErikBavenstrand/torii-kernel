#!/usr/bin/env bash
# Box art in the game lists' list with art (source/boxart.c): matched art
# by CRC, never by GBA game code (a romhack's), a cover cached across visits, a broken art file, a card
# without /torii/art, the full art pack (art.pak: its covers, an art file
# before it, a damaged one), a custom-art game renamed on the SD card page,
# and the Box art setting off. (tests/shots/games-art*
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

# id KIND KEY: box art's id for a file (artkey.h: 1 custom, 2 CRC, 3 a pack's image)
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
  python3 tests/setnor.py set "$cart/kernel-nor.bin" "$1=$2"
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

# The full art pack (/torii/art.pak, artpack.h): Zed.gba has a CRC record
# but no /torii/art at all, and gets its cover from the pack, read at its
# image's offset; the pack's other image comes first, so Zed's isn't image 0
new_cart pack
printf '%s\t-\t3248\t-\t/Zed.gba\n' "$crc" >"$cart/crc.txt"
mcopy -i "$cart/sd.img" "$cart/crc.txt" ::/torii/crc.txt
python3 tools/artpack.py write "$cart/art.pak" "00000001=tests/shots/data/art-night.bin" \
  "$crc=tests/shots/data/art-sunset.bin" "FFFFFFFF=tests/shots/data/art-night.bin"
image=$(python3 tools/artpack.py find "$cart/art.pak" "$crc")
[ "$image" = 1 ] || fail "the test pack's layout: Zed's image is $image"
pack_id=$(id 3 "$image")
mcopy -i "$cart/sd.img" "$cart/art.pak" ::/torii/art.pak
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/6.log" $(nav_game gba 1) -w "box art: cover $pack_id shown" -f 3000
grep -q "box art: /torii/art.pak: 3 CRCs, 2 covers" "$cart/6.log" ||
  fail "the pack's index wasn't read: $(grep 'box art' "$cart/6.log" | head -3)"
grep -q "^kernel: box art: cover $pack_id shown" "$cart/6.log" ||
  fail "Zed.gba's cover didn't come from the pack: $(grep 'box art\|cover' "$cart/6.log" | tail -4)"
# Reading the index and a cover at its offset keeps to the frame budget
# the shots record for the listing and the loader (tests/perf/budget.txt)
python3 - "$cart/6.log" <<'EOF' || fail "the pack's frames are over budget"
import sys
sys.path.insert(0, "tests")
import perf
found = perf.peaks(perf.lines(open(sys.argv[1], encoding="utf-8", errors="replace").read()))
budget = perf.read_budget(perf.BUDGET)
for name in ("games-artlist", "cover-load"):
    n = found.get(name, 0)
    print(f"  {name}: {n} cycles with the pack (budget {budget[name]})")
    if n > 280896 or n > budget[name] * 1.1:
        sys.exit(1)
EOF
# With a card's latency (OMEGA_SD_CYCLES: 40,000 cycles a command, 8,000 a
# sector) a larger index (2,000 CRCs, 16 KB) is read a card command a step,
# each early in a frame, so no listing frame goes over (v0.8's cart had one
# of 880,000 cycles: 4 KB at the header's odd offset, three commands). On a
# card of 4 KB clusters, as real ones have (FatFs reads a cluster at most a
# command; the test cards' are 512 bytes)
slow=$carts/pack-slow
rm -rf "$slow" && mkdir -p "$slow"
cp "$cart/kernel-nor.bin" "$slow/"
mkfs.fat -C -F 32 -s 8 -n BOXART "$slow/sd.img" 307200 >/dev/null
mmd -i "$slow/sd.img" ::/torii
mcopy -i "$slow/sd.img" tests/shots/data/art-night.bin ::/Code.gba
mcopy -i "$slow/sd.img" tests/shots/data/art-sunset.bin ::/Zed.gba
mcopy -i "$slow/sd.img" "$cart/crc.txt" ::/torii/crc.txt
# shellcheck disable=SC2046 # the CRC=file pairs
python3 tools/artpack.py write "$slow/art.pak" $(for i in $(seq 1 1999); do printf '%08X=tests/shots/data/art-night.bin ' "$i"; done) \
  "$crc=tests/shots/data/art-sunset.bin"
slow_id=$(id 3 "$(python3 tools/artpack.py find "$slow/art.pak" "$crc")")
mcopy -i "$slow/sd.img" "$slow/art.pak" ::/torii/art.pak
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_SD_CYCLES=40000:8000 OMEGA_DIR=$slow emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_game gba 1) \
  -w "box art: cover $slow_id shown" -f 4000 >"$slow/run.log" 2>&1 || true
grep -q "^kernel: box art: cover $slow_id shown" "$slow/run.log" || fail "no cover from the pack with a slow card"
! grep -q "listing step" "$slow/run.log" || fail "a slow listing step: $(grep 'listing step' "$slow/run.log" | head -2)"
worst=$(sed -n 's/^kernel: frame-cycles: games-artlist \([0-9]*\)$/\1/p' "$slow/run.log" | sort -n | tail -1)
[ "${worst:-0}" -le 280896 ] || fail "a listing frame of $worst cycles with a slow card"
echo "PASS: a game with no art file gets its cover from the art pack, and a slow card's listing keeps to the frame"

# A pack in pieces on the card: its covers load through the link map built
# from its clusters (a few pieces), or, past what the map holds, by
# FatFs's own seek. holes N BYTES: N one-cluster gaps, and the card filled
# but for room for the rest of a file of BYTES (mtools then has to put it
# in the gaps; it otherwise writes past its last file)
holes() {
  local i free
  for i in $(seq 0 $((2 * $1 - 1))); do
    head -c 512 /dev/zero >"$cart/f$i"
    mcopy -i "$cart/sd.img" "$cart/f$i" "::/torii/f$i"
  done
  for i in $(seq 0 2 $((2 * $1 - 2))); do mdel -i "$cart/sd.img" "::/torii/f$i"; done
  free=$(mdir -i "$cart/sd.img" ::/ | sed -n 's/^ *\([0-9 ]*\) bytes free.*/\1/p' | tr -d ' ')
  head -c $((free - ($2 + 511) / 512 * 512)) /dev/zero >"$cart/filler"
  mcopy -i "$cart/sd.img" "$cart/filler" ::/filler
}
# A pack of 100 covers (635 clusters of 512 bytes), Zed.gba's last
python3 - "$carts/pack" "$crc" <<'EOF'
import sys
out, zed = sys.argv[1], sys.argv[2]
night = open("tests/shots/data/art-night.bin", "rb").read()
args = []
for i in range(99):
    v = bytearray(night)
    v[16] ^= i + 1  # another colour 0: another cover
    open(f"{out}/v{i}.bin", "wb").write(v)
    args.append(f"{i + 1:08X}={out}/v{i}.bin")
open(f"{out}/args", "w").write(" ".join(args + [f"{zed}=tests/shots/data/art-sunset.bin"]))
EOF
# shellcheck disable=SC2046 # the CRC=file pairs
python3 tools/artpack.py write "$carts/pack/big.pak" $(cat "$carts/pack/args")
big_id=$(id 3 "$(python3 tools/artpack.py find "$carts/pack/big.pak" "$crc")")
for pieces in 4 300; do
  new_cart "pack-$pieces"
  mcopy -i "$cart/sd.img" "$carts/pack/crc.txt" ::/torii/crc.txt
  holes $((pieces - 1)) "$(stat -c %s "$carts/pack/big.pak")"
  mcopy -i "$cart/sd.img" "$carts/pack/big.pak" ::/torii/art.pak
  # shellcheck disable=SC2046 # nav.sh's options
  boot "$cart/p.log" $(nav_game gba 1) -w "box art: cover $big_id shown" -f 3000
  grep -q "^kernel: box art: cover $big_id shown" "$cart/p.log" ||
    fail "a pack in $pieces pieces showed no cover: $(grep 'box art\|cover' "$cart/p.log" | tail -4)"
  if [ "$pieces" = 4 ]; then
    grep -q "covers, 4 pieces on the card" "$cart/p.log" || fail "the link map: $(grep 'art.pak' "$cart/p.log")"
  else
    grep -q "art.pak is in more than 254 pieces" "$cart/p.log" || fail "no slow seek logged: $(grep 'art.pak' "$cart/p.log")"
  fi
done
echo "PASS: a pack in pieces loads its covers, through the link map or without it"

# art/<CRC32>.bin goes before the pack: the same game shows the file's cover
cart=$carts/pack
mmd -i "$cart/sd.img" ::/torii/art
mcopy -i "$cart/sd.img" tests/shots/data/art-night.bin "::/torii/art/$crc.bin"
file_id=$(id 2 "0x$crc")
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/7.log" $(nav_game gba 1) -w "box art: cover $file_id shown" -f 3000
grep -q "^kernel: box art: cover $file_id shown" "$cart/7.log" || fail "the art file didn't win over the pack"
! grep -q "cover $pack_id" "$cart/7.log" || fail "the pack's cover loaded though an art file is there"
echo "PASS: an art file by CRC goes before the pack"

# A damaged pack (cut short) and one of another version are no pack: logged
# once, and the box keeps the placeholder
new_cart pack-bad
printf '%s\t-\t3248\t-\t/Zed.gba\n' "$crc" >"$cart/crc.txt"
mcopy -i "$cart/sd.img" "$cart/crc.txt" ::/torii/crc.txt
python3 tools/artpack.py write "$cart/art.pak" "$crc=tests/shots/data/art-sunset.bin"
head -c -1 "$cart/art.pak" >"$cart/short.pak"
mcopy -i "$cart/sd.img" "$cart/short.pak" ::/torii/art.pak
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/8.log" $(nav_game gba 1) -k @90:UP -k @94:- -k @130:DOWN -k @134:- -f 900
[ "$(grep -c "kernel error: box art: /torii/art.pak is damaged" "$cart/8.log")" = 1 ] ||
  fail "the damaged pack wasn't logged once: $(grep 'art.pak' "$cart/8.log" | head -3)"
! grep -qE "box art: cover [0-9A-F]{8} shown" "$cart/8.log" || fail "a damaged pack showed a cover"
python3 -c "import sys; d = bytearray(open(sys.argv[1], 'rb').read()); d[4] = 99; open(sys.argv[1], 'wb').write(d)" "$cart/art.pak"
mcopy -o -i "$cart/sd.img" "$cart/art.pak" ::/torii/art.pak
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/9.log" $(nav_game gba 1) -f 900
grep -q "kernel error: box art: /torii/art.pak is another version" "$cart/9.log" ||
  fail "another version's pack wasn't logged: $(grep 'art.pak' "$cart/9.log" | head -3)"
! grep -qE "box art: cover [0-9A-F]{8} shown" "$cart/9.log" || fail "another version's pack showed a cover"
echo "PASS: a damaged pack, or another version's, is no pack, and logged"

# A custom-art game renamed on the SD card page (actions.c), after the game
# page showed its cover: its art file moves with it, the card's art is
# listed again (boxart_forget), and the game page shows the cover by its new
# name in the same session
new_cart rename
mmd -i "$cart/sd.img" ::/torii/art
mmd -i "$cart/sd.img" ::/torii/art/custom
mcopy -i "$cart/sd.img" tests/shots/data/art-sunset.bin ::/torii/art/custom/Zed.gba.bin
custom_id() {
  id 1 "$(python3 -c 'import sys
h = 2166136261
for c in sys.argv[1].lower().encode():
    h = ((h ^ c) * 16777619) & 0xFFFFFFFF
print(h)' "$1")"
}
old=$(custom_id Zed.gba) renamed=$(custom_id Zedq.gba)
{
  cat "$(nav_game gba 1 | sed 's/^-i //')"
  cat <<EOF
-w "box art: cover $old shown"
-k @8:B -k @12:-
-w "nav: home ready"
-k @8:LEFT -k @12:- -k @16:LEFT -k @20:- -k @24:LEFT -k @28:- -k @32:LEFT -k @36:- -k @40:A -k @44:-
-w "sdcard: ready /"
-k @8:DOWN -k @12:- -k @16:START -k @20:-
-w "sdcard: popup /Zed.gba"
-k @12:UP -k @16:- -k @20:UP -k @24:- -k @28:UP -k @32:- -k @40:A -k @44:-
-w "screen ready: rename"
-k @10:A -k @14:- -k @24:START -k @28:-
-w "rename: kept"
-k @20:B -k @24:-
-w "nav: home ready"
-k @8:RIGHT -k @12:- -k @16:RIGHT -k @20:- -k @24:RIGHT -k @28:- -k @32:RIGHT -k @36:- -k @40:A -k @44:-
-w "nav: page gba ready"
-k @8:DOWN -k @12:-
-u "box art: cover $renamed shown"
EOF
} >"$cart/route.txt"
boot "$cart/10.log" -i "$cart/route.txt" -f 4000
grep -q "rename: /Zed.gba to Zedq.gba" "$cart/10.log" || fail "the rename didn't run: $(grep 'rename\|nav:' "$cart/10.log" | tail -3)"
mtype -i "$cart/sd.img" ::/torii/art/custom/Zedq.gba.bin >/dev/null 2>&1 || fail "the custom art didn't move with the game"
[ "$(grep -c "box art: [0-9]* files listed" "$cart/10.log")" = 2 ] ||
  fail "the art wasn't listed again after the rename: $(grep 'files listed' "$cart/10.log")"
grep -q "box art: cover $renamed shown" "$cart/10.log" ||
  fail "the renamed game's cover didn't show: $(grep 'box art\|nav: page' "$cart/10.log" | tail -4)"
echo "PASS: a custom-art game's SD rename moves its art, lists the art again, and shows it by the new name"

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

# The game popup's box art row (List + art): Hide box art writes an empty
# custom art file, after which the game keeps the placeholder though its
# matched art is there; the row then says Show box art, which removes the
# file, and the art shows again. With art of its own the row says Remove
# own art, which removes that file.
new_cart hide
printf '%s\tAWRE\t3248\t-\t/Code.gba\n' "$code_crc" >"$cart/crc.txt"
mcopy -i "$cart/sd.img" "$cart/crc.txt" ::/torii/crc.txt
mmd -i "$cart/sd.img" ::/torii/art
mcopy -i "$cart/sd.img" tests/shots/data/art-night.bin "::/torii/art/$code_crc.bin"
art_row() { # log: Code.gba's popup, UP to the box art row (Delete, Info, Rename, then it), A
  # shellcheck disable=SC2046 # nav.sh's options
  boot "$1" $(nav_game_menu gba 0) -k @12:UP -k @16:- -k @24:UP -k @28:- -k @36:UP -k @40:- -k @48:UP -k @52:- \
    -k @64:A -k @68:- -w "box art: /Code.gba:" -f 3000
}
art_row "$cart/h1.log"
grep -q "^kernel: box art: /Code.gba: hidden$" "$cart/h1.log" || fail "Hide box art: $(grep 'box art' "$cart/h1.log" | tail -2)"
[ "$(mtype -i "$cart/sd.img" ::/torii/art/custom/Code.gba.bin | wc -c)" = 0 ] || fail "no empty custom art file"
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/h2.log" $(nav_to gba) -f 1500
grep -q "box art: 2 files listed" "$cart/h2.log" || fail "the art wasn't listed: $(grep "files listed" "$cart/h2.log")"
! grep -q "box art: cover $code shown" "$cart/h2.log" || fail "the hidden game's art still shows"
art_row "$cart/h3.log"
grep -q "^kernel: box art: /Code.gba: own art or hiding removed$" "$cart/h3.log" || fail "Show box art: $(grep 'box art' "$cart/h3.log" | tail -2)"
! mdir -i "$cart/sd.img" ::/torii/art/custom/Code.gba.bin >/dev/null 2>&1 || fail "the empty file stayed"
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart/h4.log" $(nav_to gba) -w "box art: cover $code shown" -f 3000
grep -q "box art: cover $code shown" "$cart/h4.log" || fail "the art didn't show again"
mcopy -i "$cart/sd.img" tests/shots/data/art-sunset.bin ::/torii/art/custom/Code.gba.bin
art_row "$cart/h5.log"
grep -q "^kernel: box art: /Code.gba: own art or hiding removed$" "$cart/h5.log" || fail "Remove own art: $(grep 'box art' "$cart/h5.log" | tail -2)"
! mdir -i "$cart/sd.img" ::/torii/art/custom/Code.gba.bin >/dev/null 2>&1 || fail "the own art stayed"
echo "PASS: the popup hides a game's box art, shows it again, and removes art of its own"
