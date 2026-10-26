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
#   rom    A cheat's ROM patches keep off the kernel's own (the patch area,
#          the entry branch); a 32 MB ROM gets its cheats only where padding
#          holds them, the patch area cut to what they need, never over its
#          data.
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

mode=${1:?usage: cheat_safety.sh irq|files|rom}
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
      if there torii/cheats/Hello.cht || there torii/cheats/Hello.cht.bak; then
        fail "delete cut at $n ($name): the game went, a cheat file stayed"
      fi
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
    if ! there "torii/cheats/$to.cht" || ! there "torii/cheats/$to.cht.bak"; then
      fail "rename cut at $n ($name): the cheat files aren't with $to.gba"
    fi
    if there "torii/cheats/$other.cht" || there "torii/cheats/$other.cht.bak"; then
      fail "rename cut at $n ($name): a cheat file is left under $other"
    fi
    [ "$n" != 0 ] || [ "$to" = Helloq ] || fail "rename: uncut, the game wasn't renamed"
  }
  cut_sweep rename_scenario rename_check
  grep -q "cutpoint [0-9]*: rename: a cheat file moved" "$carts/cutsweep-names.log" ||
    fail "the rename logged no cheat cutpoints"
  echo "PASS: cheat_safety: renaming a game moves its cheat files: a cut at each of its $cut_count steps leaves them with the game"
  ;;
rom)
  mkdir -p "$carts"
  mem() { sed -n "s/^mem frame [0-9]* $1: //p" "$cart/run.txt" | head -1 | tr -d ' \n' | tr A-F a-f; }
  # Hello padded with 0xFF to 64 KB puts the patch area at ROM 0xD000: a
  # cheat whose ROM writes cover it and the entry branch (VBA raw words)
  # gets those left out, its other patch and its RAM write kept
  python3 - "$carts/pad.gba" <<'EOF'
import sys
rom = open("tests/roms/hello/hello.gba", "rb").read()
open(sys.argv[1], "wb").write(rom + b"\xff" * (0x10000 - len(rom)))
EOF
  {
    printf 'cheat0_desc = "Over the hook"\ncheat0_code = "'
    for i in $(seq 0 15); do printf '0800D%03X:00000000+' $((i * 4)); done
    printf '08000000:00000000+080000A0:4954+0203F000:BEEF"\ncheat0_enable = true\n'
  } >"$carts/over.cht"
  new_cart over "$carts/pad.gba" Hello.gba "$carts/over.cht"
  # shellcheck disable=SC2046 # nav.sh's options
  boot $(nav_launch /Hello.gba addon) -w "launch: starting the game" -m @120:0x0203F000:2 \
    -m @120:0x0800D000:4 -m @120:0x080000A0:2 -f 3000 >"$cart/run.txt" 2>&1 || true
  grep -q "in-game code: in-game menu (RTS_ReplaceIRQ) at ROM 0000D000" "$cart/run.txt" ||
    fail "the patch area isn't at 0xD000: $(grep -a 'in-game code' "$cart/run.txt")"
  grep -q "cheats: cheat 0 (Over the hook): its ROM patch at 0800D000 is over the kernel's own, left out" "$cart/run.txt" ||
    fail "no patch over the hook was refused: $(grep -a 'cheats:' "$cart/run.txt" | tail -3)"
  grep -q "cheats: 1 ROM patches written into the game, some over the kernel's own patches left out" "$cart/run.txt" ||
    fail "the patches logged: $(grep -a 'cheats:' "$cart/run.txt" | tail -2)"
  [ "$(mem 0203F000)" = efbe ] || fail "the cheat didn't run: 0203F000 holds $(mem 0203F000)"
  [ "$(mem 0800D000)" != 00000000 ] || fail "the hook's first word was overwritten"
  [ "$(mem 080000A0)" = 5449 ] || fail "the patch off the kernel's wasn't written: $(mem 080000A0)"
  echo "PASS: cheat_safety: a cheat's ROM patches over the patch area and the entry branch are left out, the rest applied"

  # 32 MB ROMs: the patch area has no padding to go in. Data to the end:
  # the cheats are left out, the game starts. 8 KB of padding at the end:
  # the area is cut to what the picks need, and the data before it stays.
  # A 16 MB game overdumped to 32 MB (open-bus fill past its end, as the
  # card's FireRed is): the area stays where it was, the cheats run
  printf '%s\n' 'cheat0_desc = "Beef"' 'cheat0_code = "0203F000:BEEF"' 'cheat0_enable = true' >"$carts/beef.cht"
  for kind in full padded overdump; do
    python3 - "$carts/big.gba" "$kind" <<'EOF'
import random, struct, sys
rom = open("tests/roms/hello/hello.gba", "rb").read()
size, kind = 0x2000000, sys.argv[2]
if kind == "overdump":
    data = random.Random(7).randbytes(0x1000000 - len(rom))
    fill = b"".join(struct.pack("<H", (a >> 1) & 0xFFFF) for a in range(0x1000000, size, 2))
    open(sys.argv[1], "wb").write(rom + data + fill)
else:
    pad = 0x2000 if kind == "padded" else 0
    data = random.Random(7).randbytes(size - len(rom) - pad)
    open(sys.argv[1], "wb").write(rom + data + b"\xff" * pad)
EOF
    new_cart "big-$kind" "$carts/big.gba" Hello.gba "$carts/beef.cht"
    # shellcheck disable=SC2046 # nav.sh's options
    boot $(nav_launch /Hello.gba addon) -w "launch: starting the game" -m @120:0x0203F000:2 \
      -m @120:0x09FFD000:16 -m @120:0x09FFDFF0:16 -f 6000 >"$cart/run.txt" 2>&1 || true
    grep -q "launch: starting the game" "$cart/run.txt" || fail "$kind: the game didn't start: $(grep -a '^kernel' "$cart/run.txt" | tail -3)"
    rom_at() { od -An -tx1 -j "$1" -N 16 "$carts/big.gba" | tr -d ' \n'; }
    case $kind in
    full)
      grep -q "launch: the patch area at 01FFD000 is on the game's data, and the cheats' .* would be too: the cheats that run in game are left out" "$cart/run.txt" ||
        fail "full: the cheats weren't left out: $(grep -a 'launch: the patch\|in-game code' "$cart/run.txt")"
      [ "$(mem 0203F000)" != efbe ] || fail "full: the cheat ran"
      # Without cheats the menu's area is where it always was (on the data)
      grep -q "in-game code: in-game menu (RTS_ReplaceIRQ) at ROM 01FFF000\|in-game code: reset and sleep keys (Sleep_ReplaceIRQ) at ROM 01FFFD00" "$cart/run.txt" ||
        fail "full: the area without cheats moved: $(grep -a 'in-game code' "$cart/run.txt")"
      [ "$(mem 09FFD000)" = "$(rom_at $((0x1FFD000)))" ] || fail "full: the data at 01FFD000 changed"
      ;;
    padded)
      grep -q "launch: the patch area at 01FFD000 is on the game's data: cut to" "$cart/run.txt" ||
        fail "padded: the area wasn't cut: $(grep -a 'launch: the patch\|in-game code' "$cart/run.txt")"
      [ "$(mem 0203F000)" = efbe ] || fail "padded: the cheat didn't run: $(mem 0203F000)"
      [ "$(mem 09FFDFF0)" = "$(rom_at $((0x1FFDFF0)))" ] || fail "padded: the game's last data changed"
      ;;
    overdump)
      ! grep -q "launch: the patch area at" "$cart/run.txt" || fail "overdump: the fill was taken for data"
      grep -q "in-game code: in-game menu (RTS_ReplaceIRQ) at ROM 01FFD000" "$cart/run.txt" ||
        fail "overdump: the area moved: $(grep -a 'in-game code' "$cart/run.txt")"
      [ "$(mem 0203F000)" = efbe ] || fail "overdump: the cheat didn't run: $(mem 0203F000)"
      ;;
    esac
  done
  echo "PASS: cheat_safety: a 32 MB ROM gets the cheats where padding or an overdump's fill holds them, and none over its data"
  ;;
*)
  fail "no mode $mode"
  ;;
esac
