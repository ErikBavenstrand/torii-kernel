#!/usr/bin/env bash
# Real cheat codes in real games (v0.7): each game of tests/cheat_codes.toml
# that roms/ holds gets a card with its ROM and a cheat file picking its codes
# (CodeBreaker, GameShark v1/v2 encrypted and raw, Action Replay v3, VBA raw,
# and ROM patches), starts with its addons, and runs for ten seconds; then
# every address a code writes must hold the code's value (RAM writes repeat
# in each interrupt, so the game can't keep another there), and a ROM patch
# must read back from 0x08xxxxxx. The same file runs on the ROM in plain mGBA
# with mGBA's own cheat engine (mgba-shot -E), whose memory must show the
# same values at the same game frame.
#
# Codes marked interpreter (conditionals, button activators) run one at a
# time, holding their keys from game frame 500 to the dump at 600, in both;
# Torii not writing them fails (CHEAT_ENGINE_STRICT=0 only reports it). A code whose
# condition is on the game's own state (no keys) can't be set up here: it is
# only reported. Games missing from roms/ are skipped.
# Runs inside the torii-emu container after `just build` and `just
# emu-headless` (`just compat` runs it).
set -euo pipefail

# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-cheat-codes"

shot=emu/build-linux/mgba-shot
strict=${CHEAT_ENGINE_STRICT-1}
# One line per game: file, then its codes as "desc|code|addr:width:value,...|interpreter|keys"
mapfile -t games < <(
  python3 - <<'EOF'
import tomllib
import os
codes = tomllib.load(open(os.environ.get("CHEAT_CODES", "tests/cheat_codes.toml"), "rb"))["code"]
games = {}
for c in codes:
    writes = ",".join(f"{w['address']:08X}:{w['width']}:{w['value']:X}" for w in c["decoded"])
    interp = "1" if c.get("interpreter") else "0"
    games.setdefault(c["game"], []).append(f"{c['format']} {c['desc']}|{c['code']}|{writes}|{interp}|{c.get('keys', '')}")
for game, cs in games.items():
    print("\t".join([game, *cs]))
EOF
)

# card GAME CHT: a fresh card in $cart with the game as /Game.gba and CHT as its cheat file
card() {
  rm -rf "$cart" && mkdir -p "$cart"
  local kb=$((($(stat -c %s "roms/$1") / 1024 + 16384) / 1024 * 1024))
  [ "$kb" -ge 65536 ] || kb=65536
  mkfs.fat -C -F 32 -n CHEATS "$cart/sd.img" "$kb" >/dev/null
  mcopy -i "$cart/sd.img" "roms/$1" "::/Game.gba"
  mmd -i "$cart/sd.img" ::/torii ::/torii/cheats
  mcopy -i "$cart/sd.img" "$2" ::/torii/cheats/Game.cht
}

# cht DESC CODE...: a libretro file, every cheat picked (pairs of desc and code)
cht() {
  printf 'cheats = %d\n\n' $(($# / 2))
  local i=0
  while [ $# -gt 0 ]; do
    printf 'cheat%d_desc = "%s"\ncheat%d_code = "%s"\ncheat%d_enable = true\n\n' $i "$1" $i "$2" $i
    shift 2
    i=$((i + 1))
  done
}

# at ADDR WIDTH: where a store of WIDTH bytes to ADDR lands: the CPU (and
# mGBA's engine) aligns it, so a 16-bit code at an odd address writes the
# halfword below
at() { printf '%08X' $((0x$1 & ~($2 - 1))); }

# value FILE ADDR: the bytes a run dumped at ADDR, as hex
value() { sed -n "s/^mem frame [0-9]* $2: //p" "$1" | head -1 | tr -d ' ' | tr A-F a-f; }
want() { python3 -c "import sys; print(int(sys.argv[1], 16).to_bytes(int(sys.argv[2]), 'little').hex())" "$1" "$2"; }

# check WRITES TORII MGBA: sets $tbad (Torii's misses) and $mbad (mGBA's)
check() {
  local w addr width val v
  tbad='' mbad=''
  IFS=, read -r -a ws <<<"$1"
  for w in "${ws[@]}"; do
    IFS=: read -r addr width val <<<"$w"
    v=$(want "$val" "$width")
    addr=$(at "$addr" "$width")
    [ "$(value "$2" "$addr")" = "$v" ] || tbad="$tbad $addr=$(value "$2" "$addr") (want $v)"
    [ "$(value "$3" "$addr")" = "$v" ] || mbad="$mbad $addr=$(value "$3" "$addr") (want $v)"
  done
}

# dumps WRITES PREFIX: -m options for each write's address at frame PREFIX600
dumps() {
  local w addr width
  IFS=, read -r -a ws <<<"$1"
  for w in "${ws[@]}"; do
    IFS=: read -r addr width _ <<<"$w"
    printf '%s\n' -m "${2}600:0x$(at "$addr" "$width"):$width"
  done
}

# play_game N LINE: one game's codes (LINE from the list above), on carts of
# its own, so the games run side by side
play_game() {
  local f game group writes_all all tdumps mdumps tkeys mkeys hotkey what
  IFS=$'\t' read -r -a f <<<"$2"
  game=${f[0]}
  if [ ! -f "roms/$game" ]; then
    echo "SKIP: cheat_codes: $game isn't in roms/"
    return
  fi
  local cart=$carts/game$1 cht=$carts/Game$1.cht
  # The constant codes together, as before; each interpreter code on its own
  group=() writes_all=()
  for ((i = 1; i < ${#f[@]}; i++)); do
    IFS='|' read -r desc code writes interp _ <<<"${f[$i]}"
    if [ "$interp" = 0 ]; then
      group+=("$desc" "$code")
      writes_all+=("$writes")
    fi
  done
  if [ ${#group[@]} -gt 0 ]; then
    cht "${group[@]}" >"$cht"
    card "$game" "$cht"
    all=$(
      IFS=,
      echo "${writes_all[*]}"
    )
    mapfile -t tdumps < <(dumps "$all" @)
    mapfile -t mdumps < <(dumps "$all" "")
    # shellcheck disable=SC2046 # nav.sh's options
    OMEGA_DIR=$cart $shot dist/ezkernelnew.bin $(nav_launch /Game.gba addon) \
      -w "launch: starting the game" "${tdumps[@]}" -f 6000 >"$cart/run.txt" 2>&1 || true
    $shot "roms/$game" -E "$cht" "${mdumps[@]}" >"$cart/mgba.txt" 2>&1 || true
    if ! grep -q "launch: starting the game" "$cart/run.txt"; then
      echo "FAIL: cheat_codes: $game didn't start: $(grep -a '^kernel' "$cart/run.txt" | tail -2)"
    else
      for ((i = 1; i < ${#f[@]}; i++)); do
        IFS='|' read -r desc code writes interp _ <<<"${f[$i]}"
        [ "$interp" = 0 ] || continue
        check "$writes" "$cart/run.txt" "$cart/mgba.txt"
        if [ -z "$tbad$mbad" ]; then
          echo "PASS: cheat_codes: $game: $desc ($code), in Torii and in mGBA's engine"
        else
          echo "FAIL: cheat_codes: $game: $desc ($code):${tbad:+ Torii$tbad;}${mbad:+ mGBA engine$mbad;} $(grep -a 'cheats:' "$cart/run.txt" | tail -2 | tr '\n' ' ')"
        fi
      done
    fi
  fi
  for ((i = 1; i < ${#f[@]}; i++)); do
    IFS='|' read -r desc code writes interp keys <<<"${f[$i]}"
    [ "$interp" = 1 ] || continue
    cht "$desc" "$code" >"$cht"
    card "$game" "$cht"
    mapfile -t tdumps < <(dumps "$writes" @)
    mapfile -t mdumps < <(dumps "$writes" "")
    tkeys=() mkeys=()
    if [ -n "$keys" ]; then
      tkeys=(-k "@500:$keys")
      mkeys=(-k "500:$keys")
    fi
    # shellcheck disable=SC2046 # nav.sh's options
    OMEGA_DIR=$cart $shot dist/ezkernelnew.bin $(nav_launch /Game.gba addon) \
      -w "launch: starting the game" "${tkeys[@]}" "${tdumps[@]}" -f 6000 >"$cart/run.txt" 2>&1 || true
    $shot "roms/$game" -E "$cht" "${mkeys[@]}" "${mdumps[@]}" >"$cart/mgba.txt" 2>&1 || true
    check "$writes" "$cart/run.txt" "$cart/mgba.txt"
    hotkey=
    case "+$keys+" in *+L+*) case "+$keys+" in *+R+*) case "+$keys+" in *+SELECT+* | *+START+*)
      hotkey=" (holds Torii's sleep or menu hotkey, L+R+SELECT or L+R+START)"
      ;;
    esac ;; esac ;; esac
    what="$game: $desc ($code), keys ${keys:-none}$hotkey"
    if [ -z "$keys" ]; then
      echo "SKIP: cheat_codes: $what: its condition is on the game's state, not set up here; Torii:${tbad:- writes} / mGBA engine:${mbad:- writes}"
    elif [ -n "$mbad" ]; then
      echo "FAIL: cheat_codes: $what: mGBA's engine doesn't show it:$mbad; Torii:${tbad:- writes}"
    elif [ -z "$tbad" ]; then
      echo "PASS: cheat_codes: $what, in Torii and in mGBA's engine"
    elif [ "$strict" = 0 ]; then
      echo "SKIP: cheat_codes: $what: pending the interpreter: Torii$tbad; $(grep -a 'cheats: cheat' "$cart/run.txt" | tail -1)"
    else
      echo "FAIL: cheat_codes: $what: Torii$tbad; $(grep -a 'cheats:' "$cart/run.txt" | tail -2 | tr '\n' ' ')"
    fi
  done
}

logs=$(mktemp -d)
pids=()
for i in "${!games[@]}"; do
  play_game "$i" "${games[$i]}" >"$logs/$i" 2>&1 &
  pids+=($!)
done
for i in "${!games[@]}"; do
  wait "${pids[$i]}" || echo "FAIL: cheat_codes: ${games[$i]%%$'\t'*}: the runner stopped"
  cat "$logs/$i"
done >"$logs/all"
cat "$logs/all"
passed=$(grep -c '^PASS' "$logs/all" || true)
failed=$(grep -c '^FAIL' "$logs/all" || true)
pending=$(grep -c '^SKIP: .*pending the interpreter' "$logs/all" || true)
skipped=$(($(grep -c '^SKIP' "$logs/all" || true) - pending))
rm -rf "$logs"
echo "cheat_codes: $passed passed, $failed failed, $pending pending the interpreter, $skipped skipped"
[ "$failed" = 0 ]
