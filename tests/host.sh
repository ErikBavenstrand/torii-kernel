#!/usr/bin/env bash
# Builds the hardware-free kernel code and tests/host for the host with
# AddressSanitizer and UndefinedBehaviorSanitizer, then runs the tests; any
# sanitizer report fails them. Runs inside the torii-emu container.
set -euo pipefail

mkdir -p dist/host
# -funsigned-char: char is unsigned on ARM, signed on x86 hosts
cc -std=gnu11 -g -O1 -fno-omit-frame-pointer -funsigned-char \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -Wall -Wextra -Wshadow -Wundef -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes -Werror \
  -I tests/host -o dist/host/tests \
  source/sort.c source/cheat.c source/charset.c source/irqpatch.c source/savename.c source/logbuf.c source/lz77.c source/norlist.c source/crc32.c tests/host/*.c
# A hung test (an endless loop in a sort, say) fails after a minute.
if ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 timeout 60 dist/host/tests; then
  echo "PASS: host unit tests"
else
  echo "FAIL: host unit tests"
  exit 1
fi
