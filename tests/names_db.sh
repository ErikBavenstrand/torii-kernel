#!/usr/bin/env bash
# The names database (source/namesdb.h, names_store.c) on the emulated cart:
# `just names-update`'s names.db and crc.txt (tools/names_db.py, from the
# fixture dats in tests/names: no network) name the card's games at once, by
# CRC or by GBA game code, and the index sorts by those names; the boot's walk
# gives crc.txt's records their files' dates and forgets one whose file
# changed; the games on Recent without a CRC get one after the boot.
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
  python3 tools/names_db.py update "$cart/files" --offline --dats tests/names "$@" >"$cart/tool.log"
  touch -d '2025-01-01 12:00' "$cart/files/GBA/Wars.gba"
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
logged "$cart/1.log" "names: /torii/names.db from 2026.01.02: GBA 5, GBC 2, GB 1, NES 1, 3 codes"
logged "$cart/1.log" "names: 8 files' CRCs from /torii/crc.txt"
logged "$cart/1.log" "library: new index: 8 games (GBA 4, GBC 1, GB 2, NES 1)"
no_errors "$cart/1.log"
# Without names, file-name order: Hack of Minish, Homebrew, Wars, zz Minish
want="/GBA/Wars.gba
/GBA/Homebrew.gba
/GBA/Hack of Minish.gba
/GBA/zz Minish.gba"
[ "$(gba_order "$cart")" = "$want" ] || fail "the GBA games sort as
$(gba_order "$cart")"
# The walk gave every record its file's date, and crc.txt was written again
[ "$(crc_lines "$cart" | wc -l)" = 8 ] || fail "crc.txt holds $(crc_lines "$cart")"
if crc_lines "$cart" | awk -F'\t' '$4 == "-"' | grep -q .; then
  fail "records without a date: $(crc_lines "$cart")"
fi
echo "PASS: names-update's names.db and crc.txt name the games by CRC and game code, and the index sorts by them"

# Power-on 2: nothing changed: nothing rebuilt or written
boot "$cart" "$cart/2.log" -f 600 -u "library: unchanged"
logged "$cart/2.log" "library: unchanged"
not_logged "$cart/2.log" "names: wrote"
no_errors "$cart/2.log"
echo "PASS: a later boot loads the names, and changes and writes nothing"

# A file replaced (the same size, another date): its record is forgotten
# and it sorts by its file name
python3 -c 'import sys; sys.path.insert(0, "tests/names"); import fixture
open(sys.argv[1], "wb").write(fixture.rom(21, 4096, "AWRE"))' "$cart/files/GBA/Wars.gba"
touch -d '2025-06-01 12:00' "$cart/files/GBA/Wars.gba"
mcopy -o -m -i "$cart/sd.img" "$cart/files/GBA/Wars.gba" ::/GBA/Wars.gba
boot "$cart" "$cart/3.log" -f 900 -u "names: wrote /torii/crc.txt"
logged "$cart/3.log" "names: /GBA/Wars.gba changed since its CRC: forgotten"
logged "$cart/3.log" "library: the card changed: building a new index"
want="/GBA/Homebrew.gba
/GBA/Hack of Minish.gba
/GBA/zz Minish.gba
/GBA/Wars.gba"
[ "$(gba_order "$cart")" = "$want" ] || fail "after the change the GBA games sort as
$(gba_order "$cart")"
if crc_lines "$cart" | grep -q '/GBA/Wars.gba'; then fail "crc.txt keeps the changed file"; fi
no_errors "$cart/3.log"
echo "PASS: a file changed since its CRC loses its name and its record"

# The games on Recent get their CRCs after the boot: a game played is named
# without a pass of its own in the launch
cart=$carts/recent
fixture_card "$cart" --no-crc
printf '1790900000\t/GBA/Wars.gba\n1790800000\t/NES/Famicom.nes\n1790700000\t/SAVER/gone.gba\n' >"$cart/recent.txt"
mcopy -i "$cart/sd.img" "$cart/recent.txt" ::/torii/recent.txt
boot "$cart" "$cart/1.log" -f 2000 -u "names: wrote /torii/crc.txt"
logged "$cart/1.log" "names: identified 2 of 2 games"
logged "$cart/1.log" "names: wrote /torii/crc.txt"
logged "$cart/1.log" "library: the card changed: building a new index"
logged "$cart/1.log" "library: wrote /torii/library.bak (generation 2)"
[ "$(crc_lines "$cart" | cut -f5 | sort | tr '\n' ' ')" = "/GBA/Wars.gba /NES/Famicom.nes " ] ||
  fail "crc.txt holds $(crc_lines "$cart")"
[ "$(gba_order "$cart" | head -1)" = /GBA/Wars.gba ] || fail "Wars isn't first: $(gba_order "$cart")"
no_errors "$cart/1.log"
echo "PASS: the games on Recent get their CRCs and names after the boot"

# A large crc.txt (3,000 records of 100-byte paths, as No-Intro names in
# console folders make) loads whole, in steps, beside a loaded index
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
