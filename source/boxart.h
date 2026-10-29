// Box art in the game lists' list with art (decisions doc, Art): which
// cover a game has, and showing it in the page's 80x80 box in place of the
// placeholder.
//
// The lookup, the first that exists (artkey.h has the card's layout):
//	1. custom art by the game's file name, /torii/art/custom/<name>.bin, so a
//	   romhack never gets its base game's art;
//	2. matched art by the game's CRC32, from the CRC cache (/torii/crc.txt,
//	   namesdb.h); never by a GBA game code, which a romhack shares with its
//	   base game (C4);
//	3. the full art pack, /torii/art.pak (artpack.h, `just art-pack`), by
//	   the same CRC32;
//	4. the placeholder box drawn for the game's console (ui/art.h).
// Lookups never touch the card: a background job lists /torii/art and
// /torii/art/custom once (after each boot, and after anything overwrote
// PSRAM) into a set in PSRAM (PSRAM_ART_BASE), and reads the pack's index
// into PSRAM (PSRAM_ARTPACK_BASE), so a game without art costs no SD
// access, and only a cover the set or the index holds is read.
//
// Covers load through the cover cache (engine/cover.h) once the cursor has
// rested BOXART_REST frames, with the rows next to it after, as a job that
// waits while the library builds or sorts its index (library_walking; the
// boot's check walk goes on beside it) and while a menu or dialog is up.
// A cover already cached
// shows at once, even while the cursor moves. With the Box art setting off,
// or no art and no pack on the card, the box is always the placeholder.
//
// A page's use, while its view is up (the box an untagged UI_IMAGE of
// ART_BOX_SIZE square):
//	boxart_open(image, row_fn);      // as the page shows
//	boxart_select(cursor, box);      // the cursor's row, and the
//	                                 // placeholder box (ART_BOX_*) for it
//	boxart_poll();                   // each pass of the menu's loop
#ifndef TORII_BOXART_H
#define TORII_BOXART_H

#include <gba_base.h>

// The page's row as box art needs it: its path (into path, a RAM buffer of
// USERDATA_PATH_MAX bytes) and its size in bytes (0 if unknown). 0 if the
// row has none (past the end).
typedef int (*boxart_row_fn)(u32 row, char *path, u32 *size);

// The page shows with its box in image: reads the Box art setting and
// starts listing the card's art if it hasn't yet.
void boxart_open(u8 image, boxart_row_fn row);
// The cursor is on row (again): its cover if cached, else placeholder box
// box (ART_BOX_*, ui/art.h) in the theme's colours.
void boxart_select(u32 row, u32 box);
// Each pass of the menu's loop while the page is up: wants the covers once
// the cursor has rested, and shows the cursor's when it is ready.
void boxart_poll(void);
// The card's art changed (a custom art file renamed, B18): lists it again.
void boxart_forget(void);

// The game popup's Box art row: the game at path's custom art file, and
// changing it. BOXART_CUSTOM_HIDDEN writes an empty file, which shows the
// placeholder whatever art the game has; BOXART_CUSTOM_NONE removes the
// file (own art or a hiding), so its matched art shows again. 1 if done;
// the art is listed again either way.
enum {
	BOXART_CUSTOM_NONE,
	BOXART_CUSTOM_OWN,
	BOXART_CUSTOM_HIDDEN,
};
u32 boxart_custom(const char *path);
int boxart_set_custom(const char *path, u32 to);

#endif
