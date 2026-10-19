#!/usr/bin/env bash
# Print release notes: how to install, then the newest section of
# CHANGELOG.md without its heading. Used by .github/workflows/release.yml.
set -euo pipefail

cat <<'NOTES'
## Install

1. Copy `ezkernelnew.bin` to the root of the EZ-Flash Omega DE's SD card.
2. Put the card in the cart, hold R and switch the GBA on, then confirm the update.

If the update stops with `Bad NOR ID`, the cart's kernel NOR is a uniform-sector
S29GL064A (ID `0x220C`), and EZ-Flash's loader installs no kernel on it, Torii's
or the official one (`docs/recovery.md`).

NOTES
# The publish job downloads the build's files here first
if [ -f dist/ezkernelnew.sym ]; then
  id=$(sed -n 's/^; torii build id //p' dist/ezkernelnew.sym)
  cat <<NOTES
Build ID \`$id\`: each boot's log line ends with it. \`ezkernelnew.sym\` and
\`ezkernelnew.elf\` hold this build's symbols: \`just logs PATH --tag ${GITHUB_REF_NAME:-TAG}\`
names crash dump addresses with them.

NOTES
fi
cat <<'NOTES'
## Changes

NOTES
awk '/^## /{n++; next} n == 1' CHANGELOG.md | sed '/./,$!d'
