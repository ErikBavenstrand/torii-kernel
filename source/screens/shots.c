// The screenshot viewer (shots.h).
#include <stdio.h>
#include <string.h>

#include "shots.h"
#include "../actions.h"
#include "../Ezcard_OP.h"
#include "../ez_define.h"
#include "ff.h"
#include "../lang.h"
#include "../picture.h"
#include "../sdlist.h"
#include "../shotlist.h"
#include "../shotname.h"
#include "../tlog.h"
#include "../engine/cycles.h"
#include "../engine/frame.h"
#include "../engine/gfx.h"
#include "../engine/jobs.h"
#include "../engine/psram.h"
#include "../ui/dialog.h"
#include "../ui/ui.h"

extern u8 pReadCache[];

#define SV_MAX 1024  // pictures a list holds
#define SV_NAME_BYTES 256
#define SV_SLOTS 32  // decoded pictures kept in PSRAM page 2
#define SV_SLOT_BYTES 0xA000u
#define SV_OVERLAY_FRAMES 120  // the overlay goes after 2 seconds
#define SV_COPY_BYTES 0xA000u  // the VRAM queue's cap a VBlank while a picture lands
#define SV_REST_FRAMES 12  // resting this long on a picture decodes the next

_Static_assert(PIC_FIRST_COLOUR == UI_PICTURE_FIRST && PIC_COLOURS == UI_PICTURE_COLOURS,
	       "the decoder's colours are the picture widget's");

// --- Look, px ---------------------------------------------------------------------
#define BAR_Y 144  // the overlay strip, along the bottom
#define BAR_H 16
#define WHEN_X 8
#define WHEN_W 168
#define POS_W 56  // "999 of 999", from the right edge in by POS_R
#define POS_R 8

enum {
	N_PIC,
	N_BAR,
	N_WHEN,
	N_POS,
	N_NODES
};

enum {
	ACT_NONE,
	ACT_PREV,
	ACT_NEXT,
	ACT_OVERLAY,
	ACT_DELETE,
	ACT_BACK
};

// A decoded picture in PSRAM page 2's cache
struct sv_slot {
	u8 tiles[PIC_TILES_BYTES];
	u16 pal[PIC_COLOURS];
	u32 colours;
};
_Static_assert(sizeof(struct sv_slot) <= SV_SLOT_BYTES, "a cache slot");
_Static_assert(SHOTLIST_CACHE_OFFSET + SV_SLOTS * SV_SLOT_BYTES <= SHOTLIST_STAGE_OFFSET &&
		       SHOTLIST_STAGE_OFFSET + PIC_W * PIC_H * 2 <= SHOTLIST_NAMES_OFFSET &&
		       SHOTLIST_NAMES_OFFSET + SV_MAX * SV_NAME_BYTES <= SHOTLIST_TABLE_OFFSET,
	       "page 2's parts");

// pReadCache from READCACHE_SHOTS_STATE while the viewer runs
struct sv_state {
	struct sv_slot pic;  // shown, or on its way to VRAM
	struct pic_scratch scratch;  // within a decode
	FIL fil;
	u16 order[SV_MAX];  // each picture's name slot in page 2, in viewing order
	u16 tag[SV_SLOTS];  // the name slot + 1 each cache slot holds, 0 none
	u32 count, at;
	u32 ticket, overlay_until, rest;
	u8 game;  // a game's screenshots (dated names), else a folder's pictures
	u8 act, copying, landing, overlay, prefetched;
	u8 failed;  // the picture shown didn't decode: the overlay says why, and stays
	u8 back;  // the last step went to the one before
	u8 nodes;  // the view's first node
	// The read-ahead (a job): the decode under way, of the list's picture
	// ahead, from ahead_path, into pic; its file open in fil
	struct pic_dec dec;
	struct pic_src src;
	u32 ahead;
	u8 job, open;
	u8 adopt;  // a load takes the read-ahead's decode over
	u8 keeping;  // the read-ahead has decoded its picture, and is putting it in the cache
	u32 kept;  // bytes of it there
	char ahead_path[MAX_path_len + SV_NAME_BYTES];
	char dir[MAX_path_len];
	char stem[SHOTNAME_STEM_BYTES];
	char name[SV_NAME_BYTES];
	char path[MAX_path_len + SV_NAME_BYTES];
	char when[64], pos[32];
};
_Static_assert(READCACHE_SHOTS_STATE + sizeof(struct sv_state) <= MAX_pReadCache_size, "the viewer's state");
_Static_assert(READCACHE_SHOTS_TABLE + PIC_TABLE_BYTES <= READCACHE_SHOTS_STATE &&
		       READCACHE_SHOTS_TABLE >= READCACHE_STAGE + READCACHE_STAGE_SIZE,
	       "the decoder's table, clear of the stage: the read-ahead's steps run between frames");

// Within the list's read, from READCACHE_SHOTS, over the stage
struct sv_list_work {
	DIR dir;
	FILINFO info;
	struct shotname_when when[SV_MAX];
	char name[SV_NAME_BYTES] __attribute__((aligned(4)));
};
_Static_assert(READCACHE_SHOTS + sizeof(struct sv_list_work) <= READCACHE_SHOTS_STATE, "the list's read");

static struct sv_state *sv(void)
{
	return (struct sv_state *)(void *)(pReadCache + READCACHE_SHOTS_STATE);
}

static struct sv_list_work *list_work(void)
{
	return (struct sv_list_work *)(void *)(pReadCache + READCACHE_SHOTS);
}

static u8 *table(void)
{
	return pReadCache + READCACHE_SHOTS_TABLE;
}

static u8 *page2(u32 offset)
{
	return (u8 *)(PSRAM_WINDOW + offset);
}

// --- The list ------------------------------------------------------------------------

// Name slot k's name, from RAM (halfword-aligned, SV_NAME_BYTES); 0 if PSRAM refused it
static int name_put(u32 k, const char *name)
{
	SetPSRampage(SHOTLIST_PAGE);
	int ok = psram_write(page2(SHOTLIST_NAMES_OFFSET + k * SV_NAME_BYTES), name, SV_NAME_BYTES);
	SetPSRampage(PSRAM_ASSET_PAGE);
	return ok;
}

static void name_get(u32 k, char *out)
{
	SetPSRampage(SHOTLIST_PAGE);
	memcpy(out, page2(SHOTLIST_NAMES_OFFSET + k * SV_NAME_BYTES), SV_NAME_BYTES);
	SetPSRampage(PSRAM_ASSET_PAGE);
	out[SV_NAME_BYTES - 1] = 0;
}

// The game's screenshots, newest first
static void list_game(void)
{
	struct sv_state *s = sv();
	struct sv_list_work *w = list_work();
	u32 n = 0, more = 0;
	if (f_opendir(&w->dir, SHOTLIST_DIR) == FR_OK) {
		for (;;) {
			if (f_readdir(&w->dir, &w->info) != FR_OK || !w->info.fname[0])
				break;
			struct shotname_when when;
			if ((w->info.fattrib & AM_DIR) || !shotname_match(w->info.fname, s->stem, &when))
				continue;
			if (n == SV_MAX) {
				more++;
				continue;
			}
			w->when[n] = when;
			memset(w->name, 0, sizeof(w->name));
			memcpy(w->name, w->info.fname, strnlen(w->info.fname, SV_NAME_BYTES - 1));
			if (!name_put(n, w->name))
				break;
			s->order[n] = (u16)n;
			n++;
		}
		f_closedir(&w->dir);
	}
	// Newest first (Shell's sort: no recursion)
	for (u32 gap = n / 2; gap; gap /= 2)
		for (u32 i = gap; i < n; i++) {
			u16 v = s->order[i];
			u32 j = i;
			for (; j >= gap && shotname_cmp(&w->when[s->order[j - gap]], &w->when[v]) < 0; j -= gap)
				s->order[j] = s->order[j - gap];
			s->order[j] = v;
		}
	s->count = n;
	tlog("shots: %lu of %s%s", (unsigned long)n, s->stem, more ? " (more left out)" : "");
}

// The folder listing's .bmp files, in its order, from the one named name
static void list_folder(const struct sdlist *list, const char *name)
{
	struct sv_state *s = sv();
	struct sv_list_work *w = list_work();
	u32 n = 0;
	s->at = 0;
	for (u32 i = 0; i < list->count && n < SV_MAX; i++) {
		const struct sdlist_rec *r = sdlist_at(list, i);
		const char *rn = sdlist_name(r);
		if (sdlist_is_dir(r) || !shotname_is_bmp(rn))
			continue;
		memset(w->name, 0, sizeof(w->name));
		memcpy(w->name, rn, strnlen(rn, SV_NAME_BYTES - 1));  // from page 0, before page 2 is selected
		if (!name_put(n, w->name))
			break;
		if (!strcmp(w->name, name))
			s->at = n;
		s->order[n] = (u16)n;
		n++;
	}
	s->count = n;
	tlog("shots: %lu pictures in %s", (unsigned long)n, s->dir);
}

// --- A picture --------------------------------------------------------------------

static int read_fil(void *ctx, u32 offset, void *buf, u32 n)
{
	FIL *f = ctx;
	UINT got = 0;
	if (f_tell(f) != offset && f_lseek(f, offset) != FR_OK)
		return 0;
	return f_read(f, buf, n, &got) == FR_OK && got == n;
}

// The picture's lines between its passes, in PSRAM page 2
static int put_line(void *ctx, u32 y, const u16 *line)
{
	(void)ctx;
	SetPSRampage(SHOTLIST_PAGE);
	int ok = psram_write(page2(SHOTLIST_STAGE_OFFSET + y * PIC_W * 2), line, PIC_W * 2);
	SetPSRampage(PSRAM_ASSET_PAGE);
	return ok;
}

static void get_line(void *ctx, u32 y, u16 *line)
{
	(void)ctx;
	SetPSRampage(SHOTLIST_PAGE);
	memcpy(line, page2(SHOTLIST_STAGE_OFFSET + y * PIC_W * 2), PIC_W * 2);
	SetPSRampage(PSRAM_ASSET_PAGE);
}

// The list's picture at: its name into name, its path into path
static void name_path(u32 at, char *name, char *path)
{
	struct sv_state *s = sv();
	name_get(s->order[at], name);
	u32 dl = (u32)strlen(s->dir);
	snprintf(path, sizeof(s->path), "%s%s%s", s->dir, dl && s->dir[dl - 1] == '/' ? "" : "/", name);
}

// 1 if the list's picture at is in the cache
static int cached(u32 at)
{
	u32 k = sv()->order[at];
	return sv()->tag[k % SV_SLOTS] == k + 1;
}

// s->pic into the cache, as the list's picture at
// bytes more of s->pic into the cache from s->kept, as the list's
// picture at; 1 once it is all there
static int keep_some(u32 at, u32 bytes)
{
	struct sv_state *s = sv();
	u32 k = s->order[at], c = k % SV_SLOTS, n = sizeof(s->pic) - s->kept;
	n = n < bytes ? n : bytes;
	if (!s->kept)
		s->tag[c] = 0;  // half written, it holds nothing
	SetPSRampage(SHOTLIST_PAGE);
	int ok = psram_write(page2(SHOTLIST_CACHE_OFFSET + c * SV_SLOT_BYTES) + s->kept, (const u8 *)&s->pic + s->kept,
			     n);
	SetPSRampage(PSRAM_ASSET_PAGE);
	s->kept = ok ? s->kept + n : sizeof(s->pic) + 1;
	if (s->kept < sizeof(s->pic))
		return 0;
	if (s->kept == sizeof(s->pic))
		s->tag[c] = (u16)(k + 1);
	s->kept = 0;
	return 1;
}

static void keep(u32 at)
{
	sv()->kept = 0;
	keep_some(at, sizeof(struct sv_slot));
}

// Opens path and starts decoding it into s->pic (s->dec)
static int dec_begin(const char *path)
{
	struct sv_state *s = sv();
	if (f_open(&s->fil, path, FA_READ) != FR_OK)
		return PIC_BROKEN;
	s->open = 1;
	s->src = (struct pic_src){read_fil, put_line, get_line, &s->fil, (u32)f_size(&s->fil)};
	return pic_begin(&s->dec, &s->src, &s->scratch, table(), s->pic.tiles, s->pic.pal);
}

static void dec_close(void)
{
	struct sv_state *s = sv();
	if (s->open)
		f_close(&s->fil);
	s->open = 0;
}

// The read-ahead's steps: the picture at s->ahead decoded a few lines at a
// time, between frames, into the cache
#define AHEAD_LINES 2
#define AHEAD_KEEP_BYTES 0x2000
static int ahead_step(struct job *job)
{
	struct sv_state *s = sv();
	int r;
	frame_measure("shots-ahead");
	if (job->state == JOB_CANCEL) {
		if (!s->adopt)
			dec_close();
		return JOB_DONE;
	}
	if (job->state == 2)  // decoded: into the cache, a piece a step
		return keep_some(s->ahead, AHEAD_KEEP_BYTES) ? JOB_DONE : JOB_MORE;
	if (job->state == 0) {
		job->state = 1;
		r = dec_begin(s->ahead_path);
		if (r == PIC_OK)
			return JOB_MORE;
	} else {
		r = pic_step(&s->dec, AHEAD_LINES);
	}
	if (r == PIC_MORE)
		return JOB_MORE;
	dec_close();
	s->pic.colours = s->dec.colours;
	if (r != PIC_OK)
		return JOB_DONE;
	tlog("shots: ahead: %s, %lu colours%s", s->ahead_path, (unsigned long)s->pic.colours,
	     s->dec.info.exact ? ""
	     : s->dec.dither   ? " (cut, dithered)"
			       : " (cut)");
	job->state = 2;
	s->kept = 0;
	s->keeping = 1;
	return JOB_MORE;
}

static void ahead_stop(void)
{
	struct sv_state *s = sv();
	if (job_running(s->job))
		job_cancel(s->job);
	s->job = JOB_NONE;
}

// The list's picture at into s->pic, and into the cache; name_path(at)
// first. PIC_OK, or why it doesn't show (s->pic then black). A read-ahead
// of it goes on here to its end; one of another stops.
static int load(u32 at)
{
	struct sv_state *s = sv();
	u32 k = s->order[at], c = k % SV_SLOTS, start = cycles_now();
	int r = PIC_OK, adopted = 0;
	if (job_running(s->job) && s->ahead == at && s->keeping) {
		while (!keep_some(at, sizeof(struct sv_slot)))  // decoded already: the rest kept now
			;
		s->adopt = 1;
		job_cancel(s->job);
		s->adopt = 0;
		s->job = JOB_NONE;
		tlog("shots: %s: the read-ahead's picture", s->name);
		return PIC_OK;
	}
	if (job_running(s->job) && s->ahead == at && s->open) {
		s->adopt = 1;  // its decode is this one's: the job ends, its file stays open
		job_cancel(s->job);
		s->adopt = 0;
		adopted = 1;
		tlog("shots: %s: the read-ahead's decode goes on", s->name);
	}
	ahead_stop();
	if (s->tag[c] == k + 1) {
		SetPSRampage(SHOTLIST_PAGE);
		memcpy(&s->pic, page2(SHOTLIST_CACHE_OFFSET + c * SV_SLOT_BYTES), sizeof(s->pic));
		SetPSRampage(PSRAM_ASSET_PAGE);
		tlog("shots: %s from the cache in %lu cycles", s->name, (unsigned long)cycles_since(start));
		return PIC_OK;
	}
	if (!adopted)
		r = dec_begin(s->path);
	while (r == PIC_OK && (r = pic_step(&s->dec, PIC_H)) == PIC_MORE)
		r = PIC_OK;
	dec_close();
	s->pic.colours = s->dec.colours;
	u32 cycles = cycles_since(start);
	if (r != PIC_OK) {
		tlog("shots: %s: %s", s->path, r == PIC_UNSUPPORTED ? "a kind it doesn't show" : "not a picture");
		memset(s->pic.tiles, PIC_FIRST_COLOUR, sizeof(s->pic.tiles));
		s->pic.pal[0] = 0;  // black
		s->pic.colours = 1;
		return r;
	}
	const struct pic_info *info = &s->dec.info;
	tlog("shots: %s: %lux%lu, %u bits, %lu colours%s, in %lu frames (%lu cycles)", s->name, (unsigned long)info->w,
	     (unsigned long)info->h, info->bpp, (unsigned long)s->pic.colours,
	     info->exact     ? ""
	     : s->dec.dither ? " (cut, dithered)"
			     : " (cut)",
	     (unsigned long)cycles / ENGINE_FRAME_CYCLES + 1u, (unsigned long)cycles);
	keep(at);
	return PIC_OK;
}

// The overlay's left text: the shot's date and time, else its name
static void when_text(int r)
{
	struct sv_state *s = sv();
	struct shotname_when w;
	if (r != PIC_OK) {
		snprintf(s->when, sizeof(s->when), "%s",
			 S(r == PIC_UNSUPPORTED ? STR_SHOTS_UNSUPPORTED : STR_SHOTS_BROKEN));
		return;
	}
	if (s->game && shotname_match(s->name, s->stem, &w) && w.year) {
		char date[32], time[8];
		u32 n = lang_date(date, sizeof(date), w.day, w.month);
		snprintf(date + n, sizeof(date) - n, " %u", w.year);
		snprintf(time, sizeof(time), "%02u:%02u", w.hour, w.min);
		SF(s->when, sizeof(s->when), STR_SHOTS_WHEN, LS(date), LS(time));
		return;
	}
	u32 len = (u32)strlen(s->name);
	snprintf(s->when, sizeof(s->when), "%.*s", (int)(shotname_is_bmp(s->name) ? len - 4 : len), s->name);
}

static void overlay_set(int on)
{
	struct sv_state *s = sv();
	s->overlay = (u8)on;
	s->overlay_until = torii_frames + SV_OVERLAY_FRAMES;  // VBlanks: a decode ahead runs no frame
	ui_show((u8)(s->nodes + N_BAR), on);
	frame_measure("shots-overlay");
}

// Shows the list's picture at s->at: decoded (no frame runs meanwhile, so
// the one before stays up), its colours black while its tiles land
static void show(void)
{
	static const u16 black[PIC_COLOURS] = {0};
	struct sv_state *s = sv();
	name_path(s->at, s->name, s->path);
	int r = load(s->at);
	when_text(r);
	s->failed = r != PIC_OK;
	s->prefetched = 0;
	SF(s->pos, sizeof(s->pos), STR_SHOTS_POSITION, LN(s->at + 1), LN(s->count));
	ui_label_set((u8)(s->nodes + N_WHEN), s->when);
	ui_label_set((u8)(s->nodes + N_POS), s->pos);
	gfx_pal_load(UI_BANKS, black, PIC_COLOURS);
	s->copying = 1;
	overlay_set(1);
}

// Each frame: the tiles into the queue, then the colours once they have landed
static void poll(void)
{
	struct sv_state *s = sv();
	if (s->copying && ui_picture_show((u8)(s->nodes + N_PIC), s->pic.tiles)) {
		s->copying = 0;
		s->landing = 1;
		s->ticket = gfx_queue_ticket();
		gfx_commit_max_bytes = SV_COPY_BYTES;  // whole in a VBlank or two
	}
	if (s->landing) {
		frame_measure("shots-show");
		if (gfx_queue_done(s->ticket)) {
			s->landing = 0;
			gfx_commit_max_bytes = GFX_COMMIT_BYTES;
			gfx_pal_load(UI_BANKS, s->pic.pal, s->pic.colours);
			tlog_debug("screen ready: shot-picture %lu", (unsigned long)s->at + 1u);
		}
	}
	if (s->overlay && !s->landing && !s->failed && (s32)(torii_frames - s->overlay_until) >= 0)
		overlay_set(0);
	// Resting on a picture: the next one the way it went decoded into the
	// cache, so that step shows at once
	if (s->landing || s->copying || ui_input()->held)
		s->rest = frame_count();
	else if (!s->prefetched && s->count > 1 && frame_count() - s->rest >= SV_REST_FRAMES) {
		s->prefetched = 1;
		u32 next = (s->at + (s->back ? s->count - 1 : 1)) % s->count;
		if (!cached(next)) {
			char name[SV_NAME_BYTES];
			name_path(next, name, s->ahead_path);
			s->ahead = next;
			s->keeping = 0;
			s->job = job_start(ahead_step, NULL, ui_owner(ui_view_at(ui_depth() - 1)));
		}
	}
}

// --- The screen ---------------------------------------------------------------------

static int sv_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	if (e->type != UI_EV_KEYS)
		return 0;
	u16 k = e->keys;
	struct sv_state *s = sv();
	s->act = k & INPUT_LEFT    ? ACT_PREV
		 : k & INPUT_RIGHT ? ACT_NEXT
		 : k & INPUT_A     ? ACT_OVERLAY
		 : k & INPUT_START ? ACT_DELETE
		 : k & INPUT_B     ? ACT_BACK
				   : ACT_NONE;
	return s->act != ACT_NONE;
}

static const struct ui_panel_config bar_cfg = {UI_PANEL_FLAT};
static const struct ui_label_config when_cfg = {"", UI_FONT_BODY, UI_ALIGN_LEFT, UI_LABEL_SWAP, 0};
static const struct ui_label_config pos_cfg = {"", UI_FONT_BODY, UI_ALIGN_RIGHT, UI_LABEL_SWAP, 0};

// The picture on plane 1 (8bpp, character block 1), the overlay in front
// of it on plane 0, which the confirmation takes while it is up
static const struct ui_spec sv_nodes[N_NODES] = {
	[N_PIC] = {UI_PICTURE, 1, UI_NONE, 1, UI_LEFT, 0, 0, 0, 0, 0, PIC_W, PIC_H, NULL},
	[N_BAR] = {UI_PANEL, 2, UI_NONE, 0, UI_LEFT, 0, 0, UI_ROLE_PANEL, 0, BAR_Y, PIC_W, BAR_H, &bar_cfg},
	[N_WHEN] = {UI_LABEL, 3, N_BAR, 0, UI_LEFT, 0, UI_ROLE_TEXT, UI_ROLE_PANEL, WHEN_X, 0, WHEN_W, BAR_H,
		    &when_cfg},
	[N_POS] = {UI_LABEL, 4, N_BAR, 0, UI_RIGHT, 0, UI_ROLE_DIM, UI_ROLE_PANEL, POS_R, 0, POS_W, BAR_H, &pos_cfg},
};

static const struct ui_view_spec sv_view = {"shot-viewer", sv_nodes, N_NODES, UI_NONE, UI_VIEW_LR_PAGES, sv_event};

static int push(void)
{
	struct ui_view *v = ui_push(&sv_view, NULL);
	if (!v) {
		tlog_error("shots: %s", ui_error());
		return 0;
	}
	sv()->nodes = v->first;
	show();
	return 1;
}

// START: the picture off the card, after a question; 0 once none is left
static int delete_shown(void)
{
	struct sv_state *s = sv();
	struct ui_confirm ask = {S(STR_SHOTS_DELETE_ASK), S(STR_SHOTS_DELETE_TEXT), S(STR_SHOTS_DELETE), 1};
	if (!ui_confirm(UI_NONE, &ask))
		return 1;
	ahead_stop();  // the delete borrows pReadCache where the decode's table is
	if (!action_delete_file(s->path)) {
		dialog_message(NULL, S(STR_SHOTS_DELETE_FAILED), DIALOG_ERROR);
		return 1;
	}
	shotlist_forget();
	memmove(&s->order[s->at], &s->order[s->at + 1], (s->count - s->at - 1) * sizeof(s->order[0]));
	if (!--s->count)
		return 0;
	if (s->at == s->count)
		s->at--;
	show();
	return 1;
}

static void run(void)
{
	struct sv_state *s = sv();
	if (!s->count) {
		dialog_message(NULL, S(STR_SHOTS_NONE), DIALOG_INFO);
		return;
	}
	if (!push())
		return;
	for (;;) {
		u32 act;
		s->act = ACT_NONE;
		do {
			ui_frame();
			poll();
			act = s->act;  // the event handler's, in ui_frame
		} while (ui_depth() && act == ACT_NONE);
		if (!ui_depth() || act == ACT_BACK)
			break;
		if (act == ACT_PREV || act == ACT_NEXT) {
			u32 key = act == ACT_PREV ? INPUT_LEFT : INPUT_RIGHT;
			u32 to = input_move(ui_input(), (u16)key, s->at, s->count, act == ACT_PREV ? -1 : 1);
			s->back = act == ACT_PREV;
			if (to != s->at) {
				s->at = to;
				show();
			}
		} else if (act == ACT_OVERLAY) {
			overlay_set(!s->overlay);
		} else if (act == ACT_DELETE && !delete_shown()) {
			break;
		}
	}
	ahead_stop();
	gfx_commit_max_bytes = GFX_COMMIT_BYTES;
	ui_leave(UI_TR_DEEPER, -1);  // back to the caller's screen
	while (ui_depth())
		ui_pop();
}

// The caller's screen out, its views gone, the state fresh
static void begin(const char *dir)
{
	ui_leave(UI_TR_DEEPER, 1);
	while (ui_depth())
		ui_pop();
	struct sv_state *s = sv();
	memset(s, 0, sizeof(*s));
	s->job = JOB_NONE;
	snprintf(s->dir, sizeof(s->dir), "%s", dir);
}

void shots_run_game(const char *game)
{
	begin(SHOTLIST_DIR);
	struct sv_state *s = sv();
	s->game = 1;
	shotlist_stem(s->stem, sizeof(s->stem), game);
	list_game();
	run();
}

void shots_run_folder(const char *folder, const struct sdlist *list, const char *name)
{
	begin(folder);
	list_folder(list, name);
	run();
}
