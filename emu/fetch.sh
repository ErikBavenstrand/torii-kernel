#!/usr/bin/env bash
# Fetch mGBA at the pinned release and apply the Torii patches, and keep the
# checkout in step with emu/patches:
# - patches unchanged since the checkout was made: nothing to do (edits in
#   emu/mgba not yet saved with `just emu-save-patch` are kept)
# - patches changed (e.g. after a pull) and emu/mgba has no edits of its own:
#   fetch again
# - both changed: stop, since fetching again would lose the edits
set -euo pipefail
cd "$(dirname "$0")"

MGBA_TAG=0.10.5
stamp=.mgba-patches  # hash of the patches the checkout was made or saved from

patches_hash() { cat patches/*.patch | sha256sum | cut -d' ' -f1; }
checkout_hash() { git -C mgba add -A && git -C mgba diff --cached | sha256sum | cut -d' ' -f1; }

if [ -d mgba ]; then
  want=$(patches_hash)
  recorded=$(cat "$stamp" 2>/dev/null || true)
  [ "$want" = "$recorded" ] && exit 0
  have=$(checkout_hash)
  if [ "$have" = "$want" ]; then
    echo "$want" >"$stamp"
    exit 0
  fi
  if [ "$have" != "$recorded" ]; then
    echo "error: emu/patches changed, and emu/mgba has edits not in them." >&2
    echo "Save the edits with 'just emu-save-patch' (then merge), or delete emu/mgba to discard them." >&2
    exit 1
  fi
  echo "emu/patches changed: fetching mGBA again"
  rm -rf mgba
fi
git -c advice.detachedHead=false clone -q --depth 1 --branch "$MGBA_TAG" https://github.com/mgba-emu/mgba.git mgba
for patch in patches/*.patch; do
  git -C mgba apply --index "../$patch"
done
patches_hash >"$stamp"
echo "mGBA $MGBA_TAG fetched and patched"
