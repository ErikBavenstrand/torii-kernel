#!/usr/bin/env bash
# The in-game menu under each save type, and its stub failing safe
# (docs/design/v0.8/ingame-menu.md). Runs inside the torii-emu container.
#
# tests/menu_safety.sh TYPE: hello launched with BOOT WITH ADDON and save
# type TYPE (sram, eeprom8k, eeprom512, flash64k, flash128k, auto); L+R+START
# must open the menu, B close it, and the game come back as it was
# (tests/hook_watch.lua), then a save state round trip.
#
# tests/menu_safety.sh FAULT: the same at FLASH 128K, with cart SRAM failing
# in game mode (OMEGA_GAME_SRAM in the cart model): drop (writes past the
# save memory lost), flip-image (a byte of the overlay image reads wrong) or
# chip (the save chip on every page: the page register ignored while the
# FPGA presents a flash chip, unproven either way on the real cart).
# The stub must not open the menu and must give the game back untouched,
# never a black screen or a hang (Pokémon Emerald on the real cart, v0.8):
# its screen stays the reason's colour (INGAME_REFUSED_COLOURS) until A
# gives the game back, its screen as before. The menu key held for 2 s
# instead shows the theme's accent and resets into Torii, whose boot shows a
# dialog and logs why from the stub's record (struct ingame_refused),
# carried in IWRAM where page 0xF0 took no writes; with flip-image the record
# also reaches a later boot from page 0xF0.
#
# tests/menu_safety.sh emerald: the held-key run alone, in Pokémon Emerald
# (roms/; skipped without it) with drop. Its System stack starts at
# 0x03007E40, just over the record in IWRAM, and the emulated BIOS's SWIs
# write below the caller's System stack as the real BIOS's do
# (OMEGA_BIOS_STACK, emu/patches): the record was lost on the real cart
# until the stub moved that stack before its SoftReset.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
mode=${1:?usage: tests/menu_safety.sh TYPE|FAULT}
local_carts "dist/cart-menu-safety-$mode"

fault=
case $mode in
auto) save=0 ;;
sram) save=1 ;;
eeprom8k) save=2 ;;
eeprom512) save=3 ;;
flash64k) save=4 ;;
flash128k) save=5 ;;
drop | flip-image | chip) save=5 fault=$mode ;;
emerald) save=5 fault=drop ;;
*)
  echo "unknown mode $mode" >&2
  exit 2
  ;;
esac

cart=$carts/menu
mkdir -p "$cart"
rom=tests/roms/hello/hello.gba game=Hello
if [ "$mode" = emerald ]; then
  rom="roms/Pokemon - Emerald Version (Erik).gba" game=Emerald
  if [ ! -f "$rom" ]; then
    echo "SKIP: Pokémon Emerald isn't in roms/"
    exit 0
  fi
fi
mkfs.fat -C -F 32 -n MENU "$cart/sd.img" 131072 >/dev/null
mcopy -i "$cart/sd.img" "$rom" "::/$game.gba"

fail() {
  echo "FAIL ($mode): $*" >&2
  [ -z "${log:-}" ] || grep -E '^(script|crash|omega:)' <<<"$log" | grep -v ' vblank ' | tail -20 >&2
  exit 1
}

# Power-on 1 makes the cart; then the settings (as tests/menu.sh): English,
# RESET and SAVESTATE on, sleep key L+R+SELECT, menu key L+R+START
OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -f 100 >/dev/null 2>&1
python3 tests/setnor.py set "$cart/kernel-nor.bin" 0=0xE1E1 1=1 2=1 3=0 4=0 5=9 6=8 7=2 8=9 9=8 10=3 11=1 12=0 13=1 25=1
rm -rf "$cart-held"
cp -r "$cart" "$cart-held" # the held-key run below starts from here

if [ "$mode" = emerald ]; then
  key=3600 # loading 16 MB with patches takes about 2,400 frames (tests/sleep.sh)
else
  # Power-on 2: the launch, the menu key, B (hook_watch.lua; A on a refused
  # menu's white screen, back to the game), the screen's hash every frame, and
  # the EWRAM the overlay borrows (its window, 0x1FC0 bytes) every 10 frames
  close=B
  [ -z "$fault" ] || close=A
  ewram=$(for f in $(seq 500 10 1500); do printf -- '-m %d:0x02000000:0x1FC0 ' "$f"; done)
  # shellcheck disable=SC2046,SC2086 # $(nav_launch) and $ewram are lists of options
  out=$(OMEGA_DIR=$cart OMEGA_GAME_SRAM=$fault HOOK_WATCH_CLOSE=$close emu/build-linux/mgba-shot dist/ezkernelnew.bin \
    $(nav_launch /Hello.gba addon save=$save at=270) -L tests/hook_watch.lua -u 'hook watch done' -f 4000 \
    -H 1:4000 $ewram 2>"$cart/log") || fail "the run stopped with status $?"
  log=$(cat "$cart/log")
  grep -q '^crash at frame' <<<"$log" && fail "crashed"
  grep -q '^script: hook [0-9]* patch ' <<<"$log" || fail "the kernel did not hook the game"
  grep -q '^script: hook watch done' <<<"$log" || fail "the run ended early"
  key=$(sed -n 's/^script: hook \([0-9]*\) key L+R+START$/\1/p' <<<"$log" | head -1)
  [ -n "$key" ] || fail "no menu key pressed"
  hash_at() { awk -v f="$1" '$1 == "hash" && $3 == f ":" { print $4 }' <<<"$out"; }
  screens_after() { awk -v f="$1" '$1 == "hash" && $3 + 0 > f { print $4 }' <<<"$out" | sort -u | wc -l; }

  if [ -z "$fault" ]; then
    grep -q '^script: hook [0-9]* menu open$' <<<"$log" || fail "the menu did not open"
    grep -q '^script: hook [0-9]* menu left the game as it was$' <<<"$log" ||
      fail "the menu didn't leave the game as it was: $(grep -o 'menu changed .*' <<<"$log")"
    [ "$(screens_after $((key + 100)))" -gt 1 ] || fail "the game did not go on after the menu"
    [ "$(grep -cx 'kernel: screen ready: ingame' <<<"$log")" -ge 3 ] ||
      fail "the menu didn't open for the save state round trip"
    echo "PASS: the in-game menu at save type $mode opens, gives the game back as it was and saves and loads a state"
    exit 0
  fi

  # Faults: the menu never shows, the screen is the game's
  grep -q '^script: hook [0-9]* menu open$' <<<"$log" && fail "the menu opened from cart SRAM that failed"
  grep -q 'screen ready: ingame' <<<"$log" && fail "the overlay ran from cart SRAM that failed"
  [ -n "$(hash_at $((key - 1)))" ] || fail "no screen hash before the menu key"
  [ "$(screens_after "$key")" -gt 1 ] || fail "the game did not go on after the menu key"
  # The EWRAM window as it was: the last dump before the key against the one
  # 10 to 20 frames after it (hello leaves its EWRAM alone)
  ewram_at() { awk -v f="$1" '$1 == "mem" && $3 + 0 == f { $1 = $2 = $3 = ""; print; exit }' <<<"$out"; }
  before=$(ewram_at $(((key - 1) / 10 * 10)))
  after=$(ewram_at $(((key + 19) / 10 * 10)))
  [ -n "$before" ] && [ "$before" = "$after" ] || fail "the game's EWRAM window changed under the refused menu"
fi

# The menu key held for 2 s (Pokémon Emerald on the real cart: a refusal
# left no way back to Torii but a power-off, which lost the stub's record):
# the screen white while held, then the theme's accent (the backdrop
# alone), then Torii's boot with the dialog and the record's log line. A
# pressed every 40 frames answers the boot's dialogs.
answers=$(for f in $(seq $((key + 260)) 40 $((key + 1100))); do printf -- '-k %d:A -k %d:- ' "$f" $((f + 6)); done)
# shellcheck disable=SC2046,SC2086 # $(nav_launch) and $answers are lists of options
held=$(OMEGA_DIR=$cart-held OMEGA_GAME_SRAM=$fault emu/build-linux/mgba-shot dist/ezkernelnew.bin \
  $(nav_launch "/$game.gba" addon save=$save at=270) -k "$key:L+R+START" -k "$((key + 130)):-" $answers \
  -H $((key - 1)):$((key + 150)) -m $((key + 60)):0x05000000:2 -m $((key + 60)):0x04000000:2 \
  -u "in-game menu: the stub didn't open it" -f $((key + 1100)) 2>"$cart/log") ||
  fail "the held-key run stopped with status $?"
log=$(cat "$cart/log")
grep -q '^crash at frame' <<<"$log" && fail "crashed with the menu key held"
held_at() { awk -v f="$1" '$1 == "hash" && $3 == f ":" { print $4 }' <<<"$held"; }
held_range() { awk -v a="$1" -v b="$2" '$1 == "hash" && $3 + 0 >= a && $3 + 0 <= b { print $4 }' <<<"$held" | sort -u; }
white=$(held_range $((key + 10)) $((key + 110)))
[ "$(wc -l <<<"$white")" = 1 ] && [ "$white" != "$(held_at $((key - 1)))" ] ||
  fail "the screen didn't stay one colour while the refused menu's keys were held"
accent=$(held_range $((key + 126)) $((key + 138)))
[ "$(wc -l <<<"$accent")" = 1 ] && [ "$accent" != "$white" ] ||
  fail "the screen didn't turn to the accent after the menu keys' 2 s"
# The reason's colour (docs/release-checklist.md): the backdrop alone; and
# the record's detail line
case $fault in
drop) why="the game's memory didn't read back from cart SRAM: the EWRAM window" said="cart memory is unavailable with its Flash save" colour="1F 00" detail="the first difference before 0200" ;;
chip) why="no overlay image in cart SRAM" said="cart memory is unavailable with its Flash save" colour="00 7C" detail="the header's magic" ;;
*) why="the overlay image loaded from cart SRAM didn't check" said="its copy in cart memory was damaged" colour="E0 03" detail="the check word" ;;
esac
mem_at() { awk -v a="$1" '$1 == "mem" && $4 == a ":" { $1 = $2 = $3 = $4 = ""; print; exit }' <<<"$held" | xargs; }
[ "$(mem_at 05000000)" = "$colour" ] && [ "$(mem_at 04000000)" = "00 00" ] ||
  fail "the refused menu's screen isn't its colour ($colour): backdrop $(mem_at 05000000), DISPCNT $(mem_at 04000000)"
grep -q "^kernel error: in-game menu: $detail" <<<"$log" ||
  fail "Torii's boot didn't log what the check saw: $(grep 'in-game menu' <<<"$log" | tail -1)"
grep -q "^kernel: dialog: In-game menu didn't open: $game: $said" <<<"$log" ||
  fail "Torii's boot didn't say why the menu didn't open: $(grep -m1 'kernel: dialog' <<<"$log")"
grep -q "^kernel error: in-game menu: the stub didn't open it, $why" <<<"$log" ||
  fail "Torii's boot didn't log the refusal: $(grep -m1 'in-game menu' <<<"$log")"

if [ "$mode" = flip-image ]; then
  # The stub's record in page 0xF0 after the first run (A, back to the
  # game), read by the kernel's next boot (after a soft reset: page 0xF0
  # has no battery): the dialog, the save question, then the log line
  rec=$(od -A n -t x1 -j $((0xF1C00)) -N 10 "$cart/sram.bin" | tr -d ' \n')
  [ "$rec" = 54494e52464544320300 ] || fail "no refusal record (\"TINRFED2\", reason 3) in cart SRAM: $rec"
  boot_log=$(OMEGA_DIR=$cart OMEGA_SOFT_RESET=1 emu/build-linux/mgba-shot dist/ezkernelnew.bin \
    -k 100:A -k 106:- -k 160:A -k 166:- -k 220:A -k 226:- -f 500 2>&1 >/dev/null)
  grep -q "dialog: In-game menu didn't open: Hello: $said" <<<"$boot_log" ||
    fail "the kernel's next boot didn't say why the menu didn't open"
  grep -q "in-game menu: the stub didn't open it, $why" <<<"$boot_log" ||
    fail "the kernel's next boot didn't log the refusal"
fi
rm -rf "$cart-held"
if [ "$mode" = emerald ]; then
  echo "PASS: Pokémon Emerald's refused menu (cart SRAM failing) shows its colour, and the menu keys held 2 s go back to Torii, whose boot says why from the record in IWRAM, under the game's System stack"
  exit 0
fi
echo "PASS: with cart SRAM failing in game mode ($mode) the stub leaves the menu shut and shows the reason's colour: A gives the game back, the menu keys held 2 s go back to Torii, which says why"
