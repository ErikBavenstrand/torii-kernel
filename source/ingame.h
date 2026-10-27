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
// the SRAM page at 0. rom_path is the ROM's path from the card's root (the
// save's, gamesave.h), for the kernel's next boot. The cheat list is
// cheatstore_launch's (below), offered when the patch took its whole table.
// Call it once the launch's hardware use, keys and patch are known
// (gl_game_uses, gl_sleep_on, gl_cheat_on), before Launch_now. Returns 0, or
// -1 if what was written didn't read back (logged; the menu then won't open).
int ingame_prepare(const char *name, u32 rom_bytes, const char *rom_path);

// The in-game cheat list (P2), written by cheatstore_launch before the game
// loads (cart SRAM keeps it; the game takes PSRAM, where the names are):
// none listed yet; list entry n (name: len bytes of UTF-8; its cheatN_
// number and its index in the file, its INGAME_CHEAT_* flags, toggle bit and
// cycles); entry n's flags, bit and cycles again; how many are listed and
// in the table; and the table cut to its first table cheats (the extras
// left out: they lose their toggles).
void ingame_cheats_clear(void);
void ingame_cheat_put(u32 n, const char *name, u32 len, u32 number, u32 index, u32 flags, u32 bit, u32 cost);
void ingame_cheat_meta(u32 n, u32 flags, u32 bit, u32 cost);
void ingame_cheats_set(u32 count, u32 table);
void ingame_cheats_trim(u32 table);

// The save states (P3, statestore.h) of a GBA launch with Save states on,
// for the game at rom_path (file name rom_name) in its save slot: an old
// /RTS state imported as slot 1 once, then slot (or the newest, for 0) put
// in cart SRAM's buffer, to load at the game's start if slot isn't 0; the
// slots' rows go in the session block. 0 if slot was asked for and didn't
// load. ingame_states_none: a launch without them (call it at a launch's
// start).
int ingame_states_launch(const char *rom_path, const char *save_slot, const char *rom_name, u32 slot);
void ingame_states_none(void);

// At boot, once the card is up and the pending save copied: if cart SRAM
// holds the session of a game that came back to Torii (a soft reset keeps
// it), acts on the overlay's answer: the cheat list's toggles become the
// cheat file's picks. Then the answer is cleared.
void ingame_after_game(void);

// At boot, once the card is up and before the pending save is copied: if
// the in-game menu's stub refused to open (its record in cart RAM page
// 0xF0, cartlog_ingame_refused), says so in a dialog, once: the game, the
// reason in plain words, and what it costs. cartlog_boot_report then logs
// and clears the record.
void ingame_refused_notice(void);

// For the in-game probe ROM (game code INGAME_PROBE_CODE, source/ingameprobe.h)
// only: makes its scratch file and tells it the file's sector through cart
// RAM page 0xF0. Anything else: does nothing. Call it before the launch's
// last SD access.
void ingame_probe_prepare(const u8 *code);

// The embedded overlay image (ingame_overlay.s)
extern const u8 ingame_overlay[];
extern const u32 ingame_overlay_size;

#endif
