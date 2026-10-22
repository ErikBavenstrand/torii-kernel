#!/usr/bin/env bash
# The names database (source/namesdb.h, names_store.c) on the emulated cart:
# `just names-update`'s names.db and crc.txt (tools/names_db.py, from the
# fixture dats in tests/names: no network) name the card's games at once, by
# CRC only (a romhack with a listed game's code keeps its file name), and the
# index sorts by those names; the boot's walk
# gives crc.txt's records their files' dates and forgets one whose file
# changed; games without a CRC (new on the card, or changed) get one in the
# background after the walk, Recent's first, and a launch stops that and
# the next boot goes on; your own names (`just names-add`) win over the
# dats' and hold through a rebuild.
# Runs inside the torii-emu container after `just build` and `just
# emu-headless`.
#
#   tests/names_db.sh          the cases
#   tests/names_db.sh bench    Identify library's time for 1,000 games of
#                              32 KB, on the test kernel (SELECT+B)
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
local_carts "dist/cart-names-db${1:+-$1}"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

boot() { # cart log [mgba-shot options]
  local cart=$1 log=$2
  shift 2
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" >"$log" 2>&1 ||
    fail "the emulator failed: $(tail -3 "$log")"
}

logged() { # log text
  grep -qF "kernel: $2" "$1" || fail "no \"$2\" in $1: $(grep -E '^kernel( error)?: (names|library)' "$1" | tail -6)"
}

not_logged() { # log text
  if grep -qF "$2" "$1"; then fail "\"$2\" in $1"; fi
}

no_errors() { # log
  if grep -q '^kernel error:' "$1"; then fail "errors in $1: $(grep '^kernel error:' "$1" | head -3)"; fi
}

new_card() { # cart [sectors]
  rm -rf "$1" && mkdir -p "$1"
  mkfs.fat -C -F 32 -n NAMES "$1/sd.img" "${2:-65536}" >/dev/null
}

# The GBA games in list order, from the card's index
gba_order() { # cart
  local cart=$1
  rm -f "$cart/library.idx" "$cart/library.bak"
  mcopy -o -i "$cart/sd.img" ::/torii/library.idx "$cart/library.idx" 2>/dev/null || true
  mcopy -o -i "$cart/sd.img" ::/torii/library.bak "$cart/library.bak" 2>/dev/null || true
  python3 tests/library_idx.py "$cart/library.idx" "$cart/library.bak" | awk -F'\t' '$1 == "GBA" { print $2 }'
}

# crc.txt's records (the copy that holds), one per line
crc_lines() { # cart
  local cart=$1
  rm -f "$cart/crc.txt" "$cart/crc.txt.bak"
  mcopy -o -i "$cart/sd.img" ::/torii/crc.txt "$cart/crc.txt" 2>/dev/null || true
  mcopy -o -i "$cart/sd.img" ::/torii/crc.txt.bak "$cart/crc.txt.bak" 2>/dev/null || true
  python3 -c 'import sys; sys.path.insert(0, "tools"); import names_db
print(names_db.read_safe(sys.argv[1]), end="")' "$cart/crc.txt" | grep -v '^#' || true
}

# The fixture's files, and names.db (and crc.txt unless --no-crc) as
# names-update writes them, on a new card
fixture_card() { # cart [names_db.py options]
  local cart=$1
  shift
  new_card "$cart"
  mkdir -p "$cart/files"
  python3 tests/names/fixture.py roms "$cart/files"
  touch -d '2025-01-01 12:00' "$cart/files/GBA/Wars.gba"
  python3 tools/names_db.py update "$cart/files" --offline --dats tests/names "$@" >"$cart/tool.log"
  mcopy -s -m -i "$cart/sd.img" "$cart/files/"* ::/
}

if [ "${1:-}" = bench ]; then
  # 1,000 games of 32 KB in 10 folders; the test kernel's names bench
  # (tests/kernel/run.c) scans them, identifies them all and logs the time
  cart=$carts/bench
  new_card "$cart" 262144
  mkdir -p "$cart/files"
  head -c 32768 /dev/urandom >"$cart/game"
  for f in $(seq 0 9); do
    mkdir -p "$cart/files/Folder $f"
    for i in $(seq 0 99); do cp "$cart/game" "$cart/files/Folder $f/Game $((f * 100 + i)).gba"; done
  done
  mcopy -s -i "$cart/sd.img" "$cart/files/"* ::/
  OMEGA_DIR=$cart emu/build-linux/mgba-shot ezkerneltest.gba -k 1:SELECT+B -k 30:- -u 'NAMES BENCH DONE' \
    -f 200000 >"$cart/bench.log" 2>&1 || fail "the emulator failed: $(tail -3 "$cart/bench.log")"
  grep -E '^kernel: (names|library|NAMES BENCH)' "$cart/bench.log" | grep -v ' is [0-9A-F]\{8\}' | sed 's/^kernel: /  /'
  grep -q 'NAMES BENCH DONE' "$cart/bench.log" || fail "the bench didn't finish"
  exit 0
fi

# Power-on 1: names-update's files name the games at once
cart=$carts/pc
fixture_card "$cart"
grep -q "8 games, 8 CRCs computed" "$cart/tool.log" || fail "names_db.py: $(cat "$cart/tool.log")"
boot "$cart" "$cart/1.log" -f 900 -u "names: wrote /torii/crc.txt"
logged "$cart/1.log" "names: /torii/names.db v2 from 2026.01.02: GBA 5, GBC 2, GB 1, NES 1,"
logged "$cart/1.log" "names: 8 files' CRCs from /torii/crc.txt"
logged "$cart/1.log" "library: new index: 8 games (GBA 4, GBC 1, GB 2, NES 1)"
no_errors "$cart/1.log"
# Without names, file-name order: Hack of Minish, Homebrew, Wars, zz Minish.
# Hack of Minish, a romhack with the Minish Cap's code, keeps its file name
want="/GBA/Wars.gba
/GBA/Hack of Minish.gba
/GBA/Homebrew.gba
/GBA/zz Minish.gba"
[ "$(gba_order "$cart")" = "$want" ] || fail "the GBA games sort as
$(gba_order "$cart")"
# Every record has its file's date (names-update's, which the walk checked)
[ "$(crc_lines "$cart" | wc -l)" = 8 ] || fail "crc.txt holds $(crc_lines "$cart")"
if crc_lines "$cart" | awk -F'\t' '$3 == "-"' | grep -q .; then
  fail "records without a date: $(crc_lines "$cart")"
fi
echo "PASS: names-update's names.db and crc.txt name the games by CRC, not a romhack by its game code, and the index sorts by them"

# Power-on 2: nothing changed: nothing rebuilt or written
boot "$cart" "$cart/2.log" -f 600 -u "library: unchanged"
logged "$cart/2.log" "library: unchanged"
not_logged "$cart/2.log" "names: wrote"
no_errors "$cart/2.log"
echo "PASS: a later boot loads the names, and changes and writes nothing"

# A file replaced (the same size, another date): its record is forgotten,
# it sorts by its file name, and the background gives it its new CRC
python3 -c 'import sys; sys.path.insert(0, "tests/names"); import fixture
open(sys.argv[1], "wb").write(fixture.rom(21, 4096, "AWRE"))' "$cart/files/GBA/Wars.gba"
touch -d '2025-06-01 12:00' "$cart/files/GBA/Wars.gba"
mcopy -o -m -i "$cart/sd.img" "$cart/files/GBA/Wars.gba" ::/GBA/Wars.gba
boot "$cart" "$cart/3.log" -f 900 -u "names: wrote /torii/crc.txt"
logged "$cart/3.log" "names: /GBA/Wars.gba changed since its CRC: forgotten"
logged "$cart/3.log" "library: the card changed: building a new index"
logged "$cart/3.log" "names: identified 1 of 1 new games"
want="/GBA/Hack of Minish.gba
/GBA/Homebrew.gba
/GBA/zz Minish.gba
/GBA/Wars.gba"
[ "$(gba_order "$cart")" = "$want" ] || fail "after the change the GBA games sort as
$(gba_order "$cart")"
crc=$(python3 -c 'import sys, zlib; print("%08X" % zlib.crc32(open(sys.argv[1], "rb").read()))' "$cart/files/GBA/Wars.gba")
[ "$(crc_lines "$cart" | awk -F'\t' '$4 == "/GBA/Wars.gba" { print $1 }')" = "$crc" ] ||
  fail "crc.txt doesn't hold the changed file's new CRC $crc: $(crc_lines "$cart")"
no_errors "$cart/3.log"
echo "PASS: a file changed since its CRC loses its name and its record, and gets its new CRC in the background"

# names-update's dates as a PC in another local time writes them: one a
# whole hour off its file's (DST) holds and takes the cart's date; one a
# minute off (a file swapped in since) is forgotten
cart=$carts/skew
fixture_card "$cart"
python3 -c 'import sys; sys.path.insert(0, "tools"); import names_db
path = sys.argv[1] + "/files/torii/crc.txt"
rows = []
for line in names_db.read_safe(path).splitlines(keepends=True):
    f = line.split("\t")
    if len(f) == 4 and f[3] in ("/GBA/Wars.gba\n", "/GBA/Homebrew.gba\n"):
        f[2] = "%08X" % (int(f[2], 16) + (1 << 11 if "Wars" in f[3] else 1 << 5))
    rows.append("\t".join(f))
names_db.write_safe(path, "".join(rows))' "$cart"
mcopy -o -i "$cart/sd.img" "$cart/files/torii/crc.txt" "$cart/files/torii/crc.txt.bak" ::/torii/
boot "$cart" "$cart/1.log" -f 900 -u "names: wrote /torii/crc.txt"
logged "$cart/1.log" "names: /GBA/Homebrew.gba changed since its CRC: forgotten"
not_logged "$cart/1.log" "names: /GBA/Wars.gba changed"
wars_date=$(python3 -c 'import sys; sys.path.insert(0, "tools"); import names_db
print(names_db.fat_date(sys.argv[1]))' "$cart/files/GBA/Wars.gba")
[ "$(crc_lines "$cart" | awk -F'\t' '$4 == "/GBA/Wars.gba" { print $3 }')" = "$wars_date" ] ||
  fail "Wars's record isn't dated $wars_date: $(crc_lines "$cart")"
hb_date=$(python3 -c 'import sys; sys.path.insert(0, "tools"); import names_db
print(names_db.fat_date(sys.argv[1]))' "$cart/files/GBA/Homebrew.gba")
hb=$(crc_lines "$cart" | awk -F'\t' '$4 == "/GBA/Homebrew.gba" { print $3 }')
# Forgotten, or read again in the background (F2) with the file's own date
[ -z "$hb" ] || [ "$hb" = "$hb_date" ] || fail "crc.txt keeps Homebrew's old record: $(crc_lines "$cart")"
no_errors "$cart/1.log"
echo "PASS: a names-update date a whole hour off holds and takes the cart's date; a minute off is forgotten"

# Games added on a PC without names-update (no crc.txt) get their CRCs and
# names in the background after the boot, Recent's first, with no launch;
# a CRC the dats don't list (the romhack) is kept too, so the next boot
# reads nothing
cart=$carts/recent
fixture_card "$cart" --no-crc
printf '1790900000\t/GBA/Wars.gba\n1790800000\t/NES/Famicom.nes\n1790700000\t/SAVER/gone.gba\n' >"$cart/recent.txt"
mcopy -i "$cart/sd.img" "$cart/recent.txt" ::/torii/recent.txt
boot "$cart" "$cart/1.log" -f 3000 -u "names: wrote /torii/crc.txt"
logged "$cart/1.log" "names: identified 8 of 8 new games"
logged "$cart/1.log" "names: wrote /torii/crc.txt"
# The names sort the lists in place first (no walk), then one walk builds the index fresh
logged "$cart/1.log" "library: 6 database names put in the index in place"
[ "$(grep -n -m1 'database names put in the index in place' "$cart/1.log" | cut -d: -f1)" -lt \
  "$(grep -n -m1 'the card changed: building a new index' "$cart/1.log" | cut -d: -f1)" ] ||
  fail "the names weren't put in place before the walk"
logged "$cart/1.log" "library: the card changed: building a new index"
logged "$cart/1.log" "library: wrote /torii/library.bak (generation 2)"
[ "$(grep -oE '^kernel: names: /[^ ]+ is [0-9A-F]{8}' "$cart/1.log" | head -2 | cut -d' ' -f3 | tr '\n' ' ')" = \
  "/GBA/Wars.gba /NES/Famicom.nes " ] || fail "Recent's games weren't first: $(grep -E '^kernel: names: /' "$cart/1.log")"
[ "$(crc_lines "$cart" | wc -l)" = 8 ] || fail "crc.txt holds $(crc_lines "$cart")"
crc_lines "$cart" | grep -q '/GBA/Hack of Minish.gba' || fail "crc.txt has no record of the romhack"
[ "$(gba_order "$cart" | head -1)" = /GBA/Wars.gba ] || fail "Wars isn't first: $(gba_order "$cart")"
no_errors "$cart/1.log"
boot "$cart" "$cart/2.log" -f 900 -u "library: unchanged"
logged "$cart/2.log" "library: unchanged"
not_logged "$cart/2.log" "names: identified"
if grep -qE "^kernel: names: /.* is [0-9A-F]{8}" "$cart/2.log"; then fail "a CRC computed again"; fi
not_logged "$cart/2.log" "names: wrote"
no_errors "$cart/2.log"
echo "PASS: new games get their CRCs and names in the background, Recent's first, and a later boot reads none"

# A launch stops the background's CRCs (here in a 2 MB game) before
# crc.txt took the ones done: they go to the journal (crc.new), not the
# whole cache; the next boot reads it, goes on with the rest, and its write
# of crc.txt takes the lines over and deletes it
cart=$carts/journal
fixture_card "$cart" --no-crc
head -c 2097152 /dev/urandom >"$cart/Big.gba"
mcopy -i "$cart/sd.img" "$cart/Big.gba" ::/GBA/Big.gba
printf '1790900000\t/GBA/Wars.gba\n1790800000\t/GBA/Big.gba\n1790700000\t/NES/Famicom.nes\n' >"$cart/recent.txt"
mcopy -i "$cart/sd.img" "$cart/recent.txt" ::/torii/recent.txt
# shellcheck source=tests/nav.sh
. tests/nav.sh
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart" "$cart/1.log" -w "names: /GBA/Wars.gba is" $(nav_launch /GBA/Homebrew.gba at=@60) -u "launch: loading" -f 3000
logged "$cart/1.log" "names: identifying new games stopped for a launch (1 done)"
logged "$cart/1.log" "names: wrote 1 CRCs to /torii/crc.new"
not_logged "$cart/1.log" "names: wrote /torii/crc.txt"
not_logged "$cart/1.log" "names: /GBA/Big.gba is"
# (The game cut off is kept in crc.part and read on: tests/kernel/test_names.c, stepped by hand)
boot "$cart" "$cart/2.log" -k 100:A -k 106:- -f 6000 -u "names: wrote /torii/crc.txt" # A: "Save sav?"
logged "$cart/2.log" "names: 1 CRCs from /torii/crc.new"
logged "$cart/2.log" "names: identified 8 of 8 new games"
logged "$cart/2.log" "names: wrote /torii/crc.txt"
not_logged "$cart/2.log" "names: /GBA/Wars.gba is"
grep "kernel: names: identified" "$cart/2.log" | sed 's/^kernel: /  /'
[ "$(crc_lines "$cart" | wc -l)" = 9 ] || fail "crc.txt holds $(crc_lines "$cart")"
if mdir -i "$cart/sd.img" ::/torii/crc.new >/dev/null 2>&1; then fail "crc.new is still there"; fi
no_errors "$cart/1.log"
no_errors "$cart/2.log"
echo "PASS: a launch stops the background's CRCs and writes only the new ones, and the next boot goes on"

# The library's games after Recent's are identified smallest first, by size
# class (names_store.c, pass_limit): a 4.5 MB game written first waits for
# the 1.5 MB and the 100 KB game written after it
cart=$carts/order
fixture_card "$cart" --no-crc
head -c 4718592 /dev/urandom >"$cart/Huge.gba"
head -c 1572864 /dev/urandom >"$cart/Mid.gba"
head -c 102400 /dev/urandom >"$cart/Tiny.gba"
mcopy -i "$cart/sd.img" "$cart/Huge.gba" "$cart/Mid.gba" "$cart/Tiny.gba" ::/GBA/
boot "$cart" "$cart/1.log" -f 9000 -u "names: wrote /torii/crc.txt"
order=$(grep -oE '^kernel: names: /GBA/(Huge|Mid|Tiny).gba is' "$cart/1.log" | sed -E 's|.*/GBA/([A-Za-z]+.gba) is|\1|' | tr '\n' ' ')
[ "$order" = "Tiny.gba Mid.gba Huge.gba " ] || fail "the games weren't read smallest first: $order"
no_errors "$cart/1.log"
echo "PASS: the library's new games are identified smallest first"

# A game the card can't read is tried once a boot: the walk that a found
# name causes (Wars's) starts the background again, which skips it
cart=$carts/unreadable
fixture_card "$cart" --no-crc
head -c 307200 /dev/urandom >"$cart/Bad.gba"
mcopy -i "$cart/sd.img" "$cart/Bad.gba" ::/GBA/Bad.gba
# shellcheck source=tests/carts.sh
. tests/carts.sh
sectors=$(sd_sectors "$cart/sd.img" /GBA/Bad.gba)
OMEGA_SD_READ_ERROR=$sectors boot "$cart" "$cart/1.log" -f 6000 -u "names: wrote /torii/crc.txt"
[ "$(grep -c 'kernel error: names: reading /GBA/Bad.gba failed' "$cart/1.log")" = 1 ] ||
  fail "Bad.gba was tried $(grep -c 'reading /GBA/Bad.gba failed' "$cart/1.log") times: $(grep -E '^kernel( error)?: (names|library)' "$cart/1.log" | tail -8)"
[ "$(grep -c 'library: the card changed: building a new index' "$cart/1.log")" -ge 1 ] || fail "no walk followed the names found"
echo "PASS: a game the card can't read is tried once a boot"

# The cursor stays on its game while the names sort the list again, through
# the names put in place and the walk that follows: Wars is the GBA page's
# third game by its file name and the first by its database name
cart=$carts/follow
fixture_card "$cart" --no-crc
# shellcheck source=tests/nav.sh
. tests/nav.sh
# shellcheck disable=SC2046 # nav.sh's options
boot "$cart" "$cart/1.log" $(nav_game gba 2) \
  -u "library: wrote /torii/library.idx (generation 3)" -f 20000
[ "$(grep -c 'games: a new index: the cursor is on row' "$cart/1.log")" -ge 2 ] ||
  fail "the page wasn't sorted twice: $(grep -E '^kernel: (games|library)' "$cart/1.log" | tail -6)"
[ "$(grep 'games: a new index: the cursor is on row' "$cart/1.log" | grep -c 'row 0 of 4, /GBA/Wars.gba')" -ge 2 ] ||
  fail "the cursor left its game: $(grep 'a new index: the cursor' "$cart/1.log")"
echo "PASS: the cursor stays on its game across the new sorts the names cause"
# Only the rows whose text changed redraw: the walk after the names in place
# lists the same names again, so it redraws none
grep -q 'list: 0 of [0-9]* rows redraw' "$cart/1.log" ||
  fail "every row redrew for a list of the same names: $(grep 'rows redraw' "$cart/1.log")"
grep -q 'list: [1-9][0-9]* of [0-9]* rows redraw' "$cart/1.log" ||
  fail "no row redrew for the names put in place: $(grep 'rows redraw' "$cart/1.log")"
echo "PASS: a new index redraws only the rows that changed"

# The same on Favourites (A-Z), which keeps its own order of the favourites
cart=$carts/follow-fav
fixture_card "$cart" --no-crc
printf '/GBA/Hack of Minish.gba\n/GBA/Homebrew.gba\n/GBA/Wars.gba\n/GBA/zz Minish.gba\n' >"$cart/fav.txt"
mcopy -i "$cart/sd.img" "$cart/fav.txt" ::/torii/favorites.txt
# shellcheck disable=SC2046 # nav.sh's options
NAV_SHOWN="fav gba" boot "$cart" "$cart/1.log" $(NAV_SHOWN="fav gba" nav_game fav 2) \
  -u "library: wrote /torii/library.idx (generation 3)" -f 20000
[ "$(grep 'games: a new index: the cursor is on row' "$cart/1.log" | grep -c 'row 0 of 4, /GBA/Wars.gba')" -ge 2 ] ||
  fail "the cursor left its favourite: $(grep -E 'a new index|games:' "$cart/1.log" | tail -4)"
echo "PASS: the cursor stays on its favourite across the new sorts"

# crc.txt cut short by a write (left with zeros): its lines read in the
# same pass as its check are dropped, and the backup's taken
cart=$carts/backup
fixture_card "$cart"
mcopy -o -i "$cart/sd.img" "$cart/files/torii/crc.txt" ::/torii/crc.txt.bak
head -c 600 "$cart/files/torii/crc.txt" >"$cart/cut.txt"
head -c 200 /dev/zero >>"$cart/cut.txt"
mcopy -o -i "$cart/sd.img" "$cart/cut.txt" ::/torii/crc.txt
boot "$cart" "$cart/1.log" -f 900
logged "$cart/1.log" "names: /torii/crc.txt: read the backup copy"
logged "$cart/1.log" "names: 8 files' CRCs from /torii/crc.txt"
no_errors "$cart/1.log"
echo "PASS: a crc.txt cut short is passed over for its backup"

# A read-only crc.txt isn't written, and Recent's games aren't identified
# each boot for nothing
cart=$carts/read-only
fixture_card "$cart"
mcopy -i "$cart/sd.img" "$carts/recent/recent.txt" ::/torii/recent.txt
mattrib -i "$cart/sd.img" +r ::/torii/crc.txt
boot "$cart" "$cart/1.log" -f 900
logged "$cart/1.log" "names: /torii/crc.txt is read-only"
not_logged "$cart/1.log" "names: identified"
not_logged "$cart/1.log" "names: wrote"
no_errors "$cart/1.log"
echo "PASS: a read-only crc.txt is left alone, and nothing is identified for it"

# A large crc.txt (3,000 records of 100-byte paths, as No-Intro names in
# console folders make) loads whole, in steps, beside a loaded index; its
# records are in v0.4.1's form, with a code column, which still reads
cart=$carts/large
fixture_card "$cart"
boot "$cart" "$cart/1.log" -f 900 -u "names: wrote /torii/crc.txt"
python3 -c 'import sys; sys.path.insert(0, "tools"); import names_db
text = names_db.read_safe(sys.argv[1] + "/files/torii/crc.txt")
pad = "x" * 70
text += "".join(f"{i:08X}\t-\t4096\t-\t/Nintendo - Game Boy Advance/{pad} {i:04d}.gba\n" for i in range(3000))
names_db.write_safe(sys.argv[1] + "/files/torii/crc.txt", text)' "$cart"
mcopy -o -i "$cart/sd.img" "$cart/files/torii/crc.txt" "$cart/files/torii/crc.txt.bak" ::/torii/
boot "$cart" "$cart/2.log" -f 1500 -u "library: unchanged"
logged "$cart/2.log" "library: loaded 8 games"
logged "$cart/2.log" "names: 3008 files' CRCs from /torii/crc.txt"
logged "$cart/2.log" "library: unchanged"
grep "kernel: names: 3008" "$cart/2.log" | sed 's/^kernel: /  /'
no_errors "$cart/2.log"
echo "PASS: a crc.txt of 3,000 records loads whole in the background"

# A card that uses no CRCs (no names.db, no crc.txt, no /torii/art) gets
# no crc.txt and no CRC pass, whatever Recent holds
cart=$carts/unused
new_card "$cart"
mkdir -p "$cart/files"
python3 tests/names/fixture.py roms "$cart/files"
mcopy -s -i "$cart/sd.img" "$cart/files/"* ::/
printf '1790900000\t/GBA/Wars.gba\n' >"$cart/recent.txt"
mmd -i "$cart/sd.img" ::/torii
mcopy -i "$cart/sd.img" "$cart/recent.txt" ::/torii/recent.txt
boot "$cart" "$cart/1.log" -f 900
logged "$cart/1.log" "library: wrote /torii/library.idx"
not_logged "$cart/1.log" "names: identified"
not_logged "$cart/1.log" "names: wrote"
if mdir -i "$cart/sd.img" ::/torii/crc.txt >/dev/null 2>&1; then fail "crc.txt was made"; fi
no_errors "$cart/1.log"
echo "PASS: a card without names.db or art gets no crc.txt"

# Your own names (`just names-add`, into names.txt as Rename keeps them) win
# over the dats' and name a romhack, and hold when the index is built again
# (as Settings' Rescan library builds it)
cart=$carts/own
fixture_card "$cart"
printf 'Wars\tUltimate Wars\nhack of minish.gba\tZeta Hack\nNobody\tNo one\n' >"$cart/names.tsv"
python3 tools/names_db.py add "$cart/files" "$cart/names.tsv" >"$cart/add.log"
grep -q "2 games named, 0 names removed, 1 lines named no game" "$cart/add.log" || fail "names-add: $(cat "$cart/add.log")"
mcopy -o -i "$cart/sd.img" "$cart/files/torii/names.txt" "$cart/files/torii/names.txt.bak" ::/torii/
want="/GBA/Homebrew.gba
/GBA/zz Minish.gba
/GBA/Wars.gba
/GBA/Hack of Minish.gba"
boot "$cart" "$cart/1.log" -f 900 -u "names: wrote /torii/crc.txt"
logged "$cart/1.log" "library: 2 custom names parsed"
[ "$(gba_order "$cart")" = "$want" ] || fail "with own names the GBA games sort as
$(gba_order "$cart")"
mdel -i "$cart/sd.img" ::/torii/library.idx ::/torii/library.bak 2>/dev/null || true
boot "$cart" "$cart/2.log" -f 900 -u "library: wrote /torii/library.idx"
logged "$cart/2.log" "library: 2 custom names parsed"
[ "$(gba_order "$cart")" = "$want" ] || fail "after a rebuild the GBA games sort as
$(gba_order "$cart")"
no_errors "$cart/1.log"
no_errors "$cart/2.log"
echo "PASS: your own names win over the dats', name a romhack and hold through a rebuilt index"
