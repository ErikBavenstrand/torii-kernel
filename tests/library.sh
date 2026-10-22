#!/usr/bin/env bash
# The library index (source/library.h, library_scan.c): the boot builds
# /torii/library.idx from every game on the card outside the hidden folders,
# loads it on later boots and walks the card in the background, rebuilding
# it only when a game, a save or the card changed; a damaged index or
# another card's is built again; past 4,096 games it keeps the first and says
# so; and it survives a launch.
# Runs inside the torii-emu container after `just build`, `just emu-headless`
# and building tests/roms/hello. With N/M, only every Mth case from the Nth.
set -euo pipefail
# shellcheck source=tests/carts.sh
. tests/carts.sh
# shellcheck source=tests/nav.sh
. tests/nav.sh
local_carts "dist/cart-library${1:+-${1//\//-}}"
shard_init "${1:-}"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

game=tests/roms/hello/hello.gba
sz=$(stat -c %s "$game")

# boot CART LOG [mgba-shot options]: a power-on until the menu has run a while
boot() {
  local cart=$1 log=$2
  shift 2
  OMEGA_DIR=$cart emu/build-linux/mgba-shot dist/ezkernelnew.bin "$@" >"$log" 2>&1 ||
    fail "the emulator failed: $(tail -3 "$log")"
}

logged() { # log text
  grep -qF "kernel: $2" "$1" || fail "no \"$2\" in $1: $(grep '^kernel' "$1" | grep library | tail -4)"
}

not_logged() { # log text
  if grep -qF "$2" "$1"; then fail "\"$2\" in $1"; fi
}

no_errors() { # log
  if grep -q '^kernel error:' "$1"; then fail "errors in $1: $(grep '^kernel error:' "$1" | head -3)"; fi
}

# The index on the card, as tests/library_idx.py lists it: the newer valid
# one of its two files
index() { # cart [--header]
  local cart=$1
  shift
  rm -f "$cart/library.idx" "$cart/library.bak"
  mcopy -o -i "$cart/sd.img" ::/torii/library.idx "$cart/library.idx" 2>/dev/null || true
  mcopy -o -i "$cart/sd.img" ::/torii/library.bak "$cart/library.bak" 2>/dev/null || true
  python3 tests/library_idx.py "$cart/library.idx" "$cart/library.bak" "$@"
}

new_card() { # cart [mkfs.fat options]
  local cart=$1
  shift
  rm -rf "$cart" && mkdir -p "$cart"
  mkfs.fat -C -F 32 -n LIBRARY "$@" "$cart/sd.img" 65536 >/dev/null
}

put() { # cart path...
  local cart=$1
  shift
  for p in "$@"; do mcopy -i "$cart/sd.img" "$game" "::$p"; done
}

# A card with games in the root and in nested folders, and games where the
# index must not look: the hidden folders, a hidden-attribute folder, dot
# files, and a folder past the depth cap
nested_card() {
  local cart=$1
  new_card "$cart"
  mmd -i "$cart/sd.img" ::/GBA ::/GBA/RPG ::/GBA/RPG/Old ::/NES ::/SAVER ::/RTS ::/SYSTEM ::/torii \
    "::/System Volume Information" ::/.Trashes ::/Secret ::/Deep ::/Deep/2 ::/Deep/2/3 ::/Deep/2/3/4 \
    ::/Deep/2/3/4/5 ::/Deep/2/3/4/5/6 ::/Deep/2/3/4/5/6/7 ::/Deep/2/3/4/5/6/7/8
  # Golden Sun's save is in its folder under /torii/saves, Mega Man 10's beside it
  mmd -i "$cart/sd.img" ::/torii/saves ::/torii/saves/GBA ::/torii/saves/GBA/RPG "::/torii/saves/GBA/RPG/Golden Sun.gba"
  put "$cart" "/Root Game.gba" "/GBA/Metroid Fusion (USA).gba" "/GBA/Mega Man 10.gba" "/GBA/Mega Man 2.gba" \
    "/GBA/RPG/Golden Sun.gba" "/GBA/RPG/Old/Zelda.gbc" "/GBA/RPG/notes.txt" "/NES/Mario.nes" "/Tetris.gb" \
    "/torii/saves/GBA/RPG/Golden Sun.gba/Main.sav" "/GBA/Mega Man 10.Boss v1.2.sav" "/RTS/Hidden.gba" "/SYSTEM/Hidden.gba" "/torii/Hidden.gba" \
    "/System Volume Information/Hidden.gba" "/.Trashes/Hidden.gba" "/._Root Game.gba" "/Secret/Hidden.gba" \
    "/Deep/2/3/4/5/6/7/Seven.gba" "/Deep/2/3/4/5/6/7/8/Eight.gba"
  mattrib -i "$cart/sd.img" +h ::/Secret
}

nested_expected="GBA	/GBA/RPG/Golden Sun.gba	$sz	save
GBA	/GBA/Mega Man 2.gba	$sz	-
GBA	/GBA/Mega Man 10.gba	$sz	save
GBA	/GBA/Metroid Fusion (USA).gba	$sz	-
GBA	/Root Game.gba	$sz	-
GBA	/Deep/2/3/4/5/6/7/Seven.gba	$sz	-
GBC	/GBA/RPG/Old/Zelda.gbc	$sz	-
GB	/Tetris.gb	$sz	-
NES	/NES/Mario.nes	$sz	-"

if shard_case; then
  cart=$carts/nested
  nested_card "$cart"

  # Power-on 1: no index, so the boot builds one; the job writes it
  boot "$cart" "$cart/1.log" -f 300
  logged "$cart/1.log" "library: no index loaded (none on the card"
  logged "$cart/1.log" "library: new index: 9 games (GBA 6, GBC 1, GB 1, NES 1), 9 files"
  logged "$cart/1.log" "library: wrote /torii/library.idx"
  no_errors "$cart/1.log"
  [ "$(index "$cart")" = "$nested_expected" ] || fail "the index lists:
$(index "$cart")"
  index "$cart" --header | grep -qx "flags 0" || fail "the index says truncated"
  echo "PASS: the first boot indexes nested folders A-Z per console, skips hidden and too-deep ones, and finds saves"

  # Power-on 2: loaded, the walk finds nothing new and writes nothing. The
  # SD card page lists the root as the index walks it: Torii's folder and
  # the other hidden entries left out
  # shellcheck disable=SC2046 # nav.sh's options
  boot "$cart" "$cart/2.log" $(nav_to sd) -f 400
  logged "$cart/2.log" "sdcard: listed /: 3 folders, 2 files"
  logged "$cart/2.log" "library: loaded 9 games (GBA 6, GBC 1, GB 1, NES 1)"
  logged "$cart/2.log" "library: unchanged"
  not_logged "$cart/2.log" "library: wrote"
  not_logged "$cart/2.log" "screen ready: progress"
  no_errors "$cart/2.log"
  echo "PASS: a later boot loads the index, finds the card unchanged and writes nothing; the SD card page hides what it hides"

  # Changes from a PC: one game added in the root and one in a nested
  # folder, one removed, one renamed, and a save made
  put "$cart" "/Added.gba" "/GBA/RPG/Old/Added Too.gb"
  mdel -i "$cart/sd.img" "::/GBA/Mega Man 2.gba"
  mren -i "$cart/sd.img" "::/GBA/RPG/Golden Sun.gba" "::/GBA/RPG/Golden Sun 2.gba"
  mmd -i "$cart/sd.img" ::/torii/saves/NES ::/torii/saves/NES/Mario.nes
  put "$cart" "/torii/saves/NES/Mario.nes/Main.sav"
  boot "$cart" "$cart/3.log" -f 600 -u "library: wrote"
  logged "$cart/3.log" "library: loaded 9 games"
  logged "$cart/3.log" "library: the card changed: building a new index"
  logged "$cart/3.log" "library: new index: 10 games (GBA 6, GBC 1, GB 2, NES 1)"
  logged "$cart/3.log" "library: wrote /torii/library.bak (generation 2)"
  no_errors "$cart/3.log"
  want="GBA	/Added.gba	$sz	-
GBA	/GBA/RPG/Golden Sun 2.gba	$sz	-
GBA	/GBA/Mega Man 10.gba	$sz	save
GBA	/GBA/Metroid Fusion (USA).gba	$sz	-
GBA	/Root Game.gba	$sz	-
GBA	/Deep/2/3/4/5/6/7/Seven.gba	$sz	-
GBC	/GBA/RPG/Old/Zelda.gbc	$sz	-
GB	/GBA/RPG/Old/Added Too.gb	$sz	-
GB	/Tetris.gb	$sz	-
NES	/NES/Mario.nes	$sz	save"
  [ "$(index "$cart")" = "$want" ] || fail "after the changes the index lists:
$(index "$cart")"
  # Files that aren't games change nothing
  put "$cart" "/GBA/readme.txt"
  boot "$cart" "$cart/4.log" -f 300
  logged "$cart/4.log" "library: unchanged"
  echo "PASS: games added, removed and renamed in the root and nested folders, and a new save, show without a rescan"

  # A custom name sorts its game: the lists sort by the name shown (R3)
  printf '/Root Game.gba\tAAA My first game\n' >"$cart/names.txt"
  mcopy -i "$cart/sd.img" "$cart/names.txt" ::/torii/names.txt
  boot "$cart" "$cart/5.log" -f 600 -u "library: wrote"
  logged "$cart/5.log" "library: the card changed: building a new index"
  logged "$cart/5.log" "library: wrote /torii/library.idx (generation 3)"
  [ "$(index "$cart" | head -1)" = "GBA	/Root Game.gba	$sz	-" ] || fail "the custom name didn't sort first:
$(index "$cart")"
  echo "PASS: a custom name in names.txt sorts its game by it, without a rescan"
fi

if shard_case; then
  # A damaged index is built again, before the menu
  cart=$carts/damaged
  nested_card "$cart"
  boot "$cart" "$cart/1.log" -f 300
  index "$cart" >/dev/null
  printf 'X' | dd of="$cart/library.idx" bs=1 seek=100 conv=notrunc 2>/dev/null
  mcopy -o -i "$cart/sd.img" "$cart/library.idx" ::/torii/library.idx
  boot "$cart" "$cart/2.log" -f 300
  logged "$cart/2.log" "library: /torii/library.idx not loaded (a bad CRC)"
  logged "$cart/2.log" "library: new index: 9 games"
  logged "$cart/2.log" "library: wrote /torii/library.idx (generation 1)"
  [ "$(index "$cart")" = "$nested_expected" ] || fail "the rebuilt index lists: $(index "$cart")"
  echo "PASS: a damaged index is built again"

  # A write cut short (its header is written last, so the file has a blank
  # one): the other file, one generation older, is loaded, and the walk
  # finds what changed. The rebuild writes the older file, never the newer.
  put "$cart" "/Added.gba"
  boot "$cart" "$cart/3.log" -f 600 -u "library: wrote"
  logged "$cart/3.log" "library: wrote /torii/library.bak (generation 2)"
  mcopy -o -i "$cart/sd.img" ::/torii/library.bak "$cart/library.bak"
  head -c 64 /dev/zero | dd of="$cart/library.bak" bs=1 conv=notrunc 2>/dev/null
  mcopy -o -i "$cart/sd.img" "$cart/library.bak" ::/torii/library.bak
  boot "$cart" "$cart/4.log" -f 600 -u "library: wrote"
  logged "$cart/4.log" "library: loaded 9 games (GBA 6, GBC 1, GB 1, NES 1) from /torii/library.idx"
  not_logged "$cart/4.log" "no index loaded"
  not_logged "$cart/4.log" "screen ready: progress"
  logged "$cart/4.log" "library: the card changed: building a new index"
  logged "$cart/4.log" "library: wrote /torii/library.bak (generation 2)"
  no_errors "$cart/4.log"
  index "$cart" --header | grep -qx "records 10" || fail "after the cut write: $(index "$cart" --header | tr '\n' ' ')"
  echo "PASS: a write cut short leaves the older file, which the boot loads"

  # The same files on another card (another volume serial): its index
  # isn't shown, even for a moment
  index "$cart" >/dev/null
  cp "$cart/library.idx" "$carts/good.idx"
  new_card "$cart" -i 12345678
  mmd -i "$cart/sd.img" ::/torii
  put "$cart" "/Other.gba"
  mcopy -i "$cart/sd.img" "$carts/good.idx" ::/torii/library.idx
  boot "$cart" "$cart/4.log" -f 300
  logged "$cart/4.log" "library: the index is another card's"
  logged "$cart/4.log" "library: new index: 1 games (GBA 1, GBC 0, GB 0, NES 0)"
  grep -E "kernel: (library: new index|screen ready: home)" "$cart/4.log" | head -1 | grep -q "new index" ||
    fail "the menu came up before the new index"
  index "$cart" --header | grep -qx "volume_serial 12345678" || fail "the new index has another serial"
  echo "PASS: another card's index is built again before the menu"
fi

if shard_case; then
  # 4,097 games in 9 folders: the index keeps the first 4,096 in walk order
  # and says the card has more; the first scan shows its progress
  cart=$carts/many
  new_card "$cart"
  mkdir -p "$cart/files"
  for f in $(seq 0 8); do
    mkdir -p "$cart/files/Folder $f"
    for i in $(seq 0 455); do
      n=$((f * 456 + i))
      [ $n -lt 4097 ] && : >"$cart/files/Folder $f/Game $n.gba"
    done
  done
  mcopy -s -i "$cart/sd.img" "$cart/files/"* ::/
  boot "$cart" "$cart/1.log" -f 2400 -u "library: wrote"
  logged "$cart/1.log" "library: the index holds 4096 of the card's 4097 games"
  logged "$cart/1.log" "library: new index: 4096 games (GBA 4096, GBC 0, GB 0, NES 0), 4097 files"
  logged "$cart/1.log" "library: wrote /torii/library.idx"
  no_errors "$cart/1.log"
  index "$cart" --header >"$cart/header"
  grep -qx "flags 1" "$cart/header" || fail "not marked truncated: $(tr '\n' ' ' <"$cart/header")"
  grep -qx "games_seen 4097" "$cart/header" || fail "games_seen: $(tr '\n' ' ' <"$cart/header")"
  [ "$(index "$cart" | wc -l)" = 4096 ] || fail "the index lists $(index "$cart" | wc -l) games"
  grep "kernel: library: building walk" "$cart/1.log" | sed 's/^kernel: /  /'
  grep "kernel: library: wrote" "$cart/1.log" | sed 's/^kernel: /  /'
  # Power-on 2: the background walk over the same card, timed
  boot "$cart" "$cart/2.log" -f 2400 -u "library: unchanged"
  logged "$cart/2.log" "library: loaded 4096 games"
  logged "$cart/2.log" "library: unchanged"
  grep -E "kernel: library: (loaded|checking walk)" "$cart/2.log" | sed 's/^kernel: /  /'
  # Power-on 3: the GBA page's last row (UP wraps to it) says so in the
  # hint bar, and its first doesn't
  # shellcheck disable=SC2046 # nav.sh's options
  boot "$cart" "$cart/3.log" $(nav_to gba) -k @8:UP -k @12:- -w "games: Showing the first 4,096 games"
  [ "$(grep -c "games: Showing the first" "$cart/3.log")" = 1 ] || fail "the cap note showed on the first row too"
  no_errors "$cart/3.log"
  echo "PASS: past 4,096 games the index keeps 4,096 and says the card has more, and the GBA page's last row says so"
fi

if shard_case; then
  # Delete in a game list's popup (Cancel preselected; RIGHT, A deletes):
  # the game leaves the index at once (the page lists it no more, and the
  # walk then writes the index without it), Recent and the favourites; its
  # save stays (T11)
  cart=$carts/delete
  nested_card "$cart"
  boot "$cart" "$cart/1.log" -f 300
  logged "$cart/1.log" "library: wrote /torii/library.idx"
  printf '/GBA/RPG/Golden Sun.gba\n/Tetris.gb\n' >"$cart/favorites.txt"
  mcopy -i "$cart/sd.img" "$cart/favorites.txt" ::/torii/favorites.txt
  printf '/Tetris.gb\n/GBA/RPG/Golden Sun.gba\n' >"$cart/recent.txt"
  mcopy -i "$cart/sd.img" "$cart/recent.txt" "::/SAVER/Recently play.txt"
  # Golden Sun is the GBA page's first row
  NAV_SHOWN="recent fav gba gbc gb nes"
  # shellcheck disable=SC2046 # nav.sh's options
  boot "$cart" "$cart/2.log" $(nav_game_delete gba 0) \
    -w "delete: /GBA/RPG/Golden Sun.gba" -u "library: wrote" -f 2000
  logged "$cart/2.log" "delete: /GBA/RPG/Golden Sun.gba"
  no_errors "$cart/2.log"
  index "$cart" | grep -q "Golden Sun" && fail "the index still lists Golden Sun:
$(index "$cart")"
  mdir -b -i "$cart/sd.img" ::/GBA/RPG | grep -q "Golden Sun.gba" && fail "Golden Sun.gba is still on the card"
  mtype -i "$cart/sd.img" "::/torii/saves/GBA/RPG/Golden Sun.gba/Main.sav" | cmp -s - "$game" || fail "Golden Sun's save didn't stay as it was"
  favs=$(mtype -i "$cart/sd.img" ::/torii/favorites.txt | grep '^/' | tr '\n' ' ')
  [ "$favs" = "/Tetris.gb " ] || fail "after the delete, the favourites are '$favs'"
  recent=$(mtype -i "$cart/sd.img" ::/torii/recent.txt | grep -v '^[#@]' | cut -f2 | tr '\n' ' ')
  [ "$recent" = "/Tetris.gb " ] || fail "after the delete, Recent has '$recent'"
  echo "PASS: Delete in a game list's popup removes the ROM, leaves the index, Recent and the favourites; the save stays"
fi

if shard_case; then
  # The index survives a launch: a failed one (a name too long for its
  # save record) loads it again, a game's next boot loads it from the card
  cart=$carts/launch
  new_card "$cart"
  toolong=$(printf 'L%.0s' $(seq 249)).gba
  put "$cart" "/$toolong" "/Zelda.gb"
  boot "$cart" "$cart/1.log" -f 200
  logged "$cart/1.log" "library: wrote /torii/library.idx"
  # CLEAN BOOT of the long name (as a page will, through actions.c); it
  # fails and the menu comes back (B closes the error)
  # shellcheck disable=SC2046 # nav.sh's options
  boot "$cart" "$cart/2.log" $(nav_launch "/$toolong" at=180) -w "screen ready: dialog" -k @20:B -k @24:- -f 600
  grep -q "^kernel error: error 8: The save's path is too long" "$cart/2.log" || fail "the launch didn't fail"
  [ "$(grep -c 'kernel: library: loaded 2 games' "$cart/2.log")" = 2 ] ||
    fail "the index wasn't loaded again after the failed launch: $(grep 'kernel: library' "$cart/2.log")"
  # Zelda.gb starts; the next boot loads the index
  # shellcheck disable=SC2046
  boot "$cart" "$cart/3.log" $(nav_launch /Zelda.gb at=150) -f 400
  logged "$cart/3.log" "launch: starting the game"
  # (B at "Save sav?", which the game left)
  boot "$cart" "$cart/4.log" -k 100:B -k 106:- -f 400
  logged "$cart/4.log" "library: loaded 2 games"
  no_errors "$cart/4.log"
  echo "PASS: the index is loaded again after a failed launch and after a game"
fi

if shard_case; then
  # /torii a file: the index can't be kept on the card. Each boot scans,
  # keeps the index in RAM and says so once, as an error; a Rescan doesn't
  # try the write again
  cart=$carts/torii-file
  new_card "$cart"
  put "$cart" "/Root Game.gba"
  printf 'not a folder\n' >"$cart/torii"
  mcopy -i "$cart/sd.img" "$cart/torii" ::/torii
  for n in 1 2; do
    boot "$cart" "$cart/$n.log" -f 600
    logged "$cart/$n.log" "library: new index: 1 games"
    [ "$(grep -c '^kernel error:' "$cart/$n.log")" = 1 ] || fail "boot $n's errors: $(grep '^kernel error:' "$cart/$n.log")"
    grep -q '^kernel error: library: can.t make /torii/library.idx (FatFs error [0-9]*; /torii is a file): the index is kept in RAM' \
      "$cart/$n.log" || fail "boot $n didn't say why the index isn't kept: $(grep '^kernel error:' "$cart/$n.log")"
  done
  # shellcheck disable=SC2046 # nav.sh's options
  boot "$cart" "$cart/3.log" $(nav_setting rescan) $(nav_press A) -f 1500
  [ "$(grep -c 'kernel: library: new index: 1 games' "$cart/3.log")" = 2 ] || fail "Rescan didn't build the index"
  # (the user's lists, written after Settings, can't be kept either: their own errors)
  [ "$(grep -c '^kernel error: library:' "$cart/3.log")" = 1 ] ||
    fail "Rescan tried the write again: $(grep '^kernel error: library:' "$cart/3.log")"
  echo "PASS: with /torii a file, each boot keeps the index in RAM and says so once"
fi
