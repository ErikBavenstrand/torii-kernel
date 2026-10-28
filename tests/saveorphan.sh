#!/usr/bin/env bash
# The global save manager's card side (source/saveorphan.c) with the power cut
# after each of its steps (source/cutpoint.h, tests/cutsweep.sh). The test
# kernel started with SELECT and START held runs the scenario named in
# /SLOT/scenario.txt (tests/kernel/test_saveorphan.c, orphan_scenario) on a card
# set up here; after every cut:
#   - nothing is lost: each save in the starting state is still on the card
#     somewhere (the old place, the new one, the trash)
#   - a power-on of the real kernel (saveorphan_recover) leaves the operation
#     done or not done, never half: all of the orphan's files are in one place,
#     no journal is left, and fsck.fat finds nothing
# One scenario per run:
#   relink        an orphan with two slots and a history moves to a ROM with no saves
#   relink-clash  the same to a ROM that has a Main and a history folder of the other slot's name
#   trash         the orphan goes to the trash
#   empty         the trash is emptied (a cut leaves some folders; a second run finishes)
# The -split variants make the folders' first sectors full, and cut before
# every SD write too (a cut inside f_rename leaves two names on one chain).
# Saves are 512 bytes of one character, so a listing says which is which
# (tests/slotstore_tree.py). Runs inside the torii-emu container after
# `just build` and `just emu-headless`, with the test kernel built.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/cutsweep.sh
. tests/cutsweep.sh
# Several scenarios run one after the other (tests/all.sh groups the short ones)
if [ $# -gt 1 ]; then
  for sc in "$@"; do "$0" "$sc" || exit 1; done
  exit 0
fi
part=${1:?usage: $0 SCENARIO...}
base=${part%-split}
local_carts "dist/cart-saveorphan-$part"
cart=$carts/saveorphan-$part
rm -rf "$cart"
mkdir -p "$cart"

fail() {
  echo "FAIL: saveorphan $part: $*" >&2
  [ ! -f "$cart/run.log" ] || tail -12 "$cart/run.log" >&2
  exit 1
}

img=$cart/sd.img
mkfs.fat -C -F 32 -n ORPH "$img" 65536 >/dev/null

card_dir() {
  local dir=${1#::} part='' IFS=/
  for p in $dir; do
    [ -n "$p" ] || continue
    part+=/$p
    mmd -i "$img" "::$part" 2>/dev/null || true
  done
}
card_save() { # CHAR SIZE ::/PATH
  local f=$carts/tmp.sav
  head -c "$2" /dev/zero | tr '\000' "$1" >"$f"
  card_dir "$(dirname "$3")"
  mcopy -i "$img" "$f" "$3"
}
card_text() { # TEXT ::/PATH
  printf '%s' "$1" >"$carts/tmp.txt"
  card_dir "$(dirname "$2")"
  mcopy -i "$img" "$carts/tmp.txt" "$2"
}

OS=/torii/saves/G/Old.gba OB=/torii/backups/G/Old.gba
NS=/torii/saves/G/New.gba NB=/torii/backups/G/New.gba
H1=0001-20260101-000000.sav

list() {
  rm -rf "$carts/tree"
  mkdir -p "$carts/tree"
  mcopy -s -n -i "$img" ::/torii "$carts/tree/" 2>/dev/null || true
  mcopy -s -n -i "$img" ::/G "$carts/tree/" 2>/dev/null || true
  python3 tests/slotstore_tree.py "$carts/tree"
}
have() { grep -qP "\t$3\t$2\$" <<<"$1"; }
is() { grep -qxP "\Q$2\E\t$4\t$3" <<<"$1"; }
no() { ! grep -qP "^\Q$2\E(\t|/|\$)" <<<"$1"; }

kernel_args=(-k 1:SELECT+START -k 30:-)
clock=(-C 1785542400)
# shellcheck disable=SC2119,SC2120 # the cut sweep passes -X N
scenario() { kernel=ezkerneltest.gba cut_run "${clock[@]}" "${kernel_args[@]}" -u 'SLOT SCENARIO DONE' -f 3000 "$@"; }

required=()
orphan() { # the orphan's own: two slots, a record and a history each
  card_save A 512 ::$OS/Main.sav
  card_save B 512 ::$OS/Boss.sav
  card_text "save_type = 4
boot_mode = 1
slot = Boss
" ::$OS/game.ini
  card_save H 512 ::$OB/Main/$H1
  card_save J 512 ::$OB/Boss/$H1
}
case $base in
relink)
  card_save R 512 ::/G/New.gba
  orphan
  required=("A 512" "B 512" "H 512" "J 512")
  ;;
relink-clash)
  card_save R 512 ::/G/New.gba
  orphan
  card_save T 512 ::$NS/Main.sav
  card_text "save_type = 2
boot_mode = 0
slot = Main
" ::$NS/game.ini
  card_save U 512 ::$NB/Main/$H1
  card_save V 512 ::$NB/Boss/$H1
  required=("A 512" "B 512" "H 512" "J 512" "T 512" "U 512" "V 512")
  ;;
trash)
  orphan
  required=("A 512" "B 512" "H 512" "J 512")
  ;;
empty)
  card_save X 512 ::/torii/trash/20260101-000000-1/G/Old.gba/Main.sav
  card_save Y 512 ::/torii/trash/20260101-000000-1/backups/G/Old.gba/Main/$H1
  card_save Z 512 ::/torii/trash/20260102-000000-1/G/Other.gba/Main.sav
  card_save A 512 ::/torii/saves/G/Live.gba/Main.sav
  card_save R 512 ::/G/Live.gba
  required=("A 512")
  ;;
*) fail "unknown scenario $part" ;;
esac
card_text "orphan-$base
" ::/SLOT/scenario.txt
if [ "$base" != "$part" ]; then
  for i in $(seq -w 1 20); do
    card_text '' "::/torii/saves/G/PAD$i.TXT"
    card_text '' "::/torii/backups/G/PAD$i.TXT"
    card_text '' "::/torii/trash/PAD$i.TXT"
    card_text '' "::/torii/trash/20260101-000000-1/PAD$i.TXT"
  done
fi

recovery_boot() {
  cut_run "${clock[@]}" -f 400
  [ "$cut_status" = 0 ] || fail "$1: the recovery boot exited with $cut_status"
  ! grep -q '^crash at frame' "$cart/run.log" || fail "$1: the recovery boot crashed"
  grep -q 'Torii kernel boot' "$cart/run.log" || fail "$1: the recovery boot logged no boot line"
}

# Where the orphan's files are: "old" (all in the old folders), "new" or, for
# the trash, "trash"; anything mixed fails
converged() {
  local l=$1 where=$2 f
  case $base in
  relink)
    if is "$l" torii/saves/G/Old.gba/Main.sav A 512; then
      is "$l" torii/saves/G/Old.gba/Boss.sav B 512 || fail "$where: Boss.sav left the old place alone"
      is "$l" "torii/backups/G/Old.gba/Main/$H1" H 512 || fail "$where: Main's history isn't with the save"
      is "$l" "torii/backups/G/Old.gba/Boss/$H1" J 512 || fail "$where: Boss's history isn't with the save"
      no "$l" torii/saves/G/New.gba/Main.sav || fail "$where: Main.sav is in both places"
      no "$l" torii/saves/G/New.gba/Boss.sav || fail "$where: Boss.sav is in both places"
    else
      is "$l" torii/saves/G/New.gba/Main.sav A 512 || fail "$where: Main.sav is nowhere"
      is "$l" torii/saves/G/New.gba/Boss.sav B 512 || fail "$where: Boss.sav is nowhere"
      is "$l" "torii/backups/G/New.gba/Main/$H1" H 512 || fail "$where: Main's history didn't follow"
      is "$l" "torii/backups/G/New.gba/Boss/$H1" J 512 || fail "$where: Boss's history didn't follow"
      grep -qP '^torii/saves/G/New.gba/game.ini\t' <<<"$l" || fail "$where: the record didn't follow"
      no "$l" torii/saves/G/Old.gba || fail "$where: the old save folder is left"
      no "$l" torii/backups/G/Old.gba || fail "$where: the old history folder is left"
      f=$(mtype -i "$img" ::$NS/game.ini | sed -n 's/^slot = //p' | tr -d '\r')
      [ "$f" = Boss ] || fail "$where: the record says $f, not Boss"
    fi
    ;;
  relink-clash)
    # The target's own never change
    is "$l" torii/saves/G/New.gba/Main.sav T 512 || fail "$where: the target's Main.sav changed"
    is "$l" "torii/backups/G/New.gba/Main/$H1" U 512 || fail "$where: the target's Main history changed"
    is "$l" "torii/backups/G/New.gba/Boss/$H1" V 512 || fail "$where: the target's Boss history changed"
    if is "$l" torii/saves/G/Old.gba/Main.sav A 512; then
      is "$l" torii/saves/G/Old.gba/Boss.sav B 512 || fail "$where: Boss.sav left the old place alone"
      is "$l" "torii/backups/G/Old.gba/Main/$H1" H 512 || fail "$where: Main's history isn't with the save"
      is "$l" "torii/backups/G/Old.gba/Boss/$H1" J 512 || fail "$where: Boss's history isn't with the save"
      no "$l" "torii/saves/G/New.gba/Main 2.sav" || fail "$where: a save is in both places"
    else
      is "$l" "torii/saves/G/New.gba/Main 2.sav" A 512 || fail "$where: Main 2.sav is nowhere"
      is "$l" "torii/saves/G/New.gba/Boss 2.sav" B 512 || fail "$where: Boss 2.sav is nowhere"
      is "$l" "torii/backups/G/New.gba/Main 2/$H1" H 512 || fail "$where: Main 2's history didn't follow"
      is "$l" "torii/backups/G/New.gba/Boss 2/$H1" J 512 || fail "$where: Boss 2's history didn't follow"
      no "$l" torii/saves/G/Old.gba || fail "$where: the old save folder is left"
      no "$l" torii/backups/G/Old.gba || fail "$where: the old history folder is left"
      f=$(mtype -i "$img" ::$NS/game.ini | sed -n 's/^slot = //p' | tr -d '\r')
      [ "$f" = Main ] || fail "$where: the target's record says $f, not Main"
    fi
    ;;
  trash)
    if is "$l" torii/saves/G/Old.gba/Main.sav A 512; then
      is "$l" torii/saves/G/Old.gba/Boss.sav B 512 || fail "$where: Boss.sav left the old place alone"
      is "$l" "torii/backups/G/Old.gba/Main/$H1" H 512 || fail "$where: Main's history isn't with the save"
      ! grep -qP '^torii/trash/[^/]*/G/Old.gba' <<<"$l" || fail "$where: in the trash and in the saves"
    else
      grep -qP '^torii/trash/[^/]*/G/Old.gba/Main.sav\t512\tA$' <<<"$l" || fail "$where: Main.sav is nowhere"
      grep -qP '^torii/trash/[^/]*/G/Old.gba/Boss.sav\t512\tB$' <<<"$l" || fail "$where: Boss.sav is nowhere"
      grep -qP '^torii/trash/[^/]*/backups/G/Old.gba/Main/'"$H1"'\t512\tH$' <<<"$l" || fail "$where: Main's history is nowhere"
      grep -qP '^torii/trash/[^/]*/backups/G/Old.gba/Boss/'"$H1"'\t512\tJ$' <<<"$l" || fail "$where: Boss's history is nowhere"
      no "$l" torii/saves/G/Old.gba || fail "$where: the save folder is left"
      no "$l" torii/backups/G/Old.gba || fail "$where: the history folder is left"
    fi
    ;;
  empty)
    is "$l" torii/saves/G/Live.gba/Main.sav A 512 || fail "$where: a live save was touched"
    ;;
  esac
  ! grep -qP '^torii/orphanop.txt' <<<"$l" || fail "$where: the journal is left"
  fsck.fat -n "$img" >"$cart/fsck.txt" 2>&1 || true
  ! grep -vE '^(fsck\.fat |Reclaimed [0-9]+ unused clusters? |Free cluster summary |  (Auto-)?correct|Leaving filesystem unchanged|.*: [0-9]+ files, |FATs differ but appear to be intact|  Using first FAT|$)' \
    "$cart/fsck.txt" >"$cart/fsck-bad.txt" || fail "$where: fsck.fat: $(head -4 "$cart/fsck-bad.txt")"
}

check() {
  local n=$1 name=$2 l where="cut $1 ($2)"
  [ "$n" != 0 ] || where="uncut"
  l=$(list)
  for t in "${required[@]}"; do
    # shellcheck disable=SC2086 # CHAR SIZE
    have "$l" $t || fail "$where: save $t is gone"
  done
  recovery_boot "$where"
  l=$(list)
  converged "$l" "$where, after the recovery boot"
  if [ "$base" = empty ] && [ "$n" != 0 ]; then
    # The scenario run again finishes it
    scenario
    l=$(list)
    ! grep -qP '^torii/trash/' <<<"$l" || fail "$where: the trash is not empty after a second run: $(grep -P '^torii/trash/' <<<"$l" | head -3)"
  fi
  [ "$n" != 0 ] || end_state "$l"
}

end_state() {
  local l=$1
  case $base in
  relink) is "$l" torii/saves/G/New.gba/Main.sav A 512 || fail "uncut: not relinked" ;;
  relink-clash) is "$l" "torii/saves/G/New.gba/Main 2.sav" A 512 || fail "uncut: not relinked" ;;
  trash) no "$l" torii/saves/G/Old.gba || fail "uncut: still there" ;;
  empty) ! grep -qP '^torii/trash/' <<<"$l" || fail "uncut: the trash is left" ;;
  esac
}

cut_sweep scenario check
msg="$cut_count cuts"
if [ "$base" != "$part" ]; then
  cart_restore "$carts/cutsweep-start" "$cart"
  OMEGA_TRACE=1 scenario
  first=$(sed -n '1,/SLOT SCENARIO START/p' "$cart/run.log" | grep -c '^omega [0-9]*: sd write' || true)
  total=$(grep -c '^omega [0-9]*: sd write' "$cart/run.log" || true)
  [ "$total" -gt "$first" ] || fail "the scenario made no SD writes"
  for n in $(seq $((first + 1)) "$total"); do
    cart_restore "$carts/cutsweep-start" "$cart"
    OMEGA_POWER_CUT=sd:write:$n scenario
    [ "$cut_status" = 75 ] || fail "no power cut before SD write $n (exit $cut_status)"
    check "w$n" "before SD write $n"
  done
  msg+=", $((total - first)) SD writes"
fi
echo "PASS: saveorphan $part: $msg; nothing is lost, and the next power-on leaves the operation done or not done"
