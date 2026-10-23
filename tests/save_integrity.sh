#!/usr/bin/env bash
# The save copy cut at every step (source/cutpoint.h, tests/cutsweep.sh): the
# power-on copy of cart SRAM to /torii/saves/Hello.gba/Main.sav (Save_savefile and
# Save_recover in source/ezkernelnew.c), with the power cut after each
# FatFs step. After every cut:
#   - a good save exists: Main.sav is the old save or the new one, or, in the
#     gap of the swap, Main.sav.bak is the old save or Main.sav.tmp the new
#     (with no old save on the card, Main.sav is absent or the new one)
#   - the pending-save record is set until the copy is done
#   - the replaced save is never lost: it is Main.sav, or in its .bak, or a
#     version of the slot's history (/torii/backups/Hello.gba/Main), made before
#     the swap so a cut at any step keeps it, and a retried copy adds none twice
#   - a power-on that answers B at "Save sav?" (no copy) still recovers: a whole
#     save (old or new), no .tmp or .bak
#   - one that answers A ends with the new save, no .tmp or .bak, the record
#     cleared, and a clean fsck
# One part per run:
#   unknown-replace  a game missing from the save table, with a save on the
#                    card: the swap through .bak
#   unknown-new      the same with no save on the card yet
# Runs inside the torii-emu container after `just build` and `just emu-headless`.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
# shellcheck source=tests/cutsweep.sh
. tests/cutsweep.sh
part=${1:?usage: $0 unknown-replace|unknown-new}
local_carts "dist/cart-integrity-$part"
cart=$carts/integrity-$part
rm -rf "$cart"
mkdir -p "$cart"

fail() {
  echo "FAIL: save_integrity $part: $*" >&2
  [ ! -f "$cart/run.log" ] || tail -15 "$cart/run.log" >&2
  exit 1
}

random_data() { head -c "$1" /dev/urandom | tr '\000\377' '\001\002'; }
blanks() { head -c "$1" /dev/zero | tr '\000' '\377'; }
pending() { od -A n -t x2 -j $((0x790000)) -N 2 "$cart/kernel-nor.bin" | tr -d ' '; }
# card_file NAME OUT: the file's contents into OUT; fails if there is none
card_file() { mtype -i "$cart/sd.img" "::/torii/saves/Hello.gba/$1" >"$2" 2>/dev/null; }
hist_dir=::/torii/backups/Hello.gba/Main
# hist_versions: the history's .sav files, one name a line
hist_versions() { mdir -b -i "$cart/sd.img" "$hist_dir/" 2>/dev/null | grep -i '\.sav$' || true; }
# hist_has_old: 1 if some version is the old save
hist_has_old() {
  local v n=0
  while IFS= read -r v; do
    [ -n "$v" ] || continue
    mtype -i "$cart/sd.img" "$v" >"$carts/ver.sav" 2>/dev/null && is_old "$carts/ver.sav" && n=$((n + 1))
  done < <(hist_versions)
  echo "$n"
}
leftovers() { mdir -i "$cart/sd.img" ::/torii/saves/Hello.gba/ 2>/dev/null | grep -qiE 'tmp|bak'; }

# Starting state: the game launched (so the pending-save record is set), then
# its save changed in the chip.
mkfs.fat -C -F 32 -n SAVES "$cart/sd.img" 65536 >/dev/null
mcopy -i "$cart/sd.img" tests/roms/hello/hello.gba ::/Hello.gba
random_data 4096 >"$carts/a.bin"
random_data 200 >"$carts/b.bin"
{
  cat "$carts/a.bin"
  blanks $((65536 - 4096))
} >"$carts/old.sav"
if [ "$part" = unknown-replace ]; then
  mmd -i "$cart/sd.img" ::/torii ::/torii/saves ::/torii/saves/Hello.gba
  mcopy -i "$cart/sd.img" "$carts/old.sav" ::/torii/saves/Hello.gba/Main.sav
else
  : >"$carts/old.sav" # no old save: is_old never matches
fi
# shellcheck disable=SC2046 # nav.sh's options
cut_run $(nav_launch /Hello.gba at=180) -f 600
grep -q "launch: starting the game" "$cart/run.log" || fail "the setup launch didn't start the game"
# The game (hello.gba) wrote a boot record of its own; the test sets the chip:
# the old save with a part changed (a new game: random data)
{
  cat "$carts/a.bin"
  blanks $((65536 - 4096))
} >"$carts/chip.sav"
dd if="$carts/b.bin" of="$carts/chip.sav" bs=1 seek=100 conv=notrunc 2>/dev/null
dd if="$carts/chip.sav" of="$cart/sram.bin" bs=65536 count=1 conv=notrunc 2>/dev/null
[ "$(pending)" = 0011 ] || fail "no pending-save record after the setup launch"

scenario() { cut_run -k 100:A -k 106:- -f 900 "$@"; }

is_new() { cmp -s "$1" "$carts/chip.sav"; }
is_old() { [ -s "$carts/old.sav" ] && cmp -s "$1" "$carts/old.sav"; }

# The card as the cut left it: some whole save exists
check_survives() {
  local n=$1 name=$2 f="$carts/now.sav"
  if card_file Main.sav "$f"; then
    is_new "$f" || is_old "$f" || fail "cut at $n ($name): Main.sav is neither the old save nor the new one"
    # A new Main.sav means the old save went into the history first
    if [ "$part" = unknown-replace ] && is_new "$f" && [ "$(hist_has_old)" -lt 1 ]; then
      fail "cut at $n ($name): Main.sav is the new save and the old one is not in the history"
    fi
  elif [ "$part" = unknown-new ]; then
    : # nothing to lose yet
  elif card_file Main.sav.bak "$f" && is_old "$f"; then
    :
  elif [ "$(hist_has_old)" -ge 1 ]; then
    :
  elif card_file Main.sav.tmp "$f" && is_new "$f"; then
    :
  else
    fail "cut at $n ($name): no whole save on the card"
  fi
}

# After a recovery boot: nothing but a whole Main.sav left
check_whole() {
  local n=$1 name=$2 want=$3 f="$carts/now.sav"
  if card_file Main.sav "$f"; then
    case $want in
    new) is_new "$f" || fail "cut at $n ($name): Main.sav isn't the new save after the recovery boot" ;;
    *) is_new "$f" || is_old "$f" || fail "cut at $n ($name): Main.sav is neither save after the recovery boot" ;;
    esac
  else
    [ "$part" = unknown-new ] && [ "$want" = any ] || fail "cut at $n ($name): no Main.sav after the recovery boot"
  fi
  ! leftovers || fail "cut at $n ($name): a .tmp or .bak is left after the recovery boot"
  if [ "$part" = unknown-replace ] && [ "$want" = new ]; then
    # The retried copy pushed the old save once, not twice
    [ "$(hist_has_old)" = 1 ] || fail "cut at $n ($name): the old save is in the history $(hist_has_old) times, not once"
  fi
  fsck.fat -n "$cart/sd.img" >/dev/null || fail "cut at $n ($name): fsck.fat found errors"
}

check() {
  local n=$1 name=$2 b="$carts/integrity-$part-b" main=$cart
  if [ "$n" = 0 ]; then
    check_whole "$n" "$name" new
    [ "$(pending)" = 0000 ] || fail "uncut: the pending-save record is still set"
    return 0
  fi
  check_survives "$n" "$name"
  if [ "$name" = "save: pending record cleared" ]; then
    [ "$(pending)" = 0000 ] || fail "cut at $n ($name): the record is still set"
  else
    [ "$(pending)" = 0011 ] || fail "cut at $n ($name): the record is $(pending), not set"
  fi
  # B at "Save sav?" on a copy of the cart: the recovery still happens
  rm -rf "$b"
  cp -a "$cart" "$b"
  cart=$b
  cut_run -k 100:B -k 106:- -f 500
  [ "$cut_status" = 0 ] || fail "cut at $n ($name): the B boot exited with $cut_status"
  ! grep -q '^crash at frame' "$cart/run.log" || fail "cut at $n ($name): the B boot crashed"
  check_whole "$n" "$name" any
  cart=$main
  # A: the copy is retried (or was done) and the new save is the file
  cut_run -k 100:A -k 106:- -f 900
  [ "$cut_status" = 0 ] || fail "cut at $n ($name): the recovery boot exited with $cut_status"
  ! grep -q '^crash at frame' "$cart/run.log" || fail "cut at $n ($name): the recovery boot crashed"
  check_whole "$n" "$name" new
  [ "$(pending)" = 0000 ] || fail "cut at $n ($name): the record is still set after the recovery boot"
  return 0
}

cut_sweep scenario check
echo "PASS: save_integrity $part: $cut_count cuts; a good save survives each, and the recovery boot ends with the new save"
