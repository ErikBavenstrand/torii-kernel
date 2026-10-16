#!/usr/bin/env bash
# Print release notes: how to install, then the newest section of
# CHANGELOG.md without its heading. Used by .github/workflows/release.yml.
set -euo pipefail

cat <<'NOTES'
## Install

1. Copy `ezkernelnew.bin` to the root of the EZ-Flash Omega DE's SD card.
2. Put the card in the cart, hold R and switch the GBA on, then confirm the update.

## Changes

NOTES
awk '/^## /{n++; next} n == 1' CHANGELOG.md | sed '/./,$!d'
