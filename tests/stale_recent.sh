#!/usr/bin/env bash
# A Recent entry whose file is gone: opening it must show an error, drop the
# entry and leave the rest of the card alone, not make a save and start an
# emulator with no ROM. Entries open through actions.c, from tests/nav.sh's
# hook and from the Recent page (A on its row).
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/hello.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-stale-recent${1:+-$1}"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Recent's paths on the card at $1, newest first, each followed by a space
# (the game lines of /torii/recent.txt). The cases seed the v0.3 list,
# /SAVER/Recently play.txt, which the first write takes over.
recent_list() {
  mtype -i "$1/sd.img" ::/torii/recent.txt 2>/dev/null | grep -v '^[#@]' | cut -f2 | tr '\n' ' '
}

# name: the entry /Games/NAME, opened
run_case() {
  local name=$1
  local cart=$carts/stale-$name shots=dist/shots/stale-$name log
  rm -rf "$cart" "$shots"
  mkdir -p "$cart" "$shots"
  log=$cart/kernel.log
  mkfs.fat -C -F 32 -n STALE "$cart/sd.img" 65536 >/dev/null
  mmd -i "$cart/sd.img" ::/SAVER
  printf '/Games/%s\n/Other.gba\n' "$name" >"$cart/recent.txt"
  mcopy -i "$cart/sd.img" "$cart/recent.txt" "::/SAVER/Recently play.txt"

  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch "/Games/$name" at=130) \
    -s 300:"$shots/error.png" -f 300 >"$log" 2>&1 || fail "$name: the emulator failed: $(tail -3 "$log")"
  grep -qF "launch $name: can't open it" "$log" || fail "$name: no error logged: $(grep '^kernel: ' "$log" | tail -3)"
  if mdir -b -i "$cart/sd.img" ::/SAVER | grep -v 'Recently play.txt' >/dev/null; then
    fail "$name: the kernel wrote to /SAVER: $(mdir -b -i "$cart/sd.img" ::/SAVER | tr '\n' ' ')"
  fi
  # The entry comes off the list; the others stay (v0.3.2: it stayed)
  recent=$(recent_list "$cart")
  [ "$recent" = "/Other.gba " ] || fail "$name: after the error, Recent has '$recent'"
  ! mtype -i "$cart/sd.img" "::/SAVER/Recently play.txt" >/dev/null 2>&1 || fail "$name: the v0.3 list is still there"
  echo "PASS: a missing $name in Recent shows an error and comes off the list"
}

run_case Gone.gb
run_case Gone.gba

# After the error the menu must still open other games: the entry's folder
# exists here, and the kernel used to keep looking in it. A on the Recent
# page's first row opens the gone entry; B on its error goes back to the
# Recent page, now without it, and Hello.gba (the next row) opens from it.
cart=$carts/stale-after
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n STALE "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/SAVER ::/Games
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
printf '/Games/Gone.gba\n/Hello.gba\n' >"$cart/recent.txt"
mcopy -i "$cart/sd.img" "$cart/recent.txt" "::/SAVER/Recently play.txt"
NAV_SHOWN="recent gba"
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_game recent 0) \
  -k @20:A -k @24:- -w "screen ready: dialog" -k @20:B -k @24:- -w "nav: page recent ready" \
  -s "@8:$cart/after.png" -k @20:A -k @24:- -u "launch Hello.gba: code" -f 1200 >"$cart/kernel.log" 2>&1 || true
[ "$(grep -c '^kernel: nav: page recent ready' "$cart/kernel.log")" = 2 ] ||
  fail "after the error, B didn't go back to the Recent page: $(grep '^kernel: nav' "$cart/kernel.log" | tail -3)"
grep -q '^kernel: launch Hello.gba: code' "$cart/kernel.log" ||
  fail "after the error, Hello.gba didn't launch from the Recent page: $(grep '^kernel: ' "$cart/kernel.log" | tail -2)"
recent=$(recent_list "$cart")
[ "$recent" = "/Hello.gba " ] || fail "Recent has '$recent', expected /Hello.gba alone"
echo "PASS: after a missing entry's error, B goes back to the Recent page and other games open from it"

# A deleted file (as the SD card page will delete it, through actions.c)
# leaves Recent; the other entries stay
cart=$carts/stale-delete
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n STALE "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/SAVER
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Other.gba
printf '/Hello.gba\n/Other.gba\nNOR:Hello.gba\n' >"$cart/recent.txt"
mcopy -i "$cart/sd.img" "$cart/recent.txt" "::/SAVER/Recently play.txt"
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_delete /Hello.gba at=130) -f 300 \
  >"$cart/kernel.log" 2>&1
grep -q 'delete: /Hello.gba' "$cart/kernel.log" ||
  fail "Hello.gba wasn't deleted: $(grep '^kernel: ' "$cart/kernel.log" | tail -2)"
recent=$(recent_list "$cart")
[ "$recent" = "/Other.gba NOR:Hello.gba " ] || fail "after the delete, Recent has '$recent'"
echo "PASS: a deleted file leaves Recent"

# A Recent line longer than the kernel writes (an edited file) is skipped:
# its folder would overrun the path buffer. The Recent page's first row is
# then the next entry, which A opens.
cart=$carts/stale-long
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n STALE "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/SAVER
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
{
  printf '/%s/x.gba\n' "$(printf 'a%.0s' $(seq 300))"
  printf '/Hello.gba\n'
} >"$cart/recent.txt"
mcopy -i "$cart/sd.img" "$cart/recent.txt" "::/SAVER/Recently play.txt"
# The menu reads Recent at boot (its count on home) without trouble
NAV_SHOWN="recent gba"
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_game recent 0) \
  -k @20:A -k @24:- -u "launch Hello.gba: code" -f 900 >"$cart/kernel.log" 2>&1 ||
  fail "A on the Recent page didn't start Hello.gba: $(grep '^kernel: ' "$cart/kernel.log" | tail -2)"
! grep -qE 'assert failed|^crash at frame' "$cart/kernel.log" || fail "an over-long Recent line crashed the menu"
echo "PASS: an over-long Recent line is skipped, and the Recent page's first row is the next game"

# A Recent NOR game that is no longer on the game NOR shows an error and
# comes off the list (v0.3.2: it stayed)
cart=$carts/stale-nor
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n STALE "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/SAVER
printf 'NOR:Gone.gba\n/Other.gba\n' >"$cart/recent.txt"
mcopy -i "$cart/sd.img" "$cart/recent.txt" "::/SAVER/Recently play.txt"
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch NOR:Gone.gba at=130) \
  -f 300 >"$cart/kernel.log" 2>&1
grep -q '^kernel: launch Gone.gba: not on the game NOR' "$cart/kernel.log" ||
  fail "the gone NOR game showed no error: $(grep '^kernel: ' "$cart/kernel.log" | tail -2)"
recent=$(recent_list "$cart")
[ "$recent" = "/Other.gba " ] || fail "after the gone NOR game, Recent has '$recent'"
echo "PASS: a Recent NOR game gone from the NOR shows an error and comes off the list"

# Hello.gba written to NOR with WRITE TO NOR ADDON, RESET on and the other
# switches' slots erased, then started from the NOR: its launch logs
# no save states and makes no state file (v0.3.2: "savestates 65535", and
# it made one)
cart=$carts/stale-nordel
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n STALE "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/SAVER
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Other.gba
printf '/Other.gba\n' >"$cart/recent.txt"
mcopy -i "$cart/sd.img" "$cart/recent.txt" "::/SAVER/Recently play.txt"
head -c $((8 << 20)) /dev/zero | tr '\0' '\377' >"$cart/kernel-nor.bin"
python3 tests/setnor.py set "$cart/kernel-nor.bin" 1=1
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Hello.gba nor-addon at=150) -f 600 \
  >"$cart/1-write.log" 2>&1
grep -q '^kernel: nor: writing Hello.gba at 00000000, patched 1' "$cart/1-write.log" ||
  fail "Hello.gba wasn't written to NOR with addons: $(grep '^kernel: ' "$cart/1-write.log" | tail -2)"
# shellcheck disable=SC2046
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_nor_launch 0 at=130) -f 500 >"$cart/2-boot.log" 2>&1
grep -q '^kernel: launch: loading Hello.gba from NOR at .*, patched 1, savestates 0$' "$cart/2-boot.log" ||
  fail "the NOR launch logged: $(grep '^kernel: launch: loading' "$cart/2-boot.log")"
if mdir -b -i "$cart/sd.img" ::/RTS 2>/dev/null | grep . >/dev/null; then
  fail "the NOR launch with SAVESTATE off made a state file"
fi
recent=$(recent_list "$cart")
[ "$recent" = "NOR:Hello.gba /Other.gba " ] || fail "after the NOR launch, Recent has '$recent'"
echo "PASS: a NOR game written with RESET alone starts with no save states"

# For FORMAT ALL below: a copy of the cart, with another NOR game listed in
# recent.txt as edited by hand (no CRC line)
format=$carts/stale-norformat
rm -rf "$format" && cp -r "$cart" "$format"
printf 'NOR:Hello.gba\n/Other.gba\nNOR:Old.gba\n' >"$format/recent.txt"
mcopy -o -i "$format/sd.img" "$format/recent.txt" ::/torii/recent.txt

# NOR DELETE (as the NOR page will, through actions.c): A copies Hello's
# save to SAVER, then the latest NOR game goes. It leaves Recent (v0.3.2: it
# stayed).
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:A -k 104:- $(nav_nor_delete at=300) \
  -f 700 >"$cart/3-delete.log" 2>&1
grep -q 'nor: deleting Hello.gba' "$cart/3-delete.log" ||
  fail "Hello.gba wasn't deleted from NOR: $(grep '^kernel: ' "$cart/3-delete.log" | tail -2)"
recent=$(recent_list "$cart")
[ "$recent" = "/Other.gba " ] || fail "after NOR DELETE, Recent has '$recent'"
echo "PASS: a game deleted from NOR leaves Recent"

# FORMAT ALL: every NOR game leaves Recent (v0.3.2: they stayed)
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_DIR=$format emu/build-linux/mgba-shot dist/ezkernelnew.bin -k 100:A -k 104:- $(nav_nor_format at=300) \
  -w 'recent: forgot NOR:Old.gba' -h @60 >"$format/3-format.log" 2>&1 ||
  fail "FORMAT ALL: Old.gba never left Recent: $(grep '^kernel: ' "$format/3-format.log" | tail -2)"
grep -q '^kernel: nor: erased the game NOR' "$format/3-format.log" ||
  fail "FORMAT ALL didn't run: $(grep '^kernel: ' "$format/3-format.log" | tail -2)"
recent=$(recent_list "$format")
[ "$recent" = "/Other.gba " ] || fail "after FORMAT ALL, Recent has '$recent'"
echo "PASS: FORMAT ALL takes every NOR game off Recent"

# A path with "." and ".." parts starts its game and goes on Recent as the
# plain path (v0.4.1 stored it as given)
cart=$carts/stale-canonical
rm -rf "$cart" && mkdir -p "$cart"
mkfs.fat -C -F 32 -n STALE "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/SAVER ::/deep
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
# shellcheck disable=SC2046 # nav.sh's options
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /deep/./../Hello.gba at=130) -f 600 \
  >"$cart/launch.log" 2>&1 || fail "the emulator failed: $(tail -3 "$cart/launch.log")"
grep -q 'launch: loading' "$cart/launch.log" ||
  fail "/deep/./../Hello.gba didn't start: $(grep '^kernel' "$cart/launch.log" | tail -3)"
recent=$(recent_list "$cart")
[ "$recent" = "/Hello.gba " ] || fail "after /deep/./../Hello.gba, Recent has '$recent'"
echo "PASS: a path with . and .. parts starts its game and goes on Recent plain"
