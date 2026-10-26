#!/usr/bin/env bash
# Builds a host program against mGBA's own GBA cheat decoders, compiled from
# the emu/mgba checkout (only the files they need, with tools/mgba_cheats'
# flags and stubs) under ASan and UBSan, so the decoders run without the
# emulator build (tests/host.sh, tests/cheat_corpus.sh). Runs inside the
# torii-emu container. Usage: mgba_cheats.sh OUT MAIN.c...
set -euo pipefail

out=$1
shift
# The objects sit beside OUT (callers may build at once: the engine test's
# shards) and are rebuilt whenever this script or a source is newer
obj=$out.obj
mkdir -p "$obj"
srcs=(core/cheats.c gba/cheats.c gba/cheats/gameshark.c gba/cheats/parv3.c gba/cheats/codebreaker.c
  util/string.c util/table.c util/hash.c util/gbk-table.c)
# mGBA's own files are built without sanitizers: they have undefined
# behaviour of their own, which isn't what this checks (parv3.c shifts by a
# negative count for a width-8 conditional; gameshark.c's type 3 list reads
# its first cheat through a pointer that a realloc of the cheat list has
# freed). The drivers and stubs get ASan and UBSan.
flags=(-std=gnu11 -g -O1 -fno-omit-frame-pointer
  -Itools/mgba_cheats -Iemu/mgba/include -Iemu/mgba/src -DHAVE_STRDUP -DHAVE_STRNDUP)
objs=()
for src in "${srcs[@]}"; do
  o=$obj/$(echo "$src" | tr / _ | sed 's/\.c$/.o/')
  if [ ! -e "$o" ] || [ "emu/mgba/src/$src" -nt "$o" ] || [ "$0" -nt "$o" ]; then
    gcc "${flags[@]}" -w -c -o "$o" "emu/mgba/src/$src"
  fi
  objs+=("$o")
done
gcc "${flags[@]}" -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror -o "$out" "$@" tools/mgba_cheats/stubs.c "${objs[@]}"
