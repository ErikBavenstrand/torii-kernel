#!/usr/bin/env bash
# The in-game cheat list (v0.8 P2, source/overlay/ov.c, source/ingame.c) on
# the emulated cart, with the hello test ROM and a cheat file of five. The
# list shows the picks only (v0.10):
#
#   0 Count frames   picked: adds 1 to the halfword at 0x0203F000 each frame
#   1 Beef           picked: adds 1 to the halfword at 0x0203F010 each frame
#   2 Patch the ROM  picked, a ROM patch: greyed (on until Back to Torii)
#   3 Fill me        picked, a placeholder: greyed (can't run)
#   4 Slide A        not picked: not listed
#
# toggle: the list turns Count frames and Beef off; A on the greyed rows
#         does nothing. From the first frame after the menu both counters
#         stand still. Then Back to Torii: the kernel's next boot keeps the
#         toggles as the file's picks (Count frames and Beef off, the greyed
#         ones as they were), and a launch after it runs none.
# cut:    that boot's write cut at each cutpoint (tests/cutsweep.sh): the
#         file is the old picks or the new ones, and the next boot ends with
#         the new.
# once:   Count frames off, then SELECT (run once) and back to the game: the
#         counter goes up by one and stays, and the interpreter has taken
#         the request (0x3007FE1 back to 0).
#
# Runs inside the torii-emu container after `just build`, `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
# shellcheck source=tests/cutsweep.sh
. tests/cutsweep.sh

mode=${1:?usage: cheats_ingame.sh toggle|cut|once}
local_carts "dist/cart-cheats-ingame-$mode"

fail() {
  echo "FAIL: cheats_ingame $mode: $*" >&2
  exit 1
}

cart=$carts/ci
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n CHEATS "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
cht=$carts/Hello.cht
printf '%s\n' 'cheats = 5' \
  'cheat0_desc = "Count frames"' 'cheat0_code = "E203F000 0001"' 'cheat0_enable = true' \
  'cheat1_desc = "Beef"' 'cheat1_code = "E203F010 0001"' 'cheat1_enable = true' \
  'cheat2_desc = "Patch the ROM"' 'cheat2_code = "08000400:12"' 'cheat2_enable = true' \
  'cheat3_desc = "Fill me"' 'cheat3_code = "0203F020:00??"' 'cheat3_enable = true' \
  'cheat4_desc = "Slide A"' 'cheat4_code = "42010000 0000+00010226 0002"' 'cheat4_enable = false' >"$cht"
mcopy -i "$cart/sd.img" "$cht" ::/torii/cheats/Hello.cht
# Settings slots (source/ez_define.h), as tests/menu.sh: RESET and SAVESTATE
# on, menu key L+R+START; and the save copied at power-on without asking
python3 tests/setnor.py set "$cart/kernel-nor.bin" 0=0xE1E1 1=1 2=1 3=0 4=0 5=9 6=8 7=2 8=9 9=8 10=3 11=1 12=0 13=1 14=1 25=1

boot() { OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@"; }

# keys FRAME KEY...: -k press/release pairs from @FRAME, one key every 8 frames
keys() {
  local frame=$1
  shift
  for key in "$@"; do
    printf -- '-k @%d:%s -k @%d:- ' "$frame" "$key" $((frame + 4))
    frame=$((frame + 8))
  done
}

# halfword FRAME ADDR: the halfword a -m FRAME:ADDR:2 dump read, as hex
halfword() { awk -v f="$1" -v a="$2" '$1 == "mem" && $3 == f && $4 == a ":" { print $6 $5 }' <<<"$out"; }

file_picks() {
  mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht | tr -d '\r' |
    sed -n 's/^cheat\([0-9]\)_enable = \(.*\)$/\1=\2/p' | tr '\n' ' '
}

# The launch, then the menu at @300 from the game's start: Cheats (DOWN
# past Save state, Load state greyed), the list on Count frames: A (off),
# DOWN A (Beef off), DOWN A (Patch the ROM: nothing), DOWN A (Fill me:
# nothing), B, B. The menu closes 80 frames after the list's first key;
# dumps every frame after.
menu=300
list=$((menu + 40))
closed=$((list + 8 * 10 + 30))
dumps=""
for f in $(seq $((closed - 4)) $((closed + 20))); do
  dumps+="-m @$f:0x0203F000:2 -m @$f:0x0203F010:2 "
done
toggle_run() {
  # shellcheck disable=SC2046,SC2086 # option lists
  out=$(boot $(nav_launch /Hello.gba addon) -w "launch: starting the game" \
    -k @$menu:L+R+START -k @$((menu + 6)):- \
    $(keys $((menu + 16)) DOWN DOWN A) \
    $(keys $list A DOWN A DOWN A DOWN A B B) \
    -s @$((list + 8 * 7 + 4)):"$cart/list.png" \
    $dumps -m @$((closed + 20)):0x03007FE8:4 "$@" 2>"$cart/log")
  log=$(cat "$cart/log")
}

case $mode in
toggle)
  toggle_run -f 3000
  grep -q '^crash at frame' <<<"$log" && fail "crashed"
  grep -q 'cheats: /torii/cheats/Hello.cht: 4 picked, 2 run in game' <<<"$log" ||
    fail "the launch: $(grep 'cheats:' <<<"$log" | head -3)"
  grep -q '1 ROM patches; 4 listed in game' <<<"$log" || fail "the list: $(grep 'listed' <<<"$log")"
  grep -q 'screen ready: ingame-cheats' <<<"$log" || fail "the cheat list didn't open"
  # Frame by frame: the counter moved while the menu was closing, and not
  # from the first frame after it on
  base=$(awk '$1 == "mem" && $4 == "0203F000:" { print $3 }' <<<"$out" | head -1)
  [ -n "$base" ] || fail "no counter dumps: $(head -c 300 <<<"$out")"
  for at in 0203F000 0203F010; do
    first=$(halfword $((base + 4)) $at)
    last=$(halfword $((base + 24)) $at)
    [ -n "$first" ] && [ "$first" = "$last" ] || fail "the counter at $at still runs after the menu ($first, then $last)"
    [ "$first" != 0000 ] || fail "the counter at $at never ran"
  done
  # The toggles: Count frames (bit 0) and Beef (1) off
  toggles=$(awk '$1 == "mem" && $4 == "03007FE8:" { print $5 $6 $7 $8 }' <<<"$out")
  [ "$toggles" = 03000000 ] || fail "the toggles are $toggles, not 03000000"
  echo "PASS: cheats_ingame toggle: the list (the picks only) turns picked cheats off from the next frame, and greyed rows (a ROM patch, a placeholder) stay as they are"

  # The same toggles, then B to the menu on its Cheats row and Back to Torii
  # (past Screenshot, Reset game and Sleep): the kernel's boot keeps the toggles as picks
  before=$(file_picks)
  # shellcheck disable=SC2046 # option lists
  boot $(nav_launch /Hello.gba addon) -w "launch: starting the game" \
    -k @$menu:L+R+START -k @$((menu + 6)):- \
    $(keys $((menu + 16)) DOWN DOWN A) $(keys $list A DOWN A B DOWN DOWN DOWN DOWN A) \
    -w "in-game: back from /Hello.gba" -f 6000 -u "home ready" >/dev/null 2>"$cart/log"
  log=$(cat "$cart/log")
  grep -q 'cheats: /Hello.gba: 2 picks changed in game' <<<"$log" ||
    fail "the boot after the game: $(grep -E 'in-game|cheats' <<<"$log" | tail -5)"
  after=$(file_picks)
  [ "$after" = "0=false 1=false 2=true 3=true 4=false " ] ||
    fail "the file's picks: $after (were $before)"
  # A launch after it: neither counter runs
  # shellcheck disable=SC2046 # option lists
  out=$(boot $(nav_launch /Hello.gba addon) -w "launch: starting the game" -m @200:0x0203F010:2 \
    -m @200:0x0203F000:2 -m @260:0x0203F000:2 -m @260:0x0203F010:2 -f 3000 -u "hook never" 2>"$cart/log")
  log=$(cat "$cart/log")
  grep -q 'Hello.cht: 2 picked, 0 run in game' <<<"$log" || fail "the next launch: $(grep 'cheats:' <<<"$log" | head -3)"
  [ "$(awk '$1 == "mem" && $4 == "0203F000:" { print $6 $5 }' <<<"$out" | sort -u | wc -l)" = 1 ] ||
    fail "Count frames ran next launch"
  [ "$(awk '$1 == "mem" && $4 == "0203F010:" { print $6 $5 }' <<<"$out" | sort -u | wc -l)" = 1 ] ||
    fail "Beef ran next launch"
  echo "PASS: cheats_ingame toggle: Back to Torii keeps the toggles as the file's picks, and the next launch runs as they say"
  ;;
once)
  # The menu, Cheats, A (Count frames off), SELECT (run it once), B, B
  done_at=$((list + 8 * 4 + 30))
  # shellcheck disable=SC2046 # option lists
  out=$(boot $(nav_launch /Hello.gba addon) -w "launch: starting the game" \
    -k @$menu:L+R+START -k @$((menu + 6)):- \
    $(keys $((menu + 16)) DOWN DOWN A) $(keys $list A SELECT B B) \
    -m @$((list + 4)):0x0203F000:2 -m @$((done_at + 20)):0x0203F000:2 -m @$((done_at + 60)):0x0203F000:2 \
    -m @$((done_at + 60)):0x03007FE0:2 -f 3000 2>"$cart/log")
  log=$(cat "$cart/log")
  grep -q '^crash at frame' <<<"$log" && fail "crashed"
  grep -q 'screen ready: ingame-cheats' <<<"$log" || fail "the cheat list didn't open"
  # The dumps in frame order (their frames print as counted from the start)
  counts=$(awk '$1 == "mem" && $4 == "0203F000:" { print $6 $5 }' <<<"$out")
  in_menu=$(sed -n 1p <<<"$counts")
  after=$(sed -n 2p <<<"$counts")
  later=$(sed -n 3p <<<"$counts")
  [ -n "$in_menu" ] && [ -n "$after" ] || fail "no counter dumps: $(head -c 300 <<<"$out")"
  [ $((16#$after)) = $((16#$in_menu + 1)) ] || fail "the counter went from $in_menu to $after, not up by one"
  [ "$later" = "$after" ] || fail "the counter ran on after its run once ($after, then $later)"
  onoff=$(awk '$1 == "mem" && $4 == "03007FE0:" { print $6 $5 }' <<<"$out")
  [ "$onoff" = 0000 ] || fail "CheatONOFF and the run once byte read $onoff, not 0000"
  echo "PASS: cheats_ingame once: SELECT on a cheat that is off runs it once as the game resumes"
  ;;
cut)
  # The game, the toggles, Back to Torii: the boot's write of the picks cut
  # at each cutpoint (the launch's own come first). Cart SRAM doesn't survive
  # the power cut, so the next boot can't write them again: the file must
  # be the old picks or the new ones, whole, as a launch reads it.
  scenario() {
    # shellcheck disable=SC2046 # option lists
    cut_run "$@" $(nav_launch /Hello.gba addon) -w "launch: starting the game" \
      -k @$menu:L+R+START -k @$((menu + 6)):- \
      $(keys $((menu + 16)) DOWN DOWN A) $(keys $list A DOWN A B DOWN DOWN DOWN DOWN A) \
      -w "in-game: back from /Hello.gba" -f 6000 -u "home ready"
  }
  check() {
    local n=$1 name=$2 picks
    # shellcheck disable=SC2046 # option lists
    cut_run $(nav_launch /Hello.gba addon) -f 3000 -u "launch: starting"
    grep -q '^crash at frame' "$cart/run.log" && fail "cut at $n ($name): the next power-on crashed"
    picks=$(sed -n 's/^kernel: cheats: .*Hello.cht: \([0-9]\) picked, .*/\1/p' "$cart/run.log")
    case $n:$picks in
    0:2 | [1-9]*:4 | [1-9]*:2) ;;
    *) fail "cut at $n ($name): the next launch read ${picks:-no} picks: $(grep 'cheats:' "$cart/run.log" | head -3)" ;;
    esac
    return 0
  }
  cut_sweep scenario check
  echo "PASS: cheats_ingame cut: the in-game picks' write survives a cut at each of the run's $cut_count cutpoints"
  ;;
esac
