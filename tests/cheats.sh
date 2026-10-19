#!/usr/bin/env bash
# A cheat file whose names are in Latin-1 or GBK, not UTF-8: every section and
# entry must still parse, as the kernel reads .cht files byte for byte (FatFs's
# own line reader decodes UTF-8 and drops other bytes). Opens the cheat screen
# and reads the parsed list from memory.
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/hello.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
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

# Down to the game (CHEAT sorts first), open it, then CHEAT, the sixth row;
# dump the parsed list, whose names lose their spaces
list=$((16#$(sed -n 's/^\([0-9a-f]*\) [A-Za-z] pReadCache$/\1/p' dist/ezkernelnew.nm) + 0x2000))
boot -k 100:DOWN -k 104:- -k 116:A -k 120:- -k 150:DOWN -k 154:- -k 162:DOWN -k 166:- -k 174:DOWN -k 178:- -k 186:DOWN -k 190:- \
  -k 198:DOWN -k 202:- -k 214:A -k 218:- -s 300:"$shots/cheats.png" -m "300:$(printf 0x%x $list):1024" -f 300 \
  >"$cart/out.txt" 2>&1
sed -n 's/^mem frame 300 [0-9A-F]*: //p' "$cart/out.txt" | python3 -c 'import sys; sys.stdout.buffer.write(bytes.fromhex(sys.stdin.read()))' >"$cart/list.bin"
[ -s "$cart/list.bin" ] || fail "no memory dump: $(tail -3 "$cart/out.txt")"
for name in $'Vieinfinie\xe9' $'\xbd\xf0\xc7\xae' Plain; do
  grep -qaF -- "$name" "$cart/list.bin" || fail "the cheat list lacks section $(printf %q "$name")"
done
[ "$(grep -aoF ON "$cart/list.bin" | wc -l)" -eq 3 ] ||
  fail "the cheat list has $(grep -aoF ON "$cart/list.bin" | wc -l) entries, expected 3"
echo "PASS: cheat files in Latin-1 and GBK parse"

# Picking a cheat applies it: A on the first entry (the cursor starts there),
# release B to leave, then BOOT WITH ADDON (the row the menu returns to). The
# launch logs the cheat count; its one write is "1234,56"
boot -k 100:DOWN -k 104:- -k 116:A -k 120:- -k 150:DOWN -k 154:- -k 162:DOWN -k 166:- -k 174:DOWN -k 178:- -k 186:DOWN -k 190:- \
  -k 198:DOWN -k 202:- -k 214:A -k 218:- -k 250:A -k 254:- -k 270:B -k 274:- -k 310:A -k 314:- \
  -u "launch: loading" -f 600 >"$cart/apply.txt" 2>&1 || true
grep -q "launch: loading.*cheats 1 (1)" "$cart/apply.txt" ||
  fail "a picked cheat isn't applied: $(grep -a "launch:" "$cart/apply.txt" | tail -1)"
echo "PASS: a picked cheat is applied at launch"
