// A game's cheat file on the card, and its cheats at a launch (cheatstore.h).
#include "cheatstore.h"

#include <stdio.h>
#include <string.h>

#include "cheat.h"
#include "crc32.h"
#include "cutpoint.h"
#include "ez_define.h"
#include "Ezcard_OP.h"
#include "GBApatch.h"
#include "ff.h"
#include "ingame.h"
#include "ingame_abi.h"
#include "namesdb.h"
#include "tlog.h"
#include "engine/cycles.h"
#include "engine/psram.h"

extern u8 pReadCache[];

_Static_assert(sizeof(struct cheatstore) <= READCACHE_CHEATS_SIZE, "the cheat file's share of pReadCache");
_Static_assert(READCACHE_CHEATS + READCACHE_CHEATS_SIZE <= MAX_pReadCache_size, "READCACHE_CHEATS");
_Static_assert(CHEATFILE_BYTES_MAX + 4 == CHEAT_TABLE_MAX, "a cheat file's budget is the table's");
_Static_assert(CHEATFILE_TEXT_MAX <= PSRAM_CHEATS_BYTES, "the text fits its PSRAM");
// cheatstore_put may write the byte past the room's last (as it was)
_Static_assert(PSRAM_CHEATS_BASE + PSRAM_CHEATS_BYTES + 2 <= PSRAM_NAMES_DB_BASE, "the cheats' PSRAM ends below "
										  "the names database");
_Static_assert((PSRAM_CHEATS_BASE & 3) == 0, "the cheats' PSRAM is word-aligned");

u32 gl_cheat_table[CHEAT_TABLE_MAX / 4] EWRAM_BSS;
u32 gl_cheat_bytes;
u32 gl_cheat_count;
u32 gl_cheat_patches;

struct cheatstore *cheatstore(void)
{
	return (struct cheatstore *)(void *)(pReadCache + READCACHE_CHEATS);
}

// --- The text in PSRAM ---------------------------------------------------------------
//
// PSRAM takes halfword DMA from RAM (psram_write), no byte stores: a write at
// an odd place or of an odd length takes the byte beside it along, as it is.

#define CHEATS_TEXT ((char *)PSRAM_CHEATS_BASE)

// n bytes from src (RAM or PSRAM, read before anything is written) to dst
// in PSRAM, a part at a time through the stage
void cheatstore_put(char *dst, const char *src, u32 n)
{
	struct cheatstore *cs = cheatstore();
	u8 *t = (u8 *)cs->stage;
	while (n) {
		u32 lead = (u32)dst & 1, c = n < CHEATSTORE_PART ? n : CHEATSTORE_PART;
		char *at = dst - lead;
		if (lead)
			t[0] = (u8)at[0];
		memcpy(t + lead, src, c);
		u32 total = lead + c;
		if (total & 1)
			t[total++] = (u8)dst[c];
		if (!psram_write(at, t, total)) {
			// The text is no longer what was read or edited: never written back
			if (!cs->room_failed)
				tlog_error("cheats: a PSRAM write at %08lX failed", (unsigned long)(u32)at);
			cs->room_failed = 1;
		}
		dst += c;
		src += c;
		n -= c;
	}
}

// memmove within PSRAM: parts from the end when the bytes move up
static void room_move(char *dst, const char *src, u32 n)
{
	if (dst < src) {
		for (u32 done = 0; done < n; done += CHEATSTORE_PART)
			cheatstore_put(dst + done, src + done, n - done < CHEATSTORE_PART ? n - done : CHEATSTORE_PART);
		return;
	}
	while (n) {
		u32 c = n < CHEATSTORE_PART ? n : CHEATSTORE_PART;
		n -= c;
		cheatstore_put(dst + n, src + n, c);
	}
}

static const struct cheatfile_room psram_room = {CHEATS_TEXT, PSRAM_CHEATS_BYTES, cheatstore_put, room_move};

// --- A larger file's windows, in their slots on PSRAM page 1 --------------------------
//
// Page 1 is selected only within slot_put and slot_get, between FatFs calls,
// and page 0 again before they return: nothing reads PSRAM from an interrupt.

#define SLOT(k) ((char *)PSRAM_WINDOW + (u32)(k) * CHEATSTORE_SLOT)
_Static_assert(CHEATSTORE_WINDOWS *CHEATSTORE_SLOT <= PSRAM_WINDOW_BYTES, "the slots fit page 1");
_Static_assert(CHEATINDEX_BYTES + 0x8000 <= CHEATSTORE_SLOT, "a window read leaves room to edit it");
_Static_assert(CHEATSTORE_SLOT <= PSRAM_CHEATS_BYTES, "a slot's text fits the model's PSRAM");
_Static_assert(CHEATSTORE_DETAIL >= CHEATSTORE_PART, "the detail view's buffer takes a part");
_Static_assert(CHEATSTORE_CHUNK % 4 == 0, "a chunk is word-aligned for f_read");

// A window's room: its slot's size, so an edited window fits back in it
static const struct cheatfile_room window_room = {CHEATS_TEXT, CHEATSTORE_SLOT, cheatstore_put, room_move};

static const struct cheatfile_room *the_room(void)
{
	return cheatstore()->windows ? &window_room : &psram_room;
}

// n bytes from src in RAM to slot k at off
static void slot_put(u32 k, u32 off, const char *src, u32 n)
{
	SetPSRampage(CHEATSTORE_SLOT_PAGE);
	cheatstore_put(SLOT(k) + off, src, n);
	SetPSRampage(PSRAM_ASSET_PAGE);
}

// n bytes from slot k at off to dst in RAM
static void slot_get(u32 k, u32 off, char *dst, u32 n)
{
	SetPSRampage(CHEATSTORE_SLOT_PAGE);
	memcpy(dst, SLOT(k) + off, n);
	SetPSRampage(PSRAM_ASSET_PAGE);
}

// n bytes from slot j at from to the start of slot k (another), through the
// detail view's buffer
static void slot_move(u32 j, u32 from, u32 k, u32 n)
{
	char *bounce = cheatstore()->detail;
	for (u32 done = 0; done < n; done += CHEATSTORE_PART) {
		u32 c = n - done < CHEATSTORE_PART ? n - done : CHEATSTORE_PART;
		slot_get(j, from + done, bounce, c);
		slot_put(k, done, bounce, c);
	}
}

// The CRC32 of slot k's first len bytes
static u32 slot_crc(u32 k, u32 len)
{
	char *bounce = cheatstore()->detail;
	u32 crc = 0;
	for (u32 done = 0; done < len; done += CHEATSTORE_PART) {
		u32 c = len - done < CHEATSTORE_PART ? len - done : CHEATSTORE_PART;
		slot_get(k, done, bounce, c);
		crc = crc32_update(crc, bounce, c);
	}
	return crc;
}

static cheatstore_progress_fn progress_fn;

void cheatstore_progress(cheatstore_progress_fn progress)
{
	progress_fn = progress;
}

static void show_progress(u32 done, u32 total)
{
	if (progress_fn && total)
		progress_fn(done, total);
}

u32 cheatstore_windows(void)
{
	return cheatstore()->windows;
}

u32 cheatstore_window(void)
{
	return cheatstore()->window;
}

u32 cheatstore_window_start(u32 k)
{
	struct cheatstore *cs = cheatstore();
	if (!cs->windows)
		return k ? cs->file.count : 0;  // a small file: its one window
	u32 at = 0;
	for (u32 j = 0; j < k && j < cs->windows; j++)
		at += j == cs->window ? cs->file.count : cs->win[j].ix.count;
	return at;
}

u32 cheatstore_picks(void)
{
	struct cheatstore *cs = cheatstore();
	return cs->windows ? cs->n_picks : cheatfile_picks(&cs->file);
}

// The window cheat number n is in (numbers rise from window to window)
static u32 window_of(u32 n)
{
	struct cheatstore *cs = cheatstore();
	u32 k = 0;
	while (k + 1u < cs->windows && n >= cs->win[k + 1].ix.first)
		k++;
	return k;
}

// 1 if a pick is in window k
static int window_has_picks(u32 k)
{
	struct cheatstore *cs = cheatstore();
	for (u32 i = 0; i < cs->n_picks; i++)
		if (window_of(cs->picks[i].number) == k)
			return 1;
	return 0;
}

u32 cheatstore_window_picks(u32 k)
{
	struct cheatstore *cs = cheatstore();
	if (!cs->windows)
		return cheatfile_picks(&cs->file);
	u32 n = 0;
	for (u32 i = 0; i < cs->n_picks; i++)
		n += window_of(cs->picks[i].number) == k;
	return n;
}

// The pick list's entry for cheat number n, or -1
static s32 pick_find(u32 n)
{
	struct cheatstore *cs = cheatstore();
	for (u32 i = 0; i < cs->n_picks; i++)
		if (cs->picks[i].number == n)
			return (s32)i;
	return -1;
}

static int is_picked(void *ctx, u32 n)
{
	(void)ctx;
	return pick_find(n) >= 0;
}

// Cheat c picked (as decoded: what it takes of the budget), or dropped
static void pick_note(const struct cheatfile_cheat *c)
{
	struct cheatstore *cs = cheatstore();
	s32 i = pick_find(c->number);
	if (!c->picked) {
		if (i >= 0)
			cs->picks[i] = cs->picks[--cs->n_picks];
	} else {
		if (i < 0) {
			if (cs->n_picks == CHEATSTORE_PICKS)
				return;  // past the list (the budget allows far fewer)
			i = cs->n_picks++;
		}
		int ok = c->status == CHEATFILE_OK;
		cs->picks[i] = (struct cheatstore_pick){c->number, ok ? c->bytes : 0u, ok ? c->patches : 0u,
							0,         ok ? c->cost : 0u,  c->hash};
	}
	cs->picks_changed = 1;
}

void cheatstore_begin(void)
{
	struct cheatstore *cs = cheatstore();
	cheatfile_begin(&cs->work);
	if (cs->windows)
		cs->work.dec = cs->win[cs->window].ix.dec;
}

static int index_write(void);

// Reads window k ahead: up to bytes of it into its slot; 1 once it is there
// (its CRC as its index says: else it is read again when needed, and fails)
static int ahead_step(u32 k, u32 bytes)
{
	struct cheatstore *cs = cheatstore();
	struct cheatstore_window *w = &cs->win[k];
	struct cheatstore_ahead *a = &cs->ahead;
	if (w->loaded)
		return 1;
	if (w->failed)
		return 0;  // read once and found changed: not again
	if (!a->busy || a->window != k) {
		a->busy = 1;
		a->window = k;
		a->done = a->crc = 0;
	}
	if (f_open(&cs->fil, cs->path, FA_READ) != FR_OK || f_lseek(&cs->fil, w->ix.at + a->done) != FR_OK) {
		f_close(&cs->fil);
		a->busy = 0;
		return 0;
	}
	u32 end = a->done + bytes < w->ix.len ? a->done + bytes : w->ix.len;
	int ok = 1;
	while (a->done < end) {
		UINT got = 0, want = end - a->done < CHEATSTORE_CHUNK ? end - a->done : CHEATSTORE_CHUNK;
		// Up to a sector's end first: FatFs reads whole sectors straight into
		// the buffer, whose start must then be its own (word-aligned) start
		u32 into = (w->ix.at + a->done) & 511;
		if (into && want > 512 - into)
			want = 512 - into;
		if (f_read(&cs->fil, cs->part, want, &got) != FR_OK || got != want) {
			ok = 0;
			break;
		}
		slot_put(k, a->done, (const char *)cs->part, got);
		a->crc = crc32_update(a->crc, cs->part, got);
		a->done += got;
	}
	f_close(&cs->fil);
	if (!ok) {
		a->busy = 0;
		return 0;
	}
	if (a->done < w->ix.len)
		return 0;
	a->busy = 0;
	if (a->crc != w->ix.crc) {
		// The file changed under its index (edited on a PC, its size kept):
		// the index is written fitting no file, its picks kept, so the next
		// open reads the file whole and indexes it, the picks carried by hash
		tlog_error("cheats: %s: window %lu isn't as its index says: the file changed, its index set aside",
			   cs->path, (unsigned long)k + 1);
		w->failed = 1;
		cs->file_size = 0;
		index_write();
		return 0;
	}
	w->loaded = 1;
	return 1;
}

int cheatstore_ahead(u32 k, u32 bytes)
{
	struct cheatstore *cs = cheatstore();
	if (k >= cs->windows || cs->win[k].loaded || cs->win[k].failed)
		return 1;  // there, or not to be read again
	ahead_step(k, bytes);
	return cs->win[k].loaded;
}

// Window k's text in its slot, read from the card if it isn't: 0 if it
// can't be, or isn't as the index says
static int window_load(u32 k)
{
	struct cheatstore *cs = cheatstore();
	if (cs->win[k].loaded)
		return 1;
	return ahead_step(k, cs->win[k].ix.len);
}

// Window k's text from its slot into the model's PSRAM, parsed, its picks
// the file's, its decoder started at the index's state; the other windows'
// picks counted in its budget
static void window_parse(u32 k)
{
	struct cheatstore *cs = cheatstore();
	struct cheatstore_window *w = &cs->win[k];
	char *bounce = cs->detail;
	psram_begin();
	for (u32 done = 0; done < w->ix.len; done += CHEATSTORE_PART) {
		u32 c = w->ix.len - done < CHEATSTORE_PART ? w->ix.len - done : CHEATSTORE_PART;
		slot_get(k, done, bounce, c);
		cheatstore_put(CHEATS_TEXT + done, bounce, c);
	}
	cheatfile_parse_part(&cs->file, CHEATS_TEXT, w->ix.len, cs->cheats, CHEATFILE_MAX, w->ix.first, 1);
	cheatfile_picks_from(&cs->file, is_picked, NULL);
	cs->window = (u8)k;
	for (u32 i = 0; i < cs->n_picks; i++)
		if (window_of(cs->picks[i].number) != k) {
			cs->file.more_bytes += cs->picks[i].bytes;
			cs->file.more_cost += cs->picks[i].cost;
			cs->file.more_patches += cs->picks[i].patches;
		}
	cheatstore_begin();
}

// Window k read now: loaded, its text in place and parsed. 0 if it can't be
static int window_read(u32 k)
{
	if (!window_load(k))
		return 0;
	window_parse(k);
	return 1;
}

// Decodes the text read now whole, through progress (done counting from
// before cheats of the file)
static void decode_all(u32 before, u32 total)
{
	while (!cheatstore_decode(16780000u / 20))
		show_progress(before + cheatstore()->work.next, total);
}

static int same_state(const struct cheat_decoder *a, const struct cheat_decoder *b);

// The window read now back in its slot, if an edit changed its text (its
// picks written in, as the whole file's write takes them). Its pick list's
// entries follow the model's (an edit renumbers), and the decoder after it,
// should the edit have changed it (a key gone), is worked out again for the
// windows after. 0 if it no longer fits its slot, or a PSRAM write failed.
static int window_keep(void)
{
	struct cheatstore *cs = cheatstore();
	u32 k = cs->window;
	struct cheatstore_window *w = &cs->win[k];
	if (!cs->file.edited)
		return !cs->room_failed;
	if (cheatfile_text_len(&cs->file) > CHEATSTORE_SLOT) {
		tlog_error("cheats: %s: window %lu grew past its slot", cs->path, (unsigned long)k + 1);
		return 0;
	}
	char *buf = cs->detail;
	u32 pos = 0, n;
	while ((n = cheatfile_text(&cs->file, pos, buf, CHEATSTORE_PART)) > 0) {
		slot_put(k, pos, buf, n);
		pos += n;
	}
	w->ix.len = pos;
	w->ix.count = (u16)cs->file.count;
	w->edited = 1;
	// The pick list: this window's entries as the model has them now
	u32 top = k + 1u < cs->windows ? cs->win[k + 1].ix.first : 0x10000u;
	for (u32 i = 0; i < cs->n_picks;)
		if (cs->picks[i].number >= w->ix.first && cs->picks[i].number < top)
			cs->picks[i] = cs->picks[--cs->n_picks];
		else
			i++;
	cheatstore_decode(0);
	for (u32 i = 0; i < cs->file.count; i++)
		if (cs->cheats[i].picked)
			pick_note(&cs->cheats[i]);
	cs->picks_changed = 1;
	cheatfile_written(&cs->file);  // the slot has it now; the card once the file is written
	// The windows after start as this one now ends
	struct cheat_decoder end = cs->work.dec;
	for (u32 j = k + 1; j < cs->windows && !same_state(&end, &cs->win[j].ix.dec); j++) {
		cs->win[j].ix.dec = end;
		if (j + 1u == cs->windows)
			break;
		if (!window_read(j))
			return 0;  // the windows after can't be worked out: the file isn't written
		decode_all(0, 0);
		end = cs->work.dec;
	}
	if (cs->window != k && !window_read(k))
		return 0;
	return !cs->room_failed;
}

int cheatstore_window_go(u32 k)
{
	struct cheatstore *cs = cheatstore();
	if (k >= cs->windows || !window_keep())
		return 0;
	return window_read(k);
}

int cheatstore_toggle(u32 i)
{
	struct cheatstore *cs = cheatstore();
	if (!cheatfile_toggle(&cs->file, i))
		return 0;
	if (cs->windows)
		pick_note(&cs->cheats[i]);
	return 1;
}

u32 cheatstore_run(u32 i, u32 *end)
{
	struct cheatstore *cs = cheatstore();
	cheatstore_decode_to(i);
	u32 start = cheatfile_group(&cs->file, i, end);
	// A cheat not decoded yet isn't known to be a variant: on until one isn't
	while (*end < cs->file.count && cs->cheats[*end].status == CHEATFILE_NEW) {
		cheatstore_decode_to(*end);
		start = cheatfile_group(&cs->file, i, end);
	}
	return start;
}

int cheatstore_toggle_one(u32 i)
{
	struct cheatstore *cs = cheatstore();
	u32 end, start = cheatstore_run(i, &end);
	if (cs->cheats[i].picked || end - start < 2)
		return cheatstore_toggle(i);
	u16 dropped[8];
	u32 n = 0;
	for (u32 k = start; k < end; k++)
		if (k != i && cs->cheats[k].picked && n++ >= 8)
			return cheatstore_toggle(i);  // more than can be put back: as a plain pick
	n = 0;
	for (u32 k = start; k < end; k++)
		if (k != i && cs->cheats[k].picked && cheatstore_toggle(k))
			dropped[n++] = (u16)k;
	if (cheatstore_toggle(i))
		return 1;
	for (u32 k = 0; k < n; k++)
		cheatstore_toggle(dropped[k]);
	return 0;
}

int cheatstore_all_off(void)
{
	struct cheatstore *cs = cheatstore();
	for (u32 i = 0; i < cs->file.count; i++)
		if (cs->cheats[i].picked)
			cheatfile_toggle(&cs->file, i);
	if (cs->windows) {
		cs->n_picks = 0;
		cs->picks_changed = 1;
		cs->file.more_bytes = cs->file.more_cost = cs->file.more_patches = 0;
	}
	return 1;
}

// --- A larger file's index (cheatindex.h) -----------------------------------------

// The cheat file's main copy's bytes on the card (what its index keeps), or 0
static u32 card_size(void)
{
	FILINFO info;
	return f_stat(cheatstore()->path, &info) == FR_OK ? (u32)info.fsize : 0;
}

// The index's path: the cheat file's, ".cdx" for ".cht"
static void index_path(char *out)
{
	struct cheatstore *cs = cheatstore();
	u32 n = (u32)strlen(cs->path);
	memcpy(out, cs->path, n + 1);
	if (n > 4)
		memcpy(out + n - 4, CHEATINDEX_EXT, sizeof(CHEATINDEX_EXT));  // its NUL where the old one was
}

// The index read into the windows and picks: 1 if it fits the file
// (file_size its main copy's bytes), else 0, nothing kept
static int index_read(u32 file_size)
{
	struct cheatstore *cs = cheatstore();
	char path[SAFEFILE_PATH_MAX];
	char *line = cs->detail;
	struct cheatindex_line l;
	int len, magic = 0, sized = 0, ok = 1;
	index_path(path);
	cs->windows = 0;
	cs->n_picks = 0;
	if (safefile_open(path, CHEATSTORE_INDEX_MAX) == SAFEFILE_NONE)
		return 0;
	u32 at = 0;
	while (ok && (len = safefile_line(line, CHEATINDEX_LINE_MAX)) != SAFEFILE_END) {
		if (len < 0)
			continue;
		switch (cheatindex_read_line(line, (u32)len, &l)) {
		case CHEATINDEX_MAGIC_LINE:
			magic = 1;
			break;
		case CHEATINDEX_FILE:
			sized = l.size == file_size;
			break;
		case CHEATINDEX_WINDOW:
			if (cs->windows == CHEATSTORE_WINDOWS || l.w.at != at || !l.w.len ||
			    l.w.len > CHEATSTORE_SLOT || l.w.first >= CHEATINDEX_NUMBER_MAX ||
			    (cs->windows && l.w.first <= cs->win[cs->windows - 1].ix.first)) {
				ok = 0;
				break;
			}
			cs->win[cs->windows++] = (struct cheatstore_window){l.w, 0, 0, 0};
			at += l.w.len;
			break;
		case CHEATINDEX_PICK:
			// Once each, and only in a window (the windows come first)
			if (cs->n_picks < CHEATSTORE_PICKS && cs->windows && l.number >= cs->win[0].ix.first &&
			    l.number < CHEATINDEX_NUMBER_MAX + CHEATFILE_MAX && pick_find(l.number) < 0)
				cs->picks[cs->n_picks++] = (struct cheatstore_pick){
					(u16)l.number, (u16)l.bytes, (u16)l.patches, 0, l.cost, l.hash};
			break;
		default:
			break;
		}
	}
	int failed = safefile_failed();
	safefile_close();
	// The windows cover the text; a copy the kernel wrote ends in safefile's
	// CRC line after it
	if (!ok || failed || !magic || !sized || !cs->windows || at > file_size ||
	    file_size - at > SAFEFILE_CRC_LINE_LEN + 2) {
		cs->windows = 0;
		cs->n_picks = 0;
		return 0;
	}
	cs->file_size = file_size;
	cs->picks_changed = 0;
	return 1;
}

// The index's text for safefile_write: its lines made again from the start
static u32 index_fill(void *ctx, u32 pos, char *buf, u32 max)
{
	static u32 next_line, line_at, made;  // the line to make, its place in the text, and its length
	struct cheatstore *cs = ctx;
	char *line = cs->detail;
	if (!pos) {
		next_line = line_at = made = 0;
	}
	u32 n = 0;
	for (;;) {
		if (pos + n >= line_at + made) {
			// The next line
			line_at += made;
			u32 i = next_line++;
			if (i == 0)
				made = cheatindex_head(line, cs->file_size);
			else if (i - 1 < cs->windows)
				made = cheatindex_window_line(line, &cs->win[i - 1].ix);
			else if (i - 1 - cs->windows < cs->n_picks) {
				const struct cheatstore_pick *p = &cs->picks[i - 1 - cs->windows];
				made = cheatindex_pick_line(line, p->number, p->hash, p->bytes, p->cost, p->patches);
			} else {
				made = 0;
				next_line--;
				return n;
			}
		}
		u32 off = pos + n - line_at, take = made - off;
		take = take < max - n ? take : max - n;
		memcpy(buf + n, line + off, take);
		n += take;
		if (n == max)
			return n;
	}
}

// Writes the index as the windows and picks are
static int index_write(void)
{
	struct cheatstore *cs = cheatstore();
	char path[SAFEFILE_PATH_MAX];
	index_path(path);
	int ok = safefile_write(path, CHEATSTORE_INDEX_MAX, index_fill, cs);
	if (ok)
		cs->picks_changed = 0;
	tlog("cheats: %s: index %s (%u windows, %u picks)", path, ok ? "written" : "not written", cs->windows,
	     cs->n_picks);
	return ok;
}

// The reading pass of a larger file with no index that fits: the whole
// file into windows, each cut as cheatindex.h says and copied to its slot
struct scan {
	struct cheatindex_cut cut;
	u32 file, line_at, line_n;
	char line[48];
};

// A line ended: a new window from it when the cut says. 0 if the file
// can't be read in windows.
static int scan_line(struct cheatstore *cs, struct scan *sc)
{
	int r = cheatindex_cut_line(&sc->cut, sc->line, sc->line_n, sc->line_at);
	if (r == CHEATINDEX_BAD)
		return 0;
	if (r == CHEATINDEX_NEW) {
		if (cs->windows == CHEATSTORE_WINDOWS)
			return 0;
		// The next window from this line: its bytes read so far move over
		struct cheatstore_window *w = &cs->win[cs->windows - 1];
		u32 cut = sc->line_at - w->ix.at;
		slot_move(cs->windows - 1u, cut, cs->windows, sc->file - sc->line_at);
		w->ix.len = cut;
		cs->win[cs->windows++] = (struct cheatstore_window){{sc->line_at, 0, 0, 0, 0, {0}}, 1, 0, 0};
	}
	struct cheatstore_window *w = &cs->win[cs->windows - 1];
	w->ix.first = (u16)sc->cut.first;
	w->ix.count = (u16)sc->cut.count;
	return 1;
}

// Reads the open copy into windows; 1 if it could be, else 0
static int read_windows(void)
{
	struct cheatstore *cs = cheatstore();
	struct scan sc;
	int n;
	memset(&sc, 0, sizeof(sc));
	cheatindex_cut_begin(&sc.cut);
	cs->windows = 1;
	cs->window = 0;
	cs->win[0] = (struct cheatstore_window){{0, 0, 0, 0, 0, {0}}, 1, 0, 0};
	while ((n = safefile_read_part(cs->part, sizeof(cs->part))) > 0) {
		const char *p = (const char *)cs->part;
		u32 base = sc.file, at = cs->win[cs->windows - 1].ix.at;
		if (base + (u32)n - at > CHEATSTORE_SLOT || base + (u32)n > CHEATSTORE_BIG_MAX)
			return 0;
		slot_put(cs->windows - 1u, base - at, p, (u32)n);
		sc.file += (u32)n;
		// A line at a time: its first bytes kept (all a key needs)
		for (u32 i = 0; i < (u32)n;) {
			const char *nl = memchr(p + i, '\n', (u32)n - i);
			u32 end = nl ? (u32)(nl - p) : (u32)n, take = end - i;
			if (take > sizeof(sc.line) - sc.line_n)
				take = sizeof(sc.line) - sc.line_n;
			memcpy(sc.line + sc.line_n, p + i, take);
			sc.line_n += take;
			if (!nl)
				break;
			if (sc.line_n && sc.line[sc.line_n - 1] == '\r')
				sc.line_n--;
			if (!scan_line(cs, &sc))
				return 0;
			i = end + 1;
			sc.line_at = base + i;
			sc.line_n = 0;
		}
	}
	if (n < 0 || (sc.line_n && !scan_line(cs, &sc)))
		return 0;
	struct cheatstore_window *last = &cs->win[cs->windows - 1];
	last->ix.len = sc.file - last->ix.at;
	cs->file_size = sc.file;
	for (u32 j = 0; j < cs->windows; j++) {
		struct cheatstore_window *w = &cs->win[j];
		if (w->ix.len > CHEATINDEX_BYTES + 0x8000)
			return 0;  // a cheat too long for a window
		w->ix.crc = slot_crc(j, w->ix.len);
	}
	return !cs->room_failed;
}

// After read_windows: every window decoded in turn, so each keeps the
// decoder at its start and the pick list what each pick takes. Picks are
// the file's enable lines', and the old index's matched by hash (the file
// was written anew: `just cheats`, a PC). Then the index is written.
static void index_build(s32 old_picks)
{
	struct cheatstore *cs = cheatstore();
	u32 total = cheatstore_window_start(cs->windows);
	struct cheat_decoder dec;
	cheat_decoder_init(&dec);
	cs->n_picks = 0;
	for (u32 k = 0; k < cs->windows; k++) {
		cs->win[k].ix.dec = dec;
		window_parse(k);  // its picks: none in the list yet, so the text's below
		cheatfile_parse_part(&cs->file, CHEATS_TEXT, cs->win[k].ix.len, cs->cheats, CHEATFILE_MAX,
				     cs->win[k].ix.first, 1);
		cheatstore_begin();
		decode_all(cheatstore_window_start(k), total);
		for (u32 i = 0; i < cs->file.count; i++) {
			struct cheatfile_cheat *c = &cs->cheats[i];
			// An old index's picks are the file's (an un-pick lives only
			// there); with none, the text's enable lines
			if (old_picks >= 0) {
				c->picked = 0;
				for (u32 o = 0; !c->picked && c->hash && o < (u32)old_picks; o++)
					c->picked = cs->sig[o] == c->hash;
			}
			if (c->picked)
				pick_note(c);
		}
		cs->win[k].ix.count = (u16)cs->file.count;
		dec = cs->work.dec;
	}
	index_write();
}
// The open copy's text into PSRAM, a part at a time (never f_read straight
// into PSRAM: the card's DMA isn't known to reach it); its length in *len,
// though only CHEATFILE_TEXT_MAX bytes of it go in. 0 if a read failed.
static int load_text(u32 *len)
{
	struct cheatstore *cs = cheatstore();
	int n;
	*len = 0;
	psram_begin();
	while ((n = safefile_read_part(cs->part, sizeof(cs->part))) > 0) {
		if (*len + (u32)n <= CHEATFILE_TEXT_MAX)
			cheatstore_put(CHEATS_TEXT + *len, (const char *)cs->part, (u32)n);
		*len += (u32)n;
	}
	return n == 0;
}

int cheatstore_path(char *out, u32 size, const char *rom_path)
{
	const char *slash = strrchr(rom_path, '/');
	const char *name = slash ? slash + 1 : rom_path;
	const char *dot = strrchr(name, '.');
	u32 stem = dot && dot != name ? (u32)(dot - rom_path) : (u32)strlen(rom_path);
	const char *sep = rom_path[0] == '/' ? "" : "/";
	u32 n = (u32)snprintf(out, size, "%s%s%.*s.cht", CHEATS_DIR, sep, (int)stem, rom_path);
	if (n >= size || n + 4 >= SAFEFILE_PATH_MAX)
		return -1;
	return (int)n;
}

// 0 if f_stat says path is surely not on the card; a card error counts as
// there, so a file it hid is never written over
static int maybe_there(const char *path)
{
	FILINFO info;
	FRESULT r = f_stat(path, &info);
	return r != FR_NO_FILE && r != FR_NO_PATH;
}

// 1 if either copy of the cheat file at path (cheatstore()->path) may be on
// the card, though safefile_open took neither: too big, say
static int any_copy(char *path)
{
	u32 len = (u32)strlen(path);
	int there = maybe_there(path);
	memcpy(path + len, ".bak", 5);  // cheatstore_path leaves room for it
	there |= maybe_there(path);
	path[len] = 0;
	return there;
}

// 0 if the file names the dump it was made for (cheatfile_rom_crc) and the
// CRC cache (or crc.txt, before the cache is loaded) knows the ROM at
// rom_path as another: its codes would write another build's addresses. A
// ROM whose CRC isn't known yet passes. The cheat table is crc.txt's line
// buffer: cheatfile_parse fills it afresh after.
static int for_this_dump(const char *rom_path, const char *text, u32 len)
{
	u32 want, crc;
	if (!rom_path)
		return 1;  // read for itself (cheatstore_open_file)
	struct cheatstore *cs = cheatstore();
	_Static_assert(sizeof(cs->cheats) >= SAFEFILE_LINE_MAX, "crc.txt's line fits the cheat table");
	if (!cheatfile_rom_crc(text, len, &want) ||
	    !names_crc_now(rom_path, &crc, (char *)cs->cheats, sizeof(cs->cheats)) || crc == want)
		return 1;
	tlog("cheats: %s is for crc32 %08lX, and %s is %08lX: left out", cheatstore()->path, (unsigned long)want,
	     rom_path, (unsigned long)crc);
	return 0;
}

// The picks of the index there is, whatever file it was for: their hashes
// into cheatstore()->sig, for a file read anew (index_build). How many, or
// -1 with no index.
static s32 old_index_picks(void)
{
	struct cheatstore *cs = cheatstore();
	char path[SAFEFILE_PATH_MAX];
	char *line = cs->detail;
	struct cheatindex_line l;
	u32 n = 0;
	int len;
	index_path(path);
	if (safefile_open(path, CHEATSTORE_INDEX_MAX) == SAFEFILE_NONE)
		return -1;
	while ((len = safefile_line(line, CHEATINDEX_LINE_MAX)) != SAFEFILE_END)
		if (len >= 0 && cheatindex_read_line(line, (u32)len, &l) == CHEATINDEX_PICK && l.hash &&
		    n < CHEATFILE_MAX)
			cs->sig[n++] = l.hash;
	safefile_close();
	return (s32)n;
}

// The dump check (for_this_dump) on window 0, which reads it now after
static int windows_for_this_dump(const char *rom_path)
{
	struct cheatstore *cs = cheatstore();
	int ok = for_this_dump(rom_path, CHEATS_TEXT, cs->win[0].ix.len);
	window_parse(0);  // for_this_dump takes the cheat table for a line
	return ok;
}

// A larger file through its index: CHEATSTORE_READ, CHEATSTORE_OTHER_DUMP,
// or CHEATSTORE_NONE with no index that fits (the file is read whole then)
static int open_index(const char *rom_path)
{
	struct cheatstore *cs = cheatstore();
	FILINFO info;
	u32 start = torii_frames;
	if (f_stat(cs->path, &info) != FR_OK || !index_read((u32)info.fsize))
		return CHEATSTORE_NONE;
	if (!window_read(0)) {
		cs->windows = 0;
		return CHEATSTORE_NONE;
	}
	if (!windows_for_this_dump(rom_path)) {
		cs->windows = 0;
		return CHEATSTORE_OTHER_DUMP;
	}
	tlog("cheats: %s: %lu cheats in %u windows, %u picked, by its index in %lu frames", cs->path,
	     (unsigned long)cheatstore_window_start(cs->windows), cs->windows, cs->n_picks,
	     (unsigned long)(torii_frames - start));
	return CHEATSTORE_READ;
}

// A larger file with no index that fits: read whole into windows, decoded,
// its index written (cheatstore.h). CHEATSTORE_READ, or why not.
static int open_windows(const char *rom_path)
{
	struct cheatstore *cs = cheatstore();
	u32 start = torii_frames;
	s32 old = old_index_picks();
	int from = safefile_open(cs->path, CHEATSTORE_BIG_FILE_MAX);
	if (safefile_too_big() || from == SAFEFILE_NONE) {
		if (from != SAFEFILE_NONE)
			safefile_close();
		tlog("cheats: %s has no copy of at most %lu bytes: too big, left alone", cs->path,
		     (unsigned long)CHEATSTORE_BIG_FILE_MAX);
		return safefile_failed() && !safefile_too_big() ? CHEATSTORE_UNREADABLE : CHEATSTORE_BIG;
	}
	int ok = read_windows();
	int failed = safefile_failed();
	safefile_close();
	if (failed || cs->room_failed) {
		cs->windows = 0;
		tlog_error("cheats: %s can't be read", cs->path);
		return CHEATSTORE_UNREADABLE;
	}
	if (!ok) {
		cs->windows = 0;
		tlog("cheats: %s: too big, and its cheats aren't in order to read it in windows: left alone", cs->path);
		return CHEATSTORE_BIG;
	}
	window_parse(0);
	if (!windows_for_this_dump(rom_path)) {
		cs->windows = 0;
		return CHEATSTORE_OTHER_DUMP;
	}
	cs->file_size = card_size();
	index_build(old);
	window_parse(0);
	tlog("cheats: %s: %lu cheats in %u windows, %u picked, read whole and indexed in %lu frames", cs->path,
	     (unsigned long)cheatstore_window_start(cs->windows), cs->windows, cs->n_picks,
	     (unsigned long)(torii_frames - start));
	return CHEATSTORE_READ;
}

static int open_read(const char *rom_path);

int cheatstore_open(const char *rom_path)
{
	struct cheatstore *cs = cheatstore();
	if (cheatstore_path(cs->path, sizeof(cs->path), rom_path) < 0) {
		cs->file.count = 0;
		cs->windows = 0;
		tlog("cheats: %s: its cheat file's path is too long", rom_path);
		return CHEATSTORE_NONE;
	}
	return open_read(rom_path);
}

int cheatstore_open_file(const char *path)
{
	struct cheatstore *cs = cheatstore();
	if (strlen(path) + 4 >= SAFEFILE_PATH_MAX)
		return CHEATSTORE_NONE;
	snprintf(cs->path, sizeof(cs->path), "%s", path);
	return open_read(NULL);
}

// cheatstore_open's read of the file at cheatstore()->path; rom_path NULL:
// for no game, so no dump check
static int open_read(const char *rom_path)
{
	struct cheatstore *cs = cheatstore();
	cs->file.count = 0;
	cs->work.next = 0;
	cs->room_failed = 0;
	cs->windows = 0;
	cs->window = 0;
	cs->n_picks = 0;
	cs->picks_changed = 0;
	cs->ahead.busy = 0;
	int indexed = open_index(rom_path);
	if (indexed != CHEATSTORE_NONE)
		return indexed;
	int from = safefile_open(cs->path, CHEATSTORE_FILE_MAX);
	if (safefile_too_big()) {
		// A copy over a small file's read: read in windows, else neither read
		// nor ever written over (no new file made in its place)
		if (from != SAFEFILE_NONE)
			safefile_close();
		return open_windows(rom_path);
	}
	if (from == SAFEFILE_NONE) {
		if (safefile_failed()) {
			// A card error, not an absent file: never made anew over it
			tlog_error("cheats: %s can't be read", cs->path);
			return CHEATSTORE_UNREADABLE;
		}
		if (any_copy(cs->path)) {
			tlog("cheats: %s is there, but neither copy holds up: left alone", cs->path);
			return CHEATSTORE_UNREADABLE;
		}
		// None yet: an empty file, made on the first write
		cheatfile_parse(&cs->file, CHEATS_TEXT, 0, cs->cheats, CHEATFILE_MAX);
		cheatfile_begin(&cs->work);
		return CHEATSTORE_NEW;
	}
	u32 len;
	int read = load_text(&len);
	int failed = safefile_failed();
	safefile_close();
	if (!read || failed) {
		tlog_error("cheats: %s can't be read", cs->path);
		return CHEATSTORE_UNREADABLE;
	}
	if (len > CHEATFILE_TEXT_MAX) {
		// Its cheats would be read from a cut text, and the picks written
		// back over the rest: none of it, then
		tlog("cheats: %s is %lu bytes, over %lu: too big, left alone", cs->path, (unsigned long)len,
		     (unsigned long)CHEATFILE_TEXT_MAX);
		return CHEATSTORE_BIG;
	}
	if (cs->room_failed) {
		tlog_error("cheats: %s: its text didn't go into PSRAM whole", cs->path);
		return CHEATSTORE_UNREADABLE;
	}
	if (!for_this_dump(rom_path, CHEATS_TEXT, len))
		return CHEATSTORE_OTHER_DUMP;
	cheatfile_parse(&cs->file, CHEATS_TEXT, len, cs->cheats, CHEATFILE_MAX);
	cheatfile_begin(&cs->work);
	if (cs->file.dropped) {
		// Cheats numbered past the table: in windows, or left out
		tlog("cheats: %s: %lu cheats numbered past %u: read again in windows", cs->path,
		     (unsigned long)cs->file.dropped, CHEATFILE_MAX);
		int r = open_windows(rom_path);
		if (r != CHEATSTORE_BIG)
			return r;
		cheatfile_parse(&cs->file, CHEATS_TEXT, len, cs->cheats, CHEATFILE_MAX);
		cheatfile_begin(&cs->work);
		tlog("cheats: %s: %lu cheats numbered past %u are left out", cs->path, (unsigned long)cs->file.dropped,
		     CHEATFILE_MAX);
	}
	tlog_debug("cheats: %s: %lu bytes, %lu cheats", cs->path, (unsigned long)len, (unsigned long)cs->file.count);
	return CHEATSTORE_READ;
}

int cheatstore_read(const char *rom_path)
{
	return cheatstore_open(rom_path) == CHEATSTORE_READ && cheatstore()->file.count != 0;
}

void cheatstore_decoder_before(u32 i, struct cheat_decoder *out)
{
	struct cheatstore *cs = cheatstore();
	if (cs->work.next != i || i == 0) {
		cheatstore_begin();
		while (cs->work.next < i && cheatfile_next(&cs->file, &cs->work) >= 0)
			;
	}
	*out = cs->work.dec;
}

void cheatstore_decode_to(u32 i)
{
	struct cheatstore *cs = cheatstore();
	while (cs->work.next <= i && cheatfile_next(&cs->file, &cs->work) >= 0)
		;
}

int cheatstore_decode(u32 budget)
{
	struct cheatstore *cs = cheatstore();
	u32 start = cycles_now();
	while (cheatfile_next(&cs->file, &cs->work) >= 0)
		if (budget && cycles_since(start) > budget)
			return cs->work.next >= cs->file.count;
	return 1;
}

// The last file counted whole, for the game popup: its path's CRC, and the
// size and date of the copy on the card (cheatstore_peek's), and the count.
// Picks don't change what is listed; anything else that changes the file
// changes its size or date (a write here records them again).
static struct {
	u32 path_crc, size;
	u16 date, time;
	u16 listed;
	u8 valid;
} listed_cache EWRAM_BSS;
_Static_assert(CHEATFILE_MAX <= 0xFFFF, "a count fits listed_cache");

// The copy of the file at path (cheatstore()->path) whose size and date
// key the count: NAME, else NAME.bak. 0 if neither is there.
static int stat_copy(char *path, FILINFO *info)
{
	if (f_stat(path, info) == FR_OK)
		return 1;
	u32 len = (u32)strlen(path);
	memcpy(path + len, ".bak", 5);  // cheatstore_path leaves room for it
	int there = f_stat(path, info) == FR_OK;
	path[len] = 0;
	return there;
}

s32 cheatstore_listed(void)
{
	struct cheatstore *cs = cheatstore();
	u32 listed = cheatstore_window_start(cs->windows ? cs->windows : 1u);  // every cheat, as the screen lists them
	FILINFO info;
	listed_cache.valid = 0;
	if (stat_copy(cs->path, &info)) {
		listed_cache.path_crc = crc32(cs->path, (u32)strlen(cs->path));
		listed_cache.size = (u32)info.fsize;
		listed_cache.date = info.fdate;
		listed_cache.time = info.ftime;
		listed_cache.listed = (u16)listed;
		listed_cache.valid = 1;
	}
	return (s32)listed;
}

// --- Settings' clean-up ---------------------------------------------------------

// 1 if the clean-up removes cheat c (master codes too, with masters)
static int clean_takes(const struct cheatfile_cheat *c, int masters)
{
	if (c->own)
		return 0;
	switch (c->status) {
	case CHEATFILE_UNREADABLE:
	case CHEATFILE_BIG:
	case CHEATFILE_CANT_RUN:
	case CHEATFILE_NOTHING:
	case CHEATFILE_SAME:
		return 1;
	case CHEATFILE_MASTER:
		return masters;
	default:
		return 0;
	}
}

// A cheat as the clean-up compares it before and after: its status and hash
static u32 clean_sig(const struct cheatfile_cheat *c)
{
	return c->hash ^ ((u32)c->status * 0x9E3779B9u);
}

// The decoder's state that carries to the cheats after (not what it says of
// the last one read)
static int same_state(const struct cheat_decoder *a, const struct cheat_decoder *b)
{
	return a->forced == b->forced && a->gs_version == b->gs_version && a->cb_key2 == b->cb_key2 &&
	       a->cb_master == b->cb_master && a->cb_rng == b->cb_rng &&
	       !memcmp(a->cb_table, b->cb_table, sizeof(a->cb_table)) &&
	       !memcmp(a->gs_seeds, b->gs_seeds, sizeof(a->gs_seeds)) &&
	       !memcmp(a->cb_seeds, b->cb_seeds, sizeof(a->cb_seeds));
}

// The text read now cleaned (cheatstore_clean): its cheats decoded, those it
// takes removed, the rest decoded again. How many went; 0 if none was to
// go, or if one that stays read otherwise after, or the decoder ended
// otherwise: then *spoiled says the text in PSRAM was changed all the same.
static u32 clean_text(int masters, int *spoiled)
{
	struct cheatstore *cs = cheatstore();
	struct cheatfile *f = &cs->file;
	u8 drop[CHEATFILE_MAX / 8];
	u32 gone = 0, k = 0;
	*spoiled = 0;
	cheatstore_begin();
	cheatstore_decode(0);
	memset(drop, 0, sizeof(drop));
	for (u32 i = 0; i < f->count; i++) {
		const struct cheatfile_cheat *c = &f->cheats[i];
		if (clean_takes(c, masters)) {
			u32 e = c->number - f->base;
			drop[e >> 3] |= (u8)(1u << (e & 7));
			gone++;
		} else {
			cs->sig[k++] = clean_sig(c);
		}
	}
	if (!gone)
		return 0;
	struct cheat_decoder end = cs->work.dec;
	psram_begin();
	cheatfile_remove_many(f, the_room(), drop);
	*spoiled = 1;
	cheatstore_begin();
	cheatstore_decode(0);
	int same = f->count == k && !cs->room_failed && same_state(&end, &cs->work.dec);
	for (u32 i = 0; same && i < f->count; i++)
		same = clean_sig(&f->cheats[i]) == cs->sig[i];
	return same ? gone : 0;
}

s32 cheatstore_clean(const char *path)
{
	struct cheatstore *cs = cheatstore();
	int spoiled;
	if (cheatstore_open_file(path) != CHEATSTORE_READ)
		return -1;
	if (cs->windows) {
		u32 gone = 0;
		for (u32 k = 0; k < cs->windows; k++) {
			if (!cheatstore_window_go(k))
				return -1;
			u32 n = clean_text(0, &spoiled);
			if (!n && spoiled)
				window_read(k);  // as its slot has it
			gone += n;
		}
		if (gone && !cheatstore_write())
			return -1;
		return (s32)gone;
	}
	u32 gone = clean_text(1, &spoiled);
	if (!gone && spoiled) {
		// Master codes' keys decode the codes after them: kept, then
		if (cheatstore_open_file(path) != CHEATSTORE_READ)
			return -1;
		gone = clean_text(0, &spoiled);
		if (!gone) {
			if (spoiled)
				tlog("cheats: %s: its cheats read otherwise without the greyed ones: left as it is",
				     path);
			return 0;
		}
	}
	if (!gone)
		return 0;
	if (cheatfile_empty(&cs->file))
		return cheatstore_drop() ? (s32)gone : -1;
	return cheatstore_write() ? (s32)gone : -1;
}

// 1 if name is a cheat file's (".cht", any case)
static int is_cht(const char *name)
{
	u32 n = (u32)strlen(name);
	return n > 4 && name[n - 4] == '.' && (name[n - 3] | 0x20) == 'c' && (name[n - 2] | 0x20) == 'h' &&
	       (name[n - 1] | 0x20) == 't';
}

// Walks CHEATS_DIR's cheat files, folders and all (CHEATSTORE_DEPTH deep):
// with sum, each one cleaned (progress says how far of total). How many.
static u32 walk_cheat_files(struct cheatstore_sum *sum, cheatstore_progress_fn progress, u32 total)
{
	struct cheatstore_walk *w = &cheatstore()->walk;
	u32 depth = 0, n = 0;
	snprintf(w->at, sizeof(w->at), "%s", CHEATS_DIR);
	w->len[0] = (u32)strlen(w->at);
	if (f_opendir(&w->dirs[0], w->at) != FR_OK)
		return 0;
	for (;;) {
		if (f_readdir(&w->dirs[depth], &w->info) != FR_OK || !w->info.fname[0]) {
			f_closedir(&w->dirs[depth]);
			if (!depth)
				break;
			w->at[w->len[--depth]] = 0;
			continue;
		}
		const char *name = w->info.fname;
		u32 base = w->len[depth], nl = (u32)strlen(name);
		if (base + 1 + nl + 4 >= sizeof(w->at) || name[0] == '.')
			continue;  // too long a path for a cheat file's (".bak" added), or hidden
		int dir = (w->info.fattrib & AM_DIR) != 0;
		if (!dir && !is_cht(name))
			continue;
		w->at[base] = '/';
		memcpy(w->at + base + 1, name, nl + 1);
		if (dir) {
			if (depth + 1 < CHEATSTORE_DEPTH && f_opendir(&w->dirs[depth + 1], w->at) == FR_OK) {
				w->len[++depth] = base + 1 + nl;
				continue;
			}
			w->at[base] = 0;
			continue;
		}
		n++;
		if (sum) {
			if (progress)
				progress(n - 1, total);
			s32 gone = cheatstore_clean(w->at);
			if (gone < 0) {
				sum->failed++;
				tlog_error("cheats: clean-up: %s couldn't be cleaned", w->at);
			} else if (gone) {
				sum->cleaned++;
				sum->removed += (u32)gone;
				tlog("cheats: clean-up: %s: %ld cheats removed", w->at, (long)gone);
			}
		}
		w->at[base] = 0;
	}
	return n;
}

void cheatstore_clean_all(cheatstore_progress_fn progress, struct cheatstore_sum *sum)
{
	memset(sum, 0, sizeof(*sum));
	u32 total = walk_cheat_files(NULL, NULL, 0);
	sum->files = walk_cheat_files(sum, progress, total);
	if (progress)
		progress(total, total);
	tlog("cheats: clean-up: %lu files, %lu cheats removed from %lu, %lu failed", (unsigned long)sum->files,
	     (unsigned long)sum->removed, (unsigned long)sum->cleaned, (unsigned long)sum->failed);
}

// --- The counts kept on the card -------------------------------------------------
//
// COUNTS_PATH, a safe file of a line a cheat file the cheat screen has
// counted, the latest first: "PATHCRC SIZE DATETIME LISTED" in hex, as
// listed_cache holds them, so the popup has the count after a power-off too.
// A file changed since (by tools/cheats.py, say) has another size or date:
// its line is passed over until the screen counts it again.

#define COUNTS_PATH CHEATS_DIR "/counts.txt"
#define COUNTS_MAX 32  // lines kept
#define COUNTS_BYTES 1536  // the file's text at most (COUNTS_MAX lines of 29 bytes and more)
_Static_assert(COUNTS_BYTES <= CHEATSTORE_DETAIL, "the counts' text is built in the detail view's buffer");

struct count_line {
	u32 path_crc, size, stamp, listed;
};

static u32 hex_field(const char **s)
{
	u32 v = 0, digits = 0;
	while (**s == ' ')
		(*s)++;
	for (;; (*s)++, digits++) {
		char c = **s;
		u32 d = c >= '0' && c <= '9' ? (u32)(c - '0') : c >= 'A' && c <= 'F' ? (u32)(c - 'A' + 10) : 16;
		if (d == 16 || digits == 8)
			break;
		v = v << 4 | d;
	}
	return digits ? v : 0xFFFFFFFFu;
}

// A line of COUNTS_PATH into *out: 0 if it isn't one
static int count_parse(const char *line, struct count_line *out)
{
	const char *s = line;
	out->path_crc = hex_field(&s);
	out->size = hex_field(&s);
	out->stamp = hex_field(&s);
	out->listed = hex_field(&s);
	return *s == 0 && out->listed <= CHEATFILE_MAX;
}

static u32 counts_len;

static u32 counts_fill(void *ctx, u32 pos, char *buf, u32 max)
{
	u32 n = pos < counts_len ? counts_len - pos : 0;
	n = n < max ? n : max;
	memcpy(buf, (const char *)ctx + pos, n);
	return n;
}

void cheatstore_keep_listed(void)
{
	struct cheatstore *cs = cheatstore();
	if (!listed_cache.valid)
		return;
	struct count_line now = {listed_cache.path_crc, listed_cache.size,
				 (u32)listed_cache.date << 16 | listed_cache.time, listed_cache.listed};
	char *out = cs->detail, *line = (char *)cs->part;
	u32 n = (u32)snprintf(out, COUNTS_BYTES, "%08lX %lX %08lX %lX\n", (unsigned long)now.path_crc,
			      (unsigned long)now.size, (unsigned long)now.stamp, (unsigned long)now.listed);
	u32 lines = 1;
	if (safefile_open(COUNTS_PATH, COUNTS_BYTES) != SAFEFILE_NONE) {
		int len;
		struct count_line was;
		while ((len = safefile_line(line, CHEATSTORE_PART)) != SAFEFILE_END) {
			if (len < 0 || !count_parse(line, &was))
				continue;
			if (was.path_crc == now.path_crc) {
				if (lines == 1 && !memcmp(&was, &now, sizeof(was))) {
					safefile_close();
					return;  // kept already, the latest
				}
				continue;
			}
			if (lines < COUNTS_MAX && n + (u32)len + 1 < COUNTS_BYTES) {
				memcpy(out + n, line, (u32)len);
				out[n + (u32)len] = '\n';
				n += (u32)len + 1;
				lines++;
			}
		}
		int failed = safefile_failed();
		safefile_close();
		if (failed)
			return;  // never written over what couldn't be read
	} else if (safefile_failed()) {
		return;
	}
	counts_len = n;
	if (!safefile_write(COUNTS_PATH, COUNTS_BYTES, counts_fill, out))
		tlog("cheats: %s not written", COUNTS_PATH);
}

// The count COUNTS_PATH keeps for the file at path (its copy's info): -1 if none
static s32 kept_count(const char *path, const FILINFO *info)
{
	struct count_line want = {crc32(path, (u32)strlen(path)), (u32)info->fsize,
				  (u32)info->fdate << 16 | info->ftime, 0},
			  was;
	char *line = (char *)cheatstore()->part;
	s32 listed = -1;
	int len;
	if (safefile_open(COUNTS_PATH, COUNTS_BYTES) == SAFEFILE_NONE)
		return -1;
	while ((len = safefile_line(line, CHEATSTORE_PART)) != SAFEFILE_END)
		if (len >= 0 && count_parse(line, &was) && was.path_crc == want.path_crc) {
			if (was.size == want.size && was.stamp == want.stamp)
				listed = (s32)was.listed;
			break;  // the latest line for the path counts
		}
	safefile_close();
	return listed;
}

int cheatstore_peek(const char *rom_path, s32 *listed)
{
	char *path = cheatstore()->path;
	*listed = -1;
	if (cheatstore_path(path, SAFEFILE_PATH_MAX, rom_path) < 0)
		return 0;
	FILINFO info;
	if (!stat_copy(path, &info))
		return 1;
	if (listed_cache.valid && listed_cache.size == (u32)info.fsize && listed_cache.date == info.fdate &&
	    listed_cache.time == info.ftime && listed_cache.path_crc == crc32(path, (u32)strlen(path)))
		*listed = listed_cache.listed;
	else
		*listed = kept_count(path, &info);
	return 1;
}

// The read file, for an edit: its text is the room's (a larger file's
// window keeps its slot's room; window_keep sees to the windows after)
static struct cheatfile *edited_file(void)
{
	struct cheatfile *f = &cheatstore()->file;
	TLOG_ASSERT(f->text == CHEATS_TEXT);
	psram_begin();
	return f;
}

// An edit's result: a failed PSRAM write (now or before) fails it
static u32 edit_done(u32 r)
{
	if (!cheatstore()->room_failed)
		return r;
	tlog_error("cheats: %s: a PSRAM write failed, the edit can't be trusted", cheatstore()->path);
	return CHEATFILE_EDIT_FAILED;
}

u32 cheatstore_add(const char *desc, const char *code, u8 format, u32 *index)
{
	return edit_done(cheatfile_add(edited_file(), the_room(), desc, code, format, index));
}

u32 cheatstore_set_code(u32 i, const char *desc, const char *code, u8 format)
{
	return edit_done(cheatfile_set_code(edited_file(), the_room(), i, desc, code, format));
}

u32 cheatstore_rename(u32 i, const char *desc)
{
	return edit_done(cheatfile_rename(edited_file(), the_room(), i, desc));
}

u32 cheatstore_set_keys(u32 i, u16 keys)
{
	return edit_done(cheatfile_set_keys(edited_file(), the_room(), i, keys));
}

u32 cheatstore_set_guide(u32 i, const char *guide)
{
	return edit_done(cheatfile_set_guide(edited_file(), the_room(), i, guide));
}

u32 cheatstore_remove(u32 i)
{
	return edit_done(cheatfile_remove(edited_file(), the_room(), i));
}

u32 cheatstore_can_add(const char *desc)
{
	struct cheatstore *cs = cheatstore();
	return cs->room_failed ? CHEATFILE_EDIT_FAILED : cheatfile_can_add(&cs->file, the_room()->cap, desc);
}

// The safe file's text for a write: every slot, in order
static u32 slots_fill(void *ctx, u32 pos, char *buf, u32 max)
{
	struct cheatstore *cs = ctx;
	u32 at = 0, k = 0;
	while (k < cs->windows && pos >= at + cs->win[k].ix.len)
		at += cs->win[k++].ix.len;
	if (k == cs->windows)
		return 0;
	u32 n = at + cs->win[k].ix.len - pos;
	n = n < max ? n : max;
	slot_get(k, pos - at, buf, n);
	return n;
}

// A larger file's write: its picks alone into its index, or, after an edit,
// the whole file (every window's slot in turn, those not read yet read
// first) and then the index, its windows where they are now
static int write_windows(void)
{
	struct cheatstore *cs = cheatstore();
	if (!window_keep()) {
		tlog_error("cheats: %s: a window couldn't be kept: not written", cs->path);
		return 0;
	}
	u32 edited = 0, len = 0;
	for (u32 j = 0; j < cs->windows; j++)
		edited |= cs->win[j].edited;
	if (!edited)
		return cs->picks_changed ? index_write() : 1;
	for (u32 j = 0; j < cs->windows; j++) {
		if (!window_load(j)) {
			tlog_error("cheats: %s: window %lu can't be read: not written", cs->path, (unsigned long)j + 1);
			return 0;
		}
		len += cs->win[j].ix.len;
		show_progress(j + 1, cs->windows * 2u);
	}
	if (len > CHEATSTORE_BIG_MAX) {
		tlog_error("cheats: %s would be %lu bytes, over %u: not written", cs->path, (unsigned long)len,
			   CHEATSTORE_BIG_MAX);
		return 0;
	}
	u32 start = torii_frames;
	int ok = safefile_write_cut(cs->path, CHEATSTORE_BIG_FILE_MAX, slots_fill, cs);
	tlog("cheats: %s: %lu picked, %lu bytes in %u windows %s in %lu frames", cs->path,
	     (unsigned long)cheatstore_picks(), (unsigned long)len, cs->windows, ok ? "written" : "not written",
	     (unsigned long)(torii_frames - start));
	if (!ok)
		return 0;
	u32 at = 0;
	for (u32 j = 0; j < cs->windows; j++) {
		struct cheatstore_window *w = &cs->win[j];
		w->ix.at = at;
		w->ix.crc = slot_crc(j, w->ix.len);
		w->edited = 0;
		at += w->ix.len;
		show_progress(cs->windows + j + 1, cs->windows * 2u);
	}
	cs->file_size = card_size();
	index_write();
	cheatstore_listed();
	return 1;
}

int cheatstore_write(void)
{
	struct cheatstore *cs = cheatstore();
	if (cs->windows)
		return write_windows();
	if (!cheatfile_changed(&cs->file))
		return 1;
	if (cs->file.cut)
		return 0;  // never a text cut short (cheatstore_read refuses such a file)
	if (cs->room_failed) {
		tlog_error("cheats: %s: a PSRAM write of its text failed: not written", cs->path);
		return 0;
	}
	u32 len = cheatfile_text_len(&cs->file);
	if (len > CHEATFILE_TEXT_MAX) {
		tlog_error("cheats: %s would be %lu bytes, over %u: not written", cs->path, (unsigned long)len,
			   CHEATFILE_TEXT_MAX);
		return 0;
	}
	int ok = safefile_write_cut(cs->path, CHEATSTORE_FILE_MAX, cheatfile_text, &cs->file);
	tlog("cheats: %s: %lu picked, %s", cs->path, (unsigned long)cheatfile_picks(&cs->file),
	     ok ? "written" : "not written");
	if (ok) {
		cheatfile_written(&cs->file);
		cheatstore_listed();  // the count kept for the file as it is on the card now
	}
	return ok;
}

int cheatstore_delete(const char *rom_path)
{
	char *path = cheatstore()->path;
	int len = cheatstore_path(path, SAFEFILE_PATH_MAX, rom_path);
	if (len < 0)
		return 1;
	FRESULT a = f_unlink(path);
	if (a == FR_OK)
		cutpoint("cheats: the cheat file deleted");
	memcpy(path + len, ".bak", 5);  // cheatstore_path leaves room for it
	FRESULT b = f_unlink(path);
	if (b == FR_OK)
		cutpoint("cheats: its .bak deleted");
	int gone = (a == FR_OK || a == FR_NO_FILE || a == FR_NO_PATH) &&
		   (b == FR_OK || b == FR_NO_FILE || b == FR_NO_PATH);
	if (a == FR_OK || b == FR_OK || !gone)
		tlog("cheats: %s: deleted (FatFs %u, %u)", path, (unsigned)a, (unsigned)b);
	// A larger file's index, after the file: one left by a cut fits no file
	memcpy(path + len - 4, CHEATINDEX_EXT, 5);
	f_unlink(path);
	memcpy(path + len, ".bak", 5);
	f_unlink(path);
	return gone;
}

int cheatstore_drop(void)
{
	struct cheatstore *cs = cheatstore();
	if (cs->windows)
		return 0;  // a window emptied: the file's other windows stay
	char *path = cs->path;
	u32 len = (u32)strlen(path);
	// The copy a reader takes goes last, so a cut between the two leaves
	// the file as last written, or none
	int bak_last = safefile_reads_bak(path, CHEATSTORE_FILE_MAX);
	FRESULT a = FR_OK, b = FR_OK;
	if (bak_last) {
		a = f_unlink(path);
		if (a == FR_OK)
			cutpoint("cheats: the emptied file deleted");
	}
	memcpy(path + len, ".bak", 5);  // cheatstore_path leaves room for it
	b = f_unlink(path);
	if (b == FR_OK)
		cutpoint("cheats: the emptied file's .bak deleted");
	path[len] = 0;
	if (!bak_last) {
		a = f_unlink(path);
		if (a == FR_OK)
			cutpoint("cheats: the emptied file deleted");
	}
	int gone = (a == FR_OK || a == FR_NO_FILE || a == FR_NO_PATH) &&
		   (b == FR_OK || b == FR_NO_FILE || b == FR_NO_PATH);
	tlog("cheats: %s: no cheats left, deleted (FatFs %u, %u)", path, (unsigned)a, (unsigned)b);
	if (gone) {
		cheatfile_written(&cs->file);  // nothing to write: no file is what it says
		listed_cache.valid = 0;
	}
	return gone;
}

// The halfword at offset in the game's PSRAM image becomes (old & ~mask) |
// (value & mask); PSRAM shows 8 MB a page
static void rom_patch(const struct cheat_patch *p)
{
	vu16 *at = (vu16 *)((u8 *)PSRAMBase_S98 + (p->offset & 0x7FFFFF));
	SetPSRampage((u16)((p->offset >> 23) * 0x1000));
	*at = (u16)((*at & ~p->mask) | (p->value & p->mask));
	SetPSRampage(0);
}

static const char *const status_names[] = {
	"new", "ok", "master", "too big", "unreadable", "placeholder", "can't run here", "does nothing", "a duplicate"};
_Static_assert(sizeof(status_names) / sizeof(status_names[0]) == CHEATFILE_SAME + 1, "a name for each");

// The first pass's ROM patches, for the second: the game's load takes PSRAM,
// where the text is, and all of pReadCache. With the names of the first
// cheats they come from, for the second pass's log.
#define KEPT_NAMES 8
#define KEPT_NAME_LEN 24
#define KEPT_NO_NAME 0xFF
static struct {
	u32 rom;  // the ROM path's CRC32: the second pass is the same game's
	u32 n;
	struct cheat_patch patches[CHEATSTORE_ROM_PATCHES];
	u16 number[CHEATSTORE_ROM_PATCHES];  // the cheat's N
	u8 name[CHEATSTORE_ROM_PATCHES];  // its name in names, or KEPT_NO_NAME
	u32 names_used;
	char names[KEPT_NAMES][KEPT_NAME_LEN];
} kept EWRAM_BSS;

static u32 path_crc(const char *path)
{
	return crc32(path, (u32)strlen(path));
}

// Keeps cheat c's ROM patches (in w) for the second pass: all of them (the
// caller has checked they fit)
static void keep_patches(const struct cheatfile *f, const struct cheatfile_cheat *c, const struct cheatfile_work *w)
{
	u8 name = KEPT_NO_NAME;
	TLOG_ASSERT(kept.n + w->n_patches <= CHEATSTORE_ROM_PATCHES);
	if (w->n_patches && kept.names_used < KEPT_NAMES) {
		name = (u8)kept.names_used++;
		snprintf(kept.names[name], KEPT_NAME_LEN, "%.*s", c->desc != CHEATFILE_NONE ? (int)c->desc_len : 0,
			 c->desc != CHEATFILE_NONE ? f->text + c->desc : "");
	}
	for (u32 p = 0; p < w->n_patches && kept.n < CHEATSTORE_ROM_PATCHES; p++) {
		kept.patches[kept.n] = w->patches[p];
		kept.number[kept.n] = c->number;
		kept.name[kept.n] = name;
		kept.n++;
	}
}

// The second pass: the kept ROM patches into the game in PSRAM (psram_bytes
// of it), but for any past its end or over the kernel's own patches
static void write_patches(const char *rom_path, u32 psram_bytes)
{
	u32 applied = 0, skipped = 0, guarded = 0;
	if (kept.rom != path_crc(rom_path)) {
		tlog_error("cheats: %s: its ROM patches weren't kept before the load: none written", rom_path);
		return;
	}
	for (u32 p = 0; p < kept.n; p++) {
		const struct cheat_patch *pt = &kept.patches[p];
		if (pt->offset + 2 > psram_bytes) {
			skipped++;
			continue;
		}
		// Not over the kernel's own patches: the patch area (the hook, the
		// interpreter, this table), the entry branch, the hook's words
		if (rom_guard_hits(pt->offset, 2)) {
			if (!guarded++ || tlog_wants(TLOG_DEBUG))
				tlog("cheats: cheat %u (%s): its ROM patch at %08lX is over the kernel's own, left out",
				     kept.number[p], kept.name[p] != KEPT_NO_NAME ? kept.names[kept.name[p]] : "?",
				     (unsigned long)0x08000000 + pt->offset);
			continue;
		}
		rom_patch(pt);
		applied++;
	}
	tlog("cheats: %lu ROM patches written into the game%s%s", (unsigned long)applied,
	     skipped ? ", some past its end left out" : "",
	     guarded ? ", some over the kernel's own patches left out" : "");
}

void cheatstore_launch_none(void)
{
	gl_cheat_count = gl_cheat_bytes = gl_cheat_patches = 0;
	ingame_cheats_clear();
}

void cheatstore_launch(const char *rom_path, u32 psram_bytes)
{
	if (psram_bytes) {
		write_patches(rom_path, psram_bytes);
		return;
	}
	u32 cost = CHEAT_LOAD(CHEAT_COST_CALL);
	cheatstore_launch_none();
	kept.rom = path_crc(rom_path);
	kept.n = kept.names_used = 0;
	if (!cheatstore_read(rom_path))
		return;
	struct cheatstore *cs = cheatstore();
	struct cheatfile *f = &cs->file;
	u32 picks = cheatstore_picks(), last_window = 0;
	for (u32 j = 0; j < cs->n_picks && cs->windows; j++)
		if (window_of(cs->picks[j].number) > last_window)
			last_window = window_of(cs->picks[j].number);
	// The in-game list: the picks (v0.10: only they), in file order
	u32 listed = 0;
	s32 i;
	// Every cheat up to the last pick, as keys carry from cheat to cheat; a
	// larger file's windows with picks in turn, the decoder carrying from
	// each to the next. start: the window's first cheat's place in the file
	// (the in-game list's index).
	for (u32 k = 0, start = 0;;) {
		u32 last = 0;  // past the window's last pick
		for (u32 n = 0; n < f->count; n++)
			if (cs->cheats[n].picked)
				last = n + 1;
		int done = 0;
		while ((i = cheatfile_next(f, &cs->work)) >= 0) {
			const struct cheatfile_cheat *c = &cs->cheats[i];
			int show = c->picked && cheatfile_shown(c) && listed < INGAME_CHEATS;
			u8 flags = INGAME_WHY_CANT;
			u32 bit = 0, bytes = cs->work.n_prog * 4;
			if (c->picked) {
				if (c->status != CHEATFILE_OK) {
					tlog("cheats: cheat %u (%.*s): %s, left out", c->number, c->desc_len,
					     f->text + c->desc, status_names[c->status]);
				} else if (gl_cheat_bytes + bytes > CHEATFILE_BYTES_MAX ||
					   cost + cs->work.cost > CHEAT_LOAD_MAX) {
					tlog("cheats: cheat %u (%.*s): its %lu bytes and load %lu don't fit beside %lu "
					     "and "
					     "%lu, "
					     "left out",
					     c->number, c->desc_len, f->text + c->desc, (unsigned long)bytes,
					     (unsigned long)cs->work.cost, (unsigned long)gl_cheat_bytes,
					     (unsigned long)cost);
					flags = INGAME_WHY_ROOM;
				} else if (kept.n + cs->work.n_patches > CHEATSTORE_ROM_PATCHES) {
					// Its ROM patches all kept, or the cheat left out whole:
					// never its ops running without the patches they go with
					tlog("cheats: cheat %u (%.*s): its %lu ROM patches don't fit beside %lu, left "
					     "out",
					     c->number, c->desc_len, f->text + c->desc,
					     (unsigned long)cs->work.n_patches, (unsigned long)kept.n);
					flags = INGAME_WHY_ROOM;
				} else {
					flags = INGAME_CHEAT_ON |
						(cs->work.n_patches ? INGAME_WHY_LOCKED : INGAME_WHY_NONE);
					if (bytes) {
						memcpy((u8 *)gl_cheat_table + gl_cheat_bytes, cs->work.prog, bytes);
						gl_cheat_bytes += bytes;
						cost += cs->work.cost;
						bit = gl_cheat_count++;
						flags |= INGAME_CHEAT_TABLE;
						if (bit >= INGAME_CHEATS)  // no toggle: always on
							flags = (u8)((flags & ~INGAME_CHEAT_WHY) | INGAME_WHY_LOCKED);
						else if (!(flags & INGAME_CHEAT_WHY) && cheatfile_runs_once(c))
							flags |= INGAME_CHEAT_RUN_ONCE;
					}
					gl_cheat_patches += cs->work.n_patches;
					keep_patches(f, c, &cs->work);
				}
			}
			if (show) {
				u32 desc = c->desc != CHEATFILE_NONE;
				ingame_cheat_put(listed, f->text + (desc ? c->desc : c->code),
						 desc ? c->desc_len : c->code_len, c->number, (u16)(start + (u32)i),
						 flags, (u8)bit, (flags & INGAME_CHEAT_TABLE) ? (u16)cs->work.cost : 0,
						 cheatfile_activation(c));
				listed++;
			}
			if (k >= last_window && (u32)i + 1 >= last) {
				done = 1;
				break;
			}
		}
		if (done || !cs->windows)
			break;
		// The next window with picks: each decodes from its index's state,
		// so the ones between are passed by
		do
			++k;
		while (k < cs->windows && !window_has_picks(k));
		if (k >= cs->windows)
			break;
		start = cheatstore_window_start(k);
		if (!window_read(k)) {
			tlog_error("cheats: %s: window %lu can't be read: the picks after it are left out", cs->path,
				   (unsigned long)k + 1);
			break;
		}
	}
	if (cs->work.next >= f->count)
		cheatstore_listed();  // decoded whole: the game popup's count, should the launch come back
	u32 pick_bytes = gl_cheat_bytes;
	if (gl_cheat_bytes) {
		gl_cheat_table[gl_cheat_bytes / 4] = 0;  // the table's end
		gl_cheat_bytes += 4;
	}
	ingame_cheats_set(listed, gl_cheat_count);
	tlog("cheats: %s: %lu picked, %lu run in game (%lu bytes, load %lu of %u), %lu ROM "
	     "patches; %lu listed in game",
	     cs->path, (unsigned long)picks, (unsigned long)gl_cheat_count, (unsigned long)pick_bytes,
	     (unsigned long)(gl_cheat_count ? cost : 0), CHEAT_LOAD_MAX, (unsigned long)gl_cheat_patches,
	     (unsigned long)listed);
}
