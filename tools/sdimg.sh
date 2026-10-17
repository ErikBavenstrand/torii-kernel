#!/usr/bin/env bash
# Runs tools/sdimg.c (SD images through the kernel's own FatFs, with exFAT and
# UTF-8 names), building it first if needed: sdimg.sh IMG COMMAND ARGS...
# Runs inside the torii-emu container.
set -euo pipefail

out=dist/sdimg
bin=$out/sdimg
if [ ! -x "$bin" ] || [ -n "$(find tools/sdimg.c tools/sdimg.sh source/ff15 -newer "$bin" 2>/dev/null)" ]; then
  mkdir -p "$out"
  cp source/ff15/ff.c source/ff15/ff.h source/ff15/ffunicode.c source/ff15/diskio.h "$out/"
  # The kernel's FatFs settings, plus f_mkfs, labels, the UTF-8 API and no clock
  sed -e 's/^#define FF_USE_MKFS\t\t0/#define FF_USE_MKFS\t\t1/' \
    -e 's/^#define FF_LFN_UNICODE\t[0-9]/#define FF_LFN_UNICODE\t2/' \
    -e 's/^#define FF_FS_NORTC\t\t0/#define FF_FS_NORTC\t\t1/' \
    -e 's/^#define FF_USE_LABEL\t0/#define FF_USE_LABEL\t1/' \
    source/ff15/ffconf.h >"$out/ffconf.h"
  cc -std=gnu11 -O1 -g -Wall -I "$out" -o "$bin" tools/sdimg.c "$out/ff.c" "$out/ffunicode.c"
fi
exec "$bin" "$@"
