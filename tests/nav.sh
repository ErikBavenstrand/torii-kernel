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
# Routes go from the carousel's first item: RIGHT to the item, or LEFT round
# the end for the four that always show, then A (which opens the item the
# ring is turning to, so the presses needn't wait for it). Whatever item the
# card starts facing (Start on, Last place), every route first turns home to
# its first item through the tests' hook (NAV_TEST_HOME, poked at frame 30),
# and waits for "nav: home ready" after it.
#
# Routes into a page (a Settings row) come with the page's ticket: until
# then they fail, saying which one.
#
# nav_file PATH ROW... [ACTION] [hard] [addon]: from power-on into the SD card page
# (source/screens/sdcard.c) and down PATH's folders to its file, one ROW per
# part of PATH: the row it has in its folder's listing (folders first, then
# files, A-Z as the page sorts them, hidden entries left out), which the
# test knows from the card it made. Each folder opens with A; on the file,
# START opens its popup, and the route waits for the page to log that
# popup's path, so a wrong row stops the run there. ACTION then picks from
# the popup: popup (none: it stays open), launch (Launch; with hard, L held:
# a hard reset), clean (Clean boot), nor (Write to NOR), nor-addon (Write
# to NOR with addon), fav (Add to or Remove from
# favourites, then B closes it), delete (Delete, then Delete in the
# confirmation), delete-no (Delete, then Cancel, then B), info (File info,
# then A closes it), or open (no popup: A on the row, which starts a game).
# Clean boot, Boot with addon (GBA games) and Write to NOR with addon show only while an addon is on:
# say addon then (clean and nor-addon imply it), so the route counts them.
# Later options count from the route's last wait (-w): the popup's line, or
# the folder's with open. The kernel cuts a log line's text at 240 bytes,
# so a wait for a longer line (a path of about 225 bytes or more) waits for
# its first 240 bytes.
#
# nav_game PLACE ROW: nav_to a game list (recent, fav or a console), then
# DOWN ROW times, 8 frames apart from @8: the game at ROW is selected from
# @$((8 + 8 * ROW)) on (counted from "nav: page PLACE ready"; ROW < 8 keeps
# the pill still before it). nav_game_menu PLACE ROW: that, then START:
# waits for its game popup ("screen ready: game-popup"), later @N count from
# it. nav_game_delete PLACE ROW: the popup's Delete (UP wraps to it, the
# last item), asked, and Delete answered (RIGHT, A): "delete: PATH" is logged.
# nav_game_rename PLACE ROW: the popup's Rename (UP three times wraps to it
# on a GBA game), waiting for the keyboard ("screen ready: rename").
#
# The NOR page's (B15), each from power-on through nav_to nor:
#   nav_nor_game INDEX         the cursor on game INDEX (0 the first written),
#                              waiting there
#   nav_nor_open INDEX ITEM    then START and the popup's ITEM: launch, info or
#                              delete (A on it once the popup is up)
#   nav_nor_launch_page INDEX [hard]  A on game INDEX (hard: L held, a hard reset)
#   nav_nor_delete_page INDEX yes|no  the popup's Delete, then the confirmation
#                              answered: yes chooses Delete, no is B (Cancel)
#   nav_nor_format_page yes|no UP from the first row to Format all, A, and the
#                              confirmation answered the same way
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

# _nav_wait TEXT: a -w line for TEXT as the kernel logs it: its text cut at
# 240 bytes (source/tlog.c's TEXT_MAX)
_nav_wait() {
  local LC_ALL=C text=$1
  printf -- '-w "%s"\n' "$(_nav_q "${text:0:240}")"
}

# _nav_q TEXT: TEXT for a "quoted word" in an mgba-shot -i file: \ and "
# escaped
_nav_q() {
  local text=${1//\\/\\\\}
  printf '%s' "${text//\"/\\\"}"
}

# The start of every route: home, facing its first item (NAV_TEST_HOME)
_nav_home() {
  printf -- '-p 30:nav_test_action:%d\n' $((0x7E000000 | 9))
  printf -- '-w "nav: test hook 9 "\n-w "nav: home ready"\n'
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
    _nav_file "$(_nav_home)"
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
  sd) keys=(LEFT LEFT LEFT LEFT) ;;
  nor) keys=(LEFT LEFT LEFT) ;;
  settings) keys=(LEFT LEFT) ;;
  info) keys=(LEFT) ;;
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
    for (( ; index > 0; index--)); do keys+=(RIGHT); done
    ;;
  esac
  _nav_file "$(_nav_home)" "$(_nav_keys 8 "${keys[@]}" A)" "-w \"nav: page $place ready\""
}

nav_file() {
  local path=$1 hard=0 addon=0 action=popup folder='' part parts=() rows=() lines=() keys=() i
  shift
  IFS=/ read -r -a parts <<<"${path#/}"
  while [ $# -gt 0 ]; do
    case $1 in
    [0-9]*) rows+=("$1") ;;
    hard) hard=1 ;;
    addon) addon=1 ;;
    popup | launch | clean | nor | nor-addon | fav | delete | delete-no | info | open) action=$1 ;;
    *)
      _nav_fail "nav_file: unknown option $1"
      return 1
      ;;
    esac
    shift
  done
  if [ ${#rows[@]} != ${#parts[@]} ]; then
    _nav_fail "nav_file $path: ${#parts[@]} parts but ${#rows[@]} rows"
    return 1
  fi
  lines=("$(_nav_home)" "$(_nav_keys 8 LEFT LEFT LEFT LEFT A)" '-w "sdcard: ready /"')
  for ((i = 0; i < ${#parts[@]}; i++)); do
    part=${parts[$i]}
    keys=()
    for ((n = 0; n < rows[i]; n++)); do keys+=(DOWN); done
    if ((i + 1 < ${#parts[@]})); then
      folder="$folder/$part"
      lines+=("$(_nav_keys 8 "${keys[@]}" A)" "$(_nav_wait "sdcard: ready $folder")")
      continue
    fi
    if [ "$action" = open ]; then
      if [ "$hard" = 1 ]; then keys+=(L+A); else keys+=(A); fi
      lines+=("$(_nav_keys 8 "${keys[@]}")")
      continue
    fi
    lines+=("$(_nav_keys 8 "${keys[@]}" START)" "$(_nav_wait "sdcard: popup $path")")
    local a=A
    [ "$hard" = 1 ] && a=L+A
    case $action in
    popup) keys=() ;;
    launch) keys=("$a") ;;
    clean) keys=(DOWN "$a") ;;
    nor) if [ "$addon" = 1 ]; then keys=(DOWN DOWN DOWN "$a"); else keys=(DOWN "$a"); fi ;;
    nor-addon) keys=(DOWN DOWN DOWN DOWN "$a") ;;
    fav) keys=(UP UP UP A B) ;;
    delete) keys=(UP UP A RIGHT A) ;;
    delete-no) keys=(UP UP A A B) ;;
    info) keys=(UP A A) ;;
    esac
    # The popup takes A once it has opened (6 frames); a confirmation the same
    [ ${#keys[@]} -gt 0 ] && lines+=("$(_nav_keys 12 "${keys[@]}")")
  done
  _nav_file "${lines[@]}"
}

# Routes into pages, until their tickets land
nav_game() {
  local place=$1 row=${2:-0} keys=() to
  to=$(nav_to "$place") || {
    printf '%s\n' "$to"
    return 1
  }
  for ((i = 0; i < row; i++)); do keys+=(DOWN); done
  # nav_to printed "-i FILE": the DOWNs go on in its file
  [ ${#keys[@]} = 0 ] || _nav_keys 8 "${keys[@]}" >>"${to#-i }"
  printf '%s\n' "$to"
}

nav_game_menu() {
  local row=${2:-0} to
  to=$(nav_game "$@") || {
    printf '%s\n' "$to"
    return 1
  }
  printf '%s\n' "$(_nav_keys $((16 + 8 * row)) START)" '-w "screen ready: game-popup"' >>"${to#-i }"
  printf '%s\n' "$to"
}

nav_game_rename() {
  local to
  to=$(nav_game_menu "$@") || {
    printf '%s\n' "$to"
    return 1
  }
  _nav_keys 12 UP UP UP A >>"${to#-i }"
  printf '%s\n' '-w "screen ready: rename"' >>"${to#-i }"
  printf '%s\n' "$to"
}

nav_game_delete() {
  local to
  to=$(nav_game_menu "$@") || {
    printf '%s\n' "$to"
    return 1
  }
  _nav_keys 12 UP A >>"${to#-i }"
  printf '%s\n' '-w "screen ready: confirm"' "$(_nav_keys 12 RIGHT A)" >>"${to#-i }"
  printf '%s\n' "$to"
}

# The NOR page's routes (B15; the top says what each does).

# _nav_nor_to INDEX: the lines from power-on to game INDEX
_nav_nor_to() {
  local index=$1 keys=() i
  for ((i = 0; i < index; i++)); do keys+=(DOWN); done
  printf '%s\n' "$(_nav_home)" "$(_nav_keys 8 LEFT LEFT LEFT A)" '-w "nav: page nor ready"'
  if [ "$index" -gt 0 ]; then
    _nav_keys 8 "${keys[@]}"
    printf -- '-w "nor: row %d"\n' "$index"
  fi
}

# _nav_answer yes|no: the lines answering a confirmation once it is up
_nav_answer() {
  printf '%s\n' '-w "screen ready: confirm"'
  case $1 in
  yes) _nav_keys 8 RIGHT A ;;
  no) _nav_keys 8 B ;;
  *)
    echo "nav.sh: answer yes or no, not $1" >&2
    return 1
    ;;
  esac
}

nav_nor_game() {
  _nav_file "$(_nav_nor_to "$1")"
}

nav_nor_open() {
  local index=$1 item=$2 downs=()
  case $item in
  launch) ;;
  info) downs=(DOWN) ;;
  delete) downs=(DOWN DOWN) ;;
  *)
    _nav_fail "no NOR popup item $item"
    return 1
    ;;
  esac
  _nav_file "$(_nav_nor_to "$index")" "$(_nav_keys 8 START)" '-w "screen ready: popup"' \
    "$(_nav_keys 8 "${downs[@]}" A)"
}

nav_nor_launch_page() {
  local keys
  keys=$(_nav_keys 8 A)
  # L held, then A with it
  [ "${2:-}" = hard ] && keys='-k @8:L -k @16:L+A -k @20:-'
  _nav_file "$(_nav_nor_to "$1")" "$keys"
}

nav_nor_delete_page() {
  local answer
  if ! answer=$(_nav_answer "${2:-}"); then
    _nav_fail "nav_nor_delete_page INDEX yes|no"
    return 1
  fi
  _nav_file "$(_nav_nor_to "$1")" "$(_nav_keys 8 START)" '-w "screen ready: popup"' \
    "$(_nav_keys 8 DOWN DOWN A)" "$answer"
}

nav_nor_format_page() {
  local answer
  if ! answer=$(_nav_answer "${1:-}"); then
    _nav_fail "nav_nor_format_page yes|no"
    return 1
  fi
  _nav_file "$(_nav_nor_to 0)" "$(_nav_keys 8 UP)" '-w "nor: row format"' "$(_nav_keys 8 A)" "$answer"
}

# nav_setting ROW: from power-on to the Settings page with ROW selected,
# waiting there. ROW is a row of source/settings_model.h, in page order
# (_nav_setting_rows); with NAV_LED_OFF=1 the LED colours are hidden, as
# the page hides them while the LED is off.
_nav_setting_rows=(theme game-view box-art start-on dim time date 24h game-clock boot-mode fast-patching auto-save save-location save-states
  cheats rumble mode-b led led-colour sd-light reset-hotkey reset-keys sleep sleep-keys rescan identify show-all history logging)
nav_setting() {
  local want=$1 id=0 steps=0 word keys=()
  for word in "${_nav_setting_rows[@]}"; do
    [ "$word" = "$want" ] && break
    id=$((id + 1))
    case $word in led-colour | sd-light) [ "${NAV_LED_OFF:-0}" = 1 ] && continue ;; esac
    steps=$((steps + 1))
  done
  if [ $id = ${#_nav_setting_rows[@]} ]; then
    _nav_fail "no Settings row $want"
    return 1
  fi
  nav_to settings || return 1
  [ $steps = 0 ] && return 0
  for (( ; steps > 0; steps--)); do keys+=(DOWN); done
  # The row's enum settings_id, as the page logs it
  _nav_file "$(_nav_keys 8 "${keys[@]}")" "-w \"settings: row $id\""
}

# nav_press KEY...: presses the keys 8 frames apart, from 8 frames after
# the last wait (a KEY of mgba-shot's -k, as A or L+R)
nav_press() { _nav_file "$(_nav_keys 8 "$@")"; }

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
  _nav_file "$(_nav_home)" "$(_nav_keys 8 LEFT A)" '-w "screen ready: info"' \
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
  _nav_file "-P \"$at:nav_test_path:$(_nav_q "$path")\"" "-p $at:nav_test_action:$((0x7E000000 | action | arg << 8 | hard << 16))"
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
