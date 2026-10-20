// Cover streaming: cover art from SD (or ROM) into sprites, through a cache
// of decoded covers in PSRAM (decisions doc, Covers). For T6's widgets and
// v0.4's carousels.
//
// The route: SD, read a few KB a step into PSRAM (engine/sdread.h); one
// step decodes the cover with the BIOS's LZ77 into the stage (engine/
// stage.h) and writes it to its PSRAM slot; then a sprite slot that shows
// it gets sprite tiles and a palette bank, and the VBlank queue copies the
// tiles from PSRAM (COVER_UPLOADS_PER_FRAME covers a frame at most). Covers
// are assets (engine/asset.h); where an id's file is, the client says
// (cover_locate_fn), so the cover format and folder stay v0.4's choice.
//
// A screen's use:
//
//	cover_init(locate, owner);               // after engine_init
//	each frame, after input and motion:
//		cover_wants_begin();
//		cover_want(id, distance) ...      // neighbours, to prefetch
//		cover_wants_end();
//		cover_slot_show(slot, id) ...     // what is on screen
//		cover_update();                   // uploads, states, callbacks
//		then draw: a slot whose state is COVER_SLOT_READY goes in OAM
//		with cover_slot_attr0/1/2 (or_ed with position and matrix)
//	cover_shutdown();                        // when the screen goes
//
// Sprite slots show distance 0 on their own, and their covers are pinned:
// never evicted while shown. 4bpp covers each get a palette bank (shared by
// slots showing the same cover). 8bpp covers get none: 8bpp sprites all use
// the one 256-colour sprite palette, so covers shown together must share a
// palette, which the screen loads (cover_load_palette8 loads a cover's).
//
// Memory: the cache table, the slots and the loader (with its FIL) take
// about 1 KB of EWRAM; PSRAM holds COVER_PSRAM_BYTES (engine/psram.h).
// A game launch overwrites PSRAM: it calls cover_cache_invalidate first.
#ifndef TORII_ENGINE_COVER_H
#define TORII_ENGINE_COVER_H

#include <gba_base.h>
#include "covercache.h"

#define COVER_SLOTS 8
#define COVER_UPLOADS_PER_FRAME 2
// Bytes the loader reads per step
#define COVER_READ_CHUNK 4096
// A 4bpp cover's palette bank is tagged with this plus its cache entry
#define COVER_PAL_TAG 0xC000
#define COVER_PATH_MAX 256

enum {
	COVER_SRC_NONE = 0,  // the id has no cover
	COVER_SRC_FILE,  // an asset file on SD, at the path written
	COVER_SRC_ROM,  // an asset in ROM, word-aligned
};

struct cover_src {
	u8 kind;
	const void *data;  // COVER_SRC_ROM: the asset
	u32 bytes;  // COVER_SRC_ROM: its size
};

// Says where id's cover is: a path written into path (size bytes, NUL
// ended) for COVER_SRC_FILE, or data and bytes for COVER_SRC_ROM. Called
// from the loader's job step, so it may not call FatFs itself.
typedef void (*cover_locate_fn)(u32 id, struct cover_src *src, char *path, u32 size);
// Called when a slot's cover is ready to show (ok) or can't be (not ok).
typedef void (*cover_ready_fn)(u8 slot, u32 id, int ok);

enum {
	COVER_SLOT_EMPTY = 0,
	COVER_SLOT_LOADING,  // in the cache's queue, or being read and decoded
	COVER_SLOT_UPLOADING,  // tiles queued for VRAM
	COVER_SLOT_READY,  // in sprite VRAM: show it
	COVER_SLOT_FAILED,  // no cover, a bad file, or no room in sprite VRAM
};

// Figures for frame-cycles lines and Diagnostics, since cover_init: the
// slowest decode step (LZ77 and PSRAM write together), and of its parts
// the slowest LZ77 decode and PSRAM write; covers decoded. (mGBA's BIOS
// emulation times LZ77 its own way: the cart's figure is T8's to measure.)
struct cover_stats {
	u32 decode_max;
	u32 lz77_max;
	u32 write_max;
	u32 decoded;
};
extern struct cover_stats cover_stats;

// Empties the cache and the slots. owner is the view the loader job belongs
// to (jobs_cancel_owner), or JOB_OWNER_NONE.
void cover_init(cover_locate_fn locate, u8 owner);
void cover_on_ready(cover_ready_fn ready);
// Forgets everything without releasing anything, as before cover_init: for
// when engine_init (or res_init and jobs_init) has reset what the slots and
// the loader held. Closes the loader's file if open.
void cover_reset(void);
// Stops the loader (closing its file) and empties every slot, freeing
// their sprite tiles and palette banks. The cache keeps what it decoded.
void cover_shutdown(void);
// PSRAM is about to be overwritten (a game launch): stops the loader
// (closing its file) and forgets everything as cover_reset does, releasing
// nothing; cover_init again to stream. Does nothing before cover_init.
void cover_cache_invalidate(void);

// 1 while the loader job runs (reading or decoding a cover): other SD jobs
// (the library walk) wait, since they share FatFs's sector window.
int cover_loading(void);

// A batch of wants (see covercache.h): begin, want each id with its
// distance from the cursor, end (starts the loader if there is work).
void cover_wants_begin(void);
void cover_want(u32 id, u16 dist);
void cover_wants_end(void);

// Shows id in slot (0 to COVER_SLOTS - 1), replacing what it showed; the
// same id again changes nothing. Returns 0 for a bad slot.
int cover_slot_show(u8 slot, u32 id);
// Empties slot, freeing its tiles and palette bank (once its upload, if
// still queued, has landed).
void cover_slot_clear(u8 slot);
u8 cover_slot_state(u8 slot);
u32 cover_slot_id(u8 slot);
// For OAM once READY: attribute 0's shape and colour bits (or in y and the
// affine bits), attribute 1's size bits (or in x and the matrix), attribute
// 2 whole (tile number and palette bank). 0 for a slot not READY.
u16 cover_slot_attr0(u8 slot);
u16 cover_slot_attr1(u8 slot);
u16 cover_slot_attr2(u8 slot);
// The cover's size in pixels; 0 for a slot not READY.
u32 cover_slot_width(u8 slot);
u32 cover_slot_height(u8 slot);

// Loads the palette of a READY slot's cover into the sprite palette from
// colour 0 (8bpp covers). 0 if the slot isn't READY.
int cover_load_palette8(u8 slot);

// Once a frame before drawing: moves slots on (allocating sprite tiles and
// queueing uploads as their covers are decoded), calls the ready callback,
// and keeps the loader running while covers are wanted.
void cover_update(void);

// The cache, for queries and tests.
const struct cover_cache *cover_cache(void);
// A cache entry's PSRAM slot.
u8 *cover_psram_slot(u8 entry);

#endif
