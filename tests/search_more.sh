#!/usr/bin/env bash
# The in-game cheat search's 1.0 review (v0.9.23): what tests/search.sh's
# end-to-end runs don't reach, on the emulated cart with tests/roms/searchtest
# (lives 5, A loses one; twenty words of 1,000,000; L points the coins'
# pointer at VRAM).
#
# undo:     an 8-bit search; a life lost; Went up by mistake (which keeps
#           the frame counter beside the lives, so their chunk's snapshot is
#           written over), Undo (the snapshot back), Down by 1 and Equals 4
#           leave the lives alone; then Changed with nothing changed leaves
#           none.
# limits:   with a cheat list (the live block): a 32-bit search, Equals
#           1,000,000 leaves the twenty marks; sixteen are kept, the first
#           three on now; a fourth on now and a seventeenth keep do nothing;
#           Back to Torii files sixteen, three picked.
# midgame:  the lives kept on now with 9, held; All off stops them (and
#           files them off); a search goes on after Sleep and Reset game.
# sramfault: cart SRAM losing the IWRAM a pass borrows (Start does nothing,
#           the game goes on), then the EWRAM under the search's code (its
#           page doesn't open, the game goes on).
# ewram:    the ROM's stack top patched to 0x0203FFFC, as some games have:
#           the hook's spend area is in the searched run, and Start leaves
#           every element but its 128 bytes.
# filefail: the cheat file read-only, so the card takes nothing: the found
#           cheat waits in cart SRAM's page 0xF0 with its game's path; the
#           next boot, the file writable, files it.
# ptrclamp: a cheat through the coins' pointer, which L points at VRAM: the
#           interpreter writes through a pointer only into RAM.
# passcut:  the power cut while Start's pass runs (cart SRAM past the save
#           has no battery: the search is gone), then a power-on: the game
#           launches, its save as it was, and its search page starts anew.
#
# Runs inside the torii-emu container after `just build`, `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/pipes.sh
. tests/pipes.sh
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh

mode=${1:?usage: search_more.sh undo|limits|midgame|sramfault|ewram|filefail|ptrclamp|passcut}
local_carts "dist/cart-search-more-$mode"

fail() {
  echo "FAIL: search_more $mode: $*" >&2
  exit 1
}

cart=$carts/se
rom=tests/roms/searchtest/searchtest.gba
flow="set"
case $mode in limits | midgame | filefail | ptrclamp) flow=live ;; esac
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n SEARCH "$cart/sd.img" 65536 >/dev/null
if [ "$mode" = ewram ]; then
  # The stack top's literal after crt0's "mov r0, #0x1F; msr cpsr, r0; ldr
  # sp, [pc, #N]", as GBApatch.c's Get_spend_address finds it
  python3 - "$rom" "$cart/searchtest.gba" <<'EOF'
import struct, sys
d = bytearray(open(sys.argv[1], 'rb').read())
for i in range(0, 0x5000, 4):
    w1, w2, w3 = struct.unpack_from('<3I', d, i)
    if w1 & 0xFFFF001F == 0xE3A0001F and w2 in (0xE129F000, 0xE121F000):
        at = i + 16 + (w3 & 0xFFF) if w3 & 0x00F00000 == 0x00900000 else i + 16 - (w3 & 0xFFF)
        assert struct.unpack_from('<I', d, at)[0] == 0x03007F00
        struct.pack_into('<I', d, at, 0x0203FFFC)
        break
else:
    sys.exit('no stack literal')
open(sys.argv[2], 'wb').write(d)
EOF
  rom=$cart/searchtest.gba
fi
mcopy -i "$cart/sd.img" "$rom" ::/searchtest.gba
mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
if [ "$flow" = live ]; then
  cht=$carts/searchtest.cht
  printf '%s\n' 'cheats = 1' 'cheat0_desc = "Marker"' 'cheat0_code = "0203F000:01"' 'cheat0_enable = true' >"$cht"
  mcopy -i "$cart/sd.img" "$cht" ::/torii/cheats/searchtest.cht
fi
python3 tests/setnor.py set "$cart/kernel-nor.bin" 0=0xE1E1 1=1 2=1 3=0 4=0 5=9 6=8 7=2 8=9 9=8 10=3 11=1 12=0 13=1 14=1 25=1

boot() { OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@"; }

args=()
# keys FRAME KEY...: press/release pairs from @FRAME, one key every 8 frames
keys() {
  local frame=$1
  shift
  for key in "$@"; do
    args+=(-k "@$frame:$key" -k "@$((frame + 4)):-")
    frame=$((frame + 8))
  done
}
wait_for() { args+=(-w "$1"); }
menu() { args+=(-k "@${1:-200}:L+R+START" -k "@$((${1:-200} + 6)):-"); }
# The search page: with a cheat list its row is the list's last (UP from
# the first cheat), else the menu's (past Save state, Load state greyed)
to_search() {
  wait_for "screen ready: ingame"
  keys 20 DOWN DOWN A
  if [ "$flow" = live ]; then
    wait_for "screen ready: ingame-cheats"
    keys 10 UP A
  fi
  wait_for "screen ready: ingame-search"
}
back_out() { if [ "$flow" = live ]; then keys "$1" B B B; else keys "$1" B B; fi; }
# Back to Torii, the menu's last row (UP from Resume)
to_torii() {
  menu "$1"
  wait_for "screen ready: ingame"
  keys 20 UP A
}
start() {
  # shellcheck disable=SC2207 # nav_launch prints options
  args=($(nav_launch /searchtest.gba addon))
  wait_for "launch: starting the game"
  menu 300
}
run() {
  boot "${args[@]}" "$@" >"$cart/out" 2>"$cart/log" || true
  log=$(cat "$cart/log")
  grep -q '^crash at frame' <<<"$log" && fail "crashed: $(grep -A3 '^crash' <<<"$log" | drain_head -4)"
  return 0
}
lefts() { grep -E '^kernel: search: [0-9]+ left' <<<"$log" | awk '{ print $3 }' | tr '\n' ' '; }

case $mode in
undo)
  start
  to_search
  keys 10 A
  wait_for "search: started"
  wait_for "screen ready: ingame-search"
  # The rows: Same, Changed, Up, Down, Up by, Down by (5), Equals (6),
  # More, Less, From, Show them (10), Undo (11)...
  back_out 10
  keys 80 A
  menu
  to_search
  keys 10 DOWN DOWN A # Went up: a mistake, the lives went down
  wait_for "search: pass in"
  wait_for " left"
  keys 10 DOWN DOWN DOWN DOWN DOWN DOWN DOWN DOWN DOWN A # Undo
  wait_for "search: undone"
  keys 10 UP UP UP UP UP UP A A # Down by 1
  wait_for "search: pass in"
  wait_for " left"
  keys 10 DOWN A UP UP UP UP A # Equals 4
  wait_for "search: pass in"
  wait_for " left"
  keys 10 UP UP UP UP UP A # Changed: nothing did
  wait_for "search: pass in"
  wait_for " left"
  keys 10 B
  run -f 8000
  l=$(lefts)
  grep -q 'search: undo: snapshot back' <<<"$log" || fail "Undo didn't put the snapshot back: $(grep 'search:' <<<"$log" | tail -4)"
  [ "$(awk '{ print $(NF-1) }' <<<"$l")" = 1 ] || fail "Went down after the Undo lost the lives: left after each pass: $l"
  [ "$(awk '{ print $NF }' <<<"$l")" = 0 ] || fail "Changed with nothing changed left some: $l"
  echo "PASS: search_more undo: Went up, Undo, Went down kept the lives (left: $l)"
  ;;
limits)
  start
  to_search
  keys 10 UP A A DOWN A # 32 bits, Start
  wait_for "search: started"
  wait_for "screen ready: ingame-search"
  # Equals 1,000,000: the digit at place 3 of ten, up once
  keys 10 DOWN DOWN DOWN DOWN DOWN DOWN A LEFT LEFT LEFT LEFT LEFT LEFT UP A
  wait_for "search: pass in"
  wait_for " left"
  keys 10 DOWN DOWN DOWN DOWN A # Show them
  wait_for "screen ready: ingame-search-list"
  keys 10 A
  for i in $(seq 0 15); do
    wait_for "screen ready: ingame-search-pick"
    if [ "$i" -lt 3 ]; then
      keys 10 DOWN A # Keep it, on now
    elif [ "$i" = 3 ]; then
      keys 10 DOWN A UP A # on now greyed (three at most): just Keep it
    else
      keys 10 A
    fi
    wait_for "search: kept a cheat"
    keys 10 B
    wait_for "screen ready: ingame-search-list"
    keys 10 DOWN A # the next
  done
  wait_for "screen ready: ingame-search-pick" # a seventeenth: Keep it greyed
  keys 10 A DOWN A B B B B B
  to_torii 120
  wait_for "home ready"
  run -f 20000
  [ "$(lefts)" = "73600 20 " ] || fail "Start and Equals 1,000,000 at 32 bits left $(lefts)"
  kept=$(grep -c 'search: kept a cheat' <<<"$log")
  on=$(grep -c 'search: kept a cheat, on' <<<"$log")
  [ "$kept" = 16 ] && [ "$on" = 3 ] || fail "kept $kept, $on on now: 16 and 3 expected"
  filed=$(grep -cE 'cheats: .*found in game, 0300[0-9A-F]{4}:000F4240 (on|off): added' <<<"$log")
  filed_on=$(grep -cE 'cheats: .*found in game, 0300[0-9A-F]{4}:000F4240 on: added' <<<"$log")
  [ "$filed" = 16 ] && [ "$filed_on" = 3 ] || fail "filed $filed ($filed_on on): $(grep 'found in game' <<<"$log" | tail -3)"
  file=$(mktemp)
  mtype -i "$cart/sd.img" ::/torii/cheats/searchtest.cht | tr -d '\r' >"$file"
  [ "$(grep -c ':000F4240"$' "$file")" = 16 ] || fail "the file hasn't the 16: $(cat "$file")"
  rm -f "$file"
  echo "PASS: search_more limits: a 32-bit search kept 16 of 20 (3 on now, a 4th on now and a 17th refused), all filed"
  ;;
midgame)
  start
  to_search
  keys 10 A
  wait_for "search: started"
  wait_for "screen ready: ingame-search"
  keys 10 DOWN DOWN DOWN DOWN DOWN DOWN A UP UP UP UP UP A # Equals 5
  wait_for "search: pass in"
  wait_for " left"
  back_out 10
  keys 80 A
  menu
  to_search
  keys 10 DOWN DOWN DOWN DOWN DOWN A A # Down by 1
  wait_for "search: pass in"
  wait_for " left"
  keys 10 DOWN DOWN DOWN DOWN DOWN A # Show them
  wait_for "screen ready: ingame-search-list"
  keys 10 A
  wait_for "screen ready: ingame-search-pick"
  keys 10 UP A UP UP UP UP UP A DOWN DOWN A # Value 9, Keep it, on now
  wait_for "search: kept a cheat, on"
  keys 10 B B B # to the cheat list, on Cheat search
  wait_for "screen ready: ingame-cheats"
  keys 10 UP UP A # All off
  keys 30 B B
  keys 80 A
  keys 120 A
  # Sleep: the menu's Sleep row (Resume, Save, Cheats, Screenshot, Reset,
  # Sleep), woken by START+SELECT
  menu 260
  wait_for "screen ready: ingame"
  keys 20 DOWN DOWN DOWN DOWN DOWN A
  args+=(-k @200:START+SELECT -k @206:-)
  menu 300
  to_search
  keys 10 DOWN A # Changed, after the sleep: the lives are 7, 4 at the last pass
  wait_for "search: pass in"
  wait_for " left"
  back_out 10
  menu 80
  wait_for "screen ready: ingame"
  keys 20 DOWN DOWN DOWN DOWN A # Reset game
  menu 300
  to_search
  keys 10 DOWN A # Changed, after the reset: the lives are 5 again
  wait_for "search: pass in"
  wait_for " left"
  back_out 10
  to_torii 60
  wait_for "found in game"
  run -f 16000
  after=$(sed -n '/search: kept a cheat, on/,/stop at frame/p' <<<"$log" | sed -n 's/^kernel: searchtest: lives \([0-9]*\).*/\1/p' | uniq | drain_head -3 | tr '\n' ' ')
  [ "$after" = "9 8 7 " ] || fail "the lives after All off: $after (9 then 8 and 7: no longer held)"
  if ! grep -q '^stop at frame' <<<"$log" || ! grep -q '^wake at frame' <<<"$log"; then
    fail "Sleep didn't sleep and wake"
  fi
  sed -n '/^wake at frame/,$p' <<<"$log" | drain_grep -q 'searchtest: lives 5' || fail "Reset game didn't start the game again: $(grep -E 'screen ready|search:|^wake|^timed|lives' <<<"$log" | uniq | tail -8 | tr '\n' '|')"
  [ "$(lefts)" = "294400 1 1 1 1 " ] || fail "the search didn't go on after Sleep and Reset game: $(lefts)"
  grep -qE 'found in game, 0300[0-9A-F]{4}:09 off: added' <<<"$log" || fail "not filed off after All off: $(grep 'found in game' <<<"$log")"
  echo "PASS: search_more midgame: All off stopped the lives held at 9 (then $after), the search went on after Sleep and Reset game, filed off"
  ;;
sramfault)
  start
  to_search
  keys 10 A # Start: its pass's IWRAM backup doesn't read back
  keys 80 B B
  keys 100 A
  args+=(-f 3000)
  OMEGA_GAME_SRAM=drop:2C800-2C8FF run
  grep -q 'search: IWRAM backup failed' <<<"$log" || fail "a pass ran without its IWRAM backup: $(grep 'search' <<<"$log" | tail -3)"
  grep -q 'search: started' <<<"$log" && fail "the search started without its IWRAM backup"
  tail -n 40 <<<"$log" | drain_grep -q 'searchtest: lives 4' || fail "the game didn't go on: $(grep lives <<<"$log" | tail -1)"
  # The search's code: the game's EWRAM under it doesn't read back
  start
  wait_for "screen ready: ingame"
  keys 20 DOWN DOWN A
  keys 80 B
  keys 100 A
  args+=(-f 3000)
  OMEGA_GAME_SRAM=drop:25000-25003 run
  grep -q "the extension didn't load" <<<"$log" || fail "the search's code loaded over EWRAM that didn't back up"
  grep -q 'screen ready: ingame-search' <<<"$log" && fail "the search opened without its EWRAM backup"
  tail -n 40 <<<"$log" | drain_grep -q 'searchtest: lives 4' || fail "the game didn't go on after the refused search"
  echo "PASS: search_more sramfault: a pass and the search's code refused when cart SRAM didn't keep the game's memory; the game went on"
  ;;
ewram)
  start
  to_search
  keys 10 A
  wait_for "search: started"
  keys 10 B B
  keys 100 A
  run -f 3000
  grep -q 'stack literal 0203FF7C' <<<"$log" || fail "the stack top isn't EWRAM's: $(grep 'stack literal' <<<"$log")"
  [ "$(lefts)" = "294272 " ] || fail "Start left $(lefts)(294,400 less the spend area's 128 expected)"
  tail -n 40 <<<"$log" | drain_grep -q 'searchtest: lives 4' || fail "the game didn't go on"
  echo "PASS: search_more ewram: with the stack top in EWRAM, the hook's spend area isn't searched"
  ;;
filefail)
  mattrib -i "$cart/sd.img" +r ::/torii/cheats/searchtest.cht
  start
  to_search
  keys 10 A
  wait_for "search: started"
  wait_for "screen ready: ingame-search"
  keys 10 DOWN DOWN DOWN DOWN DOWN DOWN A UP UP UP UP UP A # Equals 5: the lives
  wait_for "search: pass in"
  wait_for " left"
  keys 10 DOWN DOWN DOWN DOWN A # Show them
  wait_for "screen ready: ingame-search-list"
  keys 10 A
  wait_for "screen ready: ingame-search-pick"
  keys 10 A # Keep it
  wait_for "search: kept a cheat"
  keys 10 B B B B B
  to_torii 60
  wait_for "wasn't saved: the card didn't take them"
  run -f 12000
  grep -q "the found cheats weren't written" <<<"$log" || fail "the read-only file took them: $(grep 'cheats:' <<<"$log" | tail -3)"
  # The next boot, the file writable: the wait area's cheat filed
  mattrib -i "$cart/sd.img" -r ::/torii/cheats/searchtest.cht
  args=(-w "found in game") # a boot as after a soft reset: cart SRAM kept (OMEGA_SOFT_RESET)
  OMEGA_SOFT_RESET=1 run -f 3000
  addr=$(sed -n 's/^kernel: cheats: .*found in game, \([0-9A-F]*\):05 off: added$/\1/p' <<<"$log")
  [ -n "$addr" ] || fail "the next boot didn't file the waiting cheat: $(grep 'cheats:' <<<"$log" | tail -3)"
  mtype -i "$cart/sd.img" ::/torii/cheats/searchtest.cht | tr -d '\r' | drain_grep -q "\"$addr:05\"" || fail "the file hasn't $addr:05"
  echo "PASS: search_more filefail: the card refused the found cheat; it waited and the next boot filed $addr:05"
  ;;
ptrclamp)
  # The coins' pointer: the IWRAM word that holds 0x02010000
  # shellcheck disable=SC2207 # nav_launch prints options
  args=($(nav_launch /searchtest.gba addon))
  run -f 600 -m 590:0x03000000:0x200
  ptr=$(awk '$1 == "mem" { for (i = 5; i <= NF; i++) print $4, i - 5, $i }' "$cart/out" | python3 -c '
import sys
b = {}
for line in sys.stdin:
    at, i, v = line.split()
    b[int(at.rstrip(":"), 16) + int(i)] = int(v, 16)
for a in sorted(b):
    if a % 4 == 0 and all(a + k in b for k in range(4)) and [b[a + k] for k in range(4)] == [0, 0, 1, 2]:
        print("%08X" % a)
        break
')
  [ -n "$ptr" ] || fail "no pointer to the coins in IWRAM: $(head -3 "$cart/out")"
  printf '%s\n' 'cheats = 1' 'cheat0_desc = "Coins"' "cheat0_code = \"[$ptr]+24:0999\"" 'cheat0_enable = true' 'cheat0_format = "Torii"' >"$cht"
  mcopy -o -i "$cart/sd.img" "$cht" ::/torii/cheats/searchtest.cht
  # shellcheck disable=SC2207 # nav_launch prints options
  args=($(nav_launch /searchtest.gba addon))
  wait_for "launch: starting the game"
  args+=(-k @200:L -k @206:-)
  run -f 1200 -m 1100:0x06000000:2 -m 1190:0x06000000:2
  grep -q 'searchtest: lives [0-9]* coins 0999' <<<"$log" || fail "the cheat didn't run through its pointer: $(grep coins <<<"$log" | tail -1)"
  px=$(awk '$1 == "mem" { print $5 $6 }' "$cart/out" | sort -u | tr '\n' ' ')
  [ "$px" = "C250 " ] || fail "VRAM's first pixel after L: $px (the background, C250, expected: the cheat wrote through the pointer)"
  echo "PASS: search_more ptrclamp: [$ptr]+24 wrote the coins, and nothing once its word pointed at VRAM"
  ;;
passcut)
  start
  to_search
  keys 10 A # Start: its pass runs some 45 frames
  args+=(-k @30:- -k @31:-)
  run
  grep -q 'search: started' <<<"$log" && fail "the pass ended before the cut"
  save=$(od -A n -t x1 -N 65536 "$cart/sram.bin" | md5sum)
  # Power-on (cart SRAM past the save reads 0xFF): the game, and its search page
  start
  wait_for "screen ready: ingame"
  keys 20 DOWN DOWN A
  wait_for "screen ready: ingame-search"
  keys 10 B B
  run -f 3000
  grep -q 'screen ready: ingame-search' <<<"$log" || fail "the search page didn't open after the cut"
  grep -q 'search: [0-9]* left' <<<"$log" && fail "a search survived the cut"
  [ "$(od -A n -t x1 -N 65536 "$cart/sram.bin" | md5sum)" = "$save" ] || fail "the game's save changed"
  tail -n 40 <<<"$log" | drain_grep -q 'searchtest: lives 5' || fail "the game didn't run after the cut"
  echo "PASS: search_more passcut: a cut during a pass left the game, its save and a new search's page as they should be"
  ;;
*) fail "no such mode" ;;
esac
