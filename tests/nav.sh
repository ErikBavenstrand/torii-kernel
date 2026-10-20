# shellcheck shell=bash
# Sourced by the test scripts: the ways through the v0.4 menu
# (source/screens/nav.c), as mgba-shot options to splice into a run. Each
# function prints "-i FILE", a file of options (mgba-shot -i), so texts and
# paths with blanks survive the splice:
#
#   OMEGA_DIR=$cart mgba-shot dist/ezkernelnew.bin $(nav_to settings) -s @8:page.png
#   OMEGA_DIR=$cart mgba-shot dist/ezkernelnew.bin $(nav_launch /Hello.gba) -f 400
#
# nav_to PLACE: from power-on to PLACE, waiting there (-w, so later options
# can count frames from it as @N). PLACE is home, or an item's page: recent,
# fav, gba, gbc, gb, nes (or "console gba"), sd, nor, settings, info.
# Home lists the items in the carousel's order, Recent, Favourites and the
# consoles only while they hold something: set NAV_SHOWN to the hidable items
# the card shows (default "gba": GBA games, nothing played, no favourites).
# Routes go from home's first row: DOWN to the item, or UP round the end for
# the four that always show, then A.
#
# Routes into a page (a file on the SD card page, a game's popup, a Settings
# row) come with the page's ticket: until then they fail, saying which one.
#
# nav_launch PATH [HOW] [save=N] [hard] [at=FRAME]: the menu runs a launch
# through actions.c (action_launch) once it is up and still, as a page will:
# the tests' hook (nav.h). HOW: clean (CLEAN BOOT, the default), addon
# (BOOT WITH ADDON), nor (WRITE TO NOR clean), nor-addon. save=N starts a
# GBA game with save type N (0 AUTO, 1 SRAM, 2 EEPROM 8K, 3 EEPROM 512, 4
# FLASH 64K, 5 FLASH 128K), and keeps it in its .mde; hard holds L (a hard
# reset). The hook's words are poked at frame FRAME (default 30: any frame
# after the kernel cleared its RAM, about frame 12, works: the menu waits
# for them until it is up and still). nav_delete PATH, nav_nor_launch INDEX
# [hard], nav_nor_delete and nav_nor_format run the other actions the same
# way.

_nav_item_words=(recent fav gba gbc gb nes sd nor settings info)

# _nav_file LINE...: the options in a new file, printed as "-i FILE"
_nav_file() {
  local file
  file=$(mktemp "${TMPDIR:-/tmp}/nav.XXXXXX")
  printf '%s\n' "$@" >"$file"
  printf -- '-i %s\n' "$file"
}

_nav_fail() {
  echo "nav.sh: $*" >&2
  # A run handed this waits for a line that never comes, and fails
  _nav_file '-w "nav.sh: route not built"'
  return 1
}

# _nav_keys FRAME KEY...: press/release pairs 8 frames apart, as @N lines
_nav_keys() {
  local frame=$1
  shift
  for key in "$@"; do
    printf -- '-k @%d:%s -k @%d:-\n' "$frame" "$key" $((frame + 4))
    frame=$((frame + 8))
  done
}

nav_to() {
  local place=$1
  [ "$place" = console ] && place=${2:-}
  if [ "$place" = home ]; then
    _nav_file '-w "nav: home ready"'
    return 0
  fi
  local word found=0 index=0 shown=() keys=()
  for word in "${_nav_item_words[@]}"; do
    [ "$word" = "$place" ] && found=1
  done
  if [ $found = 0 ]; then
    _nav_fail "no place $place"
    return 1
  fi
  case $place in
  sd) keys=(UP UP UP UP) ;;
  nor) keys=(UP UP UP) ;;
  settings) keys=(UP UP) ;;
  info) keys=(UP) ;;
  *)
    # The hidable items home shows, in order: the place's row among them
    for word in recent fav gba gbc gb nes; do
      case " ${NAV_SHOWN:-gba} " in *" $word "*) shown+=("$word") ;; esac
    done
    found=0
    for word in "${shown[@]}"; do
      [ "$word" = "$place" ] && found=1 && break
      index=$((index + 1))
    done
    if [ $found = 0 ]; then
      _nav_fail "$place isn't on home (NAV_SHOWN is \"${NAV_SHOWN:-gba}\")"
      return 1
    fi
    for (( ; index > 0; index--)); do keys+=(DOWN); done
    ;;
  esac
  _nav_file '-w "nav: home ready"' "$(_nav_keys 8 "${keys[@]}" A)" "-w \"nav: page $place ready\""
}

# Routes into pages, until their tickets land
nav_file() { _nav_fail "the SD card page's files come with B14 (nav_file $*)"; }
nav_game() { _nav_fail "the game lists come with B13 (nav_game $*)"; }
nav_nor_game() { _nav_fail "the NOR page's games come with B15 (nav_nor_game $*)"; }
nav_setting() { _nav_fail "the Settings page's rows come with B16 (nav_setting $*)"; }

# nav_info_page PAGE: from power-on to one of the Info page's rows opened
# (source/screens/info.c), waiting there: controls, ingame, cart or about
# (a reading page), or diag (Diagnostics' first page)
nav_info_page() {
  local page=${1:-} downs=() wait
  case $page in
  controls) ;;
  ingame) downs=(DOWN) ;;
  cart) downs=(DOWN DOWN) ;;
  about) downs=(DOWN DOWN DOWN) ;;
  diag) downs=(UP) ;;
  *)
    _nav_fail "no Info page $page"
    return 1
    ;;
  esac
  wait="screen ready: info-$page"
  [ "$page" = diag ] && wait="screen ready: diag-start"
  _nav_file '-w "nav: home ready"' "$(_nav_keys 8 UP A)" '-w "screen ready: info"' \
    "$(_nav_keys 8 "${downs[@]}" A)" "-w \"$wait\""
}

# _nav_hook ACTION PATH [OPTIONS...]: the hook's pokes (nav.h's NAV_TEST_*):
# the path first, then the action word with the save type + 1 (or a NOR
# game's index) in bits 8-15, a hard reset in bit 16 and NAV_TEST_MAGIC in
# bits 24-31
_nav_hook() {
  local action=$1 path=$2 at=30 arg=0 hard=0 opt
  shift 2
  for opt in "$@"; do
    case $opt in
    at=*) at=${opt#at=} ;;
    save=*) arg=$((${opt#save=} + 1)) ;;
    index=*) arg=${opt#index=} ;;
    hard) hard=1 ;;
    *)
      _nav_fail "unknown option $opt"
      return 1
      ;;
    esac
  done
  _nav_file "-P \"$at:nav_test_path:$path\"" "-p $at:nav_test_action:$((0x7E000000 | action | arg << 8 | hard << 16))"
}

nav_launch() {
  local path=$1 action=1
  shift
  case ${1:-} in
  clean) shift ;;
  addon) action=2 && shift ;;
  nor) action=3 && shift ;;
  nor-addon) action=4 && shift ;;
  esac
  _nav_hook $action "$path" "$@"
}

nav_delete() {
  local path=$1
  shift
  _nav_hook 5 "$path" "$@"
}

nav_nor_launch() {
  local index=$1
  shift
  _nav_hook 6 - "index=$index" "$@"
}

nav_nor_delete() { _nav_hook 7 - "$@"; }
nav_nor_format() { _nav_hook 8 - "$@"; }
