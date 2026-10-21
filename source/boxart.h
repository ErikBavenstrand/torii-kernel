// Box art in the game lists' list with art (decisions doc, Art): which
// cover a game has, and showing it in the page's 80x80 box in place of
// B2's placeholder.
//
// The lookup, the first that exists (artkey.h has the card's layout):
//	1. custom art by the game's file name, /torii/art/custom/<name>.bin, so a
//	   romhack never gets its base game's art;
//	2. matched art by the game's CRC32, then by its GBA game code, both from
//	   the CRC cache (/torii/crc.txt, namesdb.h);
//	3. the placeholder.
// Lookups never touch the card: a background job lists /torii/art and
// /torii/art/custom once (after each boot, and after anything overwrote
// PSRAM) into a set in PSRAM (PSRAM_ART_BASE), so a game without art costs
// no SD access, and only a cover the set holds is opened.
//
// Covers load through the cover cache (engine/cover.h) once the cursor has
// rested BOXART_REST frames, with the rows next to it after, as a job that
// waits while the library walks (library_walking). A cover already cached
// shows at once, even while the cursor moves. With the Box art setting off,
// or no /torii/art on the card, the box is always the placeholder.
//
// A page's use, while its view is up (the box an untagged UI_IMAGE of
// ART_BOX_SIZE square):
//	boxart_open(image, row_fn);      // as the page shows
//	boxart_select(cursor, palette);  // the cursor's row, and the
//	                                 // placeholder's colours for it
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
// The cursor is on row (again): its cover if cached, else the placeholder
// (art_box_tiles) in palette's 16 colours, which must stay valid while the
// page is up.
void boxart_select(u32 row, const u16 *palette);
// Each pass of the menu's loop while the page is up: wants the covers once
// the cursor has rested, and shows the cursor's when it is ready.
void boxart_poll(void);
// The card's art changed (a custom art file renamed): lists it again.
void boxart_forget(void);

// 1 once the card's art is listed (for tests), with the files found.
int boxart_listed(u32 *files);

#endif
