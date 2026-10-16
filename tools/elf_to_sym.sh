#!/usr/bin/env bash
# Write an armips-style symbol file (ADDRESS NAME per line) from an ELF, for
# mGBA's debugger: `load-symbols dist/ezkernelnew.sym`. Thumb functions have
# bit 0 set in the ELF, so it is cleared. Runs in the devkitARM container.
# Usage: elf_to_sym.sh <elf> <out.sym>
set -euo pipefail

/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm -n "$1" |
  while read -r addr type name; do
    case $type in [TtDdBbRr]) ;; *) continue ;; esac
    case $name in '$'* | .*) continue ;; esac
    value=$((16#$addr))
    case $type in [Tt]) value=$((value & ~1)) ;; esac
    printf '%08X %s\n' "$value" "$name"
  done >"$2"
