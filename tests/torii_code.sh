#!/usr/bin/env bash
# Torii code in a real launch (docs/torii-code.md): the hello test ROM with
# one picked cheat that counts the halfword at 0x0203F000 up to 0x10 in a
# block, and from then on writes AAAA at 0x0203F002 in its ELSE; writes 5555
# at 0x0203F004 while START is held (@); and adds 1 at 0x0203F006 for each
# press of A (@@), three here, held for several frames each. The
# interpreter runs it each frame as the game runs.
# Runs inside the torii-emu container after `just build`, `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-torii-code"

fail() {
  echo "FAIL: torii_code: $*" >&2
  exit 1
}

cart=$carts/tc
rm -rf "$cart"
mkdir -p "$cart"
mkfs.fat -C -F 32 -n CHEATS "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
code='0203F000<0010{+0203F000#0001+}{+0203F002:AAAA+}+@0008+0203F004:5555+@@0001+0203F006#0001'
printf '%s\n' 'cheats = 1' 'cheat0_desc = "Torii code"' "cheat0_code = \"$code\"" 'cheat0_enable = true' \
  >"$carts/Hello.cht"
mcopy -i "$cart/sd.img" "$carts/Hello.cht" ::/torii/cheats/Hello.cht

dump() { printf -- '-m @%d:0x0203F000:8 ' "$@"; }
# shellcheck disable=SC2046 # option lists
out=$(OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin $(nav_launch /Hello.gba addon) \
  -w "launch: starting the game" $(dump 8 100 400) \
  -k @300:START -k @310:- -k @320:A -k @326:- -k @340:A -k @346:- -k @360:A -k @366:- \
  -f @410 2>"$cart/log")
log=$(cat "$cart/log")
grep -q '^crash at frame' <<<"$log" && fail "crashed"
grep -q 'cheats: /torii/cheats/Hello.cht: 1 picked, 1 run in game' <<<"$log" ||
  fail "the launch: $(grep 'cheats:' <<<"$log" | head -3)"

# The four halfwords at a dump, as hex
at() { awk -v f="$1" '$1 == "mem" && $3 == f { print $6 $5 " " $8 $7 " " $10 $9 " " $12 $11 }' <<<"$out"; }
first=$(awk '$1 == "mem" { print $3; exit }' <<<"$out") # the dump at @8
[ -n "$first" ] || fail "no dumps: $(head -c 300 <<<"$out")"
[ "$(at $((first + 92)))" = "0010 AAAA 0000 0000" ] || fail "at frame 100: $(at $((first + 92)))"
[ "$(at $((first + 392)))" = "0010 AAAA 5555 0003" ] || fail "at frame 400: $(at $((first + 392)))"
echo "PASS: torii_code: a launch runs Torii code's block and ELSE, a held button's test and a press's, each frame"
