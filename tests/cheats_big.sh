#!/usr/bin/env bash
# A cheat file past one read (source/cheatstore.h): 2,500 cheats in windows
# of 250, laid out as tools/cheats.py writes them (no enable line but for a
# pick), and no index yet. The screen reads it whole once and writes its
# index; A picks the first cheat; UP past the first row goes round to the
# last window, where a cheat is picked too; DOWN past the last row goes
# round to the first window again. The picks go in the index alone, the
# file as it was, and the next open goes by the index. At a launch, the pick
# the file had and the two made on the screen all run.
# Runs inside the torii-emu container after `just build`, `just
# emu-headless` and building tests/roms/hello.
set -euo pipefail

# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-cheats-big${1:+-$1}"

fail() {
  echo "FAIL: cheats-big: $*" >&2
  exit 1
}

cart=$carts/cheats-big
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n CHEATS "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
# Each cheat a write of its own to EWRAM; cheat 2200 writes 0203F084 and is
# picked in the file, cheat 2499 (the last) writes 0203F088; cheat 1500
# can't be read
python3 - "$cart/Hello.cht" <<'EOF'
import sys
lines = ["# A big file", "cheats = 2500", ""]
for k in range(2500):
    code = {1500: "ZZZZ", 2200: "0203F084:22", 2499: "0203F088:33"}.get(k, f"{0x02030000 + 2 * k:08X}:{k & 0xFF:02X}")
    lines += [f'cheat{k}_desc = "Cheat {k}"', f'cheat{k}_code = "{code}"']
    lines += ["cheat2200_enable = true"] if k == 2200 else []
    lines += [""]
open(sys.argv[1], "w", newline="\n").write("\n".join(lines))
EOF
mcopy -i "$cart/sd.img" "$cart/Hello.cht" ::/torii/cheats/Hello.cht

boot() { OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@"; }

# Hello's popup, DOWN past Saves to Cheats, A; A picks cheat 0; UP round to
# the last window, UP from + Add code to cheat 2499, A picks it; DOWN to +
# Add code, DOWN round to the first window; B saves
cheat_screen() {
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @44:A -k @48:- \
    -w "screen ready: cheats" "$@"
}
cheat_screen -k @8:A -k @12:- -w "cheats: picked cheat 0" -k @8:UP -k @12:- -w "cheats: window 10 of 10" \
  -k @16:UP -k @20:- -k @32:A -k @36:- -w "cheats: picked cheat 2499" \
  -k @8:DOWN -k @12:- -k @24:DOWN -k @28:- -w "cheats: window 1 of 10" \
  -k @16:B -k @20:- -w "cheats: back" -f 30000 >"$cart/screen.txt" 2>&1 ||
  fail "the screen didn't run: $(grep -a '^kernel' "$cart/screen.txt" | tail -3)"
grep -q "cheats: /torii/cheats/Hello.cht: 2500 cheats in 10 windows, 1 picked, read whole and indexed" "$cart/screen.txt" ||
  fail "the file read: $(grep -a 'cheats:' "$cart/screen.txt" | head -3)"
grep -q "cheats: /torii/cheats/Hello.cdx: index written (10 windows, 3 picks)" "$cart/screen.txt" ||
  fail "the save: $(grep -a 'cheats:' "$cart/screen.txt" | tail -3)"
echo "PASS: cheats-big: a file of 2500 cheats with no index is read whole once and indexed; the cursor goes round its 10 windows both ways"

# The picks are the index's: the cheat file stays as it was
mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht | cmp -s - "$cart/Hello.cht" || fail "the cheat file changed"
mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cdx >"$cart/index.cdx"
for pick in 0 898 9C3; do
  grep -q "^p $pick " "$cart/index.cdx" || fail "the index has no pick $pick: $(grep -a '^p ' "$cart/index.cdx" | head -3)"
done
echo "PASS: cheats-big: picks in the first and last windows are saved in the index alone"

# Read again: through the index, the first window alone
cheat_screen -k @8:B -k @12:- -w "cheats: back" -f 30000 >"$cart/again.txt" 2>&1 ||
  fail "the screen didn't run again: $(grep -a '^kernel' "$cart/again.txt" | tail -3)"
grep -q "cheats: /torii/cheats/Hello.cht: 2500 cheats in 10 windows, 3 picked, by its index" "$cart/again.txt" ||
  fail "the read by the index: $(grep -a 'cheats:' "$cart/again.txt" | head -3)"
grep -q "cheats: /torii/cheats/Hello.cht: 250 cheats, 0 greyed, 3 picked" "$cart/again.txt" ||
  fail "the first window: $(grep -a 'cheats:' "$cart/again.txt" | head -4)"
echo "PASS: cheats-big: the file opens again by its index"

cp "$cart/kernel-nor.bin" "$cart/nor-before.bin"
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_launch /Hello.gba addon) -w "BOOT WITH ADDON" -m "@240:0x0203F084:8" -f 30000 >"$cart/launch.txt" 2>&1 ||
  fail "the launch didn't run: $(grep -a '^kernel' "$cart/launch.txt" | tail -3)"
grep -q "cheats: /torii/cheats/Hello.cht: 3 picked, 3 run in game" "$cart/launch.txt" ||
  fail "the launch's cheats: $(grep -a 'cheats:' "$cart/launch.txt" | tail -3)"
got=$(sed -n 's/^mem frame [0-9]* 0203F084: //p' "$cart/launch.txt" | tr -d ' \n' | tr A-F a-f)
if [ "${got:0:2}" != 22 ] || [ "${got:8:2}" != 33 ]; then
  fail "the picks in game: 0203F084 holds ${got:0:2}, 0203F088 ${got:8:2}, not 22 and 33"
fi
echo "PASS: cheats-big: at a launch the picks of three windows all run"

# Settings' Clean up cheats: the unreadable cheat goes from its window, the
# window's cheats after it numbered one lower, the picks kept (the cart as before the
# launch: no save to ask about at power-on)
cp "$cart/nor-before.bin" "$cart/kernel-nor.bin"
rm -f "$cart/sram.bin"
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_setting clean-cheats) -k @8:A -k @12:- -w "screen ready: dialog" -k @8:A -k @12:- \
  -w "cheats: clean-up: 1 files" -f 30000 >"$cart/clean.txt" 2>&1 ||
  fail "the clean-up didn't run: $(grep -a '^kernel' "$cart/clean.txt" | tail -3)"
grep -q "cheats: clean-up: 1 files, 1 cheats removed from 1, 0 failed" "$cart/clean.txt" ||
  fail "the clean-up: $(grep -a 'clean-up' "$cart/clean.txt" | tail -3)"
mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht >"$cart/clean.cht"
! grep -q 'ZZZZ' "$cart/clean.cht" || fail "the unreadable cheat is still there"
grep -q '^cheat1500_desc = "Cheat 1501"$' "$cart/clean.cht" || fail "the cheats after it weren't numbered lower"
# (renumbered within its window alone: the later windows keep theirs; the
# picks are the index's, written again for the file as it is now)
mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cdx >"$cart/clean.cdx"
for pick in 0 898 9C3; do
  grep -q "^p $pick " "$cart/clean.cdx" || fail "pick $pick was lost: $(grep -a '^p ' "$cart/clean.cdx" | head -3)"
done
grep -q "^file $(printf '%X' "$(stat -c %s "$cart/clean.cht")")\$" "$cart/clean.cdx" ||
  fail "the index's size isn't the file's: $(grep -a '^file' "$cart/clean.cdx")"
echo "PASS: cheats-big: Settings' Clean up cheats removes an unreadable cheat from a windowed file, numbers closed up, picks kept"

# The file as the kernel wrote it (its CRC line at the end) opens by its index
# (the cart as before the clean-up: home first, not Settings, its last place,
# and no Recent, which the launch made, before the GBA page)
cp "$cart/nor-before.bin" "$cart/kernel-nor.bin"
rm -f "$cart/sram.bin"
mdel -i "$cart/sd.img" ::/torii/recent.txt ::/torii/recent.txt.bak 2>/dev/null || true
cheat_screen -k @8:B -k @12:- -w "cheats: back" -f 30000 >"$cart/after-clean.txt" 2>&1 ||
  fail "the screen didn't run after the clean-up: $(grep -a '^kernel' "$cart/after-clean.txt" | tail -3)"
grep -q "cheats: /torii/cheats/Hello.cht: 2499 cheats in 10 windows, 3 picked, by its index" "$cart/after-clean.txt" ||
  fail "the cleaned file's open: $(grep -a 'cheats:' "$cart/after-clean.txt" | head -3)"
echo "PASS: cheats-big: the file the kernel wrote opens by its index"

# Edited on a PC, its size kept (one digit of a name in the last window): the
# index still fits by size, but that window's CRC doesn't once read. The
# index is set aside (its size 0, its picks kept), and the next open reads
# the file whole and indexes it, the picks carried by hash.
cp "$cart/nor-before.bin" "$cart/kernel-nor.bin"
rm -f "$cart/sram.bin"
mdel -i "$cart/sd.img" ::/torii/recent.txt ::/torii/recent.txt.bak 2>/dev/null || true
mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht | sed 's/^cheat2450_desc = "Cheat 2450"/cheat2450_desc = "Cheat 2459"/' >"$cart/edited.cht"
cmp -s "$cart/edited.cht" "$cart/clean.cht" && fail "the edit didn't change the file"
mcopy -o -i "$cart/sd.img" "$cart/edited.cht" ::/torii/cheats/Hello.cht
cheat_screen -k @8:UP -k @12:- -w "isn't as its index says" -f 30000 >"$cart/changed.txt" 2>&1 ||
  fail "the changed file's window wasn't caught: $(grep -a 'cheats' "$cart/changed.txt" | tail -3)"
mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cdx | grep -q '^file 0$' || fail "the stale index wasn't set aside"
cp "$cart/nor-before.bin" "$cart/kernel-nor.bin"
rm -f "$cart/sram.bin"
cheat_screen -k @8:B -k @12:- -w "cheats: back" -f 30000 >"$cart/reindexed.txt" 2>&1 ||
  fail "the screen didn't run after the change: $(grep -a '^kernel' "$cart/reindexed.txt" | tail -3)"
grep -q "cheats: /torii/cheats/Hello.cht: 2499 cheats in 10 windows, 3 picked, read whole and indexed" "$cart/reindexed.txt" ||
  fail "the changed file's read: $(grep -a 'cheats:' "$cart/reindexed.txt" | head -3)"
echo "PASS: cheats-big: a file changed under its index sets it aside when found, and is indexed anew, picks kept"
