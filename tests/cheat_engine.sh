#!/usr/bin/env bash
# The proof that Torii's GBA cheats work (v0.7): every code type of every
# format (tests/cheat_engine.py lists the cases, tools/cheat_encode.py makes
# their codes) runs on the cheat test ROM (tests/roms/cheattest) three ways:
# in plain mGBA with no cheats (the baseline), in plain mGBA with mGBA's own
# cheat engine (mgba-shot -E), and through Torii on the emulated cart with
# the case's file at /torii/cheats/CT.cht, every cheat picked, launched with
# its addons. The ROM's snapshots must match between the last two at every
# checkpoint (tests/cheat_engine.py says how they line up); each case's codes
# must also decode alike in mGBA's decoders and Torii's.
#
# Usage: cheat_engine.sh [N/M]: every case, or the Nth shard of M (tests/all.sh
# runs it as shards). Cases Torii can't run yet are reported "pending" and
# don't fail; CHEAT_ENGINE_STRICT=1 fails them too. CHEAT_ENGINE_JOBS runs that
# many cases at once (default 1). mGBA's runs hold the cheat device's own
# button (GameShark type 8, Action Replay v3 buttons) at some checkpoints;
# Torii's hold the keys CHEAT_BUTTON_KEYS names there instead (default
# R+SELECT, cheatcode.h's CHEAT_BUTTON_KEYS). Runs inside the torii-emu container after
# `just build`, `just emu-headless` and `just test-roms`.
set -euo pipefail

# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh

if [ "${1:-}" = --case ]; then
  # One case: DIR NAME (the carts' folder from the caller)
  dir=$2 name=$3 carts=$4
  shot=emu/build-linux/mgba-shot
  rom=tests/roms/cheattest/cheattest.gba
  $shot $rom -i "$dir/$name.opts" >"$dir/$name.base.txt" 2>"$dir/$name.base.log" || true
  $shot $rom -E "$dir/$name.cht" -i "$dir/$name.opts" >"$dir/$name.mgba.txt" 2>"$dir/$name.mgba.log" || true
  cart=$carts/$name
  rm -rf "$cart" && mkdir -p "$cart"
  mkfs.fat -C -F 32 -n CHEATS "$cart/sd.img" 65536 >/dev/null
  mcopy -i "$cart/sd.img" $rom ::/CT.gba
  mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
  mcopy -i "$cart/sd.img" "$dir/$name.cht" ::/torii/cheats/CT.cht
  # The device's button (-B, mGBA's) as the keys CHEAT_BUTTON_KEYS names (R and
  # SELECT, the interpreter's: source/cheatcode.h), held on top of each
  # checkpoint's own keys from -B 1 until -B 0
  awk -v button="${CHEAT_BUTTON_KEYS:-R+SELECT}" '
    function out() { print "-k @4:" (held ? (keys == "-" ? button : keys "+" button) : keys) }
    /^-k @4:/ { keys = substr($0, 7); out(); next }
    /^-B @4:/ { held = substr($0, 7) == "1"; out(); next }
    { print }
  ' "$dir/$name.opts" >"$cart/torii.opts"
  # shellcheck disable=SC2046 # nav.sh's options
  OMEGA_DIR=$cart $shot dist/ezkernelnew.bin $(nav_launch /CT.gba addon) -w "launch: starting the game" \
    -i "$cart/torii.opts" >"$dir/$name.torii.txt" 2>"$dir/$name.torii.log" || true
  rm -rf "$cart"
  exit 0
fi

shard=${1:-}
local_carts "dist/cart-cheat-engine${shard:+-${shard%/*}}"
dir=dist/cheat-engine${shard:+/${shard%/*}}
rm -rf "$dir" && mkdir -p "$dir"
start=$(date +%s)

python3 tests/cheat_engine.py gen "$dir" ${shard:+"$shard"}
# The codes decoded by Torii's decoders and by mGBA's
python3 tools/hostcc.py "$dir/cheat_corpus" tools/cheat_corpus.c source/cheatcode.c source/cheatlower.c
gcc -O1 -Wall -Werror -Iemu/mgba/include -Iemu/build-linux/include -o "$dir/cheat_oracle" \
  tools/cheat_oracle.c emu/build-linux/libmgba.a -llua5.4 -lpng -lz -lm -lpthread
mapfile -t chts < <(find "$dir" -name '*.cht' | sort)
"$dir/cheat_corpus" "${chts[@]}" >"$dir/torii.tsv" 2>/dev/null
"$dir/cheat_oracle" "${chts[@]}" >"$dir/mgba.tsv"

awk -F'\t' '$2 == 1 { print $1 }' "$dir/cases.txt" |
  xargs -P "${CHEAT_ENGINE_JOBS:-1}" -I{} "$0" --case "$dir" {} "$carts"
python3 tests/cheat_engine.py report "$dir" && status=0 || status=$?
echo "cheat_engine: $(wc -l <"$dir/cases.txt") cases in $(($(date +%s) - start)) s"
exit "$status"
