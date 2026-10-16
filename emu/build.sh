#!/usr/bin/env bash
# Build the patched mGBA library and the mgba-shot runner (headless, Linux).
# Runs inside the torii-emu container from the repository root.
set -euo pipefail

cmake -Wno-author -Wno-deprecated -S emu/mgba -B emu/build-linux -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_SHARED=OFF -DBUILD_STATIC=ON \
  -DUSE_FFMPEG=OFF -DUSE_LIBZIP=OFF -DUSE_MINIZIP=OFF -DUSE_SQLITE3=OFF \
  -DUSE_ELF=OFF -DUSE_LUA=5.4 -DUSE_EPOXY=OFF -DBUILD_GL=OFF -DBUILD_GLES2=OFF \
  -DBUILD_GLES3=OFF -DUSE_DISCORD_RPC=OFF -DUSE_EDITLINE=OFF -DUSE_LZMA=OFF \
  -DUSE_DEBUGGERS=ON -DUSE_GDB_STUB=ON -DBUILD_PERF=OFF -DBUILD_TEST=OFF \
  -DBUILD_SUITE=OFF -DBUILD_EXAMPLE=OFF >/dev/null
cmake --build emu/build-linux -j"$(nproc)" -- --no-print-directory | grep -E 'error|warning' || true
gcc -O2 -Wall -DUSE_PNG -Iemu/mgba/include -Iemu/build-linux/include \
  -o emu/build-linux/mgba-shot emu/mgba-shot.c emu/build-linux/libmgba.a \
  -llua5.4 -lpng -lz -lm -lpthread 2>&1 | grep -v -E 'lto-wrapper' || true
