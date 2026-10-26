#!/usr/bin/env bash
# Cheats (v0.7) where they could harm the game or its files.
#
#   irq    The cheats run exactly once per VBlank, whatever the game's IRQ
#          handler does (gba_rts_patch.s): tests/roms/cheattest's cheatirq
#          serves one IRQ an entry, HBlank before VBlank, under a cheat set
#          at the interpreter's cycle cap; cheatirq-vblank enables VBlank
#          alone. Neither may freeze, and an add-1 cheat must move its
#          counter by exactly 1 a frame.
#   files  A cheat file whose text is over CHEATFILE_TEXT_MAX is refused (no
#          Cheats in the popup, none applied, the file as it was); an enable
#          value of 1 is a pick; deleting a game deletes its cheat files
#          first, and renaming it moves them, each step cut in turn
#          (tests/cutsweep.sh): no cut leaves a cheat file without its game,
#          or under the other name.
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

mode=${1:?usage: cheat_safety.sh irq|files}
local_carts "dist/cart-cheat-safety-$mode"

fail() {
  echo "FAIL: cheat_safety $mode: $*" >&2
  exit 1
}

boot() { OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@"; }

# new_cart NAME ROM CARD_NAME CHT: a cart with ROM at /CARD_NAME and CHT as its
# cheat file
new_cart() {
  cart=$carts/$1
  rm -rf "$cart" && mkdir -p "$cart"
  mkfs.fat -C -F 32 -n CHEATS "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" "$2" "::/$3"
  mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
  mcopy -i "$cart/sd.img" "$4" "::/torii/cheats/${3%.*}.cht"
}

case $mode in
irq)
  # CodeBreaker: add 1 to the halfword at 0x0203F000; a slide of 550
  # halfwords (the cycle model's 26,892, the set's 27,682 of 28,000)
  cht=$carts/irq.cht
  mkdir -p "$carts"
  printf '%s\n' 'cheats = 2' \
    'cheat0_desc = "Count frames"' 'cheat0_code = "E203F000 0001"' 'cheat0_enable = true' \
    'cheat1_desc = "Heavy slide"' 'cheat1_code = "42010000 0000+00010226 0002"' 'cheat1_enable = true' >"$cht"
  for rom in cheatirq cheatirq-vblank; do
    new_cart "$rom" "tests/roms/cheattest/$rom.gba" CI.gba "$cht"
    # shellcheck disable=SC2046 # nav.sh's options
    boot $(nav_launch /CI.gba addon) -w "launch: starting the game" -u "cheatirq: frame 0600" -f 3000 \
      >"$cart/run.txt" 2>&1 || true
    grep -q "cheats: .*CI.cht: 2 picked, 2 run in game" "$cart/run.txt" ||
      fail "$rom: the launch read: $(grep -a 'cheats:' "$cart/run.txt" | head -3)"
    last=$(grep -a "cheatirq: frame" "$cart/run.txt" | tail -1)
    case $last in
    *"frame 0600 bad 0000"*) ;;
    *) fail "$rom: the game froze or the cheat didn't run once a frame: ${last:-no frame line}" ;;
    esac
    if [ "$rom" = cheatirq ]; then
      h=${last##* hblank }
      [ "$((10#$h))" -gt 100000 ] || fail "$rom: only $h HBlank IRQs in 600 frames"
    fi
  done
  echo "PASS: cheat_safety: cheats at the cycle cap run once per VBlank, with a handler that serves HBlank first and with VBlank alone"
  ;;
files)
  mkdir -p "$carts"
  there() { mtype -i "$cart/sd.img" "::/$1" >/dev/null 2>&1; }
  # Too big: a picked cheat first, then padding past CHEATFILE_TEXT_MAX
  # (0xE000) by 5 bytes, which still fits safefile's read of the file
  big=$carts/big.cht
  printf '%s\n' 'cheat0_desc = "Beef"' 'cheat0_code = "0203F000:BEEF"' 'cheat0_enable = true' >"$big"
  pad=$((0xE000 + 5 - $(stat -c %s "$big")))
  {
    head -c $((pad - 1)) /dev/zero | tr '\0' '#'
    printf '\n'
  } >>"$big"
  new_cart big tests/roms/hello/hello.gba Hello.gba "$big"
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_game_menu gba 0) -f 1200 >"$cart/popup.txt" 2>&1 || true
  grep -q "screen ready: game-popup" "$cart/popup.txt" || fail "no game popup: $(grep -a '^kernel' "$cart/popup.txt" | tail -3)"
  grep -q "cheats: /torii/cheats/Hello.cht is 57349 bytes, over 57344: too big, left alone" "$cart/popup.txt" ||
    fail "the big file wasn't refused: $(grep -a 'cheats:' "$cart/popup.txt" | head -3)"
  ! grep -q "cheats: popup:" "$cart/popup.txt" || fail "the popup offered Cheats for the big file"
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_launch /Hello.gba addon) -w "launch: starting the game" -m @120:0x0203F000:2 -f 3000 \
    >"$cart/launch.txt" 2>&1 || true
  ! grep -q "cheats: .*picked" "$cart/launch.txt" || fail "a launch applied the big file's cheats"
  ! grep -qi "mem frame [0-9]* 0203F000: ef be" "$cart/launch.txt" || fail "the big file's cheat ran"
  mtype -i "$cart/sd.img" ::/torii/cheats/Hello.cht | cmp -s - "$big" || fail "the big file changed"
  echo "PASS: cheat_safety: a cheat file over 57,344 bytes is refused: no Cheats, none applied, the file as it was"

  # cheatN_enable = 1 picks
  one=$carts/one.cht
  printf '%s\n' 'cheat0_desc = "Beef"' 'cheat0_code = "0203F000:BEEF"' 'cheat0_enable = 1' >"$one"
  new_cart one tests/roms/hello/hello.gba Hello.gba "$one"
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_launch /Hello.gba addon) -w "launch: starting the game" -m @120:0x0203F000:2 -f 3000 \
    >"$cart/launch.txt" 2>&1 || true
  grep -q "cheats: .*Hello.cht: 1 picked, 1 run in game" "$cart/launch.txt" ||
    fail "enable = 1 isn't a pick: $(grep -a 'cheats:' "$cart/launch.txt" | head -3)"
  grep -qi "mem frame [0-9]* 0203F000: ef be" "$cart/launch.txt" || fail "the cheat picked by enable = 1 didn't run"
  echo "PASS: cheat_safety: cheatN_enable = 1 is a pick"

  # Delete, cut at each step: the game goes last
  new_cart delete tests/roms/hello/hello.gba Hello.gba "$one"
  mcopy -i "$cart/sd.img" "$one" ::/torii/cheats/Hello.cht.bak
  delete_scenario() {
    # shellcheck disable=SC2046 # nav.sh's options
    cut_run "$@" $(nav_game_delete gba 0) -w "delete: /Hello.gba" -f 900
  }
  delete_check() {
    local n=$1 name=$2
    if ! there Hello.gba; then
      ! there torii/cheats/Hello.cht && ! there torii/cheats/Hello.cht.bak ||
        fail "delete cut at $n ($name): the game went, a cheat file stayed"
    fi
    [ "$n" != 0 ] || ! there Hello.gba || fail "delete: the game is still there"
    return 0
  }
  cut_sweep delete_scenario delete_check
  grep -q "cutpoint [0-9]*: cheats: its .bak deleted" "$carts/cutsweep-names.log" ||
    fail "the delete logged no cheat cutpoints"
  echo "PASS: cheat_safety: deleting a game deletes its cheat files first: no cut at its $cut_count steps leaves one without the game"

  # Rename on the SD card page (Hello to Helloq), cut at each step; the
  # next power-on finishes or undoes it, the cheat files with the game
  new_cart rename tests/roms/hello/hello.gba Hello.gba "$one"
  mcopy -i "$cart/sd.img" "$one" ::/torii/cheats/Hello.cht.bak
  rename_scenario() {
    # shellcheck disable=SC2046 # nav.sh's options
    cut_run "$@" $(nav_file /Hello.gba 0) -k @12:UP -k @16:- -k @20:UP -k @24:- -k @28:UP -k @32:- -k @40:A -k @44:- \
      -w "screen ready: rename" -k @10:A -k @14:- -k @24:START -k @28:- -w "rename: kept" -f 2400
  }
  rename_check() {
    local n=$1 name=$2 to other
    cut_run -f 300
    if there Helloq.gba; then to=Helloq other=Hello; else to=Hello other=Helloq; fi
    there "$to.gba" || fail "rename cut at $n ($name): no game left"
    ! there "$other.gba" || fail "rename cut at $n ($name): both names are on the card"
    there "torii/cheats/$to.cht" && there "torii/cheats/$to.cht.bak" ||
      fail "rename cut at $n ($name): the cheat files aren't with $to.gba"
    ! there "torii/cheats/$other.cht" && ! there "torii/cheats/$other.cht.bak" ||
      fail "rename cut at $n ($name): a cheat file is left under $other"
    [ "$n" != 0 ] || [ "$to" = Helloq ] || fail "rename: uncut, the game wasn't renamed"
  }
  cut_sweep rename_scenario rename_check
  grep -q "cutpoint [0-9]*: rename: a cheat file moved" "$carts/cutsweep-names.log" ||
    fail "the rename logged no cheat cutpoints"
  echo "PASS: cheat_safety: renaming a game moves its cheat files: a cut at each of its $cut_count steps leaves them with the game"
  ;;
*)
  fail "no mode $mode"
  ;;
esac
