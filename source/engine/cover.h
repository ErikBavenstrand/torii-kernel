// Box art from SD through a cache in PSRAM (decisions doc, Art): a screen
// says which covers it wants, a job reads them a step at a time into their
// PSRAM slots, and the screen copies a ready one into its own BG tiles
// (ui_image_show), straight from PSRAM: covers are made on the PC in the
// tiles' own format (engine/asset.h), so the GBA decodes nothing.
//
// A cover is an asset of COVER_TILES_W x COVER_TILES_H 4bpp tiles with up
// to 16 colours, COVER_FILE_BYTES at most. Where an id's file is, the
// client says (cover_locate_fn): the ids and the card's folders are its
// (boxart.h).
//
// A screen's use, after cover_init:
//
//	cover_wants_begin();
//	cover_want(id, distance) ...      // the cursor's at 0, neighbours further
//	cover_wants_end();                // starts the loader if there is work
//	...
//	u8 e = cover_pin(id);             // once READY: its tiles stay put
//	copy cover_tiles(e) and cover_palette(e) to VRAM (the VBlank queue),
//	then cover_unpin(e) once the copy has landed (gfx_queue_done)
//
// The loader is one job of no view's (it lives as long as the cache), which
// the client may pause (cover_pause_fn) while other SD work goes first: it
// then starts no new cover, and ends; the next cover_wants_end starts it
// again. A cover it is reading is finished first.
//
// Memory: the cache table and the loader (with its FIL) take about 1 KB of
// EWRAM; PSRAM holds COVER_PSRAM_BYTES (engine/psram.h). A game launch
// overwrites PSRAM: it calls cover_cache_invalidate first.
#ifndef TORII_ENGINE_COVER_H
#define TORII_ENGINE_COVER_H

#include <gba_base.h>
#include "asset.h"
#include "covercache.h"

#define COVER_TILES_W 10
#define COVER_TILES_H 10
#define COVER_TILE_BYTES (COVER_TILES_W * COVER_TILES_H * 32)
#define COVER_COLOURS 16
#define COVER_FILE_BYTES (ASSET_HEADER_BYTES + COVER_COLOURS * 2 + COVER_TILE_BYTES)
#define COVER_PATH_MAX 256

// Writes the path of id's cover into path (size bytes, NUL ended) and
// returns 1, or 0 if it has none. Called from the loader's job step, so it
// may not call FatFs itself.
typedef int (*cover_locate_fn)(u32 id, char *path, u32 size);
// Nonzero while the loader should start no new cover.
typedef int (*cover_pause_fn)(void);

// Figures for frame-cycles lines and Diagnostics, since cover_init: the
// slowest loader step (an open, or a read with its PSRAM write), and the
// covers loaded and refused.
struct cover_stats {
	u32 step_max;
	u32 loaded;
	u32 failed;
};
extern struct cover_stats cover_stats;

// Empties the cache. pause may be NULL.
void cover_init(cover_locate_fn locate, cover_pause_fn pause);
// 1 between cover_init and a reset (cover_reset, cover_cache_invalidate):
// a client that keeps covers calls cover_init again when it reads 0.
int cover_active(void);
// Forgets everything, releasing nothing, as before cover_init; closes the
// loader's file if open. For when engine_init (jobs_init) has dropped the
// loader, and for tests.
void cover_reset(void);
// PSRAM is about to be overwritten (a game launch): stops the loader
// (closing its file) and forgets everything as cover_reset does. Does
// nothing before cover_init.
void cover_cache_invalidate(void);

// 1 while the loader is reading a cover: other SD jobs (the library walk)
// wait for it.
int cover_loading(void);

// A batch of wants (see covercache.h): begin, want each id with its
// distance from the cursor, end (starts the loader if there is work and
// the client doesn't pause it).
void cover_wants_begin(void);
void cover_want(u32 id, u16 dist);
void cover_wants_end(void);

// Where id stands: COVER_FREE (not in the cache), COVER_WANTED,
// COVER_LOADING, COVER_READY or COVER_FAILED (no file, or a bad one).
u8 cover_state(u32 id);
// Pins id's cover while it is READY, so it is neither evicted nor loaded
// over: returns its entry, or COVER_NONE. Each pin needs an unpin.
u8 cover_pin(u32 id);
void cover_unpin(u8 entry);
// A pinned entry's tiles (COVER_TILE_BYTES, word-aligned) and palette, in
// PSRAM, and its colour count (at most COVER_COLOURS).
const u32 *cover_tiles(u8 entry);
const u16 *cover_palette(u8 entry);
u32 cover_colours(u8 entry);

// The cache, for queries and tests.
const struct cover_cache *cover_cache(void);

#endif
