# Licences and third-party notices

Torii is a fork of EZ-Flash's `omega-de-kernel` (Apache 2.0, `LICENSE`). It is
not made, endorsed or supported by EZ-Flash. The cart shows the same credits
under Info, Credits.

**The release binary (`ezkernelnew.bin`) is distributed under the GNU GPL 3 or
later** (`licenses/GPL-3.0.txt`). It links minilzo (GPL 2 or later) inside both
emulators, and the Apache 2.0 code in it is compatible with version 3. The
source of each release is this repository at its tag; `just build` rebuilds the
binary from it, bit for bit. Each release attaches this file and the licence
texts as `torii-licenses.zip`.

## In the binary

| Part | Copyright | Licence | Where |
|---|---|---|---|
| EZ-Flash's kernel 1.06, Torii's base | © 2021 EZ-Flash | Apache 2.0 | `source/`, `LICENSE` |
| Torii's changes and new code, the menu's own art, the golden Emerald cheats | © 2026 Erik Båvenstrand and contributors | Apache 2.0 | `source/`, `tools/`, `tests/`, `lang/`, `assets/` |
| FatFs R0.15 | © 2022 ChaN | BSD-style: keep the notice and disclaimer | `source/ff15/` |
| libgba | © 2003-2004 Dave Murphy | LGPL 2 or later, linked; `just build` allows a relink | `licenses/LGPL-2.0-libgba.txt` |
| minilzo, in both emulators | © 1996-2000 Markus F.X.J. Oberhumer | GPL 2 or later | `licenses/GPL-2.0.txt` |
| Cheat decoders, ported from mGBA | © 2013-2016 Jeffrey Pfau | MPL 2.0, file level | `source/cheatcode.c`, `licenses/MPL-2.0.txt` |
| Galmuri9, Galmuri11 Bold (U+0020-U+00FF); Galmuri9's Hangul, jamo and kana (U+3041-U+30FF, U+3131-U+318E, U+AC00-U+D7A3), in the Korean pack | © 2019-2025 Lee Minseo | SIL OFL 1.1 | `assets/fonts/galmuri/`, `licenses/OFL-1.1-Galmuri.txt` |
| Console pictures, redrawn on KyleBing's icons | © KyleBing | CC BY 4.0 | `assets/ui/README.md` |
| Jagoomba Color (GB, GBC), after Goomba Color and Goomba | EvilJagaGenius, after Dwedit and FluBBa; no notice in the source | none stated (Open points) | `emulators/`: a pinned commit and our patch |
| PocketNES (NES), after Loopy and FluBBa | Dwedit; no notice in the source | none stated (Open points) | `emulators/`: a pinned commit and our patch |
| newlib and GCC's runtime, from devkitARM r54 | their authors | BSD-style; GCC Runtime Library Exception | devkitARM |

## Never bundled

Used on the user's machine or card, never committed or released:

- **Game names** (`just names-update`): No-Intro's data through libretro-database, CC BY-SA 4.0, written to the user's own `/torii/names.db`.
- **Box art** (`just art-scrape`, `just art-pack`): libretro-thumbnails, which grants no licence.
- **Cheats** (`just cheats`): GameHacking.org's exports.
- **Games, saves, BIOS:** the user's own. `roms/` is git-ignored.
- **mGBA** (MPL 2.0), the emulated cart's emulator for tests: a pinned checkout with our patch.

## Not Torii's

`ezkernelnew.bin`, `image.bin`, `Link_kernel_image.exe` and
`EZ-FLASH OMEGA DE DOCUMENT.pdf` at the root came with the fork. They are
EZ-Flash's, are not part of a Torii release, and have no licence of their own
beyond the repository's Apache 2.0 file. `bitstreams/` (FPGA firmware cut from
`image.bin`) is git-ignored.

## Open points

Jagoomba Color and PocketNES have no licence file, and neither does Goomba,
which they descend from. Their source is public, Jagoomba's README welcomes
reuse of its changes, and EZ-Flash and Simple DE ship builds of them. But no
text grants the right to redistribute or modify them, so Torii's release of
them rests on the authors' evident intent, not on a licence. Only Jaga and
Dwedit can settle it; the roadmap tracks the request.

This is what Torii's maintainer found, not legal advice.
