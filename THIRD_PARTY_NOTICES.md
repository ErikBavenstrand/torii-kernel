# Licences and third-party notices

Torii is a fork of EZ-Flash's `omega-de-kernel` (Apache License 2.0, `LICENSE`).
It is not made, endorsed or supported by EZ-Flash. Every file's licence
header, where it has one, stays as upstream or its author wrote it. The same
credits show on the cart: Info, Credits.

The kernel binary (`ezkernelnew.bin`) links all of the code below into one
program. It contains minilzo (GPL 2 or later, inside both built-in
emulators), so **the binary as a whole is distributed under the GNU General
Public License, version 3 or later** (`licenses/GPL-3.0.txt`; the Apache
2.0 code in it is compatible with version 3). The complete source of every
release is the repository at that release's tag, and `just build` rebuilds
the binary from it, bit for bit. Each release also carries
`torii-licenses.zip`, this file with the licence texts.

## In the kernel binary

| Part | Author | Licence | Where |
|---|---|---|---|
| EZ-Flash's official kernel 1.06 source, as Torii builds on it | EZ-Flash | Apache 2.0 | `source/`, `LICENSE` |
| Torii's changes and new code | Erik Båvenstrand and contributors | Apache 2.0 | `source/`, `tools/`, `tests/`, `lang/` |
| FatFs R0.15 | ChaN | BSD-style (keep the notice and disclaimer) | `source/ff15/` |
| libgba | Dave Murphy | LGPL 2 or later | devkitARM's `libgba`, linked; the build's objects and `just build` allow a relink (`licenses/LGPL-2.0-libgba.txt`) |
| Jagoomba Color (GB, GBC) | EvilJagaGenius, after Dwedit's Goomba Color and FluBBa's Goomba | see "Open points" | `emulators/` (pinned commit, our patch) |
| PocketNES (NES) | Dwedit, after Loopy and FluBBa | see "Open points" | `emulators/` (pinned commit, our patch) |
| minilzo 2.x, inside both emulators | Markus F.X.J. Oberhumer | GPL 2 or later | `licenses/GPL-2.0.txt` |
| The cheat decoders (`source/cheatcode.c`), ported from mGBA | Jeffrey Pfau | MPL 2.0 (file level) | `licenses/MPL-2.0.txt` |
| Galmuri9 and Galmuri11 Bold, cut to U+0020-U+00FF | Lee Minseo | SIL Open Font License 1.1 | `assets/fonts/galmuri/`, `licenses/OFL-1.1-Galmuri.txt` |
| Game Boy, GBC and GBA console pictures, redrawn on KyleBing's icons | KyleBing, then Torii | CC BY 4.0 | `assets/ui/README.md` |
| The newlib C library and GCC's runtime, from devkitARM | their authors | BSD-style; GCC Runtime Library Exception | devkitARM r54 |

The rest of the menu's art, the icons, the shadows, the placeholder boxes,
and the golden Pokémon Emerald cheats are Torii's own (Apache 2.0).

## Not in the binary, and not shipped

These are used on the user's own machine or card, and never bundled:

- **Game names** (`just names-update`): No-Intro's data through libretro-database, CC BY-SA 4.0. The tool writes the user's own `/torii/names.db`.
- **Box art** (`just art-scrape`, `just art-pack`): libretro-thumbnails, which grants no licence. Fetched on the user's machine, never committed or released.
- **Cheats** (`just cheats`): GameHacking.org's exports, fetched on the user's machine, never committed or released.
- **Games and saves:** the user's own. No game, BIOS or commercial ROM is in the repository (`roms/` is git-ignored).
- **The test and development tools:** mGBA (MPL 2.0) for the emulated cart, built from a pinned checkout with our patch, never shipped.

## In the repository but not Torii's

`ezkernelnew.bin`, `image.bin`, `Link_kernel_image.exe` and
`EZ-FLASH OMEGA DE DOCUMENT.pdf` at the root came with the fork from
EZ-Flash's repository. They are EZ-Flash's, as published by them, and are not
part of what Torii releases. `bitstreams/` (FPGA firmware cut from
`image.bin` for research) is git-ignored.

## Open points

These need the authors' answer, not ours:

- **Jagoomba Color and PocketNES carry no licence file.** Their source is public and their authors invite its use (Jagoomba's README: "If you'd like to incorporate my changes into Goomba Color, you're more than welcome to"), and EZ-Flash and Simple DE ship builds of them. But no text gives anyone the right to redistribute or modify them, and Goomba's own terms are not in either repository. Until the authors say so in writing, Torii's release of a binary that includes them rests on the authors' evident intent, not on a licence. The roadmap's request to Jaga and Dwedit tracks it.
- **The EZ-Flash files above** have no licence of their own in their repository besides the Apache 2.0 file that covers the repository.

This file records what Torii's maintainer found; it is not legal advice.
