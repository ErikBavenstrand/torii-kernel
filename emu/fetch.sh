#!/usr/bin/env bash
# Fetch mGBA at the pinned release and apply the Torii patches.
# Re-running is a no-op once emu/mgba exists; delete it to start over.
set -euo pipefail
cd "$(dirname "$0")"

MGBA_TAG=0.10.5

if [ -d mgba ]; then
  exit 0
fi
git -c advice.detachedHead=false clone -q --depth 1 --branch "$MGBA_TAG" https://github.com/mgba-emu/mgba.git mgba
for patch in patches/*.patch; do
  git -C mgba apply --index "../$patch"
done
echo "mGBA $MGBA_TAG fetched and patched"
