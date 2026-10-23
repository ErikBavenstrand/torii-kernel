#!/usr/bin/env bash
# The slot store (source/slotstore.c) with the power cut after each of its
# steps (source/cutpoint.h, tests/cutsweep.sh). The test kernel started with
# SELECT and START held runs the scenario named in /SLOT/scenario.txt
# (tests/kernel/test_slotstore.c, slot_scenario) on a card set up here; after
# every cut:
#   - nothing is lost: each save in the starting state is still on the card
#     somewhere (the slot, its history, a .tmp/.bak/.new beside it, the trash)
#   - a power-on of the real kernel (slotstore_recover) leaves the operation
#     done or not done, never half: the files, the history and the game's
#     record agree, no journal, .bak or .ren is left, and fsck.fat finds nothing
# One scenario per run:
#   push, restore, import, export, create-blank, create-copy, rename,
#   rename-case, delete, delete-current, retire, purge
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
base=${part%-split} # -split: the same, with the folders' first sectors full, and cut before every SD write
local_carts "dist/cart-slotstore-$part"
cart=$carts/slotstore-$part
rm -rf "$cart"
mkdir -p "$cart"

fail() {
  echo "FAIL: slotstore $part: $*" >&2
  [ ! -f "$cart/run.log" ] || tail -12 "$cart/run.log" >&2
  exit 1
}

img=$cart/sd.img
mkfs.fat -C -F 32 -n SLOT "$img" 65536 >/dev/null

# card_dir ::/DIR: makes the folder and its parents
card_dir() {
  local dir=${1#::} part='' IFS=/
  for p in $dir; do
    [ -n "$p" ] || continue
    part+=/$p
    mmd -i "$img" "::$part" 2>/dev/null || true
  done
}
# card_save CHAR SIZE ::/PATH: a save of SIZE bytes of CHAR (an underscore: 0xFF)
card_save() {
  local f=$carts/tmp.sav
  if [ "$1" = _ ]; then head -c "$2" /dev/zero | tr '\000' '\377' >"$f"; else head -c "$2" /dev/zero | tr '\000' "$1" >"$f"; fi
  card_dir "$(dirname "$3")"
  mcopy -i "$img" "$f" "$3"
}
card_text() { # TEXT ::/PATH
  printf '%s' "$1" >"$carts/tmp.txt"
  card_dir "$(dirname "$2")"
  mcopy -i "$img" "$carts/tmp.txt" "$2"
}

S=/torii/saves/G/Game.gba B=/torii/backups/G/Game.gba
ini() { card_text "save_type = 0
boot_mode = 0
slot = $1
" ::$S/game.ini; }
H1=0001-20260101-000000.sav H2=0002-20260102-000000.sav

# The card's tree, listed as slotstore_tree.py does
list() {
  rm -rf "$carts/tree"
  mkdir -p "$carts/tree"
  mcopy -s -n -i "$img" ::/torii "$carts/tree/" 2>/dev/null || true
  mcopy -s -n -i "$img" ::/G "$carts/tree/" 2>/dev/null || true
  python3 tests/slotstore_tree.py "$carts/tree"
}
# have LISTING CHAR SIZE: some file holds that save
have() { grep -qP "\t$3\t$2\$" <<<"$1"; }
# count LISTING CHAR SIZE: how many files hold it, not counting a .new/.tmp
count() { { grep -vP '\.(new|tmp)\t' <<<"$1" || true; } | grep -cP "\t$3\t$2\$" || true; }
# is LISTING PATH CHAR SIZE
is() { grep -qxP "\Q$2\E\t$4\t$3" <<<"$1"; }
no() { ! grep -qP "^\Q$2\E(\t|/|\$)" <<<"$1"; }
record_slot() { mtype -i "$img" ::$S/game.ini 2>/dev/null | sed -n 's/^slot = //p' | tr -d '\r'; }

kernel_args=(-k 1:SELECT+START -k 30:-)
# The clock stands at 2026-08-01 00:00:00 UTC (the purge's "now" too)
clock=(-C 1785542400)
# shellcheck disable=SC2119,SC2120 # the cut sweep passes -X N
scenario() { kernel=ezkerneltest.gba cut_run "${clock[@]}" "${kernel_args[@]}" -u 'SLOT SCENARIO DONE' -f 3000 "$@"; }

# Starting states, what must never be lost (REQUIRED: "CHAR SIZE" lines), and
# the state checks
required=()
case $base in
push)
  card_save B 512 ::$S/Main.sav
  card_save A 512 ::$B/Main/$H1
  ini Main
  required=("A 512" "B 512")
  ;;
restore)
  card_save C 512 ::$S/Main.sav
  card_save A 512 ::$B/Main/$H1
  card_save B 512 ::$B/Main/$H2
  ini Main
  required=("A 512" "B 512" "C 512")
  ;;
import)
  card_save A 512 ::$S/Main.sav
  card_save I 512 ::/G/in.sav
  ini Main
  required=("A 512" "I 512")
  ;;
export)
  card_dir ::/G
  card_save A 512 ::$S/Main.sav
  ini Main
  required=("A 512")
  ;;
create-blank | create-copy)
  card_save A 512 ::$S/Main.sav
  ini Main
  required=("A 512")
  ;;
rename | rename-case)
  card_save A 512 ::$S/Main.sav
  card_save H 512 ::$B/Main/$H1
  ini Main
  required=("A 512" "H 512")
  ;;
delete)
  card_save A 512 ::$S/Main.sav
  card_save E 512 ::$S/Extra.sav
  card_save F 512 ::$B/Extra/$H1
  ini Main
  required=("A 512" "E 512" "F 512")
  ;;
delete-current)
  card_save A 512 ::$S/Main.sav
  card_save E 512 ::$S/Extra.sav
  card_save F 512 ::$B/Main/$H1
  ini Main
  required=("A 512" "E 512" "F 512")
  ;;
retire)
  card_save Z 8192 ::$S/Main.sav
  card_save A 512 ::$B/Main/$H1
  ini Main
  required=("Z 8192" "A 512")
  ;;
purge)
  card_save O 512 ::/torii/trash/20260601-000000-1/G/Game.gba/Old.sav
  card_save P 512 ::/torii/trash/20260601-000000-1/backups/G/Game.gba/Old/$H1
  card_save N 512 ::/torii/trash/20260725-000000-1/G/Game.gba/New.sav
  card_save A 512 ::$S/Main.sav
  required=("N 512" "A 512")
  ;;
*) fail "unknown scenario $part" ;;
esac
card_text "$base
" ::/SLOT/scenario.txt
if [ "$base" != "$part" ]; then
  # Folders whose first sector is full: new entries land in another sector than
  # the old ones, so a cut inside f_rename can leave two names on one chain
  for i in $(seq -w 1 20); do
    card_text '' "::$S/PAD$i.TXT"
    card_text '' "::$B/PAD$i.TXT"
  done
fi

# A power-on of the real kernel: the recovery
recovery_boot() {
  cut_run "${clock[@]}" -f 400
  [ "$cut_status" = 0 ] || fail "$1: the recovery boot exited with $cut_status"
  ! grep -q '^crash at frame' "$cart/run.log" || fail "$1: the recovery boot crashed"
  grep -q 'Torii kernel boot' "$cart/run.log" || fail "$1: the recovery boot logged no boot line"
}

# What the scenario's operation leaves once finished or undone. $1: listing,
# $2: where it is: "cut N (name)"
converged() {
  local l=$1 where=$2 n s
  case $base in
  push)
    is "$l" torii/saves/G/Game.gba/Main.sav B 512 || fail "$where: Main.sav isn't the file"
    have "$l" A 512 || fail "$where: A is gone"
    [ "$(count "$l" B 512)" -le 2 ] || fail "$where: B is in the history twice"
    ;;
  restore)
    for t in A B C; do have "$l" $t 512 || fail "$where: $t is gone"; done
    f=$(grep -P '^torii/saves/G/Game.gba/Main.sav\t' <<<"$l" | cut -f3)
    [ "$f" = A ] || [ "$f" = C ] || fail "$where: Main.sav is $f, not A or C"
    ;;
  import)
    f=$(grep -P '^torii/saves/G/Game.gba/Main.sav\t' <<<"$l" | cut -f3)
    [ "$f" = A ] || [ "$f" = I ] || fail "$where: Main.sav is $f, not A or I"
    have "$l" A 512 || fail "$where: A is gone"
    have "$l" I 512 || fail "$where: I is gone"
    ;;
  export)
    ! grep -qP '^G/out.sav\t' <<<"$l" || is "$l" G/out.sav A 512 || fail "$where: out.sav is not whole"
    ;;
  create-blank)
    ! grep -qP '^torii/saves/G/Game.gba/Fresh.sav\t' <<<"$l" || is "$l" torii/saves/G/Game.gba/Fresh.sav _ 512 || fail "$where: Fresh.sav is not whole"
    ;;
  create-copy)
    ! grep -qP '^torii/saves/G/Game.gba/Copy.sav\t' <<<"$l" || is "$l" torii/saves/G/Game.gba/Copy.sav A 512 || fail "$where: Copy.sav is not whole"
    ;;
  rename)
    if no "$l" torii/saves/G/Game.gba/Main.sav; then
      is "$l" torii/saves/G/Game.gba/Start.sav A 512 || fail "$where: neither name has the save"
      is "$l" "torii/backups/G/Game.gba/Start/$H1" H 512 || fail "$where: the history didn't follow"
      no "$l" torii/backups/G/Game.gba/Main || fail "$where: the old history folder is left"
      [ "$(record_slot)" = Start ] || fail "$where: the record says $(record_slot), not Start"
    else
      is "$l" torii/saves/G/Game.gba/Main.sav A 512 || fail "$where: Main.sav isn't the save"
      no "$l" torii/saves/G/Game.gba/Start.sav || fail "$where: both names have a save"
      is "$l" "torii/backups/G/Game.gba/Main/$H1" H 512 || fail "$where: the history moved without the save"
      [ "$(record_slot)" = Main ] || fail "$where: the record says $(record_slot), not Main"
    fi
    ;;
  rename-case)
    n=$(mdir -b -i "$img" ::$S | grep -i 'main.sav$' | sed 's|.*/||' || true)
    s=$(mdir -b -i "$img" ::$B | sed 's|/$||; s|.*/||' | grep -i '^main$' || true)
    case "$n/$s/$(record_slot)" in
    Main.sav/Main/Main | MAIN.sav/MAIN/MAIN) ;;
    *) fail "$where: the save is $n, its history $s and the record $(record_slot)" ;;
    esac
    is "$l" "torii/saves/G/Game.gba/$n" A 512 || fail "$where: the save is not whole"
    ;;
  delete)
    if is "$l" torii/saves/G/Game.gba/Extra.sav E 512; then
      is "$l" "torii/backups/G/Game.gba/Extra/$H1" F 512 || fail "$where: Extra is back without its history"
      ! grep -qP '^torii/trash/.*Extra' <<<"$l" || fail "$where: Extra is in the trash and in the slots"
    else
      grep -qP '^torii/trash/[^/]*/G/Game.gba/Extra.sav\t512\tE$' <<<"$l" || fail "$where: Extra.sav is nowhere"
      grep -qP '^torii/trash/[^/]*/backups/G/Game.gba/Extra/'"$H1"'\t512\tF$' <<<"$l" || fail "$where: Extra's history is nowhere"
      no "$l" torii/backups/G/Game.gba/Extra || fail "$where: Extra's history is left behind"
    fi
    is "$l" torii/saves/G/Game.gba/Main.sav A 512 || fail "$where: Main.sav is damaged"
    [ "$(record_slot)" = Main ] || fail "$where: the record says $(record_slot), not Main"
    ;;
  delete-current)
    is "$l" torii/saves/G/Game.gba/Extra.sav E 512 || fail "$where: Extra.sav is gone"
    if is "$l" torii/saves/G/Game.gba/Main.sav A 512; then
      is "$l" "torii/backups/G/Game.gba/Main/$H1" F 512 || fail "$where: Main is back without its history"
    else
      grep -qP '^torii/trash/[^/]*/G/Game.gba/Main.sav\t512\tA$' <<<"$l" || fail "$where: Main.sav is nowhere"
      grep -qP '^torii/trash/[^/]*/backups/G/Game.gba/Main/'"$H1"'\t512\tF$' <<<"$l" || fail "$where: Main's history is nowhere"
      [ "$(record_slot)" = Extra ] || fail "$where: the record says $(record_slot), not Extra"
    fi
    r=$(record_slot)
    is "$l" "torii/saves/G/Game.gba/$r.sav" "$([ "$r" = Main ] && echo A || echo E)" 512 || fail "$where: the current slot $r has no save"
    ;;
  retire)
    if is "$l" torii/saves/G/Game.gba/Main.sav Z 8192; then :; else
      no "$l" torii/saves/G/Game.gba/Main.sav || fail "$where: Main.sav is damaged"
      grep -qP '^torii/backups/G/Game.gba/Main/0002-[0-9-]+\.sav\t8192\tZ$' <<<"$l" || fail "$where: the old save isn't in the history"
    fi
    ;;
  purge)
    is "$l" torii/trash/20260725-000000-1/G/Game.gba/New.sav N 512 || fail "$where: the young trash is damaged"
    ;;
  esac
  # Nothing half-done is left
  ! grep -qP '^torii/slotop.txt' <<<"$l" || fail "$where: the journal is left"
  ! grep -qP '\.(bak|ren)\t' <<<"$l" || fail "$where: a .bak or .ren is left: $(grep -P '\.(bak|ren)\t' <<<"$l")"
  fsck.fat -n "$img" >"$cart/fsck.txt" 2>&1 || true
  ! grep -vE '^(fsck\.fat |Reclaimed [0-9]+ unused clusters? |Free cluster summary |  (Auto-)?correct|Leaving filesystem unchanged|.*: [0-9]+ files, |FATs differ but appear to be intact|  Using first FAT|$)' \
    "$cart/fsck.txt" >"$cart/fsck-bad.txt" || fail "$where: fsck.fat: $(head -4 "$cart/fsck-bad.txt")"
}

check() {
  local n=$1 name=$2 l
  l=$(list)
  local where="cut $n ($name)"
  [ "$n" != 0 ] || where="uncut"
  for t in "${required[@]}"; do
    # shellcheck disable=SC2086 # CHAR SIZE
    have "$l" $t || fail "$where: save $t is gone"
  done
  recovery_boot "$where"
  l=$(list)
  converged "$l" "$where, after the recovery boot"
  if [ "$base" = purge ] && [ "$n" != 0 ]; then
    # The scenario run again finishes the purge
    scenario
    l=$(list)
    ! grep -qP '^torii/trash/20260601' <<<"$l" || fail "$where: the old trash is left after a second purge"
  fi
  [ "$n" != 0 ] || end_state "$l"
}

# What the uncut run ends with
end_state() {
  local l=$1
  case $base in
  push) grep -qP '^torii/backups/G/Game.gba/Main/0002-[0-9-]+\.sav\t512\tB$' <<<"$l" || fail "uncut: B isn't version 2" ;;
  restore)
    is "$l" torii/saves/G/Game.gba/Main.sav A 512 || fail "uncut: Main.sav isn't A"
    [ "$(count "$l" C 512)" = 2 ] || [ "$(count "$l" C 512)" = 1 ] || fail "uncut: C is lost"
    ;;
  import) is "$l" torii/saves/G/Game.gba/Main.sav I 512 || fail "uncut: Main.sav isn't I" ;;
  export) is "$l" G/out.sav A 512 || fail "uncut: no out.sav" ;;
  create-blank) is "$l" torii/saves/G/Game.gba/Fresh.sav _ 512 || fail "uncut: no Fresh.sav" ;;
  create-copy) is "$l" torii/saves/G/Game.gba/Copy.sav A 512 || fail "uncut: no Copy.sav" ;;
  rename) is "$l" torii/saves/G/Game.gba/Start.sav A 512 || fail "uncut: not renamed" ;;
  rename-case) [ "$(mdir -b -i "$img" ::$S | sed 's|.*/||' | grep -i 'main.sav$')" = MAIN.sav ] || fail "uncut: not renamed" ;;
  delete) no "$l" torii/saves/G/Game.gba/Extra.sav || fail "uncut: Extra.sav is still there" ;;
  delete-current)
    no "$l" torii/saves/G/Game.gba/Main.sav || fail "uncut: Main.sav is still there"
    [ "$(record_slot)" = Extra ] || fail "uncut: the record isn't Extra"
    ;;
  retire) no "$l" torii/saves/G/Game.gba/Main.sav || fail "uncut: Main.sav is still there" ;;
  purge) no "$l" torii/trash/20260601-000000-1 || fail "uncut: the old trash is still there" ;;
  esac
}

# retire's second version number depends on the starting history (version 1)
cut_sweep scenario check
msg="$cut_count cuts"
if [ "$base" != "$part" ]; then
  # A cut before each SD write of the scenario, counted from the cart trace
  # (some land inside an f_rename)
  rm -rf "$cart"
  cp -a "$carts/cutsweep-start" "$cart"
  OMEGA_TRACE=1 scenario
  first=$(sed -n '1,/SLOT SCENARIO START/p' "$cart/run.log" | grep -c '^omega [0-9]*: sd write' || true)
  total=$(grep -c '^omega [0-9]*: sd write' "$cart/run.log" || true)
  [ "$total" -gt "$first" ] || fail "the scenario made no SD writes"
  for n in $(seq $((first + 1)) "$total"); do
    rm -rf "$cart"
    cp -a "$carts/cutsweep-start" "$cart"
    OMEGA_POWER_CUT=sd:write:$n scenario
    [ "$cut_status" = 75 ] || fail "no power cut before SD write $n (exit $cut_status)"
    check "w$n" "before SD write $n"
  done
  msg+=", $((total - first)) SD writes"
fi
echo "PASS: slotstore $part: $msg; nothing is lost, and the next power-on leaves the operation done or not done"
