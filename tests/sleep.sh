#!/usr/bin/env bash
# Sleep on the emulated cart, which sleeps as the real one does (emu/patches:
# the BIOS's Stop wakes only on a keypad, serial or Game Pak IRQ enabled in
# IE): launched with BOOT WITH ADDON, a game sleeps on the sleep keys
# (L+R+SELECT), stays asleep through other keys (the sleep keys again), wakes
# on START+SELECT, and runs again with its screen and sound as they were.
# On hello (tests/roms/hello) at FLASH 64K and FLASH 128K, and on Pokémon
# Emerald (FLASH 128K, its RTC, a cheat picked: the real cart's report) when
# roms/ has it. The in-game menu's Sleep row is in tests/menu.sh.
# Runs inside the torii-emu container.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-sleep${1:+-$1}"

fail() {
  echo "FAIL: $*" >&2
  [ -z "${log:-}" ] || grep -E '^(stop|wake|crash|kernel error)' <<<"$log" | tail -20 >&2
  exit 1
}

# sleep_game NAME ROM SAVE START [CHEAT]: NAME's cart with ROM on its card,
# launched with addons at save type SAVE; the game runs from frame START.
sleep_game() {
  local name=$1 rom=$2 save=$3 start=$4 cheat=${5:-}
  local cart=$carts/$name
  rm -rf "$cart"
  mkdir -p "$cart"
  mkfs.fat -C -F 32 -n SLEEP "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" "$rom" ::/Game.gba
  if [ -n "$cheat" ]; then
    printf 'cheats = 1\n\ncheat0_desc = "Cheat"\ncheat0_code = "%s"\ncheat0_enable = true\n' "$cheat" >"$cart/Game.cht"
    mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
    mcopy -i "$cart/sd.img" "$cart/Game.cht" ::/torii/cheats/Game.cht
  fi
  # English; In-game menu every game, save states and sleep on; the sleep
  # keys L+R+SELECT, the menu keys L+R+START (as tests/menu.sh)
  python3 tests/setnor.py set "$cart/kernel-nor.bin" 0=0xE1E1 1=1 2=1 3=1 4=0 5=9 6=8 7=2 8=9 9=8 10=3 11=1 12=0 13=1 25=1

  local before=$((start - 10)) sleep=$start again=$((start + 100)) wake=$((start + 200))
  local frames=$((start + 400))
  # shellcheck disable=SC2046 # $(nav_launch) is a list of options
  out=$(OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Game.gba addon save="$save" at=270) \
    -k "$sleep:L+R+SELECT" -k "$((sleep + 6)):-" -k "$again:L+R+SELECT" -k "$((again + 6)):-" \
    -k "$wake:START+SELECT" -k "$((wake + 30)):-" \
    -m "$before:0x04000000:2" -m "$before:0x04000084:2" -m "$frames:0x04000000:2" -m "$frames:0x04000084:2" \
    -H "$((sleep + 40)):$((wake + 28))" -H "$((wake + 100)):$frames" -f "$frames" 2>"$cart/log")
  log=$(cat "$cart/log")
  grep -q '^crash at frame' <<<"$log" && fail "$name: crashed"
  local stopped woke
  stopped=$(sed -n 's/^stop at frame \([0-9]*\)$/\1/p' <<<"$log" | head -1)
  woke=$(sed -n 's/^wake at frame \([0-9]*\)$/\1/p' <<<"$log" | head -1)
  [ -n "$stopped" ] && [ "$stopped" -ge "$sleep" ] && [ "$stopped" -lt $((sleep + 20)) ] ||
    fail "$name: L+R+SELECT at frame $sleep didn't sleep (stop at ${stopped:-none})"
  [ -n "$woke" ] || fail "$name: START+SELECT at frame $wake didn't wake it (a black screen nothing wakes)"
  [ "$woke" -ge "$wake" ] || fail "$name: woke at frame $woke, before START+SELECT (the sleep keys again at $again?)"
  # Blank while asleep, and while START+SELECT are still held: the game
  # goes on only once every key is up, so it never sees the wake keys
  [ "$(awk -v a=$((sleep + 40)) -v b=$((wake + 28)) '$1 == "hash" && $3 + 0 >= a && $3 + 0 <= b { print $4 }' <<<"$out" | sort -u | wc -l)" = 1 ] ||
    fail "$name: the screen changed while asleep or while the wake keys were held"
  [ "$(awk -v a=$((wake + 100)) '$1 == "hash" && $3 + 0 >= a { print $4 }' <<<"$out" | sort -u | wc -l)" -gt 1 ] ||
    fail "$name: the game didn't run again after waking"
  # The screen on (DISPCNT's forced blank off) and the sound's master
  # switch (SOUNDCNT_X bit 7) as before the sleep: their low bytes
  local disp_before snd_before disp_after snd_after
  read -r disp_before snd_before <<<"$(awk -v f=$before '$1 == "mem" && $3 == f { printf "0x%s ", $5 }' <<<"$out")"
  read -r disp_after snd_after <<<"$(awk -v f=$frames '$1 == "mem" && $3 == f { printf "0x%s ", $5 }' <<<"$out")"
  [ -n "${snd_after:-}" ] && [ $((disp_before & 0x80)) = 0 ] && [ $((disp_after & 0x80)) = 0 ] &&
    [ $((snd_after & 0x80)) = $((snd_before & 0x80)) ] ||
    fail "$name: the screen or the sound not back after waking (DISPCNT, SOUNDCNT_X: $disp_before $snd_before -> ${disp_after:-} ${snd_after:-})"
  echo "$name: slept at frame $stopped, woke at $woke"
}

sleep_game hello tests/roms/hello/hello.gba 4 700
sleep_game hello-flash128k tests/roms/hello/hello.gba 5 700
emerald="roms/Pokemon - Emerald Version (Erik).gba"
if [ -f "$emerald" ]; then
  # Loading 16 MB with patches takes about 2,400 frames; a cheat picked (a byte past the game's EWRAM)
  sleep_game emerald "$emerald" 5 3600 "0203FFF0:5A"
else
  echo "SKIP: Pokémon Emerald isn't in roms/"
fi
rm -rf "$carts"/{hello,hello-flash128k,emerald}
echo "PASS: L+R+SELECT puts a game to sleep (the BIOS's Stop), other keys don't wake it, START+SELECT does, and the game runs again with its screen and sound"
