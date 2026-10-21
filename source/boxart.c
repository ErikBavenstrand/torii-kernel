// See boxart.h.
#include <stdio.h>
#include <string.h>

#include "boxart.h"
#include "artkey.h"
#include "ez_define.h"
#include "Ezcard_OP.h"
#include "ff.h"
#include "library.h"
#include "namesdb.h"
#include "tlog.h"
#include "userdata.h"
#include "engine/cover.h"
#include "engine/cycles.h"
#include "engine/frame.h"
#include "engine/gfx.h"
#include "engine/jobs.h"
#include "engine/psram.h"
#include "engine/res.h"
#include "engine/stage.h"
#include "ui/art.h"
#include "ui/ui.h"

#define ART_DIR "/torii/art"
#define CUSTOM_DIR ART_DIR "/custom"
// Frames the cursor rests before its cover loads, and the rows each side of
// it loaded after it
#define BOXART_REST 8
#define BOXART_AHEAD 1
#define BOXART_WANTS (1 + 2 * BOXART_AHEAD)
// Polls between checks of the resting cursor's cover: names (CRCs) that
// load or are identified later can give it one
#define BOXART_RECHECK 30
// The listing job: cycles of work a step, and the frames the keys rest
// before it steps (it reads the card, as the library walk does)
#define LIST_STEP_CYCLES 40000
#define LIST_KEY_REST 8
// Frames to wait before trying again when the listing's job can't start (all
// job slots taken); the failure logs once
#define LIST_RETRY_FRAMES 60
#define CLEAR_CHUNK 0x1000  // bytes of the set cleared at a time
#define USERDATA_PATH_MAX_ALIGNED ((USERDATA_PATH_MAX + 3) & ~3u)

// PSRAM: the set, then each want's file name (custom art)
#define SET_BASE ((u32 *)PSRAM_ART_BASE)
#define NAME_BYTES 256
#define NAMES_BASE (PSRAM_ART_BASE + ARTSET_SLOTS * 4u)
_Static_assert(ARTSET_SLOTS * 4u + BOXART_WANTS * NAME_BYTES <= PSRAM_ART_BYTES, "box art's lookups fit their PSRAM");
_Static_assert(USERDATA_NAME_MAX < NAME_BYTES, "a want's file name fits its slot");
_Static_assert(sizeof(CUSTOM_DIR "/" ARTKEY_EXT) + USERDATA_NAME_MAX <= COVER_PATH_MAX,
	       "any game's custom art path fits");
_Static_assert(PSRAM_ART_BASE + PSRAM_ART_BYTES <= PSRAM_WINDOW + PSRAM_WINDOW_BYTES,
	       "the art region ends inside the window");
_Static_assert(USERDATA_PATH_MAX_ALIGNED + NAME_BYTES <= READCACHE_BOXART_SIZE,
	       "a path and a name fit box art's scratch");
_Static_assert(sizeof(FILINFO) <= READCACHE_STAGE_SIZE - CLEAR_CHUNK, "the listing's entry and zeros share the stage");

enum {
	LIST_NONE,  // not listed: the job starts at the next poll
	LIST_OPEN_ART,
	LIST_CLEAR,  // the folder is there: the set is emptied
	LIST_ART,
	LIST_OPEN_CUSTOM,
	LIST_CUSTOM,
	LIST_DONE,  // listed (perhaps nothing: no /torii/art)
};

struct want {
	u32 id;
	u32 key;
	u8 kind;
};

static struct {
	u8 started_up;  // boxart_open has run since the boot
	u8 image;  // the page's box
	u8 on;  // the Box art setting, read as the page opened
	u8 list;  // LIST_*
	u8 job;  // the listing's, JOB_NONE
	u8 dir_open;
	u8 full_logged;
	boxart_row_fn row_fn;
	u8 box;  // the placeholder box for the cursor's row (ART_BOX_*)
	u8 box_shown;  // the one whose tiles the image holds, ART_BOX_COUNT: none
	u32 clear_at;
	u32 started;  // torii_frames when the listing started
	u32 retry_at;  // torii_frames from which a job that couldn't start is tried again, 0 none
	u8 start_failed;  // that failure is logged
	DIR dir;
	struct artset set;
	u32 row;  // the cursor's
	u32 rest;  // polls since the cursor moved
	u32 recheck;  // polls to the next check of the cursor's cover
	u32 want_id;  // the cover the box should show, 0 the placeholder
	u32 shown;  // the one it shows (its copy queued), 0 the placeholder
	u8 placeholder_due;  // the placeholder's tiles and colours are to be (re)loaded
	u8 colours_due;  // a picture's colours wait for its tiles (colours_ticket)
	const u16 *colours;  // those colours, colours_count of them
	u8 colours_count;
	u32 colours_ticket;
	u8 pin, old_pin;  // the shown cover's entry, and the one before until its copy lands
	u32 old_ticket;
	u32 signature;  // what the last wants were made from (0: none made)
	u32 wanted;
	struct want wants[BOXART_WANTS];
} B EWRAM_BSS;

// pReadCache, within a call (ez_define.h): a row's path, then a file name
// on its way to PSRAM (word-aligned for psram_write)
static char *scratch_path(void)
{
	return (char *)pReadCache + READCACHE_BOXART;
}

static char *scratch_name(void)
{
	return (char *)pReadCache + READCACHE_BOXART + USERDATA_PATH_MAX_ALIGNED;
}

static void set_put(u32 *slot, u32 id)
{
	psram_write(slot, &id, sizeof(id));
}

// --- The listing ----------------------------------------------------------------

static void close_dir(void)
{
	if (B.dir_open) {
		f_closedir(&B.dir);
		B.dir_open = 0;
	}
}

static void add(u32 kind, u32 key)
{
	if (artset_add(&B.set, artkey_id(kind, key)) || B.full_logged)
		return;
	B.full_logged = 1;
	tlog_error("box art: more than %u files in %s; the rest are left out", ARTSET_MAX, ART_DIR);
}

// One entry of the folder open: 0 once it has none left
static int list_entry(int custom)
{
	FILINFO *fi = (FILINFO *)(stage_buffer() + CLEAR_CHUNK);
	if (f_readdir(&B.dir, fi) != FR_OK || !fi->fname[0]) {
		close_dir();
		return 0;
	}
	if (fi->fattrib & AM_DIR)
		return 1;
	u32 key;
	if (custom) {
		u32 len = artkey_custom_len(fi->fname);
		if (len)
			add(ARTKEY_CUSTOM, artkey_name(fi->fname, len));
	} else {
		u32 kind = artkey_parse(fi->fname, &key);
		if (kind != ARTKEY_NONE)
			add(kind, key);
	}
	return 1;
}

static void list_step_one(void)
{
	switch (B.list) {
	case LIST_CLEAR: {
		u8 *zeros = stage_buffer();
		memset(zeros, 0, CLEAR_CHUNK);
		psram_write((u8 *)SET_BASE + B.clear_at, zeros, CLEAR_CHUNK);
		B.clear_at += CLEAR_CHUNK;
		if (B.clear_at >= ARTSET_SLOTS * 4u)
			B.list = LIST_ART;
		break;
	}
	case LIST_OPEN_ART:
	case LIST_OPEN_CUSTOM: {
		int custom = B.list == LIST_OPEN_CUSTOM;
		if (f_opendir(&B.dir, custom ? CUSTOM_DIR : ART_DIR) == FR_OK) {
			B.dir_open = 1;
			B.list = custom ? LIST_CUSTOM : LIST_CLEAR;
		} else {
			B.list = LIST_DONE;  // no folder: nothing (more) to list
		}
		break;
	}
	case LIST_ART:
		if (!list_entry(0))
			B.list = LIST_OPEN_CUSTOM;
		break;
	case LIST_CUSTOM:
		if (!list_entry(1))
			B.list = LIST_DONE;
		break;
	default:
		break;
	}
}

// 1 while a view opens or a modal one (a menu, a dialog) is up: another
// operation may be under way, so box art reads nothing (as the library's
// and the names' jobs)
static int ui_busy(void)
{
	const struct ui_view *top = ui_top();
	return top && (top->opening || (top->spec->flags & UI_VIEW_MODAL));
}

static int list_job(struct job *job)
{
	// A launch (or Diagnostics) took PSRAM over (cover_cache_invalidate):
	// not a word more into it; boxart_poll lists again
	if (job->state == JOB_CANCEL || !cover_active()) {
		close_dir();
		B.list = LIST_NONE;  // listed again from the start
		B.job = JOB_NONE;
		return JOB_DONE;
	}
	// The user's frames first, and the library's walk
	if (library_walking() || ui_idle_frames() < LIST_KEY_REST || ui_transitioning() || ui_busy())
		return JOB_WAIT;
	frame_measure("games-artlist");  // the frames it reads the card in, for the cycle budget
	psram_begin();
	u32 start = cycles_now();
	do
		list_step_one();
	while (B.list != LIST_DONE && cycles_since(start) < LIST_STEP_CYCLES);
	if (B.list != LIST_DONE)
		return JOB_MORE;
	tlog("box art: %lu files listed in %lu frames", (unsigned long)B.set.count,
	     (unsigned long)(torii_frames - B.started));
	B.job = JOB_NONE;
	return JOB_DONE;
}

// Starts (or restarts, after an engine_init dropped it) the listing
static void list_poll(void)
{
	if (B.list == LIST_DONE || (B.job != JOB_NONE && job_running(B.job)))
		return;
	// From the start, also after an engine_init dropped the job half way
	// (its folder stays open: FatFs keeps no lock, FF_FS_LOCK 0)
	B.dir_open = 0;
	B.list = LIST_OPEN_ART;
	B.clear_at = 0;
	B.full_logged = 0;
	B.set.slots = SET_BASE;
	B.set.put = set_put;
	B.set.count = 0;
	if (B.retry_at && (s32)(torii_frames - B.retry_at) < 0)
		return;
	B.started = torii_frames;
	// No slot (every job taken): tried again a second later, logged once
	u8 quiet = res_errors_quiet;
	res_errors_quiet = 1;
	B.job = job_start(list_job, 0, JOB_OWNER_NONE);
	res_errors_quiet = quiet;
	if (B.job == JOB_NONE) {
		if (!B.start_failed)
			tlog_error("box art: the listing can't start: %s", res_error());
		B.start_failed = 1;
		B.retry_at = torii_frames + LIST_RETRY_FRAMES;
		B.retry_at += !B.retry_at;
		return;
	}
	B.retry_at = 0;
	B.start_failed = 0;
}

// --- Lookups ---------------------------------------------------------------------

// The cover of the game at path (size bytes, 0 unknown) into *w; its id, or
// 0 for none
static u32 lookup(const char *path, u32 size, struct want *w)
{
	if (B.list != LIST_DONE || !B.set.count)
		return 0;
	const char *name = strrchr(path, '/');
	name = name ? name + 1 : path;
	w->kind = ARTKEY_CUSTOM;
	w->key = artkey_name(name, strlen(name));
	w->id = artkey_id(ARTKEY_CUSTOM, w->key);
	if (artset_has(SET_BASE, w->id))
		return w->id;
	// Matched art by CRC only: a romhack shares its base game's code
	u32 crc;
	if (!size || !names_crc(path, size, &crc))
		return 0;
	w->kind = ARTKEY_CRC;
	w->key = crc;
	w->id = artkey_id(ARTKEY_CRC, crc);
	return artset_has(SET_BASE, w->id) ? w->id : 0;
}

// row's cover into *w (and, for custom art, its file name into the want's
// PSRAM slot n); its id or 0
static u32 row_cover(u32 row, struct want *w, u32 n)
{
	char *path = scratch_path();
	u32 size = 0;
	if (!B.row_fn || !B.row_fn(row, path, &size))
		return 0;
	path[USERDATA_PATH_MAX - 1] = 0;
	u32 id = lookup(path, size, w);
	if (id && w->kind == ARTKEY_CUSTOM && n < BOXART_WANTS) {
		const char *name = strrchr(path, '/');
		name = name ? name + 1 : path;
		char *aligned = scratch_name();
		u32 len = strlen(name);
		memcpy(aligned, name, len + 1);
		psram_begin();
		psram_write((void *)(NAMES_BASE + n * NAME_BYTES), aligned, (len + 2) & ~1u);
	}
	return id;
}

// The loader's locate: the path of a wanted cover
static int locate(u32 id, char *path, u32 size)
{
	for (u32 i = 0; i < B.wanted; i++) {
		const struct want *w = &B.wants[i];
		if (w->id != id)
			continue;
		if (w->kind == ARTKEY_CRC)
			snprintf(path, size, ART_DIR "/%08lX" ARTKEY_EXT, (unsigned long)w->key);
		else
			snprintf(path, size, CUSTOM_DIR "/%s" ARTKEY_EXT, (const char *)(NAMES_BASE + i * NAME_BYTES));
		return 1;
	}
	return 0;
}

// --- The box -------------------------------------------------------------------------

static void unpin_old(int force)
{
	if (B.old_pin != COVER_NONE && (force || gfx_queue_done(B.old_ticket))) {
		cover_unpin(B.old_pin);
		B.old_pin = COVER_NONE;
	}
}

// The shown cover's pin waits for its copy before it goes
static void retire_pin(void)
{
	if (B.pin == COVER_NONE)
		return;
	unpin_old(1);  // two changes in a frame: the older copy has long been queued
	B.old_pin = B.pin;
	B.old_ticket = gfx_queue_ticket();
	B.pin = COVER_NONE;
}

// Shows a picture (tiles and count colours) in the box; 0 while the VRAM
// queue is full. Colours load before the queue runs in a VBlank, so with
// the queue empty both land in that one VBlank. Behind other copies the
// tiles may take frames: the box is blanked to the card until they are in,
// then gets its colours, so a picture never shows in another's colours.
static int show(const void *tiles, const u16 *colours, u32 count)
{
	int busy = gfx_queue_len() != 0;
	if (!ui_image_show(B.image, tiles, busy ? 0 : colours, count))
		return 0;
	B.colours_due = 0;
	if (busy) {
		u16 blank[16];  // gfx_pal_load copies it at once
		u16 card = ui_theme()->surface->card;
		for (u32 i = 0; i < 16; i++)
			blank[i] = card;
		gfx_pal_load(ui_image_bank(B.image), blank, 16);
		B.colours = colours;
		B.colours_count = (u8)count;
		B.colours_ticket = gfx_queue_ticket();
		B.colours_due = 1;
	}
	return 1;
}

// The colours show() put off, once their tiles are in
static void colours_poll(void)
{
	if (B.colours_due && gfx_queue_done(B.colours_ticket)) {
		gfx_pal_load(ui_image_bank(B.image), B.colours, B.colours_count);
		B.colours_due = 0;
	}
}

// Makes the box show want_id (or the placeholder); tried again next poll
// while the VRAM queue is full
static void sync(void)
{
	colours_poll();
	if (B.want_id && B.want_id != B.shown && cover_state(B.want_id) == COVER_READY) {
		u8 e = cover_pin(B.want_id);
		if (e == COVER_NONE)
			return;
		if (!show(cover_tiles(e), cover_palette(e), cover_colours(e))) {
			cover_unpin(e);
			return;
		}
		retire_pin();
		B.pin = e;
		B.shown = B.want_id;
		B.placeholder_due = 0;
		tlog_debug("box art: cover %08lX shown", (unsigned long)B.shown);
		return;
	}
	if (B.want_id && B.want_id == B.shown)
		return;
	if (B.box >= ART_BOX_COUNT)
		return;  // no row selected since the page opened
	// The placeholder: its tiles only if a cover or another box was there
	if (B.shown || B.box_shown != B.box) {
		if (!show(art_box_tiles[B.box], art_box_colours(B.box), 16))
			return;
		retire_pin();
		B.shown = 0;
		B.box_shown = B.box;
		B.placeholder_due = 0;
	} else if (B.placeholder_due && !B.colours_due) {
		gfx_pal_load(ui_image_bank(B.image), art_box_colours(B.box), 16);
		B.placeholder_due = 0;
	}
}

// Wants the cursor's cover and its neighbours'
static void want_covers(void)
{
	struct want w;
	B.wanted = 0;
	B.want_id = row_cover(B.row, &w, 0);
	if (B.want_id)
		B.wants[B.wanted++] = w;
	for (u32 d = 1; d <= BOXART_AHEAD; d++) {
		u32 rows[2] = {B.row + d, B.row - d};  // a row before 0 wraps past every list: none
		for (u32 k = 0; k < 2; k++) {
			u32 id = row_cover(rows[k], &w, B.wanted);
			if (id)
				B.wants[B.wanted++] = w;
		}
	}
	cover_wants_begin();
	for (u32 i = 0; i < B.wanted; i++)
		cover_want(B.wants[i].id, i == 0 && B.want_id ? 0 : 1);
	cover_wants_end();
}

static int paused(void)
{
	return library_walking() || ui_busy();
}

void boxart_open(u8 image, boxart_row_fn row)
{
	if (!B.started_up) {
		B.started_up = 1;
		B.pin = B.old_pin = COVER_NONE;
		B.job = JOB_NONE;
	}
	// The last page's pins: its copies have long landed
	unpin_old(1);
	if (B.pin != COVER_NONE)
		cover_unpin(B.pin);
	B.pin = COVER_NONE;
	B.image = image;
	B.row_fn = row;
	B.on = Read_SET_info(assress_box_art) != 0;
	B.shown = 0;  // the image starts with a placeholder's tiles: which, the
	B.box_shown = ART_BOX_COUNT;  // first boxart_select decides
	B.box = ART_BOX_COUNT;
	B.colours_due = 0;
	B.want_id = 0;
	B.signature = 0;
	B.rest = 0;
	// The last page's wants go with their names (a pick now would find none)
	B.wanted = 0;
	cover_wants_begin();
	if (B.on)
		boxart_poll();
}

void boxart_select(u32 row, u32 box)
{
	if (!B.started_up)
		return;
	if (row != B.row)
		B.rest = 0;
	B.row = row;
	B.box = (u8)(box < ART_BOX_COUNT ? box : ART_BOX_OTHER);
	B.placeholder_due = 1;
	struct want w;
	u32 id = B.on && cover_active() ? row_cover(row, &w, BOXART_WANTS) : 0;
	if (id != B.want_id)
		B.signature = 0;  // another cover on this row (a relist): wanted anew
	B.want_id = id;
	sync();
}

void boxart_poll(void)
{
	if (!B.started_up)
		return;
	if (!B.on) {
		sync();  // a placeholder the full VRAM queue put off
		return;
	}
	if (!cover_active()) {
		// After the boot, or PSRAM overwritten (a launch that came back):
		// the cache and the set are gone
		cover_init(locate, paused);
		B.pin = B.old_pin = COVER_NONE;
		// A cover shown is no longer cached: an id no cover has, so the
		// placeholder replaces it
		if (B.shown)
			B.shown = 0xFFFFFFFFu;
		if (B.job != JOB_NONE && job_running(B.job))
			job_cancel(B.job);
		B.list = LIST_NONE;
		B.signature = 0;
	}
	list_poll();
	unpin_old(0);
	if (B.rest < BOXART_REST) {
		B.rest++;
		sync();  // a show the full VRAM queue put off
		return;
	}
	if (B.list != LIST_DONE) {
		sync();
		return;
	}
	if (B.recheck) {
		B.recheck--;
	} else {
		B.recheck = BOXART_RECHECK;
		struct want w;
		if (row_cover(B.row, &w, BOXART_WANTS) != B.want_id)
			B.signature = 0;  // names arrived: the row has a cover now (or another)
	}
	u32 signature = (B.row + 1) * 0x9E3779B1u ^ library_generation() * 0x85EBCA6Bu ^ (u32)library_walking();
	if (signature != B.signature) {
		B.signature = signature;
		want_covers();
	} else {
		cover_wants_end();  // the loader, if the walk paused it
	}
	sync();
}

void boxart_forget(void)
{
	if (B.job != JOB_NONE && job_running(B.job))
		job_cancel(B.job);
	B.list = LIST_NONE;
	B.signature = 0;
}
