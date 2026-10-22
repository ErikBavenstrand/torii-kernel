#!/usr/bin/env bash
# The power-cut rig (source/cutpoint.h, mgba-shot -X, tests/cutsweep.sh) on a
# throwaway scenario (tests/kernel/test_cutpoint.c: the test kernel started
# with SELECT and A held replaces /CUT/old.txt through a tmp file, then leaves
# a file open): cutpoints are counted, a cut at N leaves the SD image as the
# steps before it made it and no later, and the next power-on works.
# Runs inside the torii-emu container after `just build`, `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/cutsweep.sh
. tests/cutsweep.sh
local_carts dist/cart-cutpoint
cart=$carts/cut

fail() {
  echo "FAIL: cutpoint: $*" >&2
  exit 1
}

mkdir -p "$cart"
mkfs.fat -C -F 32 -n CUT "$cart/sd.img" 65536 >/dev/null
mmd -i "$cart/sd.img" ::/CUT
printf OLD >"$carts/old.txt"
mcopy -i "$cart/sd.img" "$carts/old.txt" ::/CUT/old.txt

scenario() { kernel=ezkerneltest.gba cut_run -k 1:SELECT+A -k 30:- -u 'CUT SCENARIO DONE' -f 3000 "$@"; }

# file NAME: its contents on the card, "-" if there is none
file() { mtype -i "$cart/sd.img" "::/CUT/$1" 2>/dev/null || printf -- -; }

# What each cutpoint's cut leaves, by the steps that finished before it
check() {
  local n=$1 name=$2 old tmp bak open
  old=$(file old.txt) tmp=$(file new.tmp) bak=$(file old.bak) open=$(file open.txt)
  case $n in
  0) [ "$old $tmp $bak" = "NEW - OLD" ] || fail "uncut: old.txt=$old new.tmp=$tmp old.bak=$bak" ;;
  1) [ "$old $tmp $bak" = "OLD NEW -" ] || fail "cut at $n ($name): old.txt=$old new.tmp=$tmp old.bak=$bak" ;;
  2) [ "$old $tmp $bak" = "- NEW OLD" ] || fail "cut at $n ($name): old.txt=$old new.tmp=$tmp old.bak=$bak" ;;
  3 | 4 | 5) [ "$old $tmp $bak" = "NEW - OLD" ] || fail "cut at $n ($name): old.txt=$old new.tmp=$tmp old.bak=$bak" ;;
  esac
  # The window and file buffer FatFs held are lost: the unclosed file is
  # not on the card, with or without data, until its f_close
  case $n in
  4) [ "$open" != OPEN ] || fail "cut at $n ($name): the unclosed file's data reached the card" ;;
  5 | 0) [ "$open" = OPEN ] || fail "cut at $n ($name): open.txt=$open" ;;
  esac
  # The recovery boot: the real kernel powers on, the card still readable
  cut_run -f 400
  [ "$cut_status" = 0 ] || fail "cut at $n ($name): the next power-on exited with $cut_status"
  grep -q '^crash at frame' "$cart/run.log" && fail "cut at $n ($name): the next power-on crashed"
  grep -q 'Torii kernel boot' "$cart/run.log" || fail "cut at $n ($name): the next power-on logged no boot line"
  return 0
}

cut_sweep scenario check
[ "$cut_count" = 5 ] || fail "$cut_count cutpoints, not 5"
echo "PASS: cutpoint: $cut_count cuts leave the card as the steps before them made it, and the next power-on works"
