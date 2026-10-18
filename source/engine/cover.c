// See cover.h.
#include <string.h>

#include <gba_compression.h>

#include "cover.h"
#include "asset.h"
#include "cycles.h"
#include "frame.h"
#include "gfx.h"
#include "jobs.h"
#include "psram.h"
#include "res.h"
#include "sdread.h"
#include "stage.h"
#include "../tlog.h"

// The stage within one loader step: decoded tiles, the palette, and the
// path the locate callback writes (read by f_open in the same step)
#define STAGE_TILES 0
#define STAGE_PALETTE ASSET_TILE_BYTES_MAX
#define STAGE_PATH (STAGE_BYTES - COVER_PATH_MAX)

enum {
	LOAD_PICK = 0,  // take the nearest wanted entry, find its cover, open its file
	LOAD_READ,  // read a chunk into the PSRAM stage
	LOAD_DECODE,  // LZ77 into the stage, then into the entry's PSRAM slot
};

struct slot {
	u32 id;
	u32 ticket;  // the upload's (gfx_queue_ticket)
	u8 state;  // COVER_SLOT_*
	u8 entry;  // the cache entry pinned, or COVER_NONE
	u8 tiles;  // RES_OBJ_VRAM handle, or RES_NONE
	u8 pal;  // RES_OBJ_PAL handle (4bpp), or RES_NONE
	u8 shape;  // OAM shape (attribute 0 bits 14-15)
	u8 size;  // OAM size (attribute 1 bits 14-15)
	u8 bpp8;
	u8 width, height;  // tiles
};

static struct cover_cache cache EWRAM_BSS;
static struct slot slots[COVER_SLOTS] EWRAM_BSS;
static struct {
	struct sd_read rd;  // the file being read
	const u8 *src;  // the asset to decode: the PSRAM stage, or ROM
	u32 src_bytes;
	cover_locate_fn locate;
	cover_ready_fn ready_cb;
	u8 entry;  // the entry loading, or COVER_NONE
	u8 job;  // JOB_NONE when not running
	u8 owner;
	u8 ready;  // cover_init has run
} loader EWRAM_BSS;
struct cover_stats cover_stats EWRAM_BSS;

u8 *cover_psram_slot(u8 entry)
{
	return (u8 *)(PSRAM_ASSET_BASE + COVER_STAGE_BYTES + (u32)entry * COVER_SLOT_BYTES);
}

static u8 *psram_stage(void)
{
	return (u8 *)PSRAM_ASSET_BASE;
}

const struct cover_cache *cover_cache(void)
{
	return &cache;
}

// --- The loader job ---------------------------------------------------------

// Decodes the asset at src (bytes long, in PSRAM or ROM) into entry's PSRAM
// slot. 1 if it is there.
static int decode(u8 entry, const u8 *src, u32 bytes)
{
	u32 id = cache.entry[entry].id;
	struct asset_header h;
	if ((u32)src & 3 || bytes < ASSET_HEADER_BYTES) {
		res_set_error("cover %08lX: %lu bytes at %08lX, not a word-aligned asset", (unsigned long)id,
			      (unsigned long)bytes, (unsigned long)(u32)src);
		return 0;
	}
	memcpy(&h, src, sizeof(h));
	u32 lz_at = ASSET_HEADER_BYTES + asset_palette_bytes(&h);
	u32 lz_word = bytes >= lz_at + 4 ? *(const u32 *)(src + lz_at) : 0;
	int err = asset_check(&h, bytes, lz_word);
	if (err != ASSET_OK) {
		res_set_error("cover %08lX: %s", (unsigned long)id, asset_error_text(err));
		return 0;
	}
	u8 *stage = stage_buffer();
	u32 start = cycles_now();
	// asset_check made sure the BIOS writes at most ASSET_TILE_BYTES_MAX
	LZ77UnCompWram(src + lz_at, stage + STAGE_TILES);
	u32 lz77 = cycles_since(start);
	memcpy(stage + STAGE_PALETTE, src + ASSET_HEADER_BYTES, asset_palette_bytes(&h));

	start = cycles_now();
	u8 *slot = cover_psram_slot(entry);
	psram_begin();
	int ok = psram_write(slot + COVER_SLOT_TILES, stage + STAGE_TILES, asset_tile_bytes(&h)) &&
		 (!h.colours ||
		  psram_write(slot + COVER_SLOT_PALETTE, stage + STAGE_PALETTE, asset_palette_bytes(&h))) &&
		 psram_write(slot + COVER_SLOT_HEADER, &h, sizeof(h));
	u32 write = cycles_since(start);
	if (lz77 > cover_stats.lz77_max)
		cover_stats.lz77_max = lz77;
	if (write > cover_stats.write_max)
		cover_stats.write_max = write;
	if (lz77 + write > cover_stats.decode_max)
		cover_stats.decode_max = lz77 + write;
	return ok;
}

static void finish(int ok)
{
	if (ok)
		cover_stats.decoded++;
	cc_load_finish(&cache, loader.entry, ok);
	loader.entry = COVER_NONE;
}

// Cleans up after the loader: its file, its entry. Safe to call twice.
static void loader_stop(void)
{
	if (loader.rd.open)
		sd_read_cancel(&loader.rd);
	if (loader.entry != COVER_NONE) {
		cc_load_abort(&cache, loader.entry);
		loader.entry = COVER_NONE;
	}
	loader.job = JOB_NONE;
}

static int loader_step(struct job *job)
{
	if (job->state == JOB_CANCEL) {
		loader_stop();
		return JOB_DONE;
	}
	if (job->state == LOAD_PICK) {
		u8 e = cc_next_load(&cache);
		if (e == COVER_NONE) {
			loader.job = JOB_NONE;
			return JOB_DONE;
		}
		cc_load_start(&cache, e);
		loader.entry = e;
		u32 id = cache.entry[e].id;
		struct cover_src src = {COVER_SRC_NONE, 0, 0};
		char *path = (char *)stage_buffer() + STAGE_PATH;
		path[0] = 0;
		if (loader.locate)
			loader.locate(id, &src, path, COVER_PATH_MAX);
		path[COVER_PATH_MAX - 1] = 0;
		if (src.kind == COVER_SRC_ROM && src.data) {
			loader.src = src.data;
			loader.src_bytes = src.bytes;
			job->state = LOAD_DECODE;
			return JOB_MORE;
		}
		if (src.kind != COVER_SRC_FILE || !path[0]) {
			tlog_debug("cover %08lX: none", (unsigned long)id);
			finish(0);
			return JOB_MORE;
		}
		// Opening is this step's SD work; reads follow, a chunk a step
		sd_read_init(&loader.rd, path, psram_stage(), COVER_STAGE_BYTES, COVER_READ_CHUNK);
		sd_read_step(&loader.rd);
	} else if (job->state == LOAD_READ) {
		sd_read_step(&loader.rd);
	} else {
		finish(decode(loader.entry, loader.src, loader.src_bytes));
		job->state = LOAD_PICK;
		return JOB_MORE;
	}
	// After an SD step
	if (loader.rd.result == SD_READ_BUSY) {
		job->state = LOAD_READ;
	} else if (loader.rd.result == SD_READ_OK) {
		loader.src = psram_stage();
		loader.src_bytes = loader.rd.size;
		job->state = LOAD_DECODE;
	} else {
		finish(0);
		job->state = LOAD_PICK;
	}
	return JOB_MORE;
}

// Starts the loader if covers are wanted and it isn't running.
static void loader_run(void)
{
	if (loader.job != JOB_NONE && job_running(loader.job))
		return;
	// A loader that ended without its cancel step (jobs_init) left its
	// file open and its entry loading
	loader_stop();
	if (cc_next_load(&cache) != COVER_NONE)
		loader.job = job_start(loader_step, 0, loader.owner);
}

// --- Sprite slots ---------------------------------------------------------

// OAM shape and size for a sprite of w x h tiles, as shape << 2 | size, or
// -1 if no single sprite has that size.
static int sprite_shape(u32 w, u32 h)
{
	static const u8 sizes[3][4][2] = {
		{{1, 1}, {2, 2}, {4, 4}, {8, 8}},  // square
		{{2, 1}, {4, 1}, {4, 2}, {8, 4}},  // wide
		{{1, 2}, {1, 4}, {2, 4}, {4, 8}},  // tall
	};
	for (int shape = 0; shape < 3; shape++)
		for (int size = 0; size < 4; size++)
			if (sizes[shape][size][0] == w && sizes[shape][size][1] == h)
				return shape << 2 | size;
	return -1;
}

static void slot_free_vram(struct slot *s)
{
	if (s->tiles != RES_NONE)
		res_unref(RES_OBJ_VRAM, s->tiles);
	if (s->pal != RES_NONE)
		res_unref(RES_OBJ_PAL, s->pal);
	s->tiles = s->pal = RES_NONE;
}

static void slot_empty(struct slot *s)
{
	slot_free_vram(s);
	if (s->entry != COVER_NONE)
		cc_unpin(&cache, s->entry);
	s->entry = COVER_NONE;
	s->state = COVER_SLOT_EMPTY;
	s->id = 0;
}

static void slot_reset(struct slot *s)
{
	s->id = s->ticket = 0;
	s->state = COVER_SLOT_EMPTY;
	s->entry = COVER_NONE;
	s->tiles = s->pal = RES_NONE;
	s->shape = s->size = s->bpp8 = s->width = s->height = 0;
}

// Takes and pins the slot's cache entry; it stays COVER_NONE (tried again
// next update) while the cache has no room.
static void slot_attach(struct slot *s)
{
	u8 e = cc_want(&cache, s->id, 0);
	if (e != COVER_NONE && cc_pin(&cache, e))
		s->entry = e;
}

static void slot_settle(u8 n, int ok)
{
	struct slot *s = &slots[n];
	s->state = ok ? COVER_SLOT_READY : COVER_SLOT_FAILED;
	if (!ok)
		slot_free_vram(s);
	frame_wake();
	if (loader.ready_cb)
		loader.ready_cb(n, s->id, ok);
}

// Allocates sprite tiles (and a palette bank) for a decoded cover and queues
// its upload. 1 if queued, 0 if it waits for room in the queue (or failed:
// the slot is then FAILED).
static int slot_upload(u8 n)
{
	struct slot *s = &slots[n];
	const u8 *psram = cover_psram_slot(s->entry);
	struct asset_header h;
	memcpy(&h, psram + COVER_SLOT_HEADER, sizeof(h));
	int shape = sprite_shape(h.width, h.height);
	if (shape < 0) {
		res_set_error("cover %08lX: %ux%u tiles is no sprite's size", (unsigned long)s->id, h.width, h.height);
		slot_settle(n, 0);
		return 0;
	}
	if (gfx_queue_len() == GFX_QUEUE_CAP)
		return 0;
	int bpp8 = h.bpp == 8;
	u16 units = (u16)(h.width * h.height * (bpp8 ? 2 : 1));
	s->tiles = res_alloc(RES_OBJ_VRAM, units, bpp8 ? 2 : 1, 0, RES_OBJ_UNITS, 0, RES_NOLOG);
	if (s->tiles == RES_NONE) {
		slot_settle(n, 0);
		return 0;
	}
	if (!bpp8) {
		// Tagged by entry, so slots showing one cover share its bank. A
		// revived bank may hold an older cover's colours: load them anyway
		s->pal = res_alloc(RES_OBJ_PAL, 1, 1, 0, RES_PAL_BANKS, (u16)(COVER_PAL_TAG | s->entry), RES_NOLOG);
		if (s->pal == RES_NONE) {
			slot_settle(n, 0);
			return 0;
		}
		u16 colours[16] = {0};
		memcpy(colours, psram + COVER_SLOT_PALETTE, (u32)h.colours * 2);
		gfx_pal_load(16 + res_start(RES_OBJ_PAL, s->pal), colours, 16);
	}
	gfx_queue_copy(res_obj_addr(s->tiles), psram + COVER_SLOT_TILES, asset_tile_bytes(&h));
	s->ticket = gfx_queue_ticket();
	s->shape = (u8)(shape >> 2);
	s->size = (u8)(shape & 3);
	s->bpp8 = (u8)bpp8;
	s->width = h.width;
	s->height = h.height;
	s->state = COVER_SLOT_UPLOADING;
	return 1;
}

// --- The API ----------------------------------------------------------------

void cover_init(cover_locate_fn locate, u8 owner)
{
	// engine_init has reset the jobs and pools: nothing to release, but a
	// file the old loader had open
	if (loader.ready && loader.rd.open)
		f_close(&loader.rd.fil);
	memset(&loader, 0, sizeof(loader));
	loader.locate = locate;
	loader.owner = owner;
	loader.entry = COVER_NONE;
	loader.job = JOB_NONE;
	loader.ready = 1;
	cc_init(&cache);
	for (u32 i = 0; i < COVER_SLOTS; i++)
		slot_reset(&slots[i]);
	memset(&cover_stats, 0, sizeof(cover_stats));
}

void cover_on_ready(cover_ready_fn ready)
{
	loader.ready_cb = ready;
}

void cover_shutdown(void)
{
	if (!loader.ready)
		return;
	if (loader.job != JOB_NONE && job_running(loader.job))
		job_cancel(loader.job);
	loader_stop();
	for (u32 i = 0; i < COVER_SLOTS; i++)
		slot_empty(&slots[i]);
}

void cover_cache_invalidate(void)
{
	if (!loader.ready)
		return;
	cover_shutdown();
	cc_invalidate(&cache);
}

void cover_wants_begin(void)
{
	if (!loader.ready)
		return;
	cc_unwant_all(&cache);
	for (u32 i = 0; i < COVER_SLOTS; i++)
		if (slots[i].entry != COVER_NONE)
			cache.entry[slots[i].entry].dist = 0;
}

void cover_want(u32 id, u16 dist)
{
	if (loader.ready)
		cc_want(&cache, id, dist);
}

void cover_wants_end(void)
{
	if (loader.ready)
		loader_run();
}

int cover_slot_show(u8 n, u32 id)
{
	if (n >= COVER_SLOTS || !loader.ready)
		return 0;
	struct slot *s = &slots[n];
	if (s->state != COVER_SLOT_EMPTY && s->id == id)
		return 1;
	slot_empty(s);
	s->id = id;
	s->state = COVER_SLOT_LOADING;
	slot_attach(s);
	loader_run();
	return 1;
}

void cover_slot_clear(u8 n)
{
	if (n < COVER_SLOTS && loader.ready)
		slot_empty(&slots[n]);
}

u8 cover_slot_state(u8 n)
{
	return n < COVER_SLOTS ? slots[n].state : COVER_SLOT_EMPTY;
}

u32 cover_slot_id(u8 n)
{
	return n < COVER_SLOTS ? slots[n].id : 0;
}

static const struct slot *ready_slot(u8 n)
{
	return n < COVER_SLOTS && slots[n].state == COVER_SLOT_READY ? &slots[n] : 0;
}

u16 cover_slot_attr0(u8 n)
{
	const struct slot *s = ready_slot(n);
	return s ? (u16)(s->shape << 14 | s->bpp8 << 13) : 0;
}

u16 cover_slot_attr1(u8 n)
{
	const struct slot *s = ready_slot(n);
	return s ? (u16)(s->size << 14) : 0;
}

u16 cover_slot_attr2(u8 n)
{
	const struct slot *s = ready_slot(n);
	if (!s)
		return 0;
	u16 bank = s->pal != RES_NONE ? res_start(RES_OBJ_PAL, s->pal) : 0;
	return (u16)(bank << 12 | res_start(RES_OBJ_VRAM, s->tiles));
}

u32 cover_slot_width(u8 n)
{
	const struct slot *s = ready_slot(n);
	return s ? s->width * 8u : 0;
}

u32 cover_slot_height(u8 n)
{
	const struct slot *s = ready_slot(n);
	return s ? s->height * 8u : 0;
}

int cover_load_palette8(u8 n)
{
	const struct slot *s = ready_slot(n);
	if (!s)
		return 0;
	const u8 *psram = cover_psram_slot(s->entry);
	struct asset_header h;
	memcpy(&h, psram + COVER_SLOT_HEADER, sizeof(h));
	gfx_pal_load(16, (const u16 *)(psram + COVER_SLOT_PALETTE), h.colours);
	return 1;
}

void cover_update(void)
{
	if (!loader.ready)
		return;
	u32 uploads = 0;
	for (u8 n = 0; n < COVER_SLOTS; n++) {
		struct slot *s = &slots[n];
		if (s->state == COVER_SLOT_LOADING) {
			if (s->entry == COVER_NONE)
				slot_attach(s);
			if (s->entry == COVER_NONE)
				continue;
			u8 state = cache.entry[s->entry].state;
			if (state == COVER_FAILED)
				slot_settle(n, 0);
			else if (state == COVER_READY && uploads < COVER_UPLOADS_PER_FRAME && slot_upload(n))
				uploads++;
		} else if (s->state == COVER_SLOT_UPLOADING && gfx_queue_done(s->ticket)) {
			slot_settle(n, 1);
		}
	}
	loader_run();
}
