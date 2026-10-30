#!/usr/bin/env bash
# In-game menu test on the emulated cart (v0.8's overlay,
# docs/design/v0.8/ingame-menu.md): save once with the hello test ROM, then
# launch it with BOOT WITH ADDON (SAVESTATE and RESET on) and a FLASH 64K save
# chip, open the in-game menu with L+R+START and leave it with B
# (tests/hook_watch.lua). The kernel must hook the ROM's VBlank IRQ; the menu
# must show within 4 frames (its text within 5), keep the game's EWRAM in
# cart SRAM page 0x20, and hand back the game's memory, registers and screen
# as they were; the game must go on, and loading the save-state buffer into
# cart SRAM must leave the game's save alone. Then each of the menu's rows:
# a state saved and loaded, the cheats turned off, Reset game, Sleep and Back
# to Torii. Runs inside the torii-emu container.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-menu${1:+-$1}"

cart=$carts/menu
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n MENU "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba

boot() {
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@"
}

fail() {
  echo "FAIL: $*" >&2
  [ -z "${log:-}" ] || grep -E '^(script|crash)' <<<"$log" | grep -v ' vblank ' | tail -20 >&2
  exit 1
}

# Power-on 1 and 2: CLEAN BOOT writes the first save, then the kernel copies
# it to SAVER/ (as in tests/smoke.sh).
# shellcheck disable=SC2046 # nav.sh's options
boot $(nav_launch /Hello.gba at=160) -f 400 >/dev/null 2>&1
boot -k 100:A -k 106:- -f 300 >/dev/null 2>&1
[ "$(mtype -i "$cart/sd.img" ::/torii/saves/Hello.gba/Main.sav | head -c 5 | od -A n -t x1 | tr -d ' \n')" = 4f4d525801 ] ||
  fail "no first save to start from"

# Settings slots (source/ez_define.h) in the kernel NOR (tests/setnor.py): English,
# RESET and SAVESTATE on, sleep key L+R+SELECT, menu key L+R+START, fast patch
# engine, clock and rumble on.
settings() {
  python3 tests/setnor.py set "$cart/kernel-nor.bin" "$@"
}
settings 0=0xE1E1 1=1 2=1 3=0 4=0 5=9 6=8 7=2 8=9 9=8 10=3 11=1 12=0 13=1 25=1
for copy in state blend extra reset sleep torii cheats; do
  rm -rf "$cart-$copy"
  cp -r "$cart" "$cart-$copy" # for the runs below
done

# keys FRAME KEY... -> mgba-shot press/release pairs, one key every 8 frames.
keys() {
  local frame=$1
  shift
  for key in "$@"; do
    printf -- '-k %d:%s -k %d:- ' "$frame" "$key" $((frame + 4))
    frame=$((frame + 8))
  done
}

# Power-on 3: BOOT WITH ADDON with SAVE TYPE FLASH 64K. A blue background
# pixel, dumped every 10 frames, shows the menu gave the screen back; the
# screen's hash every frame. hook_watch.lua compares the game's memory and
# registers before and after the menu, and writes both to $dumps.
pixel=$((0x06000000 + (80 * 240 + 120) * 2))
memdumps=$(for f in $(seq 10 10 4000); do printf -- '-m %d:0x%x:2 ' "$f" "$pixel"; done)
dumps=$cart/dumps
mkdir -p "$dumps"
# Memory dumps and screen hashes go to stdout, the script's lines to stderr.
# shellcheck disable=SC2046,SC2086 # $(nav_launch) and $memdumps are lists of options
out=$(HOOK_WATCH_DUMP=$dumps boot $(nav_launch /Hello.gba addon save=4 at=270) \
  -L tests/hook_watch.lua -u 'hook watch done' -f 4000 $memdumps -H 1:4000 2>"$cart/log")
log=$(cat "$cart/log")

grep -q '^crash at frame' <<<"$log" && fail "crashed"
grep -q '^script: hook [0-9]* patch ' <<<"$log" || fail "the kernel did not hook the game"
key=$(sed -n 's/^script: hook \([0-9]*\) key L+R+START$/\1/p' <<<"$log" | head -1)
opened=$(sed -n 's/^script: hook \([0-9]*\) menu open$/\1/p' <<<"$log" | head -1)
[ -n "$opened" ] || fail "the menu did not open"
closed=$(awk -v o="$opened" '$1 == "script:" && $4 == "menu" && $5 == "closed" && $3 > o { print $3; exit }' <<<"$log")
[ -n "$closed" ] || fail "B did not close the menu"
grep -q '^script: hook watch done' <<<"$log" || fail "the run ended early"

# How soon it shows (the frame after the key's is the first the screen can
# change in): the panel within 4 frames, its text within 5. The stub copies
# at WS0 3/1 and SRAM 8 cycles and reads its backups back first (v0.8's
# black screen on the real cart): hello runs at the BIOS's wait states
shown_in=$(sed -n 's/^script: hook [0-9]* menu shown after \([0-9]*\) frames$/\1/p' <<<"$log")
text=$(sed -n 's/^script: hook [0-9]* menu text shown after \([0-9]*\) frames$/\1/p' <<<"$log")
[ -n "$shown_in" ] && [ "$shown_in" -le 4 ] || fail "the menu took ${shown_in:-?} frames to show (4 at most)"
[ -n "$text" ] && [ "$text" -le 5 ] || fail "the menu's text took ${text:-?} frames to show (5 at most)"
echo "in-game menu: shown after $shown_in frames, its text after $text"

# The game as it was: memory (but the hook's own words), the readable
# registers, and the screen: the frame after the key's (the game frozen, the
# menu not drawn yet) against the one after the menu's last
grep -q '^script: hook [0-9]* menu left the game as it was$' <<<"$log" ||
  fail "the menu didn't leave the game as it was: $(grep -o 'menu changed .*' <<<"$log")"
hash_at() { awk -v f="$1" '$1 == "hash" && $3 == f ":" { print $4 }' <<<"$out"; }
[ -n "$(hash_at $((key + 1)))" ] && [ "$(hash_at $((key + 1)))" = "$(hash_at $((closed + 1)))" ] ||
  fail "the screen after the menu isn't the one before it (frames $((key + 1)) and $((closed + 1)))"

# The background is back, and the marker still moves.
after=$(awk -v c="$closed" '$1 == "mem" && $3 > c + 10 { print $5 $6; exit }' <<<"$out")
[ "$after" = C250 ] || fail "screen not restored after the menu (pixel $after, want C250)"
screens=$(awk -v c="$closed" '$1 == "hash" && $3 + 0 > c + 10 { print $4 }' <<<"$out" | sort -u | wc -l)
[ "$screens" -gt 1 ] || fail "the game did not go on after the menu"

# The menu kept the game's EWRAM it borrows in cart SRAM from page 0x20
# (0x20000), past the save memory, not in the game's save chip.
window=$(sed -n 's/^kernel: launch: in-game menu in cart SRAM: .* into a \([0-9]*\)-byte EWRAM window;.*$/\1/p' <<<"$log")
[ -n "$window" ] || fail "the kernel didn't put the menu in cart SRAM"
cmp -s <(head -c "$window" "$dumps/before-EWRAM.bin") <(tail -c +$((0x20000 + 1)) "$cart/sram.bin" | head -c "$window") ||
  fail "the game's EWRAM isn't in cart SRAM page 0x20"

# The save came back intact through the save-state buffer load. (The flash
# chip ignores the ROM's plain SRAM writes, so this boot isn't counted.)
od -A n -t x1 -N 5 "$cart/sram.bin" | tr -d ' \n' | grep -q '4f4d525801' ||
  fail "the game's save did not survive the addon launch (SRAM $(od -A n -t x1 -N 5 "$cart/sram.bin"))"

# run COPY KEYS... [-- MGBA-SHOT OPTIONS]: the same launch on a copy of the
# cart from before power-on 3, the menu key at the main run's frame, then
# KEYS in the menu; its output in $out and $log
run() {
  local copy=$1
  shift
  local menu=()
  while [ $# -gt 0 ] && [ "$1" != -- ]; do
    menu+=("$1")
    shift
  done
  [ $# -gt 0 ] && shift
  # shellcheck disable=SC2046 # option lists
  out=$(OMEGA_DIR=$cart-$copy OMEGA_TRACE=1 emu/build-linux/mgba-shot dist/ezkernelnew.bin \
    $(nav_launch /Hello.gba addon save=4 at=270) -k "$key:L+R+START" -k "$((key + 6)):-" \
    $(keys $((key + 40)) "${menu[@]}") "$@" 2>"$cart/log")
  log=$(cat "$cart/log")
  grep -q '^crash at frame' <<<"$log" && fail "crashed in the $copy run"
  return 0
}
# The rows, the cursor on Resume: Save state, Load state (greyed until a
# state is saved, and skipped), Cheat search (hello has no cheat list:
# tests/search.sh), Screenshot (tests/screenshot.sh), Reset
# game, Sleep, Back to Torii (the last: UP from Resume)

# A save state round trip: SAVE (down one, A: its slots, A: slot 1), and later
# LOAD (down two, A: its slots, on slot 1, A).
# The state holds the menu's IME = 0, so loading must turn IME back on, or
# the game stops at its next VBlankIntrWait.
load=$((key + 300))
hashes=$(for f in $(seq $((load + 100)) 20 $((load + 400))); do printf -- '-h %d ' "$f"; done)
# shellcheck disable=SC2046,SC2086 # $(keys) and $hashes are lists of options
run state DOWN A A -- -k "$load:L+R+START" -k "$((load + 6)):-" $(keys $((load + 40)) DOWN DOWN A A) \
  -m $((load + 400)):0x04000208:2 $hashes -f $((load + 400))
[ "$(grep -cx 'kernel: screen ready: ingame' <<<"$log")" = 2 ] || fail "the menu didn't open twice in the save state run"
[ "$(awk '$1 == "mem" { print $5 $6 }' <<<"$out")" = 0100 ] || fail "IME is off after loading a state"
[ "$(awk '$1 == "hash" { print $4 }' <<<"$out" | sort -u | wc -l)" -gt 1 ] ||
  fail "the game did not go on after loading a state"
[ "$(od -A n -c -j $((0xAFFD0)) -N 16 "$cart-state/sram.bin" | tr -d ' \n')" = TORII-STATE-OK1. ] ||
  fail "no state's mark (INGAME_STATE_MARK) in cart SRAM after SAVE"
# Never EZ-Flash's flag, which the FPGA's write-back watches
[ "$(od -A n -t x1 -j $((0xAFFF0)) -N 16 "$cart-state/sram.bin" | tr -d ' \n')" != 455a2d4f6d65676152544346494c452e ] ||
  fail "EZ-Flash's write-back flag in cart SRAM after SAVE"

# Brightness: hello dims its screen once with BLDY, which is write-only, so
# the menu must leave it alone and only blend over it. A background pixel
# must look the same before the menu and after it.
pixel() { python3 -c "import sys; sys.path.insert(0, 'tests'); import games; print(games.png_pixel(games.Path('$1'), 120, 80))"; }
run blend B -- -s "$((key - 20)):$cart-blend/before.png" -s "$((key + 200)):$cart-blend/after.png" -f $((key + 200))
before=$(pixel "$cart-blend/before.png")
after=$(pixel "$cart-blend/after.png")
[ "$before" != 1031a5 ] || fail "hello's screen isn't dimmed, so the brightness check can't tell"
[ "$after" = "$before" ] || fail "the menu lost the game's brightness (pixel $after, was $before)"

# The menu key with another key held too (a stuck or laggy button on the
# real cart, in v0.2.1): the hook matches the menu key's bits, so the menu
# still opens.
# shellcheck disable=SC2046 # $(nav_launch) is a list of options
OMEGA_DIR=$cart-extra emu/build-linux/mgba-shot dist/ezkernelnew.bin \
  $(nav_launch /Hello.gba addon save=4 at=270) \
  -k "$key:L+R+START+DOWN" -k "$((key + 6)):-" -k "$((key + 60)):B" -k "$((key + 66)):-" \
  -f $((key + 120)) >/dev/null 2>"$cart/log"
log=$(cat "$cart/log")
grep -q '^crash at frame' <<<"$log" && fail "crashed in the extra-key run"
grep -q 'screen ready: ingame' <<<"$log" || fail "L+R+START with DOWN held didn't open the menu"

# Reset game: the game starts again from its ROM, without the kernel, and runs
run reset DOWN DOWN DOWN DOWN A -- -h $((key + 400)) -h $((key + 420)) -f $((key + 420))
[ "$(grep -c '^omega [0-9]*: game started' <<<"$log")" = 2 ] || fail "Reset game didn't start the game again"
! sed -n '/^omega [0-9]*: game started/,$p' <<<"$log" | grep -q '^omega [0-9]*: mode kernel' ||
  fail "Reset game went back to the kernel"
[ "$(awk '$1 == "hash" { print $4 }' <<<"$out" | sort -u | wc -l)" = 2 ] || fail "the game didn't run after Reset game"

# Sleep: the BIOS's Stop, which only START+SELECT ends (the emulated cart
# sleeps as the real one does; tests/sleep.sh has the sleep keys), then the
# game again
run sleep DOWN DOWN DOWN DOWN DOWN A -- -k $((key + 300)):START+SELECT -k $((key + 306)):- \
  -h $((key + 400)) -h $((key + 420)) -f $((key + 420))
grep -q '^stop at frame' <<<"$log" || fail "Sleep didn't stop the CPU"
woke=$(sed -n 's/^wake at frame \([0-9]*\)$/\1/p' <<<"$log")
[ -n "$woke" ] && [ "$woke" -ge $((key + 300)) ] || fail "Sleep woke at ${woke:-no frame}, not on START+SELECT"
[ "$(awk '$1 == "hash" { print $4 }' <<<"$out" | sort -u | wc -l)" = 2 ] || fail "the game didn't go on after Sleep"

# Back to Torii: the kernel's ROM page and a reset into it
run torii UP A -- -f $((key + 300))
sed -n '/^omega [0-9]*: game started/,$p' <<<"$log" | grep -q '^omega [0-9]*: mode kernel' ||
  fail "Back to Torii didn't go back to the kernel"

# Cheats: hello with a cheat picked (its green square's byte) has the
# Cheats row after Load state, which opens the cheat list on its cheat; A
# turns it off (bit 0 of the toggles at 0x03007FE8 the interpreter reads, 1),
# and in the menu opened again, on (0). tests/cheats_ingame.sh has the rest.
printf 'cheats = 1\n\ncheat0_desc = "Green"\ncheat0_code = "02001236:58"\ncheat0_enable = true\n' >"$cart/Hello.cht"
mmd -i "$cart-cheats/sd.img" ::/torii/cheats 2>/dev/null || true
mcopy -o -i "$cart-cheats/sd.img" "$cart/Hello.cht" ::/torii/cheats/Hello.cht
# shellcheck disable=SC2046 # $(keys) is a list of options
run cheats DOWN DOWN A A B B -- -m $((key + 120)):0x03007FE8:4 -k $((key + 200)):L+R+START -k $((key + 206)):- \
  $(keys $((key + 240)) DOWN DOWN A A B B) -m $((key + 320)):0x03007FE8:4 -f $((key + 320))
grep -q 'screen ready: ingame-cheats' <<<"$log" || fail "the Cheats row didn't open the cheat list"
[ "$(awk '$1 == "mem" && $3 == '$((key + 120))' { print $5 $6 $7 $8 }' <<<"$out")" = 01000000 ] ||
  fail "the cheat list didn't turn the cheat off ($(grep '^mem' <<<"$out" | head -1))"
[ "$(awk '$1 == "mem" && $3 == '$((key + 320))' { print $5 $6 $7 $8 }' <<<"$out")" = 00000000 ] ||
  fail "the cheat list didn't turn the cheat on again"

rm -rf "$cart"-{state,blend,extra,reset,sleep,torii,cheats}
echo "PASS: the in-game menu opens on L+R+START (and with an extra key held) within $shown_in frames, B gives the game back as it was, and its rows save and load a state, turn the cheats off and on, reset the game, sleep (START+SELECT wakes it) and go back to Torii"
