#!/usr/bin/env bash
# Builds the hardware-free kernel code and tests/host for the host with
# AddressSanitizer and UndefinedBehaviorSanitizer, then runs the tests; any
# sanitizer report fails them. Runs inside the torii-emu container.
set -euo pipefail

mkdir -p dist/host
cc -std=gnu11 -g -O1 -fno-omit-frame-pointer \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -Wall -Wextra -Wshadow -Wundef -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes -Werror \
  -I tests/host -o dist/host/tests \
  source/sort.c source/cheat.c tests/host/*.c
if ASAN_OPTIONS=abort_on_error=0:detect_leaks=1 dist/host/tests; then
  echo "PASS: host unit tests"
else
  echo "FAIL: host unit tests"
  exit 1
fi
