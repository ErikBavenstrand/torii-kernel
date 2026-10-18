#!/usr/bin/env bash
# Runs the asset tests (tests/kernel/test_assets.c) on the emulated cart:
# the test kernel started with SELECT held runs only them, on a fresh cart
# with an empty FAT32 card, and logs PASS/FAIL lines. tests/kernel.sh runs
# the other tests without the cart model. Runs inside the torii-emu container.
set -uo pipefail

mkdir -p dist
cart=$(mktemp -d -p dist assets.XXXXXX)
trap 'rm -rf "$cart"' EXIT
mkfs.fat -C -F 32 -n TORII "$cart/sd.img" 65536 >/dev/null

out=$(OMEGA_DIR=$cart emu/build-linux/mgba-shot ezkerneltest.gba -k 1:SELECT -k 30:- -u 'ASSET TESTS DONE' -f 3000 2>&1)
grep -E '^kernel( error)?: ' <<<"$out" | sed 's/^kernel: //; s/^kernel error: /  /' | grep -v '^Torii kernel boot'
if ! grep -q 'ASSET TESTS DONE: [0-9]* passed, 0 failed' <<<"$out" || grep -q 'skipped' <<<"$out"; then
  grep -q 'ASSET TESTS DONE' <<<"$out" || echo "$out" | tail -20
  echo "FAIL: asset tests on the emulated cart"
  exit 1
fi
echo "PASS: asset tests on the emulated cart"
