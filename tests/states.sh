#!/usr/bin/env bash
# Save states with slots (v0.8 P3, source/statestore.c, the in-game menu's
# Save state and Load state pages) on the emulated cart, with a test ROM
# (the hello ROM, or the one given) and Save states on:
#
#   roundtrip  Save state into slot 1, play, Load state: 60 frames after the
#              load are the 60 after the save, frame for frame. Logs how
#              many frames SAVE and LOAD took.
#   keep       Save state into slot 2, Back to Torii: the next boot files it
#              as /torii/states/<rom>/Main/2.state; the next launch puts it
#              in cart SRAM, and Load state there gives the frames after the
#              save again. An old /RTS state becomes slot 1, once.
#   popup      The game popup's Load state lists the slots and starts the
#              game in the one picked: the frames after the save again.
#   unreadable The popup's Load state on a slot whose data doesn't read
#              asks first: B goes back, A starts the game from its start.
#   cut        The filing at boot cut at each cutpoint (tests/cutsweep.sh):
#              the next launch finds slot 2 whole or not at all, never broken.
#
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and `just test-roms`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
# shellcheck source=tests/cutsweep.sh
. tests/cutsweep.sh

mode=${1:?usage: states.sh roundtrip|keep|popup|unreadable|cut [ROM]}
rom=${2:-tests/roms/hello/hello.gba}
local_carts "dist/cart-states-$mode${2:+-$(basename "$2" .gba)}"

fail() {
  echo "FAIL: states $mode: $*" >&2
  exit 1
}

cart=$carts/st
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n STATES "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" "$rom" ::/Game.gba
# Settings slots (source/ez_define.h), as tests/menu.sh: RESET and SAVESTATE
# on, menu key L+R+START; the save copied at power-on without asking
python3 tests/setnor.py set "$cart/kernel-nor.bin" 0=0xE1E1 1=1 2=1 3=0 4=0 5=9 6=8 7=2 8=9 9=8 10=3 11=1 12=0 13=1 14=1 25=1

# The clock: 27 Oct 2026 14:03 UTC
export TZ=UTC
clock=1793109780

boot() { OMEGA_DIR=$cart OMEGA_TRACE=1 emu/build-linux/mgba-shot dist/ezkernelnew.bin -c $clock "$@"; }

# keys FRAME KEY...: -k press/release pairs from @FRAME, one key every 8 frames
keys() {
  local frame=$1
  shift
  for key in "$@"; do
    printf -- '-k @%d:%s -k @%d:- ' "$frame" "$key" $((frame + 4))
    frame=$((frame + 8))
  done
}

# The menu at @400 and @900 from "launch: starting the game"; the screen
# hashed every frame from 60 frames after each (-H)
save=400
load=900
hashed() { printf -- '-H @%d:@%d ' $(($1 + 60)) $(($1 + 400)); }

# hashes OUT N: the Nth run of -H hashes in OUT (mgba-shot's stdout), one a line
hashes() { awk -v n="$2" '$1 == "hash" { f = $3 + 0; if (f != last + 1) k++; last = f; if (k == n) print $4 }' <<<"$1"; }

# matching A B: "RUN DISTINCT": the longest run of frames of hash list B
# equal to frames of A, at any offset, and how many different screens it has
matching() {
  python3 - "$1" "$2" <<'PY'
import sys
a, b = sys.argv[1].split(), sys.argv[2].split()
best = (0, 0)
for off in range(-len(b) + 1, len(a)):
    run = 0
    for i in range(len(b)):
        j = i + off
        if 0 <= j < len(a) and a[j] == b[i]:
            run += 1
            if run > best[0]:
                best = (run, i)
        else:
            run = 0
run, end = best
print(run, len(set(b[end - run + 1 : end + 1])))
PY
}

# same A B WHAT: 60 or more frames of B are A's, and the game runs in them
same() {
  local run distinct
  read -r run distinct <<<"$(matching "$1" "$2")"
  [ -z "${STATES_DEBUG:-}" ] || paste <(echo "$1") <(echo "$2") >&2
  [ "$run" -ge 60 ] || fail "$3: only $run frames match the frames after the save (60 wanted)"
  [ "$distinct" -gt 1 ] || fail "$3: the matching frames are one still picture: the game didn't run"
  echo "$run"
}

# The frames the stub spent from the buffer's first page (0x40) to its
# page set back to 0, after frame FROM (OMEGA_TRACE's register lines)
stub_frames() {
  awk -v from="$1" '$1 == "omega" && $3 == "register" && $4 == "09C00000" {
      f = $2 + 0
      if (f < from) next
      if (!start && $6 == "0040") start = f
      if (start && $6 == "0000") { print f - start; exit }
    }' <<<"$2"
}

# Save state into SLOT_DOWNS + 1 (DOWN on the Save page that many times),
# then Back to Torii (UP from Resume: the last row); the frames
# after the save hashed
save_and_quit() {
  local downs=() i
  for ((i = 0; i < $1; i++)); do downs+=(DOWN); done
  shift
  # shellcheck disable=SC2046 # option lists
  "$@" $(nav_launch /Game.gba addon) -w "launch: starting the game" \
    -k @$save:L+R+START -k @$((save + 6)):- $(keys $((save + 40)) DOWN A "${downs[@]}" A) $(hashed $save) \
    -k @$load:L+R+START -k @$((load + 6)):- $(keys $((load + 40)) UP A) \
    -w "in-game: back from /Game.gba" -f 9000 -u "home ready"
}

case $mode in
roundtrip)
  # shellcheck disable=SC2046 # option lists
  out=$(boot $(nav_launch /Game.gba addon) -w "launch: starting the game" \
    -k @$save:L+R+START -k @$((save + 6)):- $(keys $((save + 40)) DOWN A A) $(hashed $save) \
    -k @$load:L+R+START -k @$((load + 6)):- $(keys $((load + 40)) DOWN DOWN A A) $(hashed $load) 2>"$cart/log")
  log=$(cat "$cart/log")
  grep -q '^crash at frame' <<<"$log" && fail "crashed"
  grep -q 'screen ready: ingame-save' <<<"$log" || fail "no Save state page"
  grep -q 'screen ready: ingame-load' <<<"$log" || fail "no Load state page"
  run=$(same "$(hashes "$out" 1)" "$(hashes "$out" 2)" "after Load state")
  start=$(awk '$1 == "omega" && $3 == "game" && $4 == "started" { print $2 + 0; exit }' <<<"$log")
  echo "states: SAVE took $(stub_frames $((start + save)) "$log") frames, LOAD $(stub_frames $((start + load)) "$log")"
  echo "PASS: states roundtrip: $run frames after Load state are those after Save state ($(basename "$rom"))"
  ;;
keep)
  out=$(save_and_quit 1 boot 2>"$cart/log")
  log=$(cat "$cart/log")
  grep -q '^crash at frame' <<<"$log" && fail "crashed"
  grep -q 'states: /torii/states/Game.gba/Main/2.state: [0-9]* bytes of 458752 packed, written' <<<"$log" ||
    fail "slot 2 wasn't filed: $(grep -E 'states|in-game' <<<"$log" | tail -5)"
  saved=$(hashes "$out" 1)
  packed=$(sed -n 's/.*2.state: \([0-9]*\) bytes of 458752 packed.*/\1/p' <<<"$log")
  kept=$(sed -n 's/^kernel: states: kept slot 2 in \([0-9]*\) frames$/\1/p' <<<"$log")
  # The next launch: slot 2 is the newest, in cart SRAM: Load state at
  # once (DOWN DOWN A: its page on slot 2, A) brings the saved frames back.
  # The state preloaded and the menu opened over it (v0.8 on the real cart:
  # Pokémon Emerald launched into a state refused the menu, EZ-Flash's flag
  # set in the buffer and the FPGA's write-back, it seems, under the stub's
  # reads). Torii marks a state its own way and never sets that flag, so
  # nothing is written back, even where kernel writes to the buffer would
  # start it too (OMEGA_RTS_KERNEL_WRITES), and the menu opens
  # shellcheck disable=SC2046 # option lists
  out=$(OMEGA_RTS_KERNEL_WRITES=1 boot $(nav_launch /Game.gba addon) -w "launch: starting the game" \
    -k @$save:L+R+START -k @$((save + 6)):- $(keys $((save + 40)) DOWN DOWN A A) $(hashed $save) \
    -f 9000 2>"$cart/log") || fail "the run stopped with status $?: $(grep -m2 'omega: ' "$cart/log")"
  log=$(cat "$cart/log")
  ! grep -q '^omega: state buffer written back' <<<"$log" ||
    fail "the state buffer was written back: EZ-Flash's flag was set ($(grep -m1 'written back' <<<"$log"))"
  grep -q 'screen ready: ingame-load' <<<"$log" || fail "the menu didn't open over the preloaded state"
  grep -q "in-game menu: the stub didn't open it" <<<"$log" && fail "the stub refused the menu over the preloaded state"
  grep -q 'states: slot 2 in cart SRAM from /torii/states/Game.gba/Main/2.state' <<<"$log" ||
    fail "the next launch didn't put slot 2 in cart SRAM: $(grep 'states' <<<"$log" | tail -3)"
  back=$(sed -n 's/^kernel: states: put back in \([0-9]*\) frames$/\1/p' <<<"$log")
  same "$saved" "$(hashes "$out" 1)" "after the next launch's Load state" >/dev/null
  echo "states: slot 2 packed to $packed bytes of 458752, filed in ${kept:-?} frames, put back in ${back:-?}"
  echo "PASS: states keep: a state saved in game is filed at the next boot, and loads after the next launch"

  # The launch after, as from Back to Torii (a soft reset keeps the cart RAM
  # without a battery): slot 2 is in cart SRAM as its file has it (the tag's
  # data_crc), so it isn't unpacked again, and Load state still brings the
  # saved frames back
  # shellcheck disable=SC2046 # option lists
  out=$(OMEGA_SOFT_RESET=1 boot $(nav_launch /Game.gba addon) -w "launch: starting the game" \
    -k @$save:L+R+START -k @$((save + 6)):- $(keys $((save + 40)) DOWN DOWN A A) $(hashed $save) \
    -f 9000 2>"$cart/log")
  log=$(cat "$cart/log")
  grep -q 'states: slot 2 already in cart SRAM' <<<"$log" ||
    fail "slot 2 was put back again: $(grep 'states' <<<"$log" | tail -3)"
  same "$saved" "$(hashes "$out" 1)" "after Load state with the state left in cart SRAM" >/dev/null
  echo "PASS: states keep: a launch whose slot is in cart SRAM already doesn't put it back"

  # An old /RTS/Game.rts with a state's flag, on a card with no states:
  # slot 1, once (renamed Game.rts.imported, so another save slot's empty
  # folder doesn't take it again), and not unpacked again after the import
  rts=$carts/Game.rts
  head -c $((0x6FFF0)) /dev/zero >"$rts"
  printf 'EZ-OmegaRTCFILE.' >>"$rts"
  mdeltree -i "$cart/sd.img" ::/torii/states
  mmd -i "$cart/sd.img" ::/RTS 2>/dev/null || true
  mcopy -o -i "$cart/sd.img" "$rts" ::/RTS/Game.rts
  for run in 1 2; do
    # shellcheck disable=SC2046 # option lists
    boot $(nav_launch /Game.gba addon) -w "launch: starting the game" -f 600 >/dev/null 2>"$cart/log"
    log=$(cat "$cart/log")
    taken=$(grep -c 'states: /RTS/Game.rts becomes slot 1 of /Game.gba' <<<"$log" || true)
    [ "$taken" = $((run == 1 ? 1 : 0)) ] || fail "launch $run: the old state taken $taken times"
    grep -q 'states: slot 1 in cart SRAM' <<<"$log" || fail "launch $run: the old state isn't in cart SRAM"
    if [ $run = 1 ]; then
      grep -q 'states: slot 1 in cart SRAM from the /RTS import' <<<"$log" || fail "the import's state was read again"
      ! grep -q 'states: slot 1 in cart SRAM from /torii' <<<"$log" || fail "the import's state was unpacked again"
      mtype -i "$cart/sd.img" ::/RTS/Game.rts.imported >/dev/null 2>&1 || fail "/RTS/Game.rts wasn't renamed"
      ! mtype -i "$cart/sd.img" ::/RTS/Game.rts >/dev/null 2>&1 || fail "/RTS/Game.rts is still there"
    fi
    # The file's flag never reaches cart SRAM: Torii's mark says the state is there
    [ "$(od -A n -t x1 -j $((0xAFFF0)) -N 16 "$cart/sram.bin" | tr -d ' \n')" = 00000000000000000000000000000000 ] ||
      fail "launch $run: EZ-Flash's write-back flag in cart SRAM"
    [ "$(od -A n -c -j $((0xAFFD0)) -N 16 "$cart/sram.bin" | tr -d ' \n')" = TORII-STATE-OK1. ] ||
      fail "launch $run: no state's mark in cart SRAM"
  done
  echo "PASS: states keep: an old /RTS state becomes slot 1, once"
  ;;
popup)
  out=$(save_and_quit 2 boot 2>"$cart/log")
  grep -q '3.state: .* written' "$cart/log" || fail "slot 3 wasn't filed: $(grep states "$cart/log" | tail -3)"
  saved=$(hashes "$out" 1)
  # The Recent list's popup: Play, Saves, Cheats, Load state; its slots, A:
  # the game starts in slot 3
  # shellcheck disable=SC2046 # option lists
  out=$(NAV_SHOWN="recent gba" boot $(NAV_SHOWN="recent gba" nav_game_menu recent 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- -k @36:DOWN -k @40:- \
    -k @48:A -k @52:- -w "screen ready: state-popup" -k @12:A -k @16:- \
    -w "launch: starting the game" -H @1:@400 -f 9000 2>"$cart/log")
  log=$(cat "$cart/log")
  grep -q 'games: start /Game.gba in its save state slot 3' <<<"$log" ||
    fail "the popup didn't start slot 3: $(grep -E 'states|games:|ready' <<<"$log" | tail -5)"
  grep -q "states: slot 3 in cart SRAM from .*, loaded at the game's start" <<<"$log" || fail "not loaded at the start"
  same "$saved" "$(hashes "$out" 1)" "the game started in slot 3" >/dev/null
  echo "PASS: states popup: the game popup's Load state starts the game in the slot picked"
  ;;
unreadable)
  # Slot 3 on the card with its head whole and its data not: the popup
  # lists it, and starting in it asks first. B: back, no launch; A: the
  # game from its start
  save_and_quit 2 boot >/dev/null 2>"$cart/log"
  grep -q '3.state: .* written' "$cart/log" || fail "slot 3 wasn't filed: $(grep states "$cart/log" | tail -3)"
  state=$carts/3.state
  mcopy -o -i "$cart/sd.img" ::/torii/states/Game.gba/Main/3.state "$state"
  printf '\xFF\xFF\xFF\xFF' | dd of="$state" bs=1 seek=1000 conv=notrunc status=none
  mcopy -o -i "$cart/sd.img" "$state" ::/torii/states/Game.gba/Main/3.state
  for key in B A; do
    # shellcheck disable=SC2046 # option lists
    NAV_SHOWN="recent gba" boot $(NAV_SHOWN="recent gba" nav_game_menu recent 0) -k @12:DOWN -k @16:- -k @24:DOWN -k @28:- \
      -k @36:DOWN -k @40:- -k @48:A -k @52:- -w "screen ready: state-popup" -k @12:A -k @16:- \
      -w "screen ready: dialog" -s @4:"$cart/dialog.png" -k @12:$key -k @16:- -h @200 -f 9000 >/dev/null 2>"$cart/log" ||
      fail "$key: no question: $(grep -E 'states|launch|ready' "$cart/log" | tail -4)"
    log=$(cat "$cart/log")
    grep -q 'states: slot 3 of /Game.gba (Main) didn.t read' <<<"$log" || fail "$key: slot 3 read"
    if [ $key = B ]; then
      grep -q 'launch: Game.gba not started: its state slot 3 doesn.t read' <<<"$log" || fail "B: the launch went on"
      grep -q 'launch: starting the game' <<<"$log" && fail "B: the game started"
    else
      grep -q 'launch: starting the game' <<<"$log" || fail "A: the game didn't start"
    fi
  done
  echo "PASS: states unreadable: a slot that doesn't read asks before the game starts, B goes back, A starts it from its start"
  ;;
cut)
  scenario() { save_and_quit 1 cut_run -c $clock "$@"; }
  check() {
    local n=$1 name=$2
    # shellcheck disable=SC2046 # option lists
    cut_run -c $clock $(nav_launch /Game.gba addon) -w "launch: starting the game" -f 3000
    grep -q '^crash at frame' "$cart/run.log" && fail "cut at $n ($name): the next launch crashed"
    grep -q "doesn't read whole" "$cart/run.log" && fail "cut at $n ($name): a broken copy was taken"
    if [ "$n" = 0 ]; then
      grep -q 'states: slot 2 in cart SRAM' "$cart/run.log" || fail "uncut: slot 2 isn't in cart SRAM"
    fi
    return 0
  }
  cut_sweep scenario check
  echo "PASS: states cut: the filing survives a cut at each of the run's $cut_count cutpoints"
  ;;
esac
