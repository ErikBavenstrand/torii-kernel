// The kernel's side of v0.8's in-game menu (docs/design/v0.8/ingame-menu.md):
// at each GBA launch it writes the overlay's image and the session block to
// cart SRAM, where the stub in the game's patch area finds them
// (source/ingame_abi.h).
#ifndef TORII_INGAME_H
#define TORII_INGAME_H

#include <gba_base.h>

// Writes the overlay (source/overlay, embedded by ingame_overlay.s) to
// INGAME_SRAM_IMAGE and a fresh session block for the game file name (its
// path's last part) of rom_bytes bytes to INGAME_SRAM_SESSION, and leaves
// the SRAM page at 0. Call it once the launch's hardware use and keys are
// known (gl_game_uses, gl_sleep_on), before Launch_now. Returns 0, or -1 if
// what was written didn't read back (logged; the menu then won't open).
int ingame_prepare(const char *name, u32 rom_bytes);

// The embedded overlay image (ingame_overlay.s)
extern const u8 ingame_overlay[];
extern const u32 ingame_overlay_size;

#endif
