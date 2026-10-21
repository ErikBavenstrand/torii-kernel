#!/usr/bin/env bash
# The cheat screen (source/showcht.c), opened from the game popup's Boot
# with addon while Cheats is on: a cheat file whose names are in Latin-1 or
# GBK, not UTF-8, must still parse every section and entry, as the kernel
# reads .cht files byte for byte (FatFs's own line reader decodes UTF-8 and
# drops other bytes); a picked cheat is applied at the launch (START), and B
# goes back to the list with none.
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/hello.
set -euo pipefail

# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-cheats${1:+-$1}"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cart=$carts/cheats
shots=dist/shots/cheats
rm -rf "$cart" "$shots"
mkdir -p "$cart" "$shots"
mkfs.fat -C -F 32 -n CHEATS "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
mmd -i "$cart/sd.img" ::/CHEAT
# Sections named in Latin-1 (e acute) and GBK, each with one entry, then ASCII
printf '[Vie infinie \xe9]\r\nON=1234,56;\r\n\r\n[\xbd\xf0\xc7\xae]\r\nON=1235,57;\r\n\r\n[Plain]\r\nON=1236,58;\r\n\r\n[GameInfo]\r\nName=Hello\r\n' \
  >"$cart/Hello.cht"
mcopy -i "$cart/sd.img" "$cart/Hello.cht" ::/CHEAT/Hello.cht

boot() { OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@"; }
# The first boot makes the kernel's NOR; then turn the CHEAT addon on (slot 4)
boot -f 60 >/dev/null 2>&1
printf '\x01\x00' | dd of="$cart/kernel-nor.bin" bs=1 seek=$((0x7B0000 + 2 * 4)) conv=notrunc status=none
# Each run starts from this cart (a launch leaves a save to copy and the
# menu on the GBA page)
mkdir -p "$cart/clean"
cp "$cart/sd.img" "$cart/kernel-nor.bin" "$cart/clean/"
fresh() {
  cp "$cart/clean/sd.img" "$cart/clean/kernel-nor.bin" "$cart/"
  rm -f "$cart/sram.bin"
}

# Hello's popup on the GBA page, Boot with addon (its fourth item), then the
# cheat screen; dump the parsed list, whose names lose their spaces
list=$((16#$(sed -n 's/^\([0-9a-f]*\) [A-Za-z] pReadCache$/\1/p' dist/ezkernelnew.nm) + 0x2000))
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @20:DOWN -k @24:- -k @28:DOWN -k @32:- -k @40:A -k @44:- \
  -w "screen ready: cheats" -s "@4:$shots/cheats.png" -m "@4:$(printf 0x%x "$list"):1024" -f 2000 \
  >"$cart/out.txt" 2>"$cart/err.txt" || fail "the cheat screen didn't open: $(grep '^kernel' "$cart/err.txt" | tail -3)"
sed -n 's/^mem frame [0-9]* [0-9A-F]*: //p' "$cart/out.txt" | python3 -c 'import sys; sys.stdout.buffer.write(bytes.fromhex(sys.stdin.read()))' >"$cart/list.bin"
[ -s "$cart/list.bin" ] || fail "no memory dump: $(tail -3 "$cart/out.txt")"
for name in $'Vieinfinie\xe9' $'\xbd\xf0\xc7\xae' Plain; do
  grep -qaF -- "$name" "$cart/list.bin" || fail "the cheat list lacks section $(printf %q "$name")"
done
[ "$(grep -aoF ON "$cart/list.bin" | wc -l)" -eq 3 ] ||
  fail "the cheat list has $(grep -aoF ON "$cart/list.bin" | wc -l) entries, expected 3"
echo "PASS: cheat files in Latin-1 and GBK parse"

# Picking a cheat applies it: A on the first entry (the cursor starts
# there), then START plays. The launch logs the cheat count; its one write
# is "1234,56"
fresh
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @20:DOWN -k @24:- -k @28:DOWN -k @32:- -k @40:A -k @44:- \
  -w "screen ready: cheats" -k @8:A -k @12:- -k @24:START -k @28:- \
  -u "launch: loading" -f 2000 >"$cart/apply.txt" 2>&1 || true
grep -q "launch: loading.*cheats 1 (1)" "$cart/apply.txt" ||
  fail "a picked cheat isn't applied: $(grep -a "launch:" "$cart/apply.txt" | tail -1)"
echo "PASS: a picked cheat is applied at launch"

# B (released) goes back to the list, with no cheat kept: Play then starts
# Hello with the addons alone
fresh
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @20:DOWN -k @24:- -k @28:DOWN -k @32:- -k @40:A -k @44:- \
  -w "screen ready: cheats" -k @8:A -k @12:- -k @24:B -k @28:- -w "cheats: back" \
  -w "screen ready: games" -k @20:A -k @24:- -u "launch: loading" -f 2000 >"$cart/back.txt" 2>&1 || true
grep -q "launch: loading.*cheats 1 (0)" "$cart/back.txt" ||
  fail "after B, the launch logged: $(grep -a "launch:" "$cart/back.txt" | tail -1)"
echo "PASS: B on the cheat screen goes back to the list, and the next launch has no cheats"
