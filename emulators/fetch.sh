#!/usr/bin/env bash
# Fetch the built-in emulators at their pinned commits and apply the Torii
# patches (emulators/patches/NAME.patch), keeping each checkout in step with
# its patch as emu/fetch.sh does for mGBA:
# - patch unchanged since the checkout was made: nothing to do (edits not
#   yet saved with `just emulators-save-patch NAME` are kept)
# - patch changed and the checkout has no edits of its own: fetch again
# - both changed: stop, since fetching again would lose the edits
# Usage: fetch.sh [NAME...] (all of them by default)
set -euo pipefail
cd "$(dirname "$0")"

# NAME REPOSITORY COMMIT
emulators=(
  # Jagoomba Color v0.5 (2022-06-08), GB and GBC; the newest release, and
  # master has nothing after it
  "jagoombacolor https://github.com/EvilJagaGenius/jagoombacolor 2a77825e04b7fcef968c2e5f97cb238296a1ebb5"
  # PocketNES 2025-4-16 (main, 2025-07-19), NES: Dwedit's maintained source,
  # newer than any tagged build
  "pocketnes https://github.com/Dwedit/PocketNES 9c07f3d1d2a26b957769c5cde942060cc9ea6e64"
)

patch_hash() { sha256sum <"patches/$1.patch" | cut -d' ' -f1; }
checkout_hash() { git -C "$1" add -A && git -C "$1" diff --cached | sha256sum | cut -d' ' -f1; }

fetch() {
  local name=$1 repo=$2 commit=$3 stamp=".$1-patch" # hash of the patch the checkout was made or saved from
  if [ -d "$name" ]; then
    local want recorded have
    want=$(patch_hash "$name")
    recorded=$(cat "$stamp" 2>/dev/null || true)
    [ "$want" = "$recorded" ] && [ "$(git -C "$name" rev-parse HEAD)" = "$commit" ] && return 0
    have=$(checkout_hash "$name")
    if [ "$have" = "$want" ] && [ "$(git -C "$name" rev-parse HEAD)" = "$commit" ]; then
      echo "$want" >"$stamp"
      return 0
    fi
    if [ "$have" != "$recorded" ]; then
      echo "error: emulators/patches/$name.patch or the pinned commit changed, and emulators/$name has edits not in the patch." >&2
      echo "Save the edits with 'just emulators-save-patch $name' (then merge), or delete emulators/$name to discard them." >&2
      exit 1
    fi
    echo "emulators/patches/$name.patch changed: fetching $name again"
    rm -rf "$name"
  fi
  git init -q "$name"
  git -C "$name" fetch -q --depth 1 "$repo" "$commit"
  git -C "$name" -c advice.detachedHead=false checkout -q FETCH_HEAD
  git -C "$name" apply --index "../patches/$name.patch"
  patch_hash "$name" >"$stamp"
  echo "$name ${commit:0:7} fetched and patched"
}

for entry in "${emulators[@]}"; do
  read -r name repo commit <<<"$entry"
  if [ $# -eq 0 ] || [[ " $* " == *" $name "* ]]; then
    fetch "$name" "$repo" "$commit"
  fi
done
