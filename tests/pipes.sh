# shellcheck shell=bash
# Sourced by the test scripts: pipeline ends that read all of their input.
#
# Under pipefail, "cmd | grep -q PAT" or "cmd | head -3" fails now and then
# with nothing wrong: grep -q and head exit as soon as they have what they
# need, and if cmd is still writing it dies of SIGPIPE (status 141), which
# fails the whole pipeline. How much cmd has left to write when that happens
# depends on scheduling, so the test passes alone and fails under load
# (tests/search_more.sh midgame, 2026-11-12; tests/cheats.sh, 754a719e).
# These do the same, then drain the rest, so the writer never sees a closed
# pipe and the status is grep's or head's.

# drain_grep GREP-ARGS...: grep, then read the rest of stdin
drain_grep() {
  local status=0
  grep "$@" || status=$?
  cat >/dev/null
  return "$status"
}

# drain_head HEAD-ARGS...: head, then read the rest of stdin
drain_head() {
  local status=0
  head "$@" || status=$?
  cat >/dev/null
  return "$status"
}
