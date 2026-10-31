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
grep -q "cheats: .*Hello.cht: 4 cheats, 1 greyed, 1 picked" "$cart/pick.txt" ||
  fail "the screen read: $(grep -a 'cheats:' "$cart/pick.txt" | head -3)"
# The popup never reads the file: no count before the screen has counted it
grep -q "cheats: popup: -1 listed" "$cart/pick.txt" ||
  fail "the popup counted: $(grep -a 'cheats: popup' "$cart/pick.txt" | head -1)"
! sed '/screen ready: game-popup/q' "$cart/pick.txt" | grep 'safefile: .*Hello.cht\|cheats: .*Hello.cht: [0-9]* bytes' >/dev/null ||
  fail "the popup read the cheat file"
grep -q "cheats: /torii/cheats/Hello.cht: 2 picked, written" "$cart/pick.txt" ||
  fail "the pick wasn't saved: $(grep -a 'cheats:' "$cart/pick.txt" | tail -2)"
mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht >"$cart/after.cht"
# The file as it was but for cheat1's enable value, and the CRC line safefile adds
sed 's/^cheat1_enable = false\r$/cheat1_enable = true\r/' "$cart/Hello.cht" >"$cart/want.cht"
head -c "$(stat -c %s "$cart/want.cht")" "$cart/after.cht" | cmp -s - "$cart/want.cht" ||
  fail "the write-back changed more than the enable line: $(diff <(cat -A "$cart/want.cht") <(cat -A "$cart/after.cht") | head -5)"
tail -n 1 "$cart/after.cht" | grep '^#crc32 ' >/dev/null || fail "the written file has no CRC line"
echo "PASS: cheats: the cheat screen picks a cheat and saves only its enable line"

# The screen keeps its count in /torii/cheats/counts.txt: after a power-off
# the popup has it (still without reading the cheat file); once the file
# changes on the card (by hand here), it has none until the screen counts it
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_game_menu gba 0) -f 3000 >"$cart/count.txt" 2>&1 || fail "the popup didn't open: $(grep -a '^kernel' "$cart/count.txt" | tail -3)"
grep -q "cheats: popup: 4 listed" "$cart/count.txt" ||
  fail "after a power-off the popup counted: $(grep -a 'cheats: popup' "$cart/count.txt" | head -1)"
! grep -q 'safefile: .*Hello.cht\|cheats: .*Hello.cht: [0-9]* bytes' "$cart/count.txt" || fail "the popup read the cheat file"
cp "$cart/sd.img" "$cart/counted.img"
mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht | sed '$d' >"$cart/edited.cht"
printf 'cheat9_desc = "By hand"\ncheat9_code = "3203F0A0+0001"\n' >>"$cart/edited.cht"
mcopy -o -i "$cart/sd.img" "$cart/edited.cht" ::/torii/cheats/Hello.cht
mdel -i "$cart/sd.img" ::/torii/cheats/Hello.cht.bak 2>/dev/null || true
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_game_menu gba 0) -f 3000 >"$cart/count.txt" 2>&1 || fail "the popup didn't open: $(grep -a '^kernel' "$cart/count.txt" | tail -3)"
grep -q "cheats: popup: -1 listed" "$cart/count.txt" ||
  fail "the popup counted a changed file: $(grep -a 'cheats: popup' "$cart/count.txt" | head -1)"
cp "$cart/counted.img" "$cart/sd.img"
echo "PASS: cheats: the popup has the screen's count after a power-off, and none for a file changed since"

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

# Activation from cheatN_keys (v0.10, docs/plans/cheat-guides.md): a cheat
# whose keys say Hold L adds 1 to its counter only while L is held; one
# whose keys say Press R adds 1 once a press, however long R is held
cp "$cart/clean/sd.img" "$cart/sd.img"
cp "$cart/clean/kernel-nor.bin" "$cart/"
rm -f "$cart/sram.bin"
printf '%s\n' 'cheats = 2' \
  'cheat0_desc = "Hold L"' 'cheat0_code = "E203F600 0001"' 'cheat0_enable = true' 'cheat0_keys = "hold L"' \
  'cheat1_desc = "Press R"' 'cheat1_code = "E203F602 0001"' 'cheat1_enable = true' 'cheat1_keys = "press R"' \
  >"$cart/Hello.cht"
mcopy -o -i "$cart/sd.img" "$cart/Hello.cht" "::/torii/cheats/Hello.cht"
# Before any key (@290), R held for 40 frames (@350), R pressed again
# (@380), L held for 20 frames (@430)
dumps=()
for f in 290 350 380 430; do
  dumps+=(-m "@$f:0x0203F600:4")
done
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_launch /Hello.gba addon) -w "BOOT WITH ADDON" "${dumps[@]}" -k @300:R -k @340:- -k @360:R -k @364:- \
  -k @400:L -k @420:- -f 4000 >"$cart/keys.txt" 2>&1 || fail "the keys' launch didn't run: $(grep -a '^kernel' "$cart/keys.txt" | tail -3)"
grep -q "cheats: .*Hello.cht: 2 picked, 2 run in game" "$cart/keys.txt" ||
  fail "the keys' launch read: $(grep -a 'cheats:' "$cart/keys.txt" | head -3)"
# counter N HALF: dump N's halfword (0: Hold L's, 1: Press R's), in decimal
counter() {
  local v
  v=$(sed -n 's/^mem frame [0-9]* 0203F600: //p' "$cart/keys.txt" | sed -n "$1p" | tr -d ' \n')
  echo $((16#${v:$(($2 * 4 + 2)):2}${v:$(($2 * 4)):2}))
}
[ "$(counter 1 0)" = 0 ] && [ "$(counter 1 1)" = 0 ] || fail "a cheat ran with no keys held: $(counter 1 0), $(counter 1 1)"
[ "$(counter 2 1)" = 1 ] || fail "Press R ran $(counter 2 1) times for one press held 40 frames"
[ "$(counter 2 0)" = 0 ] || fail "Hold L ran without L: $(counter 2 0)"
[ "$(counter 3 1)" = 2 ] || fail "Press R ran $(counter 3 1) times for two presses"
held=$(counter 4 0)
[ "$held" -ge 18 ] && [ "$held" -le 22 ] || fail "Hold L ran $held times for 20 frames held"
[ "$(counter 4 1)" = 2 ] || fail "Press R ran without R: $(counter 4 1)"
echo "PASS: cheats: a cheat's keys line in game: Hold runs while its buttons are held, Press once a press"

# A clean boot applies none
cp "$cart/picked.img" "$cart/sd.img"
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_launch /Hello.gba clean) -u "launch: loading" -f 4000 >"$cart/clean.txt" 2>&1 || true
! grep -q "cheats: .*picked" "$cart/clean.txt" || fail "a clean boot read the cheats"
echo "PASS: cheats: a clean boot applies none"

# A big file (500 cheats, 46 KB, GameShark codes that decrypt, all one code:
# the 499 after the first greyed as the same): the popup
# never reads it, so it shows no count; the screen reads it once, shows it
# at once (decoding it a slice a frame, no progress dialog), and A answers at once; B saves, and back on the
# list the popup has the count the screen found, kept for the file as saved
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
! grep -q "screen ready: progress" "$cart/big.txt" || fail "a progress dialog while the big file decoded"
grep -q "cheats: .*Hello.cht: 500 cheats, [0-9]* greyed" "$cart/big.txt" ||
  fail "the big file read: $(grep -a 'cheats:' "$cart/big.txt" | head -3)"
# Until the screen's write-back: the screen's read alone
[ "$(sed '/cheats: back/q' "$cart/big.txt" | grep -c 'safefile: .*Hello.cht')" -le 1 ] ||
  fail "the big file was read more than once"
popups=$(sed -n 's/.*cheats: popup: \(-\{0,1\}[0-9]*\) listed.*/\1/p' "$cart/big.txt" | tr '\n' ' ')
[ "$popups" = "-1 500 " ] || fail "the popup counted ${popups:-nothing}, not -1 (no count) and then 500"
echo "PASS: cheats: a file of 500 cheats is read once, shown at once and decoded as it shows, and picks at once; the popup counts it once the screen has"

# big_cht OUT COUNT BYTES [user] [from]: a file of COUNT cheats padded to
# about BYTES (LF). Cheat k (counting from FROM, numbered from 0) writes
# EWRAM, its desc padded; the first writes 0203F080, the last two are picked
# in the file: a ROM patch of the header's title (080000A0) and a write to
# 0203F084. With "user", the first is a code of the user's.
big_cht() {
  python3 - "$@" <<'EOF'
import sys
out, count, size = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
user = len(sys.argv) > 4 and sys.argv[4] == "user"
first = int(sys.argv[5]) if len(sys.argv) > 5 else 0
total = count + first

def text(pad, start):
    lines = [f"cheats = {total - start}", ""]
    for num, k in enumerate(range(start, total)):
        code = {0: "0203F080:11", total - 2: "080000A0:4954", total - 1: "0203F084:22"}.get(k, "0203F1%02X:%02X" % (k & 0x7F, k & 0xFF))
        lines += [f'cheat{num}_desc = "Cheat {k} ' + "x" * pad + '"', f'cheat{num}_code = "{code}"',
                  f"cheat{num}_enable = {'true' if k >= total - 2 else 'false'}"]
        if user and k == 0:
            lines.append(f"cheat{num}_user = true")
        lines.append("")
    return "\n".join(lines)

# The padding of the file from cheat 0, so one from cheat FROM matches it
t = text(max(0, (size - len(text(0, 0))) // total), first)
assert len(t) <= 0x30000, len(t)
open(out, "w").write(t)
EOF
}

# Files past the old 56 KB cap and 16-bit offsets (100 KB, 600 cheats) and
# near the new one (190 KB, 1,000 cheats): listed, the first cheat picked on
# the screen and saved (only its enable line changes), and at a launch the
# picks run, the ROM patch far into the file included
for big in "600 100000" "1000 190000"; do
  read -r count bytes <<<"$big"
  cp "$cart/clean/sd.img" "$cart/clean/kernel-nor.bin" "$cart/"
  rm -f "$cart/sram.bin"
  big_cht "$cart/Huge.cht" "$count" "$bytes"
  mcopy -o -i "$cart/sd.img" "$cart/Huge.cht" "::/torii/cheats/Hello.cht"
  pick boot -f 12000 >"$cart/huge.txt" 2>&1 ||
    fail "the $count-cheat file's screen didn't run: $(grep -a '^kernel' "$cart/huge.txt" | tail -3)"
  grep -q "cheats: .*Hello.cht: $count cheats, [0-9]* greyed, 2 picked" "$cart/huge.txt" ||
    fail "the $count-cheat file read: $(grep -a 'cheats:' "$cart/huge.txt" | head -3)"
  # The popup doesn't read it (reading 190 KB took 75 frames): its look at
  # the file takes far less than a frame
  ! sed '/screen ready: game-popup/q' "$cart/huge.txt" | grep 'cheats: .*Hello.cht: [0-9]* bytes' >/dev/null ||
    fail "the $count-cheat file's popup read it"
  cycles=$(sed -n 's/.*cheats: popup: -1 listed, \([0-9]*\) cycles.*/\1/p' "$cart/huge.txt")
  [ -n "$cycles" ] && [ "$cycles" -lt 100000 ] || fail "the $count-cheat file's popup took ${cycles:-?} cycles"
  grep -q "cheats: /torii/cheats/Hello.cht: 3 picked, written" "$cart/huge.txt" ||
    fail "the $count-cheat file's pick wasn't saved: $(grep -a 'cheats:' "$cart/huge.txt" | tail -2)"
  mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht >"$cart/after.cht"
  sed 's/^cheat0_enable = false$/cheat0_enable = true/' "$cart/Huge.cht" >"$cart/want.cht"
  head -c "$(stat -c %s "$cart/want.cht")" "$cart/after.cht" | cmp -s - "$cart/want.cht" ||
    fail "the $count-cheat file's write-back changed more than the enable line: $(diff "$cart/want.cht" "$cart/after.cht" | head -5)"
  launch_big() {
    # shellcheck disable=SC2046 # nav.sh's options
    boot $(nav_launch /Hello.gba addon) -w "BOOT WITH ADDON" -m @240:0x0203F080:1 -m @240:0x0203F084:1 \
      -m @240:0x080000A0:2 -f 6000
  }
  launch_big >"$cart/launch.txt" 2>&1 ||
    fail "the $count-cheat file's launch didn't run: $(grep -a '^kernel' "$cart/launch.txt" | tail -3)"
  grep -q "cheats: /torii/cheats/Hello.cht: 3 picked, 2 run in game" "$cart/launch.txt" ||
    fail "the $count-cheat file's launch read: $(grep -a 'cheats:' "$cart/launch.txt" | head -3)"
  grep -q "cheats: 1 ROM patches written into the game" "$cart/launch.txt" ||
    fail "the $count-cheat file's ROM patch wasn't written: $(grep -a 'cheats:' "$cart/launch.txt" | tail -3)"
  bmem() { sed -n "s/^mem frame [0-9]* $1: //p" "$cart/launch.txt" | tr -d ' \n' | tr A-F a-f; }
  [ "$(bmem 0203F080)" = 11 ] || fail "the $count-cheat file: 0203F080 holds $(bmem 0203F080), not 11"
  [ "$(bmem 0203F084)" = 22 ] || fail "the $count-cheat file: 0203F084 holds $(bmem 0203F084), not 22"
  [ "$(bmem 080000A0)" = 5449 ] || fail "the $count-cheat file: the ROM at 080000A0 reads $(bmem 080000A0), not 5449"
  echo "PASS: cheats: a file of $count cheats ($(stat -c %s "$cart/Huge.cht") bytes) lists, picks, saves only its enable line, and its picks run at launch with a ROM patch"
done

# Deleting a code of the user's from a 100 KB file, cut at each step: every
# cheat after it is numbered one lower in PSRAM, then the file is written;
# the next power-on reads the file as it was or as the delete left it whole
cp "$cart/clean/sd.img" "$cart/clean/kernel-nor.bin" "$cart/"
rm -f "$cart/sram.bin"
big_cht "$cart/Before.cht" 600 100000 user
big_cht "$cart/After.cht" 599 100000 user 1
mcopy -o -i "$cart/sd.img" "$cart/Before.cht" "::/torii/cheats/Hello.cht"
del_scenario() {
  # shellcheck disable=SC2046 # nav.sh's options
  cut_run "$@" $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @44:A -k @48:- \
    -w "screen ready: cheats" -k @8:START -k @12:- \
    -w "screen ready: cheat-menu" -k @16:DOWN -k @20:- -k @28:DOWN -k @32:- -k @40:DOWN -k @44:- -k @52:DOWN -k @56:- -k @64:DOWN -k @68:- -k @76:DOWN -k @80:- -k @88:A -k @92:- \
    -w "screen ready: confirm" -k @8:RIGHT -k @12:- -k @20:A -k @24:- \
    -w "cheats: deleted" -w "screen ready: cheats" -k @8:B -k @12:- -w "cheats: back" -f 12000
}
del_check() {
  local n=$1 name=$2 got
  cut_run -f 400
  [ "$cut_status" = 0 ] || fail "delete, cut at $n ($name): the next power-on exited with $cut_status"
  rm -rf "$carts/cutread" && mkdir -p "$carts/cutread"
  mcopy -n -i "$cart/sd.img" ::/torii/cheats/Hello.cht "$carts/cutread/" 2>/dev/null || true
  mcopy -n -i "$cart/sd.img" ::/torii/cheats/Hello.cht.bak "$carts/cutread/" 2>/dev/null || true
  got=$(python3 -c '
import sys
sys.path.insert(0, "tools")
import cheats
t = cheats.read_card_file(sys.argv[1])
for name in sys.argv[2:]:
    if t == open(name).read():
        print(name.rsplit("/", 1)[-1])
        break
else:
    print("neither")
' "$carts/cutread/Hello.cht" "$cart/Before.cht" "$cart/After.cht")
  case $got in
  Before.cht | After.cht) ;;
  *) fail "delete, cut at $n ($name): the file reads back as neither the old nor the new" ;;
  esac
  [ "$n" != 0 ] || [ "$got" = After.cht ] || fail "delete, uncut: the file reads back as $got"
}
cut_sweep del_scenario del_check
echo "PASS: cheats: a power cut at each of a 100 KB file's delete's $cut_count steps leaves the file as it was or with every cheat renumbered"

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
# rename keyboard (START keeps its "Code 1") and saved as a new cheat of the
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
    -w "screen ready: rename" -k @8:START -k @12:- \
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
  mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht | grep $'^cheat4_desc = "Code 1"\r$' >/dev/null ||
    fail "the $what code's name: $(mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht | grep -a cheat4_ | head -3)"
  launch_dump -m "@240:0x$at" >"$cart/launch.txt" 2>&1 ||
    fail "the $what code's launch didn't run: $(grep -a '^kernel' "$cart/launch.txt" | tail -3)"
  got=$(sed -n "s/^mem frame [0-9]* ${at%%:*}: //p" "$cart/launch.txt" | tr -d ' \n' | tr A-F a-f)
  [ "$got" = "$want" ] || fail "the typed $what code: ${at%%:*} holds $got, not $want"
  echo "PASS: cheats: a $what code typed on the keypad ($code) is saved, picked and runs after a reboot"
done

# A code of the user's (the GameShark one just added, row 3) can be edited:
# START's menu, Edit code, the keypad with its code in (B held clears it),
# a CodeBreaker code typed, the keyboard keeping its name; it stays in its
# place, picked, with its new code
cp "$cart/added.img" "$cart/sd.img"
cp "$cart/clean/kernel-nor.bin" "$cart/"
rm -f "$cart/sram.bin"
# shellcheck disable=SC2046 # nav.sh's options and the keypad's keys
boot $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @44:A -k @48:- \
  -w "screen ready: cheats" -k @8:DOWN -k @12:- -k @20:DOWN -k @24:- -k @32:DOWN -k @36:- -k @44:START -k @48:- \
  -w "screen ready: cheat-menu" -k @16:DOWN -k @20:- -k @28:A -k @32:- \
  -w "screen ready: keypad" -k @8:B -k @200:- $(python3 tests/keypad_keys.py --at 220 3203F0600011 START 2>/dev/null) \
  -w "screen ready: rename" -k @8:START -k @12:- \
  -w "cheats: edited" -w "screen ready: cheats" -k @8:B -k @12:- -w "cheats: back" -f 8000 >"$cart/edit.txt" 2>&1 ||
  fail "the edit didn't run: $(grep -a '^kernel' "$cart/edit.txt" | tail -3)"
grep -q "keypad: saved 3203F060 0011, format auto" "$cart/edit.txt" ||
  fail "the edit's keypad saved: $(grep -a 'keypad:' "$cart/edit.txt" | tail -3)"
[ "$(cheat_lines | tail -n 1)" = "3203F060 0011|1|true|" ] || fail "the edited code reads back as $(cheat_lines | tail -n 1)"
mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht | grep $'^cheat4_desc = "Code 1"\r$' >/dev/null || fail "the edited code lost its name"
[ "$(cheat_lines | wc -l)" = 5 ] || fail "the edit added a cheat: $(cheat_lines | tr '\n' ' ')"
echo "PASS: cheats: a code of the user's is edited from START's menu, in its place, its name kept"

# A code of the user's (the GameShark one just added, row 3) can be deleted:
# START's menu, Delete, asked and answered; the file is as it was before the
# add (but for its CRC line), the database's cheats untouched
cp "$cart/added.img" "$cart/sd.img"
cp "$cart/clean/kernel-nor.bin" "$cart/"
rm -f "$cart/sram.bin"
before=$(cheat_lines | head -n 4)
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @44:A -k @48:- \
  -w "screen ready: cheats" -k @8:DOWN -k @12:- -k @20:DOWN -k @24:- -k @32:DOWN -k @36:- -k @44:START -k @48:- \
  -w "screen ready: cheat-menu" -k @16:DOWN -k @20:- -k @28:DOWN -k @32:- -k @40:DOWN -k @44:- -k @52:DOWN -k @56:- -k @64:DOWN -k @68:- -k @76:DOWN -k @80:- -k @88:A -k @92:- \
  -w "screen ready: confirm" -k @8:RIGHT -k @12:- -k @20:A -k @24:- \
  -w "cheats: deleted" -w "screen ready: cheats" -k @8:B -k @12:- -w "cheats: back" -f 6000 >"$cart/delete.txt" 2>&1 ||
  fail "the delete didn't run: $(grep -a '^kernel' "$cart/delete.txt" | tail -3)"
grep -q "cheats: deleted cheat 4: ok" "$cart/delete.txt" ||
  fail "the delete logged: $(grep -a 'cheats:' "$cart/delete.txt" | tail -3)"
[ "$(cheat_lines)" = "$before" ] || fail "after the delete the cheats read: $(cheat_lines | tr '\n' ' ')"
! mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht | grep '^cheat4_' >/dev/null || fail "cheat 4's lines are still there"
echo "PASS: cheats: a code of the user's is deleted from START's menu, the database's cheats untouched"

# A database cheat (the first that can be picked, cheat 1) renamed: START's menu, Rename, "q" typed after
# its name, START; its desc changes and a renamed line keeps it from `just
# cheats`, its code and pick as they were
cp "$cart/added.img" "$cart/sd.img"
cp "$cart/clean/kernel-nor.bin" "$cart/"
rm -f "$cart/sram.bin"
before=$(cheat_lines)
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @44:A -k @48:- \
  -w "screen ready: cheats" -k @8:START -k @12:- \
  -w "screen ready: cheat-menu" -k @16:DOWN -k @20:- -k @28:A -k @32:- \
  -w "screen ready: rename" -k @10:A -k @14:- -k @24:START -k @28:- \
  -w "cheats: renamed" -w "screen ready: cheats" -k @8:B -k @12:- -w "cheats: back" -f 6000 >"$cart/rename.txt" 2>&1 ||
  fail "the rename didn't run: $(grep -a '^kernel' "$cart/rename.txt" | tail -3)"
grep -q "cheats: renamed cheat 1: ok" "$cart/rename.txt" ||
  fail "the rename logged: $(grep -a 'cheats:' "$cart/rename.txt" | tail -3)"
mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht | grep $'^cheat1_desc = ".*q"\r$' >/dev/null || fail "cheat 1's name didn't change"
mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht | grep $'^cheat1_renamed = true\r$' >/dev/null || fail "cheat 1 has no renamed line"
[ "$(cheat_lines)" = "$before" ] || fail "after the rename the cheats read: $(cheat_lines | tr '\n' ' ')"
echo "PASS: cheats: a database cheat is renamed from START's menu, marked so \`just cheats\` keeps the name"

# --- Activations and guides (docs/plans/cheat-guides.md) -------------------------
# cheat_file: the card's Hello.cht, CRs dropped
cheat_file() { mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht | tr -d '\r'; }
# The cheat screen on Hello's cheats, the cursor on cheat 1, then KEYS...,
# then B leaves it: cheat_run LOG KEYS...
cheat_run() {
  local log=$1
  shift
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @44:A -k @48:- \
    -w "screen ready: cheats" "$@" -k @8:B -k @12:- -w "cheats: back" -f 8000 >"$log" 2>&1 ||
    fail "$(basename "$log" .txt) didn't run: $(grep -a '^kernel' "$log" | tail -3)"
}
cp "$cart/added.img" "$cart/sd.img"
cp "$cart/clean/kernel-nor.bin" "$cart/"
rm -f "$cart/sram.bin"

# SELECT opens a cheat's Guide page, which scrolls; B goes back to the list
cheat_run "$cart/guide.txt" -k @8:SELECT -k @12:- -w "screen ready: cheat-guide" \
  -k @16:DOWN -k @20:- -k @28:DOWN -k @32:- -k @44:B -k @48:- -w "screen ready: cheats"
grep -q "cheats: guide of cheat 1, " "$cart/guide.txt" ||
  fail "SELECT didn't open the guide: $(grep -a 'cheats:' "$cart/guide.txt" | tail -3)"
echo "PASS: cheats: SELECT opens a cheat's Guide page, and B comes back"

# Buttons: Hold, L and R held a moment then let go, is written as the cheat's
# keys line (cheat 2, the row under the cursor's: its code only writes RAM);
# the row shows its tag. From the code then removes the line.
cheat_run "$cart/hold.txt" -k @8:DOWN -k @12:- -k @24:START -k @28:- -w "screen ready: cheat-menu" \
  -k @16:DOWN -k @20:- -k @28:DOWN -k @32:- -k @44:A -k @48:- -w "screen ready: cheat-buttons" \
  -k @16:UP -k @20:- -k @28:UP -k @32:- -k @44:A -k @48:- -k @64:L+R -k @104:- \
  -w "cheats: buttons captured" -k @16:B -k @20:- -w "screen ready: cheats"
grep -q "cheats: buttons of cheat 2: hold L+R: ok" "$cart/hold.txt" ||
  fail "Hold wasn't set: $(grep -a 'cheats:' "$cart/hold.txt" | tail -3)"
cheat_file | grep -x 'cheat2_keys = "hold L+R"' >/dev/null || fail "cheat 2's keys line: $(cheat_file | grep cheat2_)"
[ "$(cheat_lines | wc -l)" = 5 ] || fail "setting buttons changed the cheats: $(cheat_lines | tr '\n' ' ')"
echo "PASS: cheats: Buttons captures a held combination and writes it as the cheat's keys line"
cheat_run "$cart/code.txt" -k @8:DOWN -k @12:- -k @24:START -k @28:- -w "screen ready: cheat-menu" \
  -k @16:DOWN -k @20:- -k @28:DOWN -k @32:- -k @44:A -k @48:- -w "screen ready: cheat-buttons" \
  -k @16:DOWN -k @20:- -k @28:DOWN -k @32:- -k @44:A -k @48:- -k @60:B -k @64:- -w "screen ready: cheats"
grep -q "cheats: buttons of cheat 2: from the code: ok" "$cart/code.txt" ||
  fail "From the code wasn't set: $(grep -a 'cheats:' "$cart/code.txt" | tail -3)"
! cheat_file | grep '^cheat2_keys' >/dev/null || fail "From the code left a keys line: $(cheat_file | grep cheat2_)"
echo "PASS: cheats: Buttons' From the code removes the cheat's keys line"
# Cheat 1 patches the ROM too, so it can't wait on buttons: A on Hold
# starts no capture, and B leaves the file as it was
cheat_run "$cart/ungated.txt" -k @8:START -k @12:- -w "screen ready: cheat-menu" \
  -k @16:DOWN -k @20:- -k @28:DOWN -k @32:- -k @44:A -k @48:- -w "screen ready: cheat-buttons" \
  -k @16:UP -k @20:- -k @28:UP -k @32:- -k @44:A -k @48:- -k @64:L+R -k @104:- -k @130:B -k @134:- \
  -w "screen ready: cheats"
! grep -q "cheats: buttons captured\|cheats: buttons of cheat 1:" "$cart/ungated.txt" ||
  fail "Hold was set on a cheat that patches the ROM: $(grep -a 'cheats:' "$cart/ungated.txt" | tail -3)"
! cheat_file | grep '^cheat1_keys' >/dev/null || fail "cheat 1 got a keys line: $(cheat_file | grep cheat1_)"
echo "PASS: cheats: Buttons keeps Hold off a cheat that patches the ROM"

# Edit guide: the keyboard of lines, q, Enter (the function row's fourth
# key) and n typed, START; the file's guide line holds them with "\n". Then
# deleted to nothing, the guide line goes.
cheat_run "$cart/guide-edit.txt" -k @8:START -k @12:- -w "screen ready: cheat-menu" \
  -k @16:DOWN -k @20:- -k @28:DOWN -k @32:- -k @40:DOWN -k @44:- -k @56:A -k @60:- -w "screen ready: rename" \
  -k @8:A -k @12:- -k @24:UP -k @28:- -k @36:UP -k @40:- -k @48:RIGHT -k @52:- -k @60:RIGHT -k @64:- \
  -k @72:RIGHT -k @76:- -k @88:A -k @92:- -k @104:UP -k @108:- -k @120:A -k @124:- -k @136:START -k @140:- \
  -w "screen ready: cheats"
grep -q "cheats: guide of cheat 1 set: ok" "$cart/guide-edit.txt" ||
  fail "the guide wasn't set: $(grep -a 'cheats:' "$cart/guide-edit.txt" | tail -3)"
cheat_file | grep -x 'cheat1_guide = "q\\nn"' >/dev/null || fail "cheat 1's guide line: $(cheat_file | grep cheat1_)"
echo "PASS: cheats: Edit guide's keyboard of lines writes the guide, its line break written escaped"
cheat_run "$cart/guide-rm.txt" -k @8:START -k @12:- -w "screen ready: cheat-menu" \
  -k @16:DOWN -k @20:- -k @28:DOWN -k @32:- -k @40:DOWN -k @44:- -k @56:A -k @60:- -w "screen ready: rename" \
  -k @8:B -k @12:- -k @20:B -k @24:- -k @32:B -k @36:- -k @48:START -k @52:- -w "screen ready: cheats"
grep -q "cheats: guide of cheat 1 removed: ok" "$cart/guide-rm.txt" ||
  fail "the guide wasn't removed: $(grep -a 'cheats:' "$cart/guide-rm.txt" | tail -3)"
! cheat_file | grep '^cheat1_guide' >/dev/null || fail "an empty guide left its line: $(cheat_file | grep cheat1_)"
[ "$(cheat_lines | wc -l)" = 5 ] || fail "the guide's edits changed the cheats: $(cheat_lines | tr '\n' ' ')"
echo "PASS: cheats: an emptied guide removes the cheat's guide line"

# The last code deleted: a file of one code of the user's goes, both copies,
# rather than staying as "cheats = 0"; cut at each step, the next power-on
# reads the file with its code, or none
cp "$cart/clean/sd.img" "$cart/clean/kernel-nor.bin" "$cart/"
rm -f "$cart/sram.bin"
printf '%s\n' 'cheats = 1' '' 'cheat0_desc = "Mine"' 'cheat0_code = "3203F090+0044"' 'cheat0_enable = true' \
  'cheat0_user = true' >"$cart/Last.cht"
mcopy -o -i "$cart/sd.img" "$cart/Last.cht" ::/torii/cheats/Hello.cht
mcopy -o -i "$cart/sd.img" "$cart/Last.cht" ::/torii/cheats/Hello.cht.bak
last_scenario() {
  # shellcheck disable=SC2046 # nav.sh's options
  cut_run "$@" $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @44:A -k @48:- \
    -w "screen ready: cheats" -k @8:START -k @12:- \
    -w "screen ready: cheat-menu" -k @16:DOWN -k @20:- -k @28:DOWN -k @32:- -k @40:DOWN -k @44:- -k @52:DOWN -k @56:- -k @64:DOWN -k @68:- -k @76:DOWN -k @80:- -k @88:A -k @92:- \
    -w "screen ready: confirm" -k @8:RIGHT -k @12:- -k @20:A -k @24:- \
    -w "cheats: deleted" -w "screen ready: cheats" -k @8:B -k @12:- -w "cheats: back" -f 6000
}
last_check() {
  local n=$1 name=$2 lines
  cut_run -f 400
  [ "$cut_status" = 0 ] || fail "last delete, cut at $n ($name): the next power-on exited with $cut_status"
  lines=$(cheat_lines | tr '\n' ' ')
  case $lines in
  "3203F090+0044|1|true| " | "") ;;
  *) fail "last delete, cut at $n ($name): the cheats read back as: $lines" ;;
  esac
  if [ "$n" = 0 ]; then
    [ -z "$lines" ] || fail "last delete, uncut: the code is still there: $lines"
    ! mdir -b -i "$cart/sd.img" ::/torii/cheats 2>/dev/null | grep Hello >/dev/null ||
      fail "the emptied cheat file is still on the card: $(mdir -b -i "$cart/sd.img" ::/torii/cheats | tr '\n' ' ')"
  fi
}
cut_sweep last_scenario last_check
echo "PASS: cheats: deleting the last code deletes its file, both copies, and a cut at each of its $cut_count steps leaves the file or none"

# A file with no number left (cheat 1023 is the table's last): + Add code
# is greyed with why from the start, and A on it (UP wraps there) opens no
# keypad
cp "$cart/clean/sd.img" "$cart/clean/kernel-nor.bin" "$cart/"
rm -f "$cart/sram.bin"
big_cht "$cart/Full.cht" 1024 100000
mcopy -o -i "$cart/sd.img" "$cart/Full.cht" "::/torii/cheats/Hello.cht"
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @44:A -k @48:- \
  -w "screen ready: cheats" -k @8:UP -k @12:- -k @24:A -k @28:- -k @60:B -k @64:- -w "cheats: back" -f 12000 \
  >"$cart/full.txt" 2>&1 || fail "the full file's screen didn't run: $(grep -a '^kernel' "$cart/full.txt" | tail -3)"
grep -q "cheats: + Add code greyed: No room: the file is full" "$cart/full.txt" ||
  fail "the full file's + Add code: $(grep -a 'cheats:' "$cart/full.txt" | head -3)"
! grep -q "screen ready: keypad" "$cart/full.txt" || fail "A on a greyed + Add code opened the keypad"
echo "PASS: cheats: a file with no number left greys + Add code up front, and A on it opens nothing"

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

# A file made by just cheats names its dump ("# for crc32 X"): when the CRC
# cache knows the ROM as another dump, the popup still has Cheats (it never
# reads the file, so no count), the cheat screen says why it is left out,
# and a launch applies none of it; when the CRC is the ROM's, the screen
# lists it and a launch runs its pick
crc=$(python3 -c 'import sys, zlib; print("%08X" % zlib.crc32(open(sys.argv[1], "rb").read()))' tests/roms/hello/hello.gba)
size=$(stat -c %s tests/roms/hello/hello.gba)
for want in DEADBEEF "$crc"; do
  cp "$cart/clean/sd.img" "$cart/clean/kernel-nor.bin" "$cart/"
  rm -f "$cart/sram.bin"
  printf '%s\t%s\t-\t/Hello.gba\n' "$crc" "$size" >"$cart/crc.txt"
  mcopy -o -i "$cart/sd.img" "$cart/crc.txt" ::/torii/crc.txt
  printf '%s\n' "# for crc32 $want" 'cheats = 1' '' 'cheat0_desc = "Dump"' 'cheat0_code = "3203F070+0033"' \
    'cheat0_enable = true' >"$cart/Dump.cht"
  mcopy -o -i "$cart/sd.img" "$cart/Dump.cht" ::/torii/cheats/Hello.cht
  if [ "$want" = "$crc" ]; then expect_line="screen ready: cheats"; else expect_line="Its cheat file is for another dump"; fi
  # Hello's popup, DOWN past Saves to Cheats, A: the screen, or its dialog
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_game_menu gba 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @44:A -k @48:- \
    -w "$expect_line" -f 1200 >"$cart/dump-popup.txt" 2>&1 ||
    fail "the cheat screen for crc32 $want didn't run: $(grep -a '^kernel' "$cart/dump-popup.txt" | tail -3)"
  grep -q "cheats: popup: -1 listed" "$cart/dump-popup.txt" ||
    fail "the popup for crc32 $want: $(grep -a 'cheats:' "$cart/dump-popup.txt" | tail -2)"
  launch_dump -m "@240:0x0203F070:1" >"$cart/dump-launch.txt" 2>&1 ||
    fail "the launch for crc32 $want didn't run: $(grep -a '^kernel' "$cart/dump-launch.txt" | tail -3)"
  got=$(sed -n 's/^mem frame [0-9]* 0203F070: //p' "$cart/dump-launch.txt" | tr -d ' \n' | tr A-F a-f)
  left="cheats: /torii/cheats/Hello.cht is for crc32 $want, and /Hello.gba is $crc: left out"
  if [ "$want" = "$crc" ]; then
    grep -q "cheats: .*Hello.cht: 1 cheats, 0 greyed, 1 picked" "$cart/dump-popup.txt" ||
      fail "the screen for its own dump: $(grep -a 'cheats:' "$cart/dump-popup.txt" | tail -2)"
    [ "$got" = 33 ] || fail "the cheat for its own dump: 0203F070 holds $got, not 33"
  else
    grep -qF "$left" "$cart/dump-popup.txt" ||
      fail "the screen read another dump's file: $(grep -a 'cheats:\|names:' "$cart/dump-popup.txt" | tail -3)"
    grep -q "dialog: Hello: Its cheat file is for another dump of the game." "$cart/dump-popup.txt" ||
      fail "the screen's dialog: $(grep -a 'dialog:' "$cart/dump-popup.txt" | tail -2)"
    ! grep -q "screen ready: cheats" "$cart/dump-popup.txt" || fail "the cheat screen opened another dump's file"
    grep -qF "$left" "$cart/dump-launch.txt" ||
      fail "the launch read another dump's file: $(grep -a 'cheats:\|names:' "$cart/dump-launch.txt" | tail -3)"
    [ "$got" != 33 ] || fail "another dump's cheat ran: 0203F070 holds 33"
  fi
done
echo "PASS: cheats: a file for another dump (# for crc32): Cheats in the popup, the screen says why it's left out, a launch applies none; its own dump's runs"
