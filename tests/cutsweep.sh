# shellcheck shell=bash
# shellcheck disable=SC2034,SC2154 # $cart and $carts are the caller's; it reads $cut_count, $cut_status
# Sourced by the power-cut tests: runs a scenario with the power cut at each
# cutpoint (source/cutpoint.h), then boots again to check what survived.
#
# cut_sweep SCENARIO_FN CHECK_FN: needs $carts (tests/carts.sh) and $cart, the
# emulated cart's folder (OMEGA_DIR), holding the starting state: its SD
# image, SRAM and kernel NOR.
#   1. SCENARIO_FN [-X N] runs the scenario on $cart and returns. It must run
#      mgba-shot as `cut_run [-X N] ARGS...`, so the cut option goes in.
#   2. A first run without a cut, from a copy of the starting state, counts
#      the cutpoints M ("cutpoints: M" in the run's stderr). M = 0 fails.
#   3. For N = 1..M the cart is reset to the starting state (cart_restore,
#      which copies back only the files a run changed) and the scenario
#      runs with -X N; the run must stop with status 75, right after the
#      Nth cutpoint's log line.
#   4. CHECK_FN N NAME is called with $cart as the cut left it, before any
#      recovery boot: it may read the files and run its own recovery boots
#      (`cut_run` without -X). N = 0 is the uncut run, NAME the cutpoint's name
#      ("end" for N = 0).
# Each cut's run log is $cart/cut-N.log. Failures use `fail` if the test
# defines it, else print and exit 1.
#
# How the cart looks at a cut: mgba-shot stops at once and the files are as
# they were at that instant. Every SD write command has already reached
# sd.img (the model finishes a write as the kernel issues it), so the card
# holds exactly the sectors the kernel wrote. What FatFs still holds in RAM
# is lost: its window sector (directory entries and FAT, written on f_sync,
# f_close, or when it moves to another sector) and a file's sector buffer
# (until f_sync/f_close, or the next sector). Kernel NOR and SRAM are as the
# kernel left them; a NOR program or erase is not interrupted, as the model
# finishes each operation at once (OMEGA_POWER_CUT cuts inside an erase).

# cut_run [-X N] MGBA_SHOT_ARGS...: one power-on of $kernel (default
# dist/ezkernelnew.bin) on $cart; stderr to $cart/run.log; sets $cut_status.
cut_run() {
  cut_status=0
  OMEGA_DIR=$cart emu/build-linux/mgba-shot "${kernel:-dist/ezkernelnew.bin}" "$@" >/dev/null 2>"$cart/run.log" || cut_status=$?
}

_cut_fail() {
  if declare -F fail >/dev/null; then fail "$@"; else
    echo "FAIL: cutsweep: $*" >&2
    exit 1
  fi
}

cut_sweep() {
  local scenario=$1 check=$2 start=$carts/cutsweep-start n m name
  cart_blank "$cart"
  rm -rf "$start"
  cp -a "$cart" "$start"
  "$scenario"
  [ "$cut_status" = 0 ] || _cut_fail "the uncut scenario exited with $cut_status"
  m=$(sed -n 's/^cutpoints: //p' "$cart/run.log")
  [ "${m:-0}" -ge 1 ] || _cut_fail "the scenario logged no cutpoints"
  cp "$cart/run.log" "$carts/cutsweep-names.log"
  "$check" 0 end
  for n in $(seq 1 "$m"); do
    cart_restore "$start" "$cart"
    "$scenario" -X "$n"
    [ "$cut_status" = 75 ] || _cut_fail "the cut at cutpoint $n of $m ended with status $cut_status, not 75"
    cp "$cart/run.log" "$cart/cut-$n.log"
    name=$(sed -n "s/^kernel: cutpoint $n: //p" "$cart/cut-$n.log")
    [ -n "$name" ] || _cut_fail "cut $n: the run never logged cutpoint $n"
    "$check" "$n" "$name"
  done
  cut_count=$m
}
