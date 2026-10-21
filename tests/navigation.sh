#!/usr/bin/env bash
# The navigation shell (source/screens/nav.c): tests/nav.sh's routes reach
# every item's page and fail clearly where a page isn't built yet; each
# Start on value starts where it says, a hidden item giving home facing the
# nearest one (T19); Last place comes back after a power cycle as home
# facing the item last faced (a page's or home's); every Start on value
# holds through power cycles, a game and the Reset hotkey, set in Settings
# too; and a deleted game leaves home's counts at once.
# Runs inside the torii-emu container after `just build` and `just
# emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-navigation${1:+-$1}"

fail() {
  echo "FAIL: navigation: $*" >&2
  [ ! -f "${log:-}" ] || grep '^kernel' "$log" | grep -v 'frame-' | tail -8 >&2
  exit 1
}

# A card with a GBA and a GB game, one played and one a favourite: home
# shows Recent, Favourites, GBA, GB, SD card, NOR, Settings, Info
export NAV_SHOWN="recent fav gba gb"
new_cart() {
  cart=$carts/$1
  rm -rf "$cart"
  mkdir -p "$cart"
  mkfs.fat -C -F 32 -n NAV "$cart/sd.img" 65536 >/dev/null
  mmd -i "$cart/sd.img" ::/SAVER ::/torii
  mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
  head -c 32768 /dev/zero >"$cart/zelda.gb"
  mcopy -i "$cart/sd.img" "$cart/zelda.gb" ::/Zelda.gb
  printf '/Hello.gba\n' >"$cart/recent.txt"
  mcopy -i "$cart/sd.img" "$cart/recent.txt" "::/SAVER/Recently play.txt"
  printf '/Zelda.gb\n' >"$cart/fav.txt"
  mcopy -i "$cart/sd.img" "$cart/fav.txt" ::/torii/favorites.txt
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin -f 1 >/dev/null 2>&1
}

# boot NAME OPTIONS...: one power-on, logged to $cart/NAME.log ($log)
boot() {
  log=$cart/$1.log
  shift
  local status=0
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" >"$log" 2>&1 || status=$?
  [ $status = 0 ] || fail "$(basename "$log"): mgba-shot exited with $status"
  ! grep -qE 'assert failed|^crash at frame|^kernel error' "$log" || fail "$(basename "$log"): the kernel failed"
}
logged() { grep -qF -- "$1" "$log" || fail "$(basename "$log") didn't log '$1'"; }

# start_on VALUE: the Start on setting (slot 32, source/settings_model.h)
start_on() {
  printf %b "\\x$(printf %02x "$1")\\x00" |
    dd of="$cart/kernel-nor.bin" bs=1 seek=$((0x7B0000 + 2 * 32)) conv=notrunc status=none
}

# Every route, from power-on on a card new to the menu (a route starts on
# home: Last place would start where the one before left)
new_cart routes
cp -r "$cart" "$carts/routes-new"
for place in home recent fav gba gb sd nor settings info "console gb"; do
  rm -rf "$cart" && cp -r "$carts/routes-new" "$cart"
  # shellcheck disable=SC2046,SC2086 # nav.sh's options; a place may be two words
  boot "to-${place// /-}" $(nav_to $place)
  case $place in
  home) logged "nav: home ready" ;;
  console*) logged "nav: page ${place#console } ready" ;;
  *) logged "nav: page $place ready" ;;
  esac
done
# A hidden item, and a page's own routes, fail clearly
! nav_to gbc >/dev/null 2>"$cart/err.txt" || fail "nav_to gbc didn't fail with GBC hidden"
grep -q "gbc isn't on home" "$cart/err.txt" || fail "nav_to gbc said: $(cat "$cart/err.txt")"
! nav_file /Hello.gba >/dev/null 2>"$cart/err.txt" || fail "nav_file without its rows didn't fail"
grep -q "1 parts but 0 rows" "$cart/err.txt" || fail "nav_file said: $(cat "$cart/err.txt")"
echo "PASS: tests/nav.sh reaches home and every page, and a route it can't take fails clearly"

# The Info page's routes: each reading page fills in and wraps its text
# (an assert fails one that doesn't fit), and Diagnostics' first page comes
# up from its row
new_cart info
cp -r "$cart" "$carts/info-new"
for page in controls ingame cart about diag; do
  rm -rf "$cart" && cp -r "$carts/info-new" "$cart"
  # shellcheck disable=SC2046 # nav.sh's options
  boot "info-$page" $(nav_info_page $page)
  [ $page = diag ] || logged "info: info-$page,"
done
! nav_info_page help >/dev/null 2>"$cart/err.txt" || fail "nav_info_page help didn't fail"
echo "PASS: tests/nav.sh reaches each Info page and Diagnostics from Info"

# Last place on a reading page is the Info item, never the page or its row:
# the boot after faces Info on home
rm -rf "$cart" && cp -r "$carts/info-new" "$cart"
# shellcheck disable=SC2046
boot info-last $(nav_info_page cart) -k @400:- -f 900
mtype -i "$cart/sd.img" ::/torii/recent.txt | grep -qx '@boot info' ||
  fail "recent.txt's boot position on Your cart: $(mtype -i "$cart/sd.img" ::/torii/recent.txt | head -1)"
boot info-last-2 -w "nav: home ready" -f 300
logged "nav: start on 0: home-info"
echo "PASS: Last place on an Info page comes back as home facing Info"

# Start on: each value, from the log (registry_start's tests cover the rest)
new_cart start
for case in "0 home-recent" "1 recent" "2 home-recent" "3 fav" "4 sd" "5 gba" "6 home-gb" "7 gb" "8 home-sd"; do
  read -r value place <<<"$case"
  start_on "$value"
  boot "start-$value" -w "nav: start on $value:" -f 300
  logged "nav: start on $value: $place"
  grep -qxF "kernel: nav: start on $value: $place" "$log" || fail "start on $value: $(grep -o 'nav: start on.*' "$log")"
done
echo "PASS: Start on starts on Last place, Recent, Home, Favourites, SD card and each console, hidden ones on home facing the nearest"

# Last place: a page left open is kept as its item once the menu rested
# long enough to write it, and comes back as home facing that item; then
# home turned to the next item; a boot that opens the item's page and
# comes back writes nothing
new_cart last
boot_line() { mtype -i "$cart/sd.img" ::/torii/recent.txt | head -1; }
# shellcheck disable=SC2046
boot 1 $(nav_to gb) -k @400:- -f 900
logged "safefile: writing /torii/recent.txt"
[ "$(boot_line)" = "@boot gb" ] || fail "recent.txt's boot position on the GB page: $(boot_line)"
boot 2 -w "nav: home ready" -k @10:RIGHT -k @14:- -k @400:- -f 900
logged "nav: start on 0: home-gb"
[ "$(boot_line)" = "@boot sd" ] || fail "recent.txt's boot position after RIGHT: $(boot_line)"
boot 3 -w "nav: home ready" -k @10:A -k @14:- -w "nav: page sd ready" -k @10:B -k @14:- -w "nav: home ready" -k @400:- -f 900
logged "nav: start on 0: home-sd"
! grep -q "safefile: writing" "$log" || fail "a boot that stayed on its item wrote recent.txt"
# An older kernel's lines (as hand edits, which win over the copy): a page
# and its row, and home's "home-" word
for line in "@boot gb 3" "@boot home-gb 0"; do
  printf '%s\n/Hello.gba\n' "$line" >"$cart/recent.txt"
  mcopy -o -i "$cart/sd.img" "$cart/recent.txt" ::/torii/recent.txt
  boot old -w "nav: home ready" -f 300
  logged "nav: start on 0: home-gb"
done
# Into the next item's page and out, back to the item on the card, then
# rested: nothing to write
printf '@boot gb\n/Hello.gba\n' >"$cart/recent.txt"
mcopy -o -i "$cart/sd.img" "$cart/recent.txt" ::/torii/recent.txt
boot 4 -w "nav: home ready" -k @10:RIGHT -k @14:- -k @22:A -k @26:- -w "nav: page sd ready" -k @10:B -k @14:- \
  -w "nav: home ready" -k @10:LEFT -k @14:- -k @400:- -f 900
logged "nav: start on 0: home-gb"
! grep -q "safefile: writing" "$log" || fail "turning away and back to the boot item wrote recent.txt"
echo "PASS: Last place comes back as home facing the item last faced, older kernels' lines read as their item, and a boot that stays on its item writes nothing"

# Start on through power cycles and games (D1), with Auto save on (no
# question at power-on after a game) and the Reset hotkey on (L+R+START)
setting() { printf %b "\\x$(printf %02x "$2")\\x00" | dd of="$cart/kernel-nor.bin" bs=1 seek=$((0x7B0000 + 2 * $1)) conv=notrunc status=none; }
started() { grep -o "nav: start on .*" "$log" | sed -n "${2:-1}p" | grep -qxF "nav: start on $1" || fail "$(basename "$log") didn't start on $1: $(grep -o 'nav: start on.*' "$log" | tr '\n' ' ')"; }
new_cart cycle
setting 14 1
setting 1 1
# Last place: GBA's page, A on its first game; the game runs, then power-off
boot 1 -w "nav: home ready" -k @8:RIGHT -k @12:- -k @16:RIGHT -k @20:- -k @24:A -k @28:- -w "nav: page gba ready" \
  -k @20:A -k @24:- -w "launch: starting" -f @600
started "0: home-recent"
boot 2 -w "nav: start on" -f @300
started "0: home-gba"
# Recent's page, A on its first game, then the Reset hotkey in the game:
# the kernel restarts facing Recent; RIGHT, rest, power-off
boot 3 -w "nav: home ready" -k @8:LEFT -k @12:- -k @16:LEFT -k @20:- -k @24:A -k @28:- -w "nav: page recent ready" \
  -k @20:A -k @24:- -w "launch: starting" -k @300:L+R+START -k @330:- -w "nav: start on" -w "nav: home ready" \
  -k @10:RIGHT -k @14:- -k @400:- -f @900
started "0: home-gba"
started "0: home-recent" 2
boot 4 -w "nav: start on" -f @300
started "0: home-fav"
# The SD card page, A on its first file (Hello.gba)
boot 5 -w "nav: home ready" -k @8:RIGHT -k @12:- -k @16:RIGHT -k @20:- -k @24:RIGHT -k @28:- -k @32:A -k @36:- \
  -w "nav: page sd ready" -k @20:A -k @24:- -w "launch: starting" -f @600
boot 6 -w "nav: start on" -f @300
started "0: home-sd"
# Settings sets Start on GBA (Last place, Recent, Home, Favourites, SD card,
# GBA), saved as it is confirmed, B at once; the next power-on opens GBA
# shellcheck disable=SC2046
boot 7 -w "nav: home ready" -k @8:RIGHT -k @12:- -k @16:RIGHT -k @20:- -k @24:A -k @28:- -w "nav: page settings ready" \
  $(nav_press DOWN DOWN DOWN A RIGHT RIGHT RIGHT RIGHT RIGHT A B) -w "nav: home ready" -f @30
logged "settings: row 3 set to GBA"
boot 8 -w "nav: start on" -f @300
started "5: gba"
# Every other value: a power-on, a game from the menu (the tests' hook), and
# a power-on after it, which starts in the same place
for case in "1 recent" "2 home-recent" "3 fav" "4 sd" "5 gba" "6 home-gb" "7 gb" "8 home-sd"; do
  read -r value place <<<"$case"
  setting 32 "$value"
  # shellcheck disable=SC2046
  boot "game-$value" $(nav_launch /Hello.gba) -w "launch: starting" -f @300
  started "$value: $place"
  boot "after-$value" -w "nav: start on" -f @300
  started "$value: $place"
done
echo "PASS: each Start on value, set in Settings or on the cart, starts where it says after power cycles, a game and the Reset hotkey; Last place faces the item a game started from"

# A deleted game (as a page will delete it, through actions.c) leaves home
# at once: Zelda.gb was the only GB game and the only favourite
new_cart delete
# shellcheck disable=SC2046
boot 1 $(nav_delete /Zelda.gb at=60) -f 400
logged "delete: /Zelda.gb"
logged "home: 6 items"
! mtype -i "$cart/sd.img" ::/torii/favorites.txt | grep -q Zelda || fail "Zelda.gb is still a favourite"
boot 2 -w "nav: home ready" -f 300
! grep -q "library: new index" "$log" || fail "the boot after the delete rebuilt the index"
echo "PASS: a deleted game leaves home, the favourites and the library index at once"

# The test hook takes an action only with its magic word (nav.h): a stray
# write to EWRAM can't start one
new_cart hook
boot 1 -P 60:nav_test_path:/Hello.gba -p 60:nav_test_action:1 -f 300
! grep -q "nav: test hook" "$log" || fail "the hook ran an action without its magic"
echo "PASS: the test hook takes no action without its magic word"
