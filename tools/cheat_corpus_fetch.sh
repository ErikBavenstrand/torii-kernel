#!/usr/bin/env bash
# Fetches libretro-database's GBA cheats (https://github.com/libretro/libretro-database,
# "cht/Nintendo - Game Boy Advance", CC-BY-SA-4.0) into dist/cheat-corpus for
# `just cheat-corpus`: a sparse, shallow clone in dist/cheat-corpus-git, the
# .cht files copied out, and the commit they came from in
# dist/cheat-corpus/SOURCE.txt. The files are never committed.
#
# Usage: cheat_corpus_fetch.sh [--refresh]  (without it, a corpus already
# fetched is kept)
set -euo pipefail

out=dist/cheat-corpus
git_dir=dist/cheat-corpus-git
folder="cht/Nintendo - Game Boy Advance"

if [ "${1:-}" != --refresh ] && compgen -G "$out/*.cht" >/dev/null; then
  exit 0
fi
rm -rf "$git_dir"
git clone -q --depth 1 --filter=blob:none --sparse https://github.com/libretro/libretro-database.git "$git_dir"
git -C "$git_dir" sparse-checkout set "$folder"
rm -rf "$out"
mkdir -p "$out"
cp "$git_dir/$folder"/*.cht "$out/"
{
  echo "libretro-database $(git -C "$git_dir" rev-parse HEAD) ($(git -C "$git_dir" log -1 --format=%cs))"
  echo "$folder, CC-BY-SA-4.0: https://github.com/libretro/libretro-database"
} >"$out/SOURCE.txt"
echo "cheat corpus: $(find "$out" -name '*.cht' | wc -l) files from $(head -1 "$out/SOURCE.txt")"
