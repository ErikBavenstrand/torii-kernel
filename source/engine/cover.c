// See cover.h.
#include <string.h>

#include "cover.h"
#include "cycles.h"
#include "frame.h"
#include "jobs.h"
#include "psram.h"
#include "res.h"
#include "sdread.h"
#include "stage.h"
#include "../tlog.h"

_Static_assert(COVER_FILE_BYTES <= COVER_SLOT_BYTES, "a cover's file fits its slot");
_Static_assert(COVER_SLOT_BYTES % 32 == 0, "slots keep the tiles word-aligned");
// psram.h's map: the cache from 4 MB, below the Favourites page's order at 4.5 MB
_Static_assert(PSRAM_ASSET_BASE + COVER_PSRAM_BYTES <= PSRAM_RESERVED_BASE, "the cover cache ends below 4.5 MB");

// The path the locate callback writes, in the stage clear of the SD
// reader's chunk (read by f_open in the same step)
#define STAGE_PATH (STAGE_BYTES - COVER_PATH_MAX)
// The SD reader's chunk: a whole cover in one read
#define COVER_READ_CHUNK 4096
_Static_assert(COVER_SLOT_BYTES <= COVER_READ_CHUNK && COVER_READ_CHUNK <= STAGE_PATH,
	       "a cover is one chunk, and it and the path share the stage");

enum {
	LOAD_PICK = 0,  // take the nearest wanted entry, find its file, open it
	LOAD_READ,  // read it into the entry's PSRAM slot, then check it
};

static struct cover_cache cache EWRAM_BSS;
static struct {
	struct sd_read rd;  // the file being read
	cover_locate_fn locate;
	cover_pause_fn pause;
	u8 entry;  // the entry loading, or COVER_NONE
	u8 job;  // JOB_NONE when not running
	u8 ready;  // cover_init has run
} loader EWRAM_BSS;
struct cover_stats cover_stats EWRAM_BSS;

static u8 *slot_of(u8 entry)
{
	return (u8 *)(PSRAM_ASSET_BASE + (u32)entry * COVER_SLOT_BYTES);
}

const struct cover_cache *cover_cache(void)
{
	return &cache;
}

// --- The loader job ---------------------------------------------------------

// 1 if the size bytes read into entry's slot are a cover.
static int check(u8 entry, u32 size)
{
	struct asset_header h;
	memcpy(&h, slot_of(entry), sizeof(h));
	int err = asset_check(&h, size);
	if (err == ASSET_OK && (h.bpp != 4 || h.width != COVER_TILES_W || h.height != COVER_TILES_H))
		err = ASSET_ERR_FORMAT;
	if (err != ASSET_OK) {
		res_set_error("cover %08lX: %s (box art is %ux%u 4bpp tiles)", (unsigned long)cache.entry[entry].id,
			      asset_error_text(err), COVER_TILES_W, COVER_TILES_H);
		return 0;
	}
	return 1;
}

static void finish(int ok)
{
	if (ok) {
		cover_stats.loaded++;
		tlog_debug("cover %08lX loaded", (unsigned long)cache.entry[loader.entry].id);
	} else {
		cover_stats.failed++;
	}
	cc_load_finish(&cache, loader.entry, ok);
	loader.entry = COVER_NONE;
	frame_wake();
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
	u32 start = cycles_now();
	if (job->state == LOAD_PICK) {
		u8 e = cc_next_load(&cache);
		if (e == COVER_NONE || (loader.pause && loader.pause())) {
			loader.job = JOB_NONE;
			return JOB_DONE;
		}
		frame_measure("cover-load");  // the frames it reads the card in, for the cycle budget
		cc_load_start(&cache, e);
		loader.entry = e;
		char *path = (char *)stage_buffer() + STAGE_PATH;
		path[0] = 0;
		int found = loader.locate && loader.locate(cache.entry[e].id, path, COVER_PATH_MAX);
		path[COVER_PATH_MAX - 1] = 0;
		if (!found || !path[0]) {
			tlog_debug("cover %08lX: none", (unsigned long)cache.entry[e].id);
			finish(0);
			return JOB_MORE;
		}
		// Opening is this step's SD work; the read is the next one's
		psram_begin();
		sd_read_init(&loader.rd, path, slot_of(e), COVER_SLOT_BYTES, COVER_READ_CHUNK);
		loader.rd.finish = 0;
		sd_read_step(&loader.rd);
	} else {
		frame_measure("cover-load");
		sd_read_step(&loader.rd);
	}
	if (loader.rd.result == SD_READ_BUSY) {
		job->state = LOAD_READ;
	} else {
		finish(loader.rd.result == SD_READ_OK && check(loader.entry, loader.rd.size));
		job->state = LOAD_PICK;
	}
	u32 took = cycles_since(start);
	if (took > cover_stats.step_max)
		cover_stats.step_max = took;
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
	if (cc_next_load(&cache) != COVER_NONE && !(loader.pause && loader.pause()))
		loader.job = job_start(loader_step, 0, JOB_OWNER_NONE);
}

// --- The API ----------------------------------------------------------------

void cover_reset(void)
{
	// engine_init has reset the jobs: nothing to release, but a file the
	// old loader had open
	if (loader.ready && loader.rd.open)
		f_close(&loader.rd.fil);
	memset(&loader, 0, sizeof(loader));
	loader.entry = COVER_NONE;
	loader.job = JOB_NONE;
	cc_init(&cache);
	memset(&cover_stats, 0, sizeof(cover_stats));
}

void cover_init(cover_locate_fn locate, cover_pause_fn pause)
{
	cover_reset();
	TLOG_ASSERT(!((u32)stage_buffer() & 3));  // pReadCache, as the linker places it
	loader.locate = locate;
	loader.pause = pause;
	loader.ready = 1;
}

int cover_active(void)
{
	return loader.ready;
}

void cover_cache_invalidate(void)
{
	if (!loader.ready)
		return;
	if (loader.job != JOB_NONE && job_running(loader.job))
		job_cancel(loader.job);
	loader_stop();
	cover_reset();
}

int cover_loading(void)
{
	return loader.ready && loader.job != JOB_NONE && job_running(loader.job) && loader.entry != COVER_NONE;
}

void cover_wants_begin(void)
{
	if (loader.ready)
		cc_unwant_all(&cache);
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

u8 cover_state(u32 id)
{
	u8 e = loader.ready ? cc_find(&cache, id) : COVER_NONE;
	return e == COVER_NONE ? COVER_FREE : cache.entry[e].state;
}

u8 cover_pin(u32 id)
{
	u8 e = loader.ready ? cc_find(&cache, id) : COVER_NONE;
	if (e == COVER_NONE || cache.entry[e].state != COVER_READY || !cc_pin(&cache, e))
		return COVER_NONE;
	return e;
}

void cover_unpin(u8 entry)
{
	cc_unpin(&cache, entry);
}

const u32 *cover_tiles(u8 entry)
{
	return (const u32 *)(slot_of(entry) + ASSET_HEADER_BYTES + asset_palette_bytes((const void *)slot_of(entry)));
}

const u16 *cover_palette(u8 entry)
{
	return (const u16 *)(slot_of(entry) + ASSET_HEADER_BYTES);
}

u32 cover_colours(u8 entry)
{
	return ((const struct asset_header *)slot_of(entry))->colours;
}
