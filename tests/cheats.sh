#!/usr/bin/env bash
# Cheats (v0.7): a GBA game's cheat file, /torii/cheats/<rom path less its
# extension>.cht in libretro's format, picked on the cheat screen from the
# game popup's Cheats (source/screens/cheats.c). A pick is saved in the
# file's enable line (only that changes), survives a reboot, and every launch
# with addons applies it: its ops run each frame from the IRQ hook
# (source/cheatrun.s), and its ROM patch is in the game's PSRAM image. Every
# op type runs in game: conditionals with ELSE blocks, the device button (R
# and SELECT held) and CodeBreaker's key test, pointer writes, add/or/and,
# fills, and writes at their widths to palette and VRAM. The pick's
# write-back survives a power cut at each of its steps (tests/cutsweep.sh).
# Codes typed on the code keypad (source/screens/keypad.c): a CodeBreaker and
# an encrypted GameShark code added, named, saved and run after a reboot; one
# deleted again; a placeholder filled in, in place; a new code's save cut at
# each step.
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
# title), one the file picks already, one not picked; CRLF, a comment and an
# unknown key, which the write-back keeps byte for byte
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
# The popup's count is the screen's: the master code isn't counted
grep -q "cheats: popup: 3 listed" "$cart/pick.txt" ||
  fail "the popup counted: $(grep -a 'cheats: popup' "$cart/pick.txt" | head -1)"
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
grep -q "BOOT WITH ADDON: .*cheats 1 (2)" "$cart/launch.txt" ||
  fail "the launch logged: $(grep -a 'BOOT WITH ADDON\|cheats:' "$cart/launch.txt" | tail -3)"
grep -q "cheats: 1 ROM patches written into the game" "$cart/launch.txt" ||
  fail "the ROM patch wasn't written: $(grep -a 'cheats:' "$cart/launch.txt" | tail -3)"
mem() { sed -n "s/^mem frame [0-9]* $1: //p" "$cart/launch.txt" | tr -d ' \n' | tr A-F a-f; }
[ "$(mem 0203F000 | cut -c1-4)" = efbe ] ||
  fail "EWRAM 0203F000 holds $(mem 0203F000), not the cheat's BEEF"
[ "$(mem 03006000)" = 5a ] || fail "IWRAM 03006000 holds $(mem 03006000), not 5A"
[ "$(mem 0203F010)" = 42 ] || fail "the file's own pick: 0203F010 holds $(mem 0203F010), not 42"
[ "$(mem 0203F020)" != 01 ] || fail "a cheat not picked ran"
[ "$(mem 080000A0)" = 5449 ] || fail "the ROM at 080000A0 reads $(mem 080000A0), not the patch's 5449"
echo "PASS: cheats: after a reboot, the picks are applied at launch (RAM writes and a ROM patch)"

# The interpreter: a cheat of each kind, all picked, run in game each frame
cp "$cart/clean/sd.img" "$cart/sd.img"
cp "$cart/clean/kernel-nor.bin" "$cart/"
rm -f "$cart/sram.bin"
n=0
code() {
  printf 'cheat%d_desc = "%s"\ncheat%d_code = "%s"\ncheat%d_enable = true\n\n' $n "$1" $n "$2" $n
  n=$((n + 1))
}
{
  code "Pointer" "0203F100:0203F200"
  code "Pointer write (Action Replay v3)" "4223F100 0004BEEF"
  code "Condition's value" "8203F010 4321"
  code "If equal, then" "7203F010 4321+3203F012 0055"
  code "If not equal, then" "A203F010 4321+3203F013 0066"
  code "Block if equal, else" "8A23F010 00004321+0023F014 000000AB+00000000 60000000+0023F015 000000CD+00000000 40000000"
  code "Block if not equal, else" "9223F010 00004321+0023F016 000000EE+00000000 60000000+0023F017 00000077+00000000 40000000"
  code "Button (R+SELECT)" "00000000 1023F300+000000AA 00000000"
  code "Key test (A)" "D0000020 0001+3203F400 0077"
  code "Write, or, and" "8203F020 00F0+2203F020 000F+6203F020 0F3C"
  code "Write, add" "8203F022 0100+E203F022 0023"
  code "Slide" "4203F500 1234+00030004 0002"
  code "Palette, VRAM, a palette byte" "05000010:7C1F+06014000:1234+05000021:5A"
} >"$cart/Hello.cht"
mcopy -o -i "$cart/sd.img" "$cart/Hello.cht" "::/torii/cheats/Hello.cht"
# Before any key (@240), with R+SELECT held (@330), with A held (@390)
dumps=()
for f in 240 330 390; do
  for m in 0203F208:2 0203F012:2 0203F014:4 0203F300:1 0203F400:1 0203F020:4 0203F500:8 05000010:2 05000020:2 06014000:2; do
    dumps+=(-m "@$f:0x$m")
  done
done
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_launch /Hello.gba addon) -w "BOOT WITH ADDON" "${dumps[@]}" -k @300:R+SELECT -k @340:- -k @360:A -k @400:- \
  -f 4000 >"$cart/interp.txt" 2>&1 || fail "the interpreter's launch didn't run: $(grep -a '^kernel' "$cart/interp.txt" | tail -3)"
grep -q "cheats: .*Hello.cht: 13 picked, 13 run in game" "$cart/interp.txt" ||
  fail "the launch read: $(grep -a 'cheats:' "$cart/interp.txt" | head -3)"
at() { sed -n "s/^mem frame [0-9]* $2: //p" "$cart/interp.txt" | sed -n "$1p" | tr -d ' \n' | tr A-F a-f; }
want() {
  local got
  got=$(at "$1" "$2")
  [ "$got" = "$3" ] || fail "$4: $2 holds $got at dump $1, not $3"
}
want 1 0203F208 efbe "a pointer write"
v=$(at 1 0203F012)
[ "${v:0:2}" = 55 ] && [ "${v:2:2}" != 66 ] || fail "if equal must write and if not equal mustn't: 0203F012 holds $v"
v=$(at 1 0203F014)
[ "${v:0:2}" = ab ] && [ "${v:2:2}" != cd ] && [ "${v:4:2}" != ee ] && [ "${v:6:2}" = 77 ] ||
  fail "an ELSE block runs when false and not when true: 0203F014 holds $v"
want 1 05000010 1f7c "a halfword to the palette"
want 1 05000020 5a5a "a byte to the palette (the hardware writes it to both halves)"
want 1 06014000 3412 "a halfword to VRAM"
want 1 0203F020 3c002301 "write, or, and; write, add"
want 1 0203F500 341237123a123d12 "a slide: value and address steps"
[ "$(at 1 0203F300)" != aa ] || fail "the button cheat ran with no keys held"
[ "$(at 1 0203F400)" != 77 ] || fail "the key test passed with no keys held"
want 2 0203F300 aa "the device button, R and SELECT held"
[ "$(at 2 0203F400)" != 77 ] || fail "the key test passed without A"
want 3 0203F400 77 "CodeBreaker's key test, A held"
echo "PASS: cheats: every op type runs in game: conditions and ELSE, the button and key test, pointers, add/or/and, slides, palette and VRAM"

# A clean boot applies none
cp "$cart/picked.img" "$cart/sd.img"
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_launch /Hello.gba clean) -u "launch: loading" -f 4000 >"$cart/clean.txt" 2>&1 || true
! grep -q "cheats: .*picked" "$cart/clean.txt" || fail "a clean boot read the cheats"
echo "PASS: cheats: a clean boot applies none"

# A big file (500 cheats, 46 KB, GameShark codes that decrypt): the popup
# doesn't wait for it to decode, so it shows no count; the screen reads it
# once, decodes it behind a progress dialog, and A answers at once; B saves,
# and back on the list the popup has the count the screen found
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
  -w "screen ready: cheats" -k @8:A -k @12:- -w "cheats: picked cheat 0" -k @8:B -k @12:- \
  -w "screen ready: games" -k @8:START -k @12:- -w "screen ready: game-popup" -f 8000 >"$cart/big.txt" 2>&1 ||
  fail "the big file's screen didn't run: $(grep -a '^kernel' "$cart/big.txt" | tail -3)"
grep -q "screen ready: progress" "$cart/big.txt" || fail "no progress dialog while the big file decoded"
grep -q "cheats: .*Hello.cht: 500 cheats, 0 master codes hidden" "$cart/big.txt" ||
  fail "the big file read: $(grep -a 'cheats:' "$cart/big.txt" | head -3)"
# Until the screen's write-back: the popup's read and the screen's
[ "$(sed '/cheats: back/q' "$cart/big.txt" | grep -c 'safefile: .*Hello.cht')" -le 2 ] ||
  fail "the big file was read more than once"
popups=$(sed -n 's/.*cheats: popup: \(-\{0,1\}[0-9]*\) listed.*/\1/p' "$cart/big.txt" | tr '\n' ' ')
[ "$popups" = "-1 500 " ] || fail "the popup counted ${popups:-nothing}, not -1 (no count) and then 500"
echo "PASS: cheats: a file of 500 cheats is read once, decoded behind a progress dialog, and picks at once; the popup counts it once the screen has"

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

# --- Typing codes (the code keypad, source/screens/keypad.c) ---------------------------
# A code typed on the keypad from the cheat screen's + Add code, named on the
# rename keyboard (A types q, START keeps it) and saved as a new cheat of the
# user's: picked, written at once, and run at the next launch. Keys from
# tests/keypad_keys.py. add_code RUNNER CODE [MGBA-SHOT ARGS]: RUNNER is boot
# or cut_run.
add_code() {
  local runner=$1 code=$2
  shift 2
  # shellcheck disable=SC2046 # nav.sh's options and the keypad's keys
  "$runner" "$@" $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @44:A -k @48:- \
    -w "screen ready: cheats" -k @8:DOWN -k @12:- -k @20:DOWN -k @24:- -k @32:DOWN -k @36:- -k @44:A -k @48:- \
    -w "screen ready: keypad" $(python3 tests/keypad_keys.py "$code" START 2>/dev/null) \
    -w "screen ready: rename" -k @8:A -k @12:- -k @20:START -k @24:- \
    -w "cheats: added" -w "screen ready: cheats" -k @8:B -k @12:- -w "cheats: back" -f 8000
}
# The cheat file as the kernel would read it, by tools/cheats.py: one line a
# cheat, "CODE|ENABLED|USER|FORMAT"
cheat_lines() {
  rm -rf "$carts/cutread" && mkdir -p "$carts/cutread"
  mcopy -n -i "$cart/sd.img" ::/torii/cheats/Hello.cht "$carts/cutread/" 2>/dev/null || true
  mcopy -n -i "$cart/sd.img" ::/torii/cheats/Hello.cht.bak "$carts/cutread/" 2>/dev/null || true
  python3 -c '
import sys
sys.path.insert(0, "tools")
import cheats
t = cheats.read_card_file(sys.argv[1])
for c in cheats.parse_cht(t or ""):
    print("|".join((c[1], str(int(c[2])), c.extra.get("user", ""), c.extra.get("format", ""))))
' "$carts/cutread/Hello.cht"
}
launch_dump() {
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_launch /Hello.gba addon) -w "BOOT WITH ADDON" "$@" -f 4000
}

for typed in "3203F030 0077|0203F030:1|77|CodeBreaker" "D5A7153C 73B76013|0203F040:2|efbe|GameShark v1"; do
  IFS='|' read -r code at want what <<<"$typed"
  cp "$cart/clean/sd.img" "$cart/clean/kernel-nor.bin" "$cart/"
  rm -f "$cart/sram.bin"
  add_code boot "${code// /}" >"$cart/add.txt" 2>&1 ||
    fail "the keypad's $what code wasn't added: $(grep -a '^kernel' "$cart/add.txt" | tail -3)"
  grep -q "keypad: saved $code, format auto" "$cart/add.txt" ||
    fail "the keypad saved: $(grep -a 'keypad:' "$cart/add.txt" | tail -3)"
  grep -q "cheats: /torii/cheats/Hello.cht: 2 picked, written" "$cart/add.txt" ||
    fail "the $what code wasn't picked and written: $(grep -a 'cheats:' "$cart/add.txt" | tail -3)"
  cp "$cart/sd.img" "$cart/added.img"
  last=$(cheat_lines | tail -n 1)
  [ "$last" = "$code|1|true|" ] || fail "the $what code reads back as $last"
  mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht | grep -q $'^cheat4_desc = "q"\r$' ||
    fail "the $what code's name: $(mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht | grep -a cheat4_ | head -3)"
  launch_dump -m "@240:0x$at" >"$cart/launch.txt" 2>&1 ||
    fail "the $what code's launch didn't run: $(grep -a '^kernel' "$cart/launch.txt" | tail -3)"
  got=$(sed -n "s/^mem frame [0-9]* ${at%%:*}: //p" "$cart/launch.txt" | tr -d ' \n' | tr A-F a-f)
  [ "$got" = "$want" ] || fail "the typed $what code: ${at%%:*} holds $got, not $want"
  echo "PASS: cheats: a $what code typed on the keypad ($code) is saved, picked and runs after a reboot"
done

# A code of the user's (the GameShark one just added, row 3) can be deleted:
# SELECT's menu, Delete, asked and answered; the file is as it was before the
# add (but for its CRC line), the database's cheats untouched
cp "$cart/added.img" "$cart/sd.img"
cp "$cart/clean/kernel-nor.bin" "$cart/"
rm -f "$cart/sram.bin"
before=$(cheat_lines | head -n 4)
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @44:A -k @48:- \
  -w "screen ready: cheats" -k @8:DOWN -k @12:- -k @20:DOWN -k @24:- -k @32:DOWN -k @36:- -k @44:SELECT -k @48:- \
  -w "screen ready: cheat-menu" -k @16:DOWN -k @20:- -k @28:A -k @32:- \
  -w "screen ready: confirm" -k @8:RIGHT -k @12:- -k @20:A -k @24:- \
  -w "cheats: deleted" -w "screen ready: cheats" -k @8:B -k @12:- -w "cheats: back" -f 6000 >"$cart/delete.txt" 2>&1 ||
  fail "the delete didn't run: $(grep -a '^kernel' "$cart/delete.txt" | tail -3)"
grep -q "cheats: deleted cheat 4: ok" "$cart/delete.txt" ||
  fail "the delete logged: $(grep -a 'cheats:' "$cart/delete.txt" | tail -3)"
[ "$(cheat_lines)" = "$before" ] || fail "after the delete the cheats read: $(cheat_lines | tr '\n' ' ')"
! mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht | grep -q '^cheat4_' || fail "cheat 4's lines are still there"
echo "PASS: cheats: a code of the user's is deleted from SELECT's menu, the database's cheats untouched"

# A placeholder (Pick a level, 3203F050+00??) filled in on the keypad: the
# stand-ins typed over, the code replaced in place, picked and run
cp "$cart/clean/sd.img" "$cart/clean/kernel-nor.bin" "$cart/"
rm -f "$cart/sram.bin"
printf '%s\r\n' 'cheats = 2' '' \
  'cheat0_desc = "Pick a level"' 'cheat0_code = "3203F050+00??"' 'cheat0_enable = false' '' \
  'cheat1_desc = "Already on"' 'cheat1_code = "3203F010+0042"' 'cheat1_enable = true' \
  >"$cart/Fill.cht"
mcopy -o -i "$cart/sd.img" "$cart/Fill.cht" "::/torii/cheats/Hello.cht"
# shellcheck disable=SC2046 # nav.sh's options and the keypad's keys
boot $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @44:A -k @48:- \
  -w "screen ready: cheats" -k @8:A -k @12:- -w "screen ready: keypad" \
  $(python3 tests/keypad_keys.py 5A START 2>/dev/null) \
  -w "cheats: filled in" -w "screen ready: cheats" -k @8:B -k @12:- -w "cheats: back" -f 6000 >"$cart/fill.txt" 2>&1 ||
  fail "the placeholder's keypad didn't run: $(grep -a '^kernel' "$cart/fill.txt" | tail -3)"
grep -q "keypad: saved 3203F050 005A, format auto" "$cart/fill.txt" ||
  fail "the keypad saved: $(grep -a 'keypad:' "$cart/fill.txt" | tail -3)"
mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht >"$cart/after.cht"
sed 's/^cheat0_code = "3203F050+00??"\r$/cheat0_code = "3203F050 005A"\r/; s/^cheat0_enable = false\r$/cheat0_enable = true\r/' \
  "$cart/Fill.cht" >"$cart/want.cht"
head -c "$(stat -c %s "$cart/want.cht")" "$cart/after.cht" | cmp -s - "$cart/want.cht" ||
  fail "the fill changed more than its code and pick: $(diff <(cat -A "$cart/want.cht") <(cat -A "$cart/after.cht") | head -5)"
launch_dump -m "@240:0x0203F050:1" >"$cart/launch.txt" 2>&1 ||
  fail "the filled-in code's launch didn't run: $(grep -a '^kernel' "$cart/launch.txt" | tail -3)"
got=$(sed -n 's/^mem frame [0-9]* 0203F050: //p' "$cart/launch.txt" | tr -d ' \n' | tr A-F a-f)
[ "$got" = 5a ] || fail "the filled-in code: 0203F050 holds $got, not 5a"
echo "PASS: cheats: a placeholder filled in on the keypad is replaced in place, picked, and runs"

# Saving a new code, cut at each step: the next power-on reads the file
# without it or with it whole, never a broken one
cp "$cart/clean/sd.img" "$cart/clean/kernel-nor.bin" "$cart/"
rm -f "$cart/sram.bin"
add_scenario() { add_code cut_run 3203F0300077 "$@"; }
add_check() {
  local n=$1 name=$2 lines
  cut_run -f 400
  [ "$cut_status" = 0 ] || fail "add, cut at $n ($name): the next power-on exited with $cut_status"
  grep -q 'Torii kernel boot' "$cart/run.log" || fail "add, cut at $n ($name): no boot line"
  lines=$(cheat_lines | tr '\n' ' ')
  case $lines in
  "0000ABCD 0007+100F68A4 0007|0|| 0203F000:BEEF+03006000:5A+080000A0:4954|0|| 3203F010+0042|1|| 74000130 03FE+3203F020 0001|0|| ") ;;
  "0000ABCD 0007+100F68A4 0007|0|| 0203F000:BEEF+03006000:5A+080000A0:4954|0|| 3203F010+0042|1|| 74000130 03FE+3203F020 0001|0|| 3203F030 0077|1|true| ") ;;
  *) fail "add, cut at $n ($name): the cheats read back as: $lines" ;;
  esac
  [ "$n" != 0 ] || [[ $lines == *"3203F030 0077|1|true|"* ]] || fail "add, uncut: the code isn't there: $lines"
}
cut_sweep add_scenario add_check
echo "PASS: cheats: a power cut at each of a new code's $cut_count write steps leaves the file without it or with it"
