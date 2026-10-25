#!/usr/bin/env bash
# Cheats (v0.7): a GBA game's cheat file, /torii/cheats/<rom path less its
# extension>.cht in libretro's format, picked on the cheat screen from the
# game popup's Cheats (source/screens/cheats.c). A pick is saved in the
# file's enable line (only that changes), survives a reboot, and every launch
# with addons applies it: its byte writes repeat in the IRQ hook at their own
# addresses, and its ROM patch is in the game's PSRAM image. The pick's
# write-back survives a power cut at each of its steps (tests/cutsweep.sh).
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/hello.
set -euo pipefail

# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
# shellcheck source=tests/cutsweep.sh
. tests/cutsweep.sh
local_carts "dist/cart-cheats${1:+-$1}"

fail() {
  echo "FAIL: cheats: $*" >&2
  exit 1
}

cart=$carts/cheats
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n CHEATS "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
# A master code (hidden), the cheat the test picks (EWRAM and IWRAM writes,
# a halfword and a word: four bytes in all, and a ROM patch of the header's
# title), one the file picks already, one that needs v0.8; CRLF, a comment
# and an unknown key, which the write-back keeps byte for byte
printf '%s\r\n' '# Hello, by hand' 'cheats = 4' '' \
  'cheat0_desc = "(M) Must Be On"' 'cheat0_code = "0000ABCD 0007+100F68A4 0007"' 'cheat0_enable = false' '' \
  'cheat1_desc = "Infinite lives"' 'cheat1_code = "0203F000:BEEF+03006000:5A+080000A0:4954"' 'cheat1_enable = false' '' \
  'cheat2_desc = "Already on"' 'cheat2_code = "3203F010+0042"' 'cheat2_enable = true' 'cheat2_note = "kept"' '' \
  'cheat3_desc = "Moon jump"' 'cheat3_code = "74000130 03FE+3203F020 0001"' 'cheat3_enable = false' \
  >"$cart/Hello.cht"
mcopy -i "$cart/sd.img" "$cart/Hello.cht" "::/torii/cheats/Hello.cht"

boot() { OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@"; }
boot -f 60 >/dev/null 2>&1
mkdir -p "$cart/clean"
cp "$cart/sd.img" "$cart/kernel-nor.bin" "$cart/clean/"

# Hello's popup on the GBA page, DOWN past Saves to Cheats, A; on the
# cheat screen A picks the first row (Infinite lives), B saves
pick() {
  # shellcheck disable=SC2046 # nav.sh's options
  "$@" $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @44:A -k @48:- \
    -w "screen ready: cheats" -k @8:A -k @12:- -k @24:B -k @28:- -w "cheats: back" -f 4000
}

pick boot >"$cart/pick.txt" 2>&1 || fail "the cheat screen didn't run: $(grep -a '^kernel' "$cart/pick.txt" | tail -3)"
grep -q "cheats: .*Hello.cht: 3 cheats, 1 master codes hidden, 1 picked" "$cart/pick.txt" ||
  fail "the screen read: $(grep -a 'cheats:' "$cart/pick.txt" | head -3)"
grep -q "cheats: /torii/cheats/Hello.cht: 2 picked, written" "$cart/pick.txt" ||
  fail "the pick wasn't saved: $(grep -a 'cheats:' "$cart/pick.txt" | tail -2)"
mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht >"$cart/after.cht"
# The file as it was but for cheat1's enable value, and the CRC line safefile adds
sed 's/^cheat1_enable = false\r$/cheat1_enable = true\r/' "$cart/Hello.cht" >"$cart/want.cht"
head -c "$(stat -c %s "$cart/want.cht")" "$cart/after.cht" | cmp -s - "$cart/want.cht" ||
  fail "the write-back changed more than the enable line: $(diff <(cat -A "$cart/want.cht") <(cat -A "$cart/after.cht") | head -5)"
tail -n 1 "$cart/after.cht" | grep -q '^#crc32 ' || fail "the written file has no CRC line"
echo "PASS: cheats: the cheat screen picks a cheat and saves only its enable line"

# A reboot keeps the pick, and Boot with addon applies it with no question:
# the writes reach their addresses every frame, the ROM patch is in PSRAM
cp "$cart/sd.img" "$cart/picked.img"
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_launch /Hello.gba addon) -w "BOOT WITH ADDON" \
  -m @240:0x0203F000:4 -m @240:0x03006000:1 -m @240:0x0203F010:1 -m @240:0x0203F020:1 -m @240:0x080000A0:2 \
  -f 4000 >"$cart/launch.txt" 2>&1 || fail "the launch didn't run: $(grep -a '^kernel' "$cart/launch.txt" | tail -3)"
grep -q "BOOT WITH ADDON: .*cheats 1 (4)" "$cart/launch.txt" ||
  fail "the launch logged: $(grep -a 'BOOT WITH ADDON\|cheats:' "$cart/launch.txt" | tail -3)"
grep -q "cheats: 1 ROM patches written into the game" "$cart/launch.txt" ||
  fail "the ROM patch wasn't written: $(grep -a 'cheats:' "$cart/launch.txt" | tail -3)"
mem() { sed -n "s/^mem frame [0-9]* $1: //p" "$cart/launch.txt" | tr -d ' \n' | tr A-F a-f; }
[ "$(mem 0203F000 | cut -c1-4)" = efbe ] ||
  fail "EWRAM 0203F000 holds $(mem 0203F000), not the cheat's BEEF"
[ "$(mem 03006000)" = 5a ] || fail "IWRAM 03006000 holds $(mem 03006000), not 5A"
[ "$(mem 0203F010)" = 42 ] || fail "the file's own pick: 0203F010 holds $(mem 0203F010), not 42"
[ "$(mem 0203F020)" != 01 ] || fail "the v0.8 cheat ran"
[ "$(mem 080000A0)" = 5449 ] || fail "the ROM at 080000A0 reads $(mem 080000A0), not the patch's 5449"
echo "PASS: cheats: after a reboot, the picks are applied at launch (RAM writes and a ROM patch)"

# A clean boot applies none
cp "$cart/picked.img" "$cart/sd.img"
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_launch /Hello.gba clean) -u "launch: loading" -f 4000 >"$cart/clean.txt" 2>&1 || true
! grep -q "cheats: .*picked" "$cart/clean.txt" || fail "a clean boot read the cheats"
echo "PASS: cheats: a clean boot applies none"

# A big file (500 cheats, 46 KB, GameShark codes that decrypt): read once,
# decoded behind a progress dialog, then the screen; A answers at once
cp "$cart/clean/sd.img" "$cart/clean/kernel-nor.bin" "$cart/"
rm -f "$cart/sram.bin"
python3 - "$cart/Big.cht" <<'EOF'
import sys
lines = ["cheats = 500", ""]
for n in range(500):
    lines += [f'cheat{n}_desc = "Cheat number {n}"', f'cheat{n}_code = "8C1A761B 31E2378B"', f"cheat{n}_enable = false", ""]
open(sys.argv[1], "w").write("\n".join(lines))
EOF
mcopy -o -i "$cart/sd.img" "$cart/Big.cht" "::/torii/cheats/Hello.cht"
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @44:A -k @48:- \
  -w "screen ready: cheats" -k @8:A -k @12:- -w "cheats: picked cheat 0" -f 6000 >"$cart/big.txt" 2>&1 ||
  fail "the big file's screen didn't run: $(grep -a '^kernel' "$cart/big.txt" | tail -3)"
grep -q "screen ready: progress" "$cart/big.txt" || fail "no progress dialog while the big file decoded"
grep -q "cheats: .*Hello.cht: 500 cheats, 0 master codes hidden" "$cart/big.txt" ||
  fail "the big file read: $(grep -a 'cheats:' "$cart/big.txt" | head -3)"
[ "$(grep -c 'safefile: .*Hello.cht' "$cart/big.txt")" -le 2 ] || fail "the big file was read more than once"
echo "PASS: cheats: a file of 500 cheats is read once, decoded behind a progress dialog, and picks at once"

# The pick's write-back, cut at each step: either the old picks or the new
# are read at the next power-on, never a broken file
cp "$cart/clean/sd.img" "$cart/clean/kernel-nor.bin" "$cart/"
rm -f "$cart/sram.bin"
scenario() { pick cut_run "$@"; }
check() {
  local n=$1 name=$2
  cut_run -f 400
  [ "$cut_status" = 0 ] || fail "cut at $n ($name): the next power-on exited with $cut_status"
  grep -q 'Torii kernel boot' "$cart/run.log" || fail "cut at $n ($name): no boot line"
  # The copy the kernel reads, as tools/cheats.py reads it
  rm -rf "$carts/cutread" && mkdir -p "$carts/cutread"
  mcopy -n -i "$cart/sd.img" ::/torii/cheats/Hello.cht "$carts/cutread/" 2>/dev/null || true
  mcopy -n -i "$cart/sd.img" ::/torii/cheats/Hello.cht.bak "$carts/cutread/" 2>/dev/null || true
  picks=$(python3 -c '
import sys
sys.path.insert(0, "tools")
import cheats
t = cheats.read_card_file(sys.argv[1])
print("none" if t is None else "".join("1" if e else "0" for _, _, e in cheats.parse_cht(t)))
' "$carts/cutread/Hello.cht")
  case $picks in
  0010 | 0110) ;;
  *) fail "cut at $n ($name): the picks read back as $picks" ;;
  esac
  [ "$n" != 0 ] || [ "$picks" = 0110 ] || fail "uncut: the picks read back as $picks"
}
cut_sweep scenario check
echo "PASS: cheats: a power cut at each of the write-back's $cut_count steps leaves the old picks or the new"
