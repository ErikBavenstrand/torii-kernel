// The hardware side of the UI (ui.h): the frame loop, planes, windows and
// blending, the VRAM staging area, palettes and fades, and DIM.
#include <gba_systemcalls.h>
#include <string.h>

#include "ui_priv.h"
#include "art.h"
#include "keyq.h"
#include "../engine/cycles.h"
#include "../engine/frame.h"
#include "../engine/gfx.h"
#include "../engine/jobs.h"
#include "../engine/res.h"
#include "../Ezcard_OP.h"
#include "../ez_define.h"
#include "../tlog.h"

#define REG_VCOUNT_ (*(vu16 *)0x04000006)

// DISPCNT bits
#define DISP_MODE0_OBJ1D 0x0040
#define DISP_BG(p) (0x0100 << (p))
#define DISP_OBJ 0x1000
#define DISP_WIN0 0x2000
#define DISP_WIN1 0x4000
// Window and blend layer bits
#define LAYER_BG(p) (1u << (p))
#define LAYER_OBJ 0x10
#define LAYER_BD 0x20
#define LAYER_ALL 0x1F
#define WIN_EFFECTS 0x20
#define BLEND_ALPHA 0x0040
#define BLEND_BRIGHTEN 0x0080
#define BLEND_DARKEN 0x00C0
// Sprite attributes (the zoom's sprite)
#define OBJ_AFFINE 0x0100
#define OBJ_DOUBLE 0x0200
#define OBJ_SIZE_64 0xC000
#define OBJ_MATRIX(m) ((m) << 9)
#define ZOOM_PRIO 3  // the zoom's sprite: under every plane (it shares the screen only with the title bar's)

// Screen transitions (ui_leave)
enum {
	TR_IDLE,
	TR_OUT,   // the screen goes out (ui_leave runs these frames)
	TR_GAP,   // nothing shows: the next screen is set up and drawn
	TR_IN,    // the next screen comes in; it takes keys already (not A, START
		  // and SELECT pressed before: ui_frame drops those)
};
// How a transition fades
enum {
	TR_ALPHA,    // the screen's planes over the backdrop
	TR_DARKEN,   // BLDY towards black: under a dialog, and out of the engine
};
// The open and close zoom (UI_TR_OPEN, UI_TR_CLOSE): which, and its phase
enum {
	ZOOM_NONE,
	ZOOM_OPEN,
	ZOOM_CLOSE,
};
enum {
	ZP_SPRITE,  // the sprite zooms (no page shows but the open's title bar)
	ZP_PAGE,    // the page's card comes in (open) or goes (close)
};
// Frames a zoom waits for its screen's uploads at most: one that keeps
// streaming (covers, a marquee) shows anyway
#define ZOOM_READY_WAIT 60
// The theme wipe's split: the theme's BG run, then the UI's sprite bank
#define WIPE_BG_WORDS (UI_THEME_RUN_COLOURS / 2)
#define WIPE_OBJ_WORDS 8
_Static_assert(WIPE_BG_WORDS + WIPE_OBJ_WORDS <= GFX_SPLIT_WORDS, "the wipe's colours fit the split's IWRAM");
// A modal view fades in over the first third of its opening, then dims what
// is under it (one blend at a time); closing runs it backwards
#define OPEN_FADE (UI_OPEN / 3)

// A page's fade-in (ui_plane_reveal), or the old page's fade-out before it
// (ui_plane_fade_out)
enum {
	REVEAL_OFF,
	REVEAL_ASKED,    // the next regs show the plane at its first step
	REVEAL_WRITTEN,  // those regs wait for their commit (DISPCNT may be held)
	REVEAL_RUN,      // on screen: one step a frame
};

#define TAG_PALETTE 0x5500
#define TAG_OBJ_PALETTE 0x5501
#define TAG_SHARED 0x5510  // + character block
#define BGCNT_32X64 0x8000  // a list's map (UI_RING_MAP_LINES)
#define PRIO_NONE 0xFF      // a view whose sprites don't show
#define STAGE_WORDS 512    // the staging area: one 2 KB screenblock
#define STAGE_SPANS 8      // map row spans staged until the queue empties

// Map rows staged: copy holds rows rows of map from ty on, staged in frame
struct stage_span {
	u16 *map;
	u16 *copy;
	u32 frame;
	u8 rows;
};

// Frames the cycle budget leaves for jobs: what draw doesn't need
#define JOB_DEADLINE (ENGINE_FRAME_CYCLES / 2)

static struct {
	InputState in;
	u16 *stage;        // the staging area in VRAM
	u16 stage_used;    // words given out since the queue was last empty
	u8 stage_map;      // its res handle
	u8 palette;        // res handle of the theme's banks (UI_BANKS)
	u8 obj_palette;    // and of the sprites' bank (bank 0's roles)
	u8 shared[3];      // shared tiles of each character block planes use
	u16 shared_first[3];
	struct stage_span spans[STAGE_SPANS];
	u8 span_count;
	// Priorities this frame (obj_levels): each view's planes from level[d]
	// on, its front planes there and the rest (and its sprites, unless
	// obj_prio[d] is PRIO_NONE) a level down
	u8 level[UI_VIEW_CAP];
	u8 obj_prio[UI_VIEW_CAP];
	u8 obj_lower;      // sprites of views under the topmost animated one show: its blends take them
	u8 obj_top;        // that view (or one over it) shows sprites
	u8 hidden;         // planes hidden for a page change
	u8 hide_pending;   // hidden, but no DISPCNT without them written yet
	u32 hide_frame[UI_PLANES];  // the frame whose regs hid them
	u8 pop_wait;       // 1: popped, regs not updated; 2: updated in pop_frame
	u8 lists_hidden;   // planes of lists hidden with ui_show
	u16 wake_keys;     // keys that woke the screen from DIM, ignored until let go
	// Keys fired while a screen went out or a dialog closed, for the next
	// view, and keys queued for the next frames (keyq.h)
	struct keyq keys;
	u32 pop_frame;
	// A page's fade-in or fade-out
	u8 reveal;        // REVEAL_*
	u8 reveal_out;    // 1: the fade-out
	u8 reveal_plane;
	u8 reveal_step;   // frames of it shown, of reveal_frames
	u8 reveal_frames; // the theme's page_fade (page_fade_out) when it started
	u32 reveal_frame; // the frame whose regs showed the plane
	// Screen transitions: the half running goes from level, x and y 0 to 1
	// (level 0 shown, UI_OPEN gone; x, y the screen's offset in px)
	u8 tr_phase;      // TR_*
	u8 tr_mode;       // TR_ALPHA, TR_DARKEN
	u8 tr_effects;    // UI_TR_* of the transition running
	u8 tr_frames;     // of the half running
	u8 tr_step;       // frames of it shown
	u8 tr_wait;       // TR_IN: the regs showing the screen are not on screen yet
	u8 tr_in_frames;  // for the half coming in
	u8 capture;       // ui_close: keys wait for the view under it (keys.held)
	u8 gap_regs;      // the last regs show no view (a screen gone)
	u8 tr_pushed;     // TR_GAP: a view was pushed since the screen went
	u8 active;        // between ui_init and ui_shutdown
	s16 tr_level, tr_dx, tr_dy;
	s16 tr_l0, tr_l1, tr_x0, tr_x1, tr_y0, tr_y1;
	s16 tr_in_x, tr_in_y;  // where the screen coming in starts
	u32 tr_frame;     // the frame whose regs showed the screen coming in
	u32 shown_frame[UI_VIEW_CAP];  // the frame whose regs showed each view
	s16 last_dim;     // the dim under a dialog in the last regs (BLDY)
	s16 carry_dim;    // the dim a dialog popped since then left
	// Fades
	MotionId fade;
	u16 fade_colour;
	// DIM and the modal dim
	u32 idle;          // frames with no key held
	u32 dim_after;     // frames idle before dimming; 0 off
	MotionId dim_motion;
	s16 dim_y;         // BLDY of DIM (0-16)
	u8 dimmed;
	// A popup's, a confirmation's or the letter card's window, slide and dim
	struct ui_overlay ov;
	// The open and close zoom: the sprite ui_zoom_set gave, the zoom running
	struct ui_zoom zoom_from;
	u8 zoom_set;      // zoom_from is for the next ui_leave
	u8 zoom;          // ZOOM_*
	u8 zoom_phase;    // ZP_*
	u8 zoom_started;  // the sprite's tiles have landed: its frames count
	u8 zoom_ready;    // the screen it waits for is drawn and in VRAM
	u16 zoom_frame;   // frames of the phase
	u16 zoom_total;   // frames of the open's title bar, since it showed
	u8 zoom_slide;    // and the frames its slide takes: what the open has left then
	u32 zoom_ticket;  // the sprite's tile copy
	u8 zoom_tiles_h, zoom_bank_h, zoom_matrix_h;  // its resources, taken in ui_init
	u16 zoom_tiles;
	u8 zoom_bank, zoom_matrix;
	u8 zoom_eva;      // this frame's: the page's blend level (0-16)
	s16 zoom_title;   // px its title bar is moved right (negative: left)
	u8 zoom_eva0;     // where a close starts from (an open cut short)
	s16 zoom_dy0, zoom_title0;
	// The theme wipe
	const struct ui_theme *wipe_from, *wipe_to;
	u8 wipe;          // 0 off, 1 asked (its first frame loads the theme), 2 running
	s8 wipe_dir;      // 1 down to wipe_to, -1 back up to wipe_from
	u8 wipe_step;     // frames of it, 0 to its frames
} ui EWRAM_BSS;

void ui_wake(void)
{
	frame_wake();
}

void ui_queue_keys(u16 keys)
{
	keyq_queue(&ui.keys, keys);
	ui_wake();
}

const InputState *ui_input(void)
{
	return &ui.in;
}

const struct torii_font *ui_font(u32 which)
{
	const struct ui_theme *t = ui_theme();
	return which == UI_FONT_HEADING ? t->heading : t->body;
}

// --- Shared tiles -----------------------------------------------------------

// One 4bpp tile row: nibble x is pixel x
static u32 row_of(const u8 *px)
{
	u32 w = 0;
	for (u32 x = 0; x < 8; x++)
		w |= (u32)(px[x] & 15) << (4 * x);
	return w;
}

// The outline and rounded tiles: a top-left corner, mirrored for the others
static void corner(u32 *tile, int round, int mirror_x, int mirror_y)
{
	// Distance from the corner's edges decides each pixel
	for (u32 y = 0; y < 8; y++) {
		u8 px[8];
		for (u32 x = 0; x < 8; x++) {
			u32 cx = mirror_x ? 7 - x : x, cy = mirror_y ? 7 - y : y;
			u8 v;
			if (round) {
				// A 4 px radius: transparent outside the arc
				u32 dx = cx < 4 ? 4 - cx : 0, dy = cy < 4 ? 4 - cy : 0;
				v = dx * dx + dy * dy > 16 ? UI_ROLE_NONE : UI_ROLE_PANEL;
			} else {
				v = cx == 0 || cy == 0 ? UI_ROLE_BORDER : UI_ROLE_PANEL;
			}
			px[x] = v;
		}
		tile[y] = row_of(px);
	}
}

static void edge(u32 *tile, u32 side)
{
	// side 0 top, 1 bottom, 2 left, 3 right
	for (u32 y = 0; y < 8; y++) {
		u8 px[8];
		for (u32 x = 0; x < 8; x++) {
			int on = (side == 0 && y == 0) || (side == 1 && y == 7) || (side == 2 && x == 0) ||
				 (side == 3 && x == 7);
			px[x] = on ? UI_ROLE_BORDER : UI_ROLE_PANEL;
		}
		tile[y] = row_of(px);
	}
}

static void make_shared(u32 *tiles)
{
	for (u32 role = 0; role < 10; role++)
		for (u32 y = 0; y < 8; y++)
			tiles[(UI_TILE_SOLID + role) * 8 + y] = role * 0x11111111u;
	for (u32 i = 0; i < 4; i++) {
		corner(&tiles[(UI_TILE_BORDER + i) * 8], 0, (int)(i & 1), (int)(i >> 1));
		corner(&tiles[(UI_TILE_ROUND + i) * 8], 1, (int)(i & 1), (int)(i >> 1));
		edge(&tiles[(UI_TILE_BORDER + 4 + i) * 8], i);
	}
}

u16 ui_shared_entry(u32 plane, u32 tile)
{
	return ui_entry(ui.shared_first[ui_plane_cb(plane)] + tile, UI_BANK_NORMAL);
}

u8 ui_tiles(u8 node, u32 count, u16 tag, u16 *first)
{
	u32 cb = ui_plane_cb(ui_node_spec(node)->plane);
	// Block 2's (plane 3's) from the top of its reach, as its shared tiles
	// are: from the bottom, they would split the long run block 1's list
	// takes from [512, 1536)
	u16 lo = (u16)(cb * 512);
	u8 h = res_alloc(RES_BG_VRAM, (u16)count, 1, lo, (u16)(lo + 1024), tag, cb == 2 ? POOL_TOP : 0);
	*first = h == RES_NONE ? 0 : res_bg_tile_index(h, 0, (u8)cb);
	return h;
}

u16 *ui_map(u8 node)
{
	return res_bg_addr(ui_node_view(node)->maps[ui_node_spec(node)->plane]);
}

int ui_node_shown(u8 node)
{
	return (ui_node_view(node)->flags & UI_V_SHOWN) != 0;
}

int ui_map_busy(u8 node, u32 ty, u32 rows)
{
	u16 *map = ui_map(node) + ty * 32, *end = map + rows * 32;
	for (u32 i = 0; i < ui.span_count; i++) {
		const struct stage_span *s = &ui.spans[i];
		if (s->frame != frame_count() && s->map < end && map < s->map + s->rows * 32)
			return 1;
	}
	return 0;
}

u16 *ui_map_rows(u8 node, u32 ty, u32 rows)
{
	u16 *map = ui_map(node) + ty * 32, *end = map + rows * 32;
	if (!ui_node_shown(node))
		return map;
	// Rows staged this frame are changed in their copy (all of it, or
	// merged into a new one); rows staged in an earlier frame whose copy
	// may still be waiting in the queue are left until it has landed
	for (u32 i = 0; i < ui.span_count; i++) {
		const struct stage_span *s = &ui.spans[i];
		u16 *s_end = s->map + s->rows * 32;
		if (s->map >= end || map >= s_end)
			continue;
		if (s->frame != frame_count()) {
			ui_wake();
			return 0;
		}
		if (map >= s->map && end <= s_end)
			return s->copy + (map - s->map);
	}
	u32 words = rows * 16;
	if (ui.stage_used + words > STAGE_WORDS || ui.span_count == STAGE_SPANS ||
	    gfx_queue_len() >= GFX_QUEUE_CAP - 2) {
		ui_wake();
		return 0;
	}
	u16 *copy = ui.stage + ui.stage_used * 2;
	// The VRAM to VRAM copy keeps the entries the caller doesn't change
	CpuFastSet(map, copy, words);
	// and rows staged this frame by others come from their copies
	for (u32 i = 0; i < ui.span_count; i++) {
		const struct stage_span *s = &ui.spans[i];
		u16 *s_end = s->map + s->rows * 32;
		u16 *a = s->map > map ? s->map : map, *b = s_end < end ? s_end : end;
		if (a < b)
			memcpy(copy + (a - map), s->copy + (a - s->map), (u32)(b - a) * 2);
	}
	if (!gfx_queue_copy(map, copy, words * 4))
		return 0;
	struct stage_span *s = &ui.spans[ui.span_count++];
	s->map = map;
	s->copy = copy;
	s->rows = (u8)rows;
	s->frame = frame_count();
	ui.stage_used = (u16)(ui.stage_used + words);
	return copy;
}

int ui_map_fill(u8 node, u16 entry)
{
	const struct ui_node *n = ui_node(node);
	u32 tx = (u32)n->x / 8, ty = (u32)n->y / 8, w = (u32)n->w / 8, h = (u32)n->h / 8;
	u16 *rows = ui_map_rows(node, ty, h);
	if (!rows)
		return 0;
	for (u32 y = 0; y < h; y++)
		for (u32 x = 0; x < w; x++)
			rows[y * 32 + tx + x] = entry;
	return 1;
}

void ui_plane_hide(u32 plane, int hidden)
{
	u8 bit = (u8)(1u << plane);
	if (hidden) {
		if (!(ui.hidden & bit))
			ui.hide_pending |= bit;
		ui.hidden |= bit;
	} else {
		ui.hidden &= (u8)~bit;
		ui.hide_pending &= (u8)~bit;
	}
	ui_wake();
}

int ui_plane_hidden_now(u32 plane)
{
	// Hidden once regs without it were written in an earlier frame, and
	// that frame's commit wrote DISPCNT
	return (ui.hidden >> plane & 1) && !(ui.hide_pending >> plane & 1) && frame_count() != ui.hide_frame[plane] &&
	       !(gfx_dirty_regs & GFX_DIRTY_DISPCNT);
}

void ui_plane_reveal(u32 plane)
{
	ui_plane_hide(plane, 0);
	ui.reveal_frames = ui_theme()->page_fade;
	ui.reveal = ui.reveal_frames > 1 ? REVEAL_ASKED : REVEAL_OFF;
	ui.reveal_out = 0;
	ui.reveal_plane = (u8)plane;
	ui.reveal_step = 1;
}

// The plane's blend level (eva, 0-16) at a step of the fade running
static u32 reveal_eva(u32 step)
{
	u32 eva = 16u * step / ui.reveal_frames;
	return ui.reveal_out ? 16 - eva : eva;
}

static s32 modal_dim_now(void);

int ui_plane_fade_out(u32 plane)
{
	u32 frames = ui_theme()->page_fade_out;
	if (ui.reveal != REVEAL_OFF && ui.reveal_plane == plane) {
		if (ui.reveal_out)
			return 1;  // already going
		if (frames > 1) {
			// Fading in: out from where it is
			u32 eva = reveal_eva(ui.reveal_step);
			ui.reveal_out = 1;
			ui.reveal_frames = (u8)frames;
			ui.reveal_step = (u8)((16 - eva) * frames / 16);
			ui.reveal_step = ui.reveal_step ? ui.reveal_step : 1;
			ui_wake();
			return 1;
		}
	}
	// One fade at a time; none while a darken blend is on (regs_update)
	if (frames <= 1 || ui.reveal != REVEAL_OFF || (ui.hidden >> plane & 1) || ui.dim_y > 0 || modal_dim_now() > 0)
		return 0;
	ui.reveal = REVEAL_ASKED;
	ui.reveal_out = 1;
	ui.reveal_plane = (u8)plane;
	ui.reveal_frames = (u8)frames;
	ui.reveal_step = 1;
	ui_wake();
	return 1;
}

int ui_plane_fading_out(u32 plane)
{
	return ui.reveal != REVEAL_OFF && ui.reveal_out && ui.reveal_plane == plane;
}

// One frame of the fade-in: a step once the regs showing the plane are on
// screen. Called after the commit.
static void reveal_tick(u32 dt)
{
	if (ui.reveal == REVEAL_OFF)
		return;
	ui_wake();  // its last frame too: the regs drop the blend
	if (ui.reveal == REVEAL_WRITTEN && frame_count() != ui.reveal_frame && !(gfx_dirty_regs & GFX_DIRTY_DISPCNT))
		ui.reveal = REVEAL_RUN;  // shown in this VBlank at the first step
	if (ui.reveal == REVEAL_RUN) {
		ui.reveal_step = (u8)(ui.reveal_step + dt);
		if (ui.reveal_step >= ui.reveal_frames)
			ui.reveal = REVEAL_OFF;
	}
}

int ui_draw_room(u32 cycles, u32 budget)
{
	return frame_elapsed() + cycles <= budget;
}

int ui_plane_owned(u8 node)
{
	u32 plane = ui_node_spec(node)->plane;
	const struct ui_view *v = ui_node_view(node);
	for (u32 d = ui_depth(); d-- > 0;) {
		const struct ui_view *w = ui_view_at(d);
		if (w->planes >> plane & 1)
			return w == v;
	}
	return 0;
}

// --- Palettes and fades ----------------------------------------------------

static void fade_set(void *ctx, s32 value)
{
	(void)ctx;
	s32 f = FIX_ROUND(value);
	if (f <= 0) {
		gfx_pal_use_faded(0);
		return;
	}
	// Only the banks in use: a whole-palette fade cost about 45k cycles a frame
	palette_fade_banks(gfx_pal_faded, gfx_pal_unfaded, gfx_pal_used, ui.fade_colour, (u32)f);
	gfx_pal_mark(gfx_pal_used);
	gfx_pal_use_faded(1);
}

// Loads the first banks of the UI's (an item switch changes banks 0 and
// 1; a theme switch all of them), and the sprites' bank
static u16 zoom_backdrop(void);

static void load_palette(u32 banks)
{
	u16 pal[UI_BANKS * 16];
	ui_theme_banks(ui_theme(), ui_item(), pal);
	if (ui.zoom)
		pal[0] = zoom_backdrop();  // the backdrop is the zoom's (a page pushed under it)
	gfx_pal_load(UI_THEME_RUN_FIRST / 16, pal, banks * 16);
	gfx_pal_load(16 + ui_obj_bank(), pal, 16);  // the sprites': bank 0's roles
	if (gfx_pal_faded_on)
		fade_set(0, motion_value(ui.fade));
}

static void theme_load(const struct ui_theme *theme)
{
	ui_theme_select(theme);
	ui_list_cache_clear();  // rows may draw differently
	ui.in.timing = &ui_theme()->input;
	load_palette(UI_BANKS);
	ui_wake();
}

// --- The theme wipe ----------------------------------------------------------
// The palette holds the new theme, so lines from the top show it; a split
// (gfx.h) at the edge copies the old colours back in, which every line under
// it shows. Each VBlank the split's top copy puts the new ones back.

// Ends a wipe on the theme it was going to
static void wipe_end(void)
{
	if (!ui.wipe)
		return;
	const struct ui_theme *t = ui.wipe_dir < 0 ? ui.wipe_from : ui.wipe_to;
	int loaded = ui.wipe == 2;
	ui.wipe = 0;
	gfx_set_split(NULL);
	if (!loaded || t != ui_theme())
		theme_load(t);
}

void ui_wipe_finish(void)
{
	if (!ui.wipe)
		return;
	wipe_end();
	// One frame commits it: the split off, every line on the new theme
	if (!ui_dispatching && ui.active)
		ui_frame();
}

int ui_wiping(void)
{
	return ui.wipe != 0;
}

void ui_theme_set(const struct ui_theme *theme)
{
	ui.wipe = 0;  // ends at once on this theme
	gfx_set_split(NULL);
	theme_load(theme);
}

void ui_theme_switch(const struct ui_theme *theme)
{
	theme = theme ? theme : &ui_theme_dark;
	if (ui.wipe) {
		// The other way: the edge goes back up, or down again
		if (theme == ui.wipe_to || theme == ui.wipe_from)
			ui.wipe_dir = theme == ui.wipe_to ? 1 : -1;
		else
			ui_theme_set(theme);
		ui_wake();
		return;
	}
	if (theme == ui_theme())
		return;
	// Not under a screen change or a zoom: a screen change ends a wipe, and
	// a zoom's motion and the wipe's edge aren't run together
	if (!ui.active || !theme->motion.theme_wipe.frames || ui.tr_phase != TR_IDLE || ui.zoom) {
		ui_theme_set(theme);
		return;
	}
	// The old colours as the screen shows them, for under the edge
	const u16 *pal = gfx_pal_faded_on ? gfx_pal_faded : gfx_pal_unfaded;
	memcpy(gfx_split_data, pal + UI_THEME_RUN_FIRST, WIPE_BG_WORDS * 4);
	memcpy(gfx_split_data + WIPE_BG_WORDS, pal + 256 + 16 * ui_obj_bank(), WIPE_OBJ_WORDS * 4);
	ui.wipe_from = ui_theme();
	ui.wipe_to = theme;
	ui.wipe = 1;
	ui.wipe_dir = 1;
	ui.wipe_step = 0;
	ui_wake();
}

// One frame of the wipe: the edge's line from its step, eased
static void wipe_tick(u32 dt)
{
	if (!ui.wipe)
		return;
	const struct ui_move *w = &ui.wipe_to->motion.theme_wipe;
	if (ui.wipe == 1) {
		// The new theme and the split's first line go in the same commit
		theme_load(ui.wipe_to);
		ui.wipe = 2;
		ui.wipe_step = 1;
	} else {
		s32 step = ui.wipe_step + ui.wipe_dir * (s32)dt;
		ui.wipe_step = (u8)(step < 0 ? 0 : step > w->frames ? w->frames : step);
	}
	if ((ui.wipe_dir > 0 && ui.wipe_step >= w->frames) || (ui.wipe_dir < 0 && !ui.wipe_step)) {
		wipe_end();
		return;
	}
	u32 e = ease_at((Ease)w->ease, (u32)ui.wipe_step * 0x10000u / w->frames);
	u32 line = (160u * e + EASE_ONE / 2) >> 15;
	line = line < 1 ? 1 : line > 159 ? 159 : line;
	u32 bank = ui_obj_bank();
	const struct gfx_split split = {
		(u8)line,
		{
			{0x05000000u + UI_THEME_RUN_FIRST * 2u, &gfx_pal_unfaded[UI_THEME_RUN_FIRST], 0, WIPE_BG_WORDS,
			 0},
			{0x05000200u + bank * 32u, &gfx_pal_unfaded[256 + 16 * bank], WIPE_BG_WORDS, WIPE_OBJ_WORDS, 0},
		},
	};
	gfx_set_split(&split);
	if (tlog_in_emulator() && !(ui_top() && ui_top()->opening))  // a popup opening: its own figures
		frame_measure("ui-wipe");
	ui_wake();
}

void ui_item_set(u32 item)
{
	ui_item_select(item);
	load_palette(UI_BANK_CONSOLES + 1);
	ui_wake();
}

void ui_fade(u16 colour, u32 to, u32 frames)
{
	if (to > FADE_MAX)
		to = FADE_MAX;
	if (!frames)
		frames = ui_theme()->fade_frames;
	ui.fade_colour = colour;
	// One tween for good, restarted from where it rests (a dead handle,
	// after a motion_reset, gets a new one)
	if (ui.fade != MOTION_NONE && (motion_running(ui.fade) || motion_value(ui.fade)))
		motion_tween_to(ui.fade, FIX((s32)to), frames);
	else {
		motion_free(ui.fade);
		ui.fade = motion_tween(MOTION_TO_CALL(fade_set, 0), 0, FIX((s32)to), frames, EASE_IN_OUT_CUBIC, 0);
	}
	ui_wake();
}

int ui_fading(void)
{
	return motion_running(ui.fade);
}

// --- DIM ---------------------------------------------------------------------

u32 ui_dim_setting(void)
{
	u16 minutes = Read_SET_info(assress_dim);
	return minutes >= 1 && minutes <= 60 ? minutes : 0;
}

void ui_dim_set_frames(u32 frames)
{
	ui.dim_after = frames;
	ui.idle = 0;
}

void ui_dim_set_minutes(u32 minutes)
{
	ui_dim_set_frames(minutes * 60 * 60);
}

u32 ui_idle_frames(void)
{
	return ui.idle;
}

// Brightens the screen at once
static void dim_off(void)
{
	ui.dimmed = 0;
	motion_free(ui.dim_motion);
	ui.dim_motion = MOTION_NONE;
	ui.dim_y = 0;
	ui_wake();
}

// Counts idle frames and dims or brightens; 1 if a key woke the screen (it
// does nothing else)
static int dim_step(u32 dt)
{
	ui.wake_keys &= ui.in.held;
	if (ui.in.held) {
		ui.idle = 0;
		if (ui.dimmed) {
			ui.wake_keys = ui.in.held;  // they don't repeat either
			dim_off();
			return 1;
		}
		return 0;
	}
	if (ui.idle < 0xFFFFFF00u)
		ui.idle += dt;
	if (ui.dim_after && !ui.dimmed && ui.idle >= ui.dim_after) {
		ui.dimmed = 1;
		ui.dim_motion =
			motion_tween(MOTION_TO_S16(&ui.dim_y), 0, FIX(ui_theme()->idle_dim), 60, EASE_IN_OUT_CUBIC, 0);
		tlog_debug("ui: dimmed after %lu idle frames", (unsigned long)ui.idle);
	}
	return 0;
}

// --- Planes ---------------------------------------------------------------------

// The topmost shown view with plane p (one still being drawn doesn't take
// the plane from the view under it yet)
static struct ui_view *plane_owner(u32 p, u32 *depth)
{
	for (u32 d = ui_depth(); d-- > 0;) {
		struct ui_view *v = ui_view_at(d);
		if ((v->planes >> p & 1) && (v->flags & UI_V_SHOWN)) {
			*depth = d;
			return v;
		}
	}
	return 0;
}

// --- Modal views and transitions ---------------------------------------------

// 1 if v opens and closes with motion (a dialog or menu over a screen)
static int animated(const struct ui_view *v)
{
	return v && (v->flags & UI_V_ANIM);
}

// The topmost such view (a message over an in-session progress dialog is
// not one: the progress dialog's dim stays), its depth in *depth; NULL none
static const struct ui_view *anim_view(u32 *depth)
{
	for (u32 d = ui_depth(); d-- > 1;) {
		const struct ui_view *v = ui_view_at(d);
		if (animated(v)) {
			*depth = d;
			return v;
		}
	}
	return 0;
}

// The BLDY the topmost animated view dims what is under it by (0-16); 0
// while it fades in
static s32 modal_dim_now(void)
{
	u32 d;
	const struct ui_view *v = anim_view(&d);
	if (!v || v->open_blend <= OPEN_FADE)
		return 0;
	u32 t = (u32)(v->open_blend - OPEN_FADE) * 0x10000u / (UI_OPEN - OPEN_FADE);
	return (s32)((ui_theme()->modal_dim * ease_at(EASE_OUT_CUBIC, t) + EASE_ONE / 2) >> 15);
}

// The open_blend a modal view starts from when the views under it are
// already dimmed by y (a dialog replacing a menu): the dim carries on
static u16 blend_for_dim(s32 y)
{
	u32 dim = ui_theme()->modal_dim;
	if (y <= 0 || !dim)
		return 0;
	if ((u32)y >= dim)
		return UI_OPEN;
	return (u16)(OPEN_FADE + (UI_OPEN - OPEN_FADE) * (u32)y / dim);
}

static u16 step_towards(u16 value, s32 dir, u32 dt, u32 frames)
{
	s32 v = value;
	s32 step = frames ? (s32)((UI_OPEN * dt + frames - 1) / frames) : UI_OPEN;
	v += dir * step;
	return (u16)(v < 0 ? 0 : v > UI_OPEN ? UI_OPEN : v);
}

// One step of a modal view's opening (dir 1) or closing (-1); its offset
// follows (ease out, so closing eases in)
static void modal_step(struct ui_view *v, s32 dir, u32 dt)
{
	const struct ui_theme *t = ui_theme();
	u32 frames = dir > 0 ? t->dialog_in : t->dialog_out;
	v->open = step_towards(v->open, dir, dt, frames);
	v->open_blend = step_towards(v->open_blend, dir, dt, frames);
	u32 e = ease_at(EASE_OUT_CUBIC, (u32)v->open << 8);
	v->offset = (s16)((t->slide * (EASE_ONE - e) + EASE_ONE / 2) >> 15);
	if ((dir > 0 && v->open == UI_OPEN && v->open_blend == UI_OPEN) || (dir < 0 && !v->open && !v->open_blend))
		v->opening = 0;
	ui_wake();
}

// Modal views opening or closing: an opening one steps once its first frame
// is on screen (draw_all gives it its fade's first step with its planes)
static void modal_tick(u32 dt)
{
	for (u32 d = 1; d < ui_depth(); d++) {
		struct ui_view *v = ui_view_at(d);
		if (!animated(v) || !v->opening)
			continue;
		if (v->opening > 0 && (!(v->flags & UI_V_SHOWN) || frame_count() == ui.shown_frame[d] ||
				       (gfx_dirty_regs & GFX_DIRTY_DISPCNT))) {
			ui_wake();
			continue;
		}
		modal_step(v, v->opening, dt);
	}
}

int ui_transitioning(void)
{
	return ui.tr_phase != TR_IDLE;
}

// Where the half running is, from its step
static void tr_apply(void)
{
	u32 t = ui.tr_frames ? (u32)ui.tr_step * 0x10000u / ui.tr_frames : 0x10000;
	// Going out is linear, so the first frame answers the key; coming in eases out
	u32 e = ease_at(ui.tr_phase == TR_OUT ? EASE_LINEAR : EASE_OUT_CUBIC, t);
	ui.tr_level = (s16)(ui.tr_l0 + (((s32)ui.tr_l1 - ui.tr_l0) * (s32)e >> 15));
	ui.tr_dx = (s16)(ui.tr_x0 + (((s32)ui.tr_x1 - ui.tr_x0) * (s32)e >> 15));
	ui.tr_dy = (s16)(ui.tr_y0 + (((s32)ui.tr_y1 - ui.tr_y0) * (s32)e >> 15));
}

static void tr_gap(void)
{
	if (ui.tr_phase != TR_GAP)
		ui.tr_pushed = 0;  // the next screen comes in once it is pushed and drawn
	ui.tr_phase = TR_GAP;
	ui.tr_level = UI_OPEN;
	// What comes in starts there: a view pushed now is laid out at it
	ui.tr_dx = ui.tr_in_x;
	ui.tr_dy = ui.tr_in_y;
}

// The next screen: kind's way in, coming from dir's side
static void tr_set_in(u32 kind, s32 dir)
{
	const struct ui_transition *t = &ui_theme()->transition[kind < UI_TR_KINDS ? kind : UI_TR_FADE];
	ui.tr_effects = t->effects;
	ui.tr_in_frames = t->in;
	ui.tr_in_x = (s16)(t->effects & UI_TR_SLIDE_X ? dir * t->px : 0);
	ui.tr_in_y = (s16)(t->effects & UI_TR_SLIDE_Y ? dir * t->px : 0);
}

static int zoom_leave(u32 kind);
static void zoom_end(void);
static void zoom_cut(void);

void ui_enter(u32 kind, s32 dir)
{
	wipe_end();
	zoom_end();
	ui.zoom_set = 0;
	tr_set_in(kind, dir);
	ui.tr_mode = TR_DARKEN;  // from black
	tr_gap();
	// Black from the next commit: regs_update sets it only after that
	// frame's commit, which may first put up a new session's palette (the
	// boot's notice showed the background for a frame)
	if (ui.tr_effects & UI_TR_FADES)
		gfx_set_blend((u16)(BLEND_DARKEN | LAYER_ALL | LAYER_BD), 0, 16);
	ui_wake();
}

void ui_leave(u32 kind, s32 dir)
{
	TLOG_ASSERT(!ui_dispatching);
	if (kind >= UI_TR_KINDS)
		kind = UI_TR_FADE;
	wipe_end();  // the screen goes in the theme the wipe was going to
	if (kind == UI_TR_OPEN || kind == UI_TR_CLOSE) {
		if (zoom_leave(kind))
			return;
		kind = UI_TR_FADE;  // no sprite to carry
	}
	ui.zoom_set = 0;
	zoom_cut();  // another change cuts a zoom short: the screen goes from as it shows
	const struct ui_transition *t = &ui_theme()->transition[kind];
	s32 from = ui.tr_phase == TR_IN ? ui.tr_level : 0;
	u8 mode = kind == UI_TR_LEAVE ? TR_DARKEN : TR_ALPHA;
	// A dialog or menu goes to black with the screen (E10): the dim under it
	// deepens until it meets the fade, then everything darkens together
	// (regs_update); the caller pops it with the screen
	if (ui_depth() > 1 && (ui_top()->spec->flags & UI_VIEW_MODAL))
		mode = TR_DARKEN;
	if (ui.tr_phase != TR_IDLE && ui.tr_mode == TR_DARKEN)
		mode = TR_DARKEN;
	int gone = ui.tr_phase == TR_GAP || ui.tr_phase == TR_OUT;
	tr_set_in(kind, dir);
	ui.tr_mode = mode;
	if (gone || !ui_depth() || !t->out) {
		tr_gap();
		ui_wake();
		return;
	}
	ui.tr_phase = TR_OUT;
	// Keys the old screen hasn't had yet are the next one's too
	keyq_hold_queued(&ui.keys);
	ui.tr_frames = t->out;
	ui.tr_step = 0;
	ui.tr_l0 = (s16)from;
	ui.tr_l1 = UI_OPEN;
	ui.tr_x0 = ui.tr_dx;
	ui.tr_y0 = ui.tr_dy;
	ui.tr_x1 = (s16)(t->effects & UI_TR_SLIDE_X ? -dir * t->px : 0);
	ui.tr_y1 = (s16)(t->effects & UI_TR_SLIDE_Y ? -dir * t->px : 0);
	tr_apply();
	ui_wake();
	// Keys meanwhile go to the next screen (ui_frame holds them)
	for (u32 guard = 0; ui.tr_phase == TR_OUT && guard < 4u * 60; guard++)
		ui_frame();
	if (ui.tr_phase == TR_OUT)
		tr_gap();
	// One more, whose VBlank puts the gone screen up: the caller's reads run
	// over it, and the next view may draw at once (ui_hook_popped)
	ui_frame();
}

// 1 once every view on the stack is drawn and shown
static int all_shown(void)
{
	if (!ui_depth())
		return 0;
	for (u32 d = 0; d < ui_depth(); d++)
		if (!(ui_view_at(d)->flags & UI_V_SHOWN))
			return 0;
	return 1;
}

// --- The open and close zoom ---------------------------------------------------
// The open (UI_TR_OPEN): home goes at once and the UI's own sprite, a copy of
// its front object, zooms and fogs out over the paper (ZP_SPRITE) while the
// page draws hidden; the page's title bar shows as soon as the page is
// drawn, its heading sliding in (drawn further left each frame: its plane
// holds the hint bar too, and a plane can't scroll a part of itself), alone
// in WIN0, where no blend reaches it. Then the
// card rises and fades in (ZP_PAGE): the page's planes alpha-blend over the
// paper, the front one (text) over the card, so text fades with its card
// (research, B19). The page takes keys from then on, as a screen coming in.
// The close plays the page phase backwards first (ui_leave's frames), then
// the sprite comes back out of the fog while home draws hidden; home shows,
// sprites and all, in the frame the sprite goes. No two screens are ever
// resident at once: the sprite's tiles, bank and matrix are the UI's own.

void ui_zoom_set(const struct ui_zoom *zoom)
{
	ui.zoom_from = *zoom;
	ui.zoom_set = 1;
}

// t at frame of frames, eased (Q15: 0 to EASE_ONE)
static u32 ease_frames(u32 ease, u32 frame, u32 frames)
{
	if (!frames || frame >= frames)
		return EASE_ONE;
	return ease_at((Ease)ease, frame * 0x10000u / frames);
}

static u32 eased(const struct ui_move *m, u32 frame)
{
	return ease_frames(m->ease, frame, m->frames);
}

static s32 lerp(s32 a, s32 b, u32 e)
{
	return a + (((b - a) * (s32)e) >> 15);
}

// How far the sprite has zoomed and fogged (Q15): 0 at its place, EASE_ONE
// gone into the paper
static u32 zoom_amount(void)
{
	const struct ui_motions *m = &ui_theme()->motion;
	if (ui.zoom_phase != ZP_SPRITE)
		return EASE_ONE;
	return ui.zoom == ZOOM_OPEN ? eased(&m->open_zoom, ui.zoom_frame)
				    : EASE_ONE - eased(&m->close_zoom, ui.zoom_frame);
}

// The backdrop: the item's tint while the sprite is near its place, the
// paper once it has gone
static u16 zoom_backdrop(void)
{
	return ui_colour_mix(ui.zoom_from.sky, ui_theme()->surface->bg, zoom_amount() >> 7);
}

// The page's values this frame: blend level, rise, title bar
static void zoom_values(void)
{
	const struct ui_motions *m = &ui_theme()->motion;
	s32 slide = -(s32)m->open_slide, rise = m->open_rise;
	if (ui.zoom == ZOOM_OPEN) {
		u32 e = ui.zoom_phase == ZP_PAGE ? eased(&m->open_card, ui.zoom_frame) : 0;
		ui.zoom_eva = (u8)((16u * e + EASE_ONE / 2) >> 15);
		ui.tr_dy = (s16)lerp(rise, 0, e);
		ui.zoom_title = (s16)lerp(slide, 0, ease_frames(m->open.ease, ui.zoom_total, ui.zoom_slide));
	} else if (ui.zoom_phase == ZP_PAGE) {
		u32 e = eased(&m->close_card, ui.zoom_frame);
		ui.zoom_eva = (u8)lerp(ui.zoom_eva0, 0, e);
		ui.tr_dy = (s16)lerp(ui.zoom_dy0, rise, e);
		ui.zoom_title = (s16)lerp(ui.zoom_title0, slide, e);
	} else {
		ui.zoom_eva = 0;
		ui.tr_dy = (s16)rise;
		ui.zoom_title = (s16)slide;
	}
	ui.tr_dx = 0;
	ui.tr_level = 0;
}

static void zoom_phase(u32 phase)
{
	ui.zoom_phase = (u8)phase;
	ui.zoom_frame = 0;
	ui.zoom_ready = 0;
}

static u8 m_open_frames(void)
{
	return ui_theme()->motion.open.frames;
}

static void zoom_end(void)
{
	if (!ui.zoom)
		return;
	ui.zoom = ZOOM_NONE;
	gfx_pal_unfaded[0] = ui_theme()->surface->bg;  // the backdrop the theme gives
	gfx_pal_mark(1);
	ui.tr_phase = TR_IDLE;
	ui.tr_level = ui.tr_dx = ui.tr_dy = 0;
	ui.zoom_title = 0;
	ui.zoom_eva = 16;
	keyq_release(&ui.keys);  // keys held meanwhile: the screen's now
	for (u32 d = 0; d < ui_depth(); d++)
		if (!animated(ui_view_at(d)))
			ui_view_at(d)->offset = 0;
	ui_wake();
}

// Another screen change during a zoom: the zoom ends, and the change goes on
// from the screen as it shows: nothing in the sprite phase (the screen drawn
// under it hidden), the card's fade and rise as they are in the page phase
static void zoom_cut(void)
{
	if (!ui.zoom)
		return;
	int sprite = ui.zoom_phase == ZP_SPRITE;
	u32 eva = ui.zoom_eva > 16 ? 16 : ui.zoom_eva;
	s16 dy = ui.tr_dy;
	zoom_end();
	if (sprite) {
		ui.tr_phase = TR_GAP;
		ui.tr_level = UI_OPEN;
	} else {
		ui.tr_phase = TR_IN;
		ui.tr_level = (s16)((16 - eva) * UI_OPEN / 16);
		ui.tr_dy = dy;
	}
}

// Starts a zoom: 0 if it can't (no sprite set, no room, a dialog up)
static int zoom_begin(u32 kind)
{
	if (!ui.zoom_set || ui.zoom_tiles_h == RES_NONE || ui.zoom_bank_h == RES_NONE || ui.zoom_matrix_h == RES_NONE ||
	    (ui_depth() && (ui_top()->spec->flags & UI_VIEW_MODAL)) || gfx_queue_len() >= GFX_QUEUE_CAP)
		return 0;
	ui.zoom_set = 0;
	// Where a close starts from: the open's card, if B cut it short
	if (ui.zoom == ZOOM_OPEN) {
		ui.zoom_eva0 = ui.zoom_eva;  // 0 and risen in its sprite phase
		ui.zoom_dy0 = ui.tr_dy;
		ui.zoom_title0 = ui.zoom_title;
	} else {
		ui.zoom_eva0 = 16;
		ui.zoom_dy0 = ui.zoom_title0 = 0;
	}
	if (!gfx_queue_copy((void *)(uintptr_t)(0x06010000u + ui.zoom_tiles * 32u),
			    art_sprite_tiles[ui.zoom_from.sprite % ART_SPRITE_COUNT], ART_SPRITE_TILES * 32))
		return 0;
	ui.zoom_ticket = gfx_queue_ticket();
	ui.zoom_started = 0;
	ui.zoom_total = 0;
	ui.zoom_slide = m_open_frames();
	ui.zoom = (u8)(kind == UI_TR_OPEN ? ZOOM_OPEN : ZOOM_CLOSE);
	zoom_phase(kind == UI_TR_OPEN ? ZP_SPRITE : ZP_PAGE);
	ui.tr_effects = 0;
	ui.tr_mode = TR_ALPHA;
	ui.tr_pushed = 0;
	keyq_hold_queued(&ui.keys);  // keys the old screen hasn't had are the next one's
	zoom_values();
	ui_wake();
	return 1;
}

static int zoom_leave(u32 kind)
{
	if (!zoom_begin(kind))
		return 0;
	if (kind == UI_TR_OPEN) {
		// Home goes at once: one frame puts the sprite in its place (its
		// tiles land in that frame's VBlank), the next one's VBlank shows it
		// alone. Only then may the caller pop home and push the page, whose
		// first tiles go straight into VRAM home showed
		ui.tr_phase = TR_GAP;
		ui_frame();
		ui_frame();
		return 1;
	}
	// The page goes first, frames run meanwhile (keys held for home)
	ui.tr_phase = TR_OUT;
	for (u32 guard = 0; ui.zoom == ZOOM_CLOSE && ui.zoom_phase == ZP_PAGE && guard < 4u * 60; guard++)
		ui_frame();
	if (ui.zoom == ZOOM_CLOSE && ui.zoom_phase == ZP_PAGE) {
		zoom_phase(ZP_SPRITE);
		ui.tr_phase = TR_GAP;
	}
	// One more, whose VBlank puts up the paper alone: the caller pops the page
	ui_frame();
	return 1;
}

// One frame of the zoom, in place of the transition's
static void zoom_tick(u32 dt)
{
	const struct ui_motions *m = &ui_theme()->motion;
	ui_wake();
	// The sprite's frames count from the first that shows it (its frame 0
	// at its place, as the screen it leaves showed it)
	u32 step = ui.zoom_phase == ZP_SPRITE && !ui.zoom_started ? 0 : dt;
	if (!ui.zoom_started && gfx_queue_done(ui.zoom_ticket))
		ui.zoom_started = 1;
	if (ui.zoom_ready && ui.zoom == ZOOM_OPEN)
		ui.zoom_total = (u16)(ui.zoom_total + dt);
	if (!ui.zoom_ready && ui.tr_pushed && all_shown() && (!gfx_queue_len() || ui.zoom_frame >= ZOOM_READY_WAIT)) {
		ui.zoom_ready = 1;
		// The title bar slides in over what the open has left, half of it
		// at least, so a page that took long to draw still slides
		u32 gone = ui.zoom_frame, half = m->open.frames / 2u;
		ui.zoom_slide = (u8)(m->open.frames - (gone < half ? gone : half));
	}
	ui.zoom_frame = (u16)(ui.zoom_frame + step);
	if (ui.zoom == ZOOM_OPEN && ui.zoom_phase == ZP_SPRITE) {
		// The card comes once the sprite has gone and the page is ready;
		// the page takes keys from then on, as a screen coming in (the
		// d-pad, L, R and B held meanwhile; A, START and SELECT were dropped)
		if (ui.zoom_frame >= m->open_zoom.frames && ui.zoom_ready) {
			u8 ready = ui.zoom_ready;
			zoom_phase(ZP_PAGE);
			ui.zoom_ready = ready;
			ui.tr_phase = TR_IN;
			keyq_release(&ui.keys);
		}
	} else if (ui.zoom == ZOOM_OPEN) {
		if (ui.zoom_frame >= m->open_card.frames && ui.zoom_total >= ui.zoom_slide) {
			zoom_end();
			return;
		}
	} else if (ui.zoom_phase == ZP_PAGE) {
		if (ui.zoom_frame >= m->close_card.frames) {
			zoom_phase(ZP_SPRITE);
			ui.tr_phase = TR_GAP;
			ui.tr_pushed = 0;
		}
	} else if (ui.zoom_frame >= m->close_zoom.frames && ui.zoom_ready) {
		// Home, drawn and uploaded: it shows in this frame, the sprite goes
		zoom_end();
		return;
	}
	if (ui.zoom_frame > 0x7FFF)
		ui.zoom_frame = 0x7FFF;
	zoom_values();
	if (tlog_in_emulator())
		frame_measure(ui.zoom == ZOOM_OPEN ? "ui-open" : "ui-close");
}

// The sprite, in this frame's OAM: double size, scaled about its anchor row,
// its bank fogged toward the backdrop
static void zoom_obj(void)
{
	if (!ui.zoom || ui.zoom_phase != ZP_SPRITE || !ui.zoom_started)
		return;
	const struct ui_zoom *z = &ui.zoom_from;
	u32 e = zoom_amount(), fog = e >> 7;
	if (fog >= 256)
		return;  // gone into the paper
	u32 scale = (u32)lerp(z->scale, ui_theme()->motion.open_scale, e);
	scale = scale < 16 ? 16 : scale;
	u32 pd0 = (0x10000u + z->scale / 2u) / (z->scale ? z->scale : 256u);
	u32 pd = (0x10000u + scale / 2) / scale;
	// The anchor's screen row stays: the texel row r shows (r - 32) * 256 / pd
	// rows from the box's centre (the hardware samples (pd * dy >> 8) + 32)
	s32 r = (s32)z->anchor - 32;
	s32 cy = z->cy + r * 256 / (s32)pd0 - r * 256 / (s32)pd;
	s32 x = z->cx - 64, y = cy - 64;
	// (y's 8 bits wrap: a box more than 96 px above the top would show its
	// first rows at the bottom)
	if (x <= -128 || x >= 240 || y < -96 || y >= 160)
		return;
	u16 bd = zoom_backdrop(), c[16];
	c[0] = 0;
	for (u32 i = 1; i < 16; i++)
		c[i] = ui_colour_mix(z->colours[i], bd, fog);
	gfx_pal_load(16 + ui.zoom_bank, c, 16);
	gfx_affine_set(ui.zoom_matrix, (s16)pd, 0, 0, (s16)pd);
	gfx_oam_add((u16)(OBJ_AFFINE | OBJ_DOUBLE | (y & 0xFF)),
		    (u16)(OBJ_SIZE_64 | OBJ_MATRIX(ui.zoom_matrix) | (x & 0x1FF)),
		    (u16)(ui.zoom_tiles | ZOOM_PRIO << 10 | (u32)ui.zoom_bank << 12));
}

// 1 if node's sprite shows during the zoom: the title bar's once the page
// shows, the rest only past half way into the card's fade (a sprite
// doesn't take the planes' blend: it would show at full over a faint card)
static int zoom_obj_shows(u8 node)
{
	int title = ui_node_spec(node)->kind == UI_TITLE;
	if (ui.zoom_phase == ZP_SPRITE)
		return title && ui.zoom == ZOOM_OPEN && ui.zoom_ready;
	return title || ui.zoom_eva >= 8;
}

void ui_title_offset(const struct ui_view *v, s32 *shift, s32 *dy)
{
	int zoom = ui.zoom && !animated(v);
	*shift = zoom ? ui.zoom_title : 0;
	*dy = zoom ? 0 : v->offset;
}

// The registers while a zoom runs, over what regs_update set: the paper
// round the sprite, then the page's title bar (its plane alone in WIN0),
// then its card fading over the paper
static void zoom_regs(void)
{
	u16 dispcnt = DISP_MODE0_OBJ1D | DISP_OBJ;
	gfx_pal_unfaded[0] = zoom_backdrop();
	gfx_pal_mark(1);
	// The page's title bar, and its first list
	u8 title = UI_NONE, list = UI_NONE;
	u32 planes = 0;
	for (u32 p = 0; p < UI_PLANES; p++) {
		u32 d;
		if (plane_owner(p, &d) && !((ui.hidden | ui.lists_hidden) >> p & 1))
			planes |= 1u << p;
	}
	for (u32 d = 0; d < ui_depth(); d++) {
		const struct ui_view *v = ui_view_at(d);
		for (u32 i = 0; i < v->count; i++) {
			u8 n = (u8)(v->first + i);
			u32 kind = v->spec->nodes[i].kind;
			if (kind == UI_TITLE && title == UI_NONE)
				title = n;
			if (kind == UI_LIST && list == UI_NONE && !(ui_node(n)->flags & UI_N_HIDDEN))
				list = n;
		}
	}
	int page = ui.zoom_phase == ZP_PAGE || (ui.zoom == ZOOM_OPEN && ui.zoom_ready);
	ui.gap_regs = (u8)!page;
	if (!page || title == UI_NONE) {
		gfx_set_dispcnt(page ? (u16)(dispcnt | planes << 8) : dispcnt);
		gfx_set_blend(0, 0, 0);
		return;
	}
	u32 tp = ui_node_spec(title)->plane, band = ui_theme()->look.title_h;
	if (ui.zoom_phase == ZP_SPRITE)
		planes &= 1u << tp;
	dispcnt |= (u16)(planes << 8 | DISP_WIN0);
	// WIN0: the title bar's band, its plane and sprites only, unblended
	gfx_set_win(0, 240);
	gfx_set_win(2, (u16)band);
	u32 in0 = LAYER_BG(tp) | LAYER_OBJ, out = LAYER_OBJ, in1 = 0;
	if (ui.zoom_phase == ZP_PAGE) {
		out = planes | LAYER_OBJ | WIN_EFFECTS;
		if (list != UI_NONE) {
			// The list clips with WIN1 meanwhile
			const struct ui_node *n = ui_node(list);
			u32 lp = ui_node_spec(list)->plane;
			s32 y0 = n->y + ui_view_at(n->view)->offset, y1 = y0 + n->h;
			y0 = y0 < 0 ? 0 : y0 > 160 ? 160 : y0;
			y1 = y1 < 0 ? 0 : y1 > 160 ? 160 : y1;
			gfx_set_win(1, (u16)(n->x << 8 | (n->x + n->w)));
			gfx_set_win(3, (u16)(y0 << 8 | y1));
			in1 = out;
			out &= ~LAYER_BG(lp);
			dispcnt |= DISP_WIN1;
		}
		// The page's planes over the paper, its front one (text) over the
		// planes behind it too
		u32 front = planes & -planes;
		u32 eva = ui.zoom_eva > 16 ? 16 : ui.zoom_eva;
		gfx_set_blend((u16)(BLEND_ALPHA | planes | (((planes & ~front) | LAYER_BD) << 8)),
			      (u16)(eva | (16 - eva) << 8), 0);
	} else {
		gfx_set_blend(0, 0, 0);
	}
	gfx_set_win(4, (u16)(in0 | in1 << 8));
	gfx_set_win(5, (u16)out);
	gfx_set_dispcnt(dispcnt);
	// The title bar's plane (and the hint bar on it) never rises: its
	// heading slides by being drawn further left (ui_title_offset). It goes
	// in front of the card, which rises over the hint bar: a card pixel
	// there would blend with the hints under it as its second target, and
	// show them whole while the card is faint
	gfx_set_scroll(tp, 0, 0);
	gfx_set_bgcnt(tp, (u16)(gfx_regs.bgcnt[tp] & ~3u));
}

// One frame of the transition, before the widgets' update (lists read their
// view's offset there)
static void tr_tick(u32 dt)
{
	if (ui.zoom) {
		zoom_tick(dt);
	} else if (ui.tr_phase == TR_OUT) {
		ui.tr_step = (u8)(ui.tr_step + dt > ui.tr_frames ? ui.tr_frames : ui.tr_step + dt);
		tr_apply();
		if (ui.tr_step >= ui.tr_frames)
			tr_gap();
		ui_wake();
	} else if (ui.tr_phase == TR_IN) {
		if (ui.tr_wait && frame_count() != ui.tr_frame && !(gfx_dirty_regs & GFX_DIRTY_DISPCNT))
			ui.tr_wait = 0;  // the first step is on screen
		if (!ui.tr_wait) {
			ui.tr_step = (u8)(ui.tr_step + dt > ui.tr_frames ? ui.tr_frames : ui.tr_step + dt);
			tr_apply();
			if (ui.tr_step >= ui.tr_frames) {
				ui.tr_phase = TR_IDLE;
				ui.tr_level = ui.tr_dx = ui.tr_dy = 0;
			}
		}
		ui_wake();
	} else if (ui.tr_phase == TR_GAP) {
		ui_wake();
	}
	// Screens move with the transition; dialogs and menus with their own opening
	for (u32 d = 0; d < ui_depth(); d++) {
		struct ui_view *v = ui_view_at(d);
		if (!animated(v))
			v->offset = ui.tr_dy;
	}
}

// Called in regs_update once every view is shown: the screen comes in, its
// first step in the regs being written
static void tr_begin_in(void)
{
	ui.tr_phase = TR_IN;
	ui.tr_frames = ui.tr_in_frames;
	ui.tr_step = ui.tr_frames ? 1 : 0;
	ui.tr_wait = 1;
	ui.tr_frame = frame_count();
	ui.tr_l0 = UI_OPEN;
	ui.tr_l1 = 0;
	ui.tr_x0 = ui.tr_in_x;
	ui.tr_y0 = ui.tr_in_y;
	ui.tr_x1 = ui.tr_y1 = 0;
	tr_apply();
	// Its first frame where it was laid out (lists set their scroll from
	// the view's offset before this)
	ui.tr_dx = ui.tr_in_x;
	ui.tr_dy = ui.tr_in_y;
	if (!ui.tr_frames) {
		ui.tr_phase = TR_IDLE;
		ui.tr_level = ui.tr_dx = ui.tr_dy = 0;
	}
	// Keys pressed while the last screen went out: this one's, from next frame
	keyq_release(&ui.keys);
	for (u32 d = 0; d < ui_depth(); d++) {
		struct ui_view *v = ui_view_at(d);
		if (!animated(v))
			v->offset = ui.tr_dy;
	}
}

void ui_close(void)
{
	struct ui_view *v = ui_top();
	if (!v)
		return;
	if (animated(v) && !ui_dispatching && ui_theme()->dialog_out) {
		v->opening = -1;
		ui.capture = 1;
		for (u32 guard = 0; ui_top() == v && ui_top()->opening && guard < 4u * 60; guard++)
			ui_frame();
		ui.capture = 0;
	}
	struct keyq keys = ui.keys;  // the pop forgets the queue
	keyq_release(&keys);
	keyq_clear(&ui.keys);
	ui_pop();
	// The view under it gets keys pressed while it closed (the pop forgot
	// the queue)
	if (!ui_dispatching) {
		ui.keys = keys;
		ui_wake();
	}
}

// --- Overlays -------------------------------------------------------------------

void ui_overlay_set(const struct ui_overlay *overlay)
{
	ui.ov = *overlay;
	ui_wake();
}

void ui_overlay_drop(const void *owner)
{
	if (ui.ov.owner != owner)
		return;
	memset(&ui.ov, 0, sizeof(ui.ov));
	ui_wake();
}

static s32 view_dx(const struct ui_view *v);

// The windows while an overlay's window is on: WIN0 is its, the first list
// (of lists, top view first) clips with WIN1 (a second goes unclipped for
// those frames), and plane 0 shows only in WIN0
static void overlay_windows(u32 lists, const u32 *list_plane, const struct ui_node *const *list_node, u16 *dispcnt)
{
	const struct ui_overlay *o = &ui.ov;
	u32 outside = LAYER_ALL & ~LAYER_BG(0);
	if (lists > 1)
		lists = 1;
	if (lists)
		outside &= ~LAYER_BG(list_plane[0]);
	s32 x0 = o->x0 < 0 ? 0 : o->x0, x1 = o->x1 > 240 ? 240 : o->x1;
	s32 y0 = o->y0 < 0 ? 0 : o->y0, y1 = o->y1 > 160 ? 160 : o->y1;
	x1 = x1 < x0 ? x0 : x1;
	y1 = y1 < y0 ? y0 : y1;
	u32 in0 = outside | LAYER_BG(0), in1 = 0;
	if (lists) {
		// The list shows in WIN0 too where their lines meet: an overlay's
		// card covers most of WIN0, and the list's pixels beside it (a
		// popup's notch, its corners) matter more than a ring row peeking
		// out past the list's lines for the few frames of the motion
		const struct ui_node *n = list_node[0];
		const struct ui_view *v = ui_view_at(n->view);
		s32 ly0 = n->y + v->offset, ly1 = ly0 + n->h, lx0 = n->x + view_dx(v), lx1 = lx0 + n->w;
		ly0 = ly0 < 0 ? 0 : ly0 > 160 ? 160 : ly0;
		ly1 = ly1 < 0 ? 0 : ly1 > 160 ? 160 : ly1;
		lx0 = lx0 < 0 ? 0 : lx0 > 240 ? 240 : lx0;
		lx1 = lx1 < 0 ? 0 : lx1 > 240 ? 240 : lx1;
		gfx_set_win(1, (u16)(lx0 << 8 | lx1));
		gfx_set_win(3, (u16)(ly0 << 8 | ly1));
		in1 = outside | LAYER_BG(list_plane[0]);
		if (y0 < ly1 && y1 > ly0)
			in0 |= LAYER_BG(list_plane[0]);
		*dispcnt |= DISP_WIN1;
	}
	gfx_set_win(0, (u16)(x0 << 8 | x1));
	gfx_set_win(2, (u16)(y0 << 8 | y1));
	gfx_set_win(4, (u16)((in0 | WIN_EFFECTS) | (in1 | WIN_EFFECTS) << 8));
	gfx_set_win(5, (u16)(outside | WIN_EFFECTS));
	*dispcnt |= DISP_WIN0;
}

// --- Registers ------------------------------------------------------------------

// The px a view's planes are moved sideways (a transition's slide)
static s32 view_dx(const struct ui_view *v)
{
	return animated(v) ? 0 : ui.tr_dx;
}

s32 ui_view_dx(const struct ui_view *v)
{
	return view_dx(v);
}

// Another blend takes over from a page's fade: a fade-in just shows; a
// fade-out's plane goes in these regs (DISPCNT is written already), so the
// old page doesn't come back at full for a frame before the list hides it
static void reveal_drop(void)
{
	if (ui.reveal != REVEAL_OFF && ui.reveal_out) {
		u32 p = ui.reveal_plane;
		ui.hidden |= (u8)(1u << p);
		ui.hide_pending &= (u8) ~(1u << p);
		ui.hide_frame[p] = frame_count();
		gfx_set_dispcnt((u16)(gfx_regs.dispcnt & ~DISP_BG(p)));
	}
	ui.reveal = REVEAL_OFF;
}

static void regs_update(void)
{
	if (ui.tr_phase == TR_GAP && ui.tr_pushed && all_shown() && !ui.zoom)
		tr_begin_in();
	u32 top = ui_depth();
	u16 dispcnt = DISP_MODE0_OBJ1D | DISP_OBJ;
	u32 lower = 0;   // planes of views under the topmost animated view
	u32 shown = 0;   // planes views show
	u32 top_planes = 0;  // planes of it and the views over it
	u32 da = 0;
	const struct ui_view *tv = anim_view(&da);
	int modal = tv != 0;
	int tr = ui.tr_phase != TR_IDLE;
	// A screen that has gone shows nothing (the backdrop)
	int gone = tr && ui.tr_level >= UI_OPEN;
	ui.gap_regs = (u8)gone;
	if (gone)
		dispcnt &= (u16)~DISP_OBJ;  // the screen's sprites go with it
	for (u32 p = 0; p < UI_PLANES; p++) {
		u32 d;
		struct ui_view *v = plane_owner(p, &d);
		if (!v)
			continue;
		u32 prio = ui.level[d] + (ui.obj_prio[d] != PRIO_NONE && !(v->front >> p & 1));
		if (prio > 3)
			prio = 3;
		gfx_set_bgcnt(p, (u16)(prio | ui_plane_cb(p) << 2 | (u32)res_bg_screenblock(v->maps[p]) << 8 |
				       (v->tall >> p & 1 ? BGCNT_32X64 : 0)));
		if (!((ui.hidden | ui.lists_hidden) >> p & 1) && !gone)
			dispcnt |= (u16)DISP_BG(p);
		shown |= LAYER_BG(p);
		if (modal && d < da)
			lower |= LAYER_BG(p);
		else if (modal)
			top_planes |= LAYER_BG(p);
	}
	// Sprites blend with the planes of their views: those under the
	// animated view darken with them, unless it shows sprites itself
	// (obj_levels hides theirs then), which then fade in with it
	if (modal && ui.obj_lower)
		lower |= LAYER_OBJ;
	else if (modal && ui.obj_top)
		top_planes |= LAYER_OBJ;
	// Static planes follow their view's slide; lists set their own scroll
	// (with the view's offset), and are moved sideways here
	u32 scrollers = 0, list_planes = 0, win_plane[UI_SCROLLERS] = {0, 0};
	const struct ui_node *win_node[UI_SCROLLERS] = {0, 0};
	for (u32 d = top; d-- > 0;) {
		struct ui_view *v = ui_view_at(d);
		for (u32 i = 0; i < v->count; i++) {
			const struct ui_spec *s = &v->spec->nodes[i];
			const struct ui_node *n = ui_node((u8)(v->first + i));
			if (s->kind != UI_LIST)
				continue;
			u32 d2;
			if (plane_owner(s->plane, &d2) != v)
				continue;
			list_planes |= 1u << s->plane;
			if (view_dx(v))
				gfx_set_scroll(s->plane, (u16)(-(n->x + view_dx(v))), gfx_regs.bgofs[2 * s->plane + 1]);
			// ui_push allows at most UI_SCROLLERS
			if ((n->flags & UI_N_HIDDEN) || scrollers == UI_SCROLLERS)
				continue;
			win_plane[scrollers] = s->plane;
			win_node[scrollers] = n;
			scrollers++;
		}
	}
	for (u32 p = 0; p < UI_PLANES; p++) {
		u32 d;
		struct ui_view *v = plane_owner(p, &d);
		if (v && !(list_planes >> p & 1))
			gfx_set_scroll(p, (u16)(-view_dx(v) - (p ? 0 : ui.ov.dx)), (u16)(-v->offset));
	}
	if (ui.ov.window) {
		overlay_windows(scrollers, win_plane, win_node, &dispcnt);
	} else if (scrollers) {
		u32 outside = LAYER_ALL;
		for (u32 w = 0; w < scrollers; w++)
			outside &= ~LAYER_BG(win_plane[w]);
		u16 inside[2] = {0, 0};
		s32 rect[UI_SCROLLERS][4];
		for (u32 w = 0; w < scrollers; w++) {
			const struct ui_node *n = win_node[w];
			const struct ui_view *v = ui_view_at(n->view);
			s32 y0 = n->y + v->offset, y1 = y0 + n->h;
			s32 x0 = n->x + view_dx(v), x1 = x0 + n->w;
			y0 = y0 < 0 ? 0 : y0 > 160 ? 160 : y0;
			y1 = y1 < 0 ? 0 : y1 > 160 ? 160 : y1;
			x0 = x0 < 0 ? 0 : x0 > 240 ? 240 : x0;
			x1 = x1 < 0 ? 0 : x1 > 240 ? 240 : x1;
			rect[w][0] = x0;
			rect[w][1] = y0;
			rect[w][2] = x1;
			rect[w][3] = y1;
			gfx_set_win(w, (u16)(x0 << 8 | x1));
			gfx_set_win(2 + w, (u16)(y0 << 8 | y1));
			inside[w] = (u16)((outside | LAYER_BG(win_plane[w])) | WIN_EFFECTS);
			dispcnt |= w ? DISP_WIN1 : DISP_WIN0;
		}
		// WIN0 (the top view's list) wins where both are: the list under it
		// shows there too when its rectangle holds WIN0's, so a menu fading
		// in or out shows the list behind it, not a hole
		if (scrollers == 2 && rect[1][0] <= rect[0][0] && rect[1][1] <= rect[0][1] &&
		    rect[1][2] >= rect[0][2] && rect[1][3] >= rect[0][3])
			inside[0] |= (u16)LAYER_BG(win_plane[1]);
		gfx_set_win(4, (u16)(inside[0] | inside[1] << 8));
		gfx_set_win(5, (u16)(outside | WIN_EFFECTS));
	}
	gfx_set_dispcnt(dispcnt);
	// The hides asked for are in this frame's regs
	for (u32 p = 0; p < UI_PLANES; p++)
		if (ui.hide_pending >> p & 1)
			ui.hide_frame[p] = frame_count();
	ui.hide_pending = 0;
	if (ui.pop_wait == 1) {
		ui.pop_wait = 2;
		ui.pop_frame = frame_count();
	}
	// Blending, one effect at a time: DIM darkens everything; a transition
	// fades the screen; a dialog fades in, then darkens what is under it
	s32 modal_y = modal_dim_now();
	ui.last_dim = (s16)modal_y;
	ui.carry_dim = 0;
	if (ui.dim_y > 0) {
		s32 y = ui.dim_y > modal_y ? ui.dim_y : modal_y;
		reveal_drop();  // the page just shows
		gfx_set_blend((u16)(BLEND_DARKEN | LAYER_ALL | LAYER_BD), 0, (u16)(y > 16 ? 16 : y));
	} else if (tr && (ui.tr_effects & UI_TR_FADES) && !gone && ui.tr_level > 0) {
		reveal_drop();
		if (ui.tr_mode == TR_ALPHA && !modal) {
			u32 eva = 16 - ((u32)ui.tr_level * 16 + UI_OPEN / 2) / UI_OPEN;
			gfx_set_blend((u16)(BLEND_ALPHA | shown | LAYER_OBJ | LAYER_BD << 8),
				      (u16)(eva | (16 - eva) << 8), 0);
		} else {
			// Everything darkens with the fade, a dialog too (E10, E11),
			// but while the fade is lighter than the dim under the dialog,
			// the dim stays and the dialog shows as it is
			s32 y = (ui.tr_level * 16 + UI_OPEN / 2) / UI_OPEN;
			u32 targets = LAYER_ALL | LAYER_BD;
			if (modal && y <= modal_y) {
				y = modal_y;
				targets = lower | LAYER_BD;
			}
			gfx_set_blend((u16)(BLEND_DARKEN | targets), 0, (u16)y);
		}
	} else if (tr && (ui.tr_effects & UI_TR_FADES) && gone && ui.tr_mode == TR_DARKEN) {
		reveal_drop();
		gfx_set_blend((u16)(BLEND_DARKEN | LAYER_ALL | LAYER_BD), 0, 16);  // black
	} else if (modal && tv->open_blend < UI_OPEN && tv->open_blend <= OPEN_FADE && !tr) {
		// The dialog fades in over the screen
		reveal_drop();
		u32 eva = ((u32)tv->open_blend * 16 + OPEN_FADE / 2) / OPEN_FADE;
		gfx_set_blend((u16)(BLEND_ALPHA | top_planes | (lower | LAYER_BD) << 8), (u16)(eva | (16 - eva) << 8),
			      0);
	} else if (modal && modal_y > 0) {
		reveal_drop();
		gfx_set_blend((u16)(BLEND_DARKEN | lower | LAYER_BD), 0, (u16)modal_y);
	} else if (ui.ov.dim > 0) {
		// A confirmation: plane 0 holds it (and a popup under it, dimmed by
		// its palette), so it stays as it is
		reveal_drop();
		gfx_set_blend((u16)((ui_theme()->confirm_brighten ? BLEND_BRIGHTEN : BLEND_DARKEN) |
				    (LAYER_ALL & ~LAYER_BG(0)) | LAYER_BD),
			      0, (u16)(ui.ov.dim > 16 ? 16 : ui.ov.dim));
	} else if (ui.reveal != REVEAL_OFF) {
		// The list's plane over everything behind it
		u32 p = LAYER_BG(ui.reveal_plane), eva = reveal_eva(ui.reveal_step);
		gfx_set_blend((u16)(BLEND_ALPHA | p | ((LAYER_ALL | LAYER_BD) & ~p) << 8), (u16)(eva | (16 - eva) << 8),
			      0);
		if (ui.reveal == REVEAL_ASKED) {
			ui.reveal = REVEAL_WRITTEN;
			ui.reveal_frame = frame_count();
		}
	} else {
		gfx_set_blend(0, 0, 0);
	}
	if (ui.zoom)
		zoom_regs();
}

// --- Sprites -------------------------------------------------------------------

u32 ui_obj_bank(void)
{
	return res_start(RES_OBJ_PAL, ui.obj_palette);
}

void ui_view_uses_objs(struct ui_view *v, u32 front)
{
	v->flags |= UI_V_OBJS;
	v->front |= (u8)(1u << front | 1u);
}

int ui_plane_blending(u32 plane)
{
	return ui.reveal != REVEAL_OFF && ui.reveal_plane == plane;
}

// 1 if the topmost animated view (at da) blends over the views under it
// now: fading in, or dimming them
static int lower_blended(const struct ui_view *tv)
{
	return tv && ((tv->open_blend < UI_OPEN && tv->open_blend <= OPEN_FADE) || modal_dim_now() > 0);
}

// This frame's priorities, before the update adds the sprites: from the top
// view down, each takes a level for its planes, and one more for its
// sprites and the planes behind them; none show past level 3. Sprites hide
// where a blend can't take them: those of views under the animated one
// while it blends over them with sprites of its own (both would take its
// blend), the animated view's own while it fades in or out (a sprite on
// its card has no second target under it, so it would show unfaded), and
// every view's past the middle of a screen's alpha fade (the same)
static void obj_levels(void)
{
	u32 da = 0;
	const struct ui_view *tv = anim_view(&da);
	ui.obj_top = 0;
	for (u32 d = da; tv && d < ui_depth(); d++)
		ui.obj_top |= (ui_view_at(d)->flags & UI_V_OBJS) != 0;
	// Not before it shows: until then the views under it show as they were
	int shown = tv && (tv->flags & UI_V_SHOWN);
	int blended = shown && lower_blended(tv);
	int fading = shown && tv->open_blend < UI_OPEN && tv->open_blend <= OPEN_FADE;
	int gone = ui.tr_phase != TR_IDLE && ui.tr_mode == TR_ALPHA && ui.tr_level * 2 > UI_OPEN;
	u32 level = 0;
	ui.obj_lower = 0;
	for (u32 d = ui_depth(); d-- > 0;) {
		const struct ui_view *v = ui_view_at(d);
		int objs = (v->flags & UI_V_OBJS) && !gone && !(tv && d < da && blended && ui.obj_top) &&
			   !(tv && d >= da && fading);
		ui.level[d] = (u8)(level < 3 ? level : 3);
		if (objs && level + 1 <= 3) {
			ui.obj_prio[d] = (u8)(level + 1);
			level += 2;
			if (tv && d < da)
				ui.obj_lower = 1;
		} else {
			ui.obj_prio[d] = PRIO_NONE;
			level++;
		}
	}
}

int ui_obj_add(u8 node, u16 attr0, u16 attr1, u16 attr2)
{
	u32 d = ui_node(node)->view, prio = d < UI_VIEW_CAP ? ui.obj_prio[d] : PRIO_NONE;
	if (prio == PRIO_NONE || (ui.zoom && !zoom_obj_shows(node)))
		return 0;
	return gfx_oam_add(attr0, attr1, (u16)((attr2 & ~0x0C00u) | prio << 10)) != GFX_OAM_NONE;
}

// --- The stack ----------------------------------------------------------------

const struct ui_theme *ui_theme_setting(void)
{
	return Read_SET_info(assress_theme) == 1 ? &ui_theme_light : &ui_theme_dark;
}

void ui_init(const struct ui_theme *theme)
{
	memset(&ui, 0, sizeof(ui));
	ui_core_reset();
	ui_list_cache_clear();
	ui_theme_select(theme ? theme : ui_theme_setting());
	input_init(&ui.in, &ui_theme()->input);
	// Keys already held (from the screen before) count as held, not pressed
	input_poll(&ui.in, 1);
	ui.palette = res_palette(RES_BG_PAL, UI_BANKS, TAG_PALETTE);
	TLOG_ASSERT(ui.palette != RES_NONE && res_start(RES_BG_PAL, ui.palette) == 0);
	ui.obj_palette = res_palette(RES_OBJ_PAL, 1, TAG_OBJ_PALETTE);
	TLOG_ASSERT(ui.obj_palette != RES_NONE);
	for (u32 cb = 0; cb < 3; cb++) {
		// Blocks 0's and 1's at their bottom, block 2's at the top of its
		// reach, so they don't split the long runs lists need: block 1's
		// list grows up from its bottom, block 2's into what that leaves
		// (block 1's top would cut that in two, and two lists then don't fit)
		u16 lo = (u16)(cb * 512), hi = (u16)(lo + 1024);
		ui.shared[cb] = res_alloc(RES_BG_VRAM, UI_SHARED_TILES, 1, lo, hi, (u16)(TAG_SHARED + cb),
					  cb == 2 ? POOL_TOP : 0);
		TLOG_ASSERT(ui.shared[cb] != RES_NONE);
		ui.shared_first[cb] = res_bg_tile_index(ui.shared[cb], 0, (u8)cb);
		// Nothing shows these tiles yet: write them straight to VRAM
		if (res_fresh(RES_BG_VRAM))
			make_shared(res_bg_addr(ui.shared[cb]));
	}
	ui.stage_map = res_bg_map(1, 0);
	TLOG_ASSERT(ui.stage_map != RES_NONE);
	ui.stage = res_bg_addr(ui.stage_map);
	// The zoom's sprite: the session's, as it outlives the screens it joins
	// (without room, opening and closing are fades)
	ui.zoom_tiles_h = res_obj_tiles(ART_SPRITE_TILES, 0, 0);
	ui.zoom_bank_h = res_palette(RES_OBJ_PAL, 1, 0);
	ui.zoom_matrix_h = res_affine(0);
	if (ui.zoom_tiles_h != RES_NONE)
		ui.zoom_tiles = res_start(RES_OBJ_VRAM, ui.zoom_tiles_h);
	if (ui.zoom_bank_h != RES_NONE)
		ui.zoom_bank = (u8)res_start(RES_OBJ_PAL, ui.zoom_bank_h);
	if (ui.zoom_matrix_h != RES_NONE)
		ui.zoom_matrix = (u8)res_start(RES_AFFINE, ui.zoom_matrix_h);
	ui_dim_set_minutes(ui_dim_setting());
	load_palette(UI_BANKS);
	gfx_set_dispcnt(DISP_MODE0_OBJ1D | DISP_OBJ);
	ui.active = 1;
	ui_wake();
}

int ui_active(void)
{
	return ui.active;
}

void ui_shutdown(void)
{
	ui.active = 0;
	while (ui_depth())
		ui_core_pop();
	motion_free(ui.fade);
	motion_free(ui.dim_motion);
	ui.zoom = ZOOM_NONE;
	ui.zoom_set = 0;
	ui.wipe = 0;
	gfx_split_off();  // its interrupt never outlives the session (a game may follow)
	res_release(RES_AFFINE, ui.zoom_matrix_h);
	res_release(RES_OBJ_PAL, ui.zoom_bank_h);
	res_release(RES_OBJ_VRAM, ui.zoom_tiles_h);
	ui.zoom_tiles_h = ui.zoom_bank_h = ui.zoom_matrix_h = RES_NONE;
	ui.tr_phase = TR_IDLE;
	keyq_clear(&ui.keys);
	ui.capture = 0;
	memset(&ui.ov, 0, sizeof(ui.ov));
	res_release(RES_BG_VRAM, ui.stage_map);
	for (u32 cb = 0; cb < 3; cb++)
		res_release(RES_BG_VRAM, ui.shared[cb]);
	res_release(RES_BG_PAL, ui.palette);
	res_release(RES_OBJ_PAL, ui.obj_palette);
	gfx_set_blend(0, 0, 0);
	// No sprite of the UI's stays up
	gfx_oam_begin();
	gfx_oam_end();
}

u32 ui_push_fail_test EWRAM_BSS;

struct ui_view *ui_push(const struct ui_view_spec *spec, void *ctx)
{
	if (ui_push_fail_test) {
		ui_push_fail_test--;
		ui_set_error("view %s: failed for a test (ui_push_fail_test)", spec->name);
		return 0;
	}
	struct ui_view *v = ui_core_push(spec, ctx);
	if (!v)
		return 0;
	// Its maps are cleared and drawn once nothing popped shows their VRAM.
	// A dialog over a screen opens with motion once it shows (modal_tick):
	// from where the dim under it already is, if a menu just went. Over
	// another dialog (the boot's notice) it just shows, as the boot waits on it
	if ((spec->flags & UI_VIEW_MODAL) && ui_depth() > 1 &&
	    !(ui_view_at(ui_depth() - 2)->spec->flags & UI_VIEW_MODAL)) {
		const struct ui_theme *t = ui_theme();
		v->flags |= UI_V_ANIM;
		v->opening = 1;
		v->open = t->dialog_in ? 0 : UI_OPEN;
		v->open_blend = t->dialog_in ? blend_for_dim(ui.carry_dim) : UI_OPEN;
		v->offset = v->open ? 0 : t->slide;
	}
	ui.tr_pushed = 1;
	tlog_debug("ui: pushed %s (%u nodes, %lu bytes of state)", spec->name, spec->count,
		   (unsigned long)ui_arena_used());
	return v;
}

void ui_hook_popped(u8 planes)
{
	// Its maps and tiles may go to the next view at once, but they show
	// until regs without them are committed: a view pushed now waits. A
	// screen that went out (ui_leave) already has regs without it written:
	// it waits only for their commit, at the start of the next frame
	if (ui.gap_regs) {
		ui.pop_wait = 2;
		ui.pop_frame = frame_count();
	} else {
		ui.pop_wait = 1;
	}
	keyq_drop_queued(&ui.keys);
	// A dialog pushed next keeps the dim this one left (a menu replaced by a
	// question); the next frame forgets it
	if (ui.last_dim > ui.carry_dim)
		ui.carry_dim = ui.last_dim;
	ui.hidden &= (u8)~planes;
	ui.hide_pending &= (u8)~planes;
	// An overlay's window or dim goes with its plane's view
	if (planes & 1)
		memset(&ui.ov, 0, sizeof(ui.ov));
	if (planes >> ui.reveal_plane & 1)
		ui.reveal = REVEAL_OFF;
	// The view under it shows again as it was: its maps were left alone
	ui_wake();
}

// --- The frame ------------------------------------------------------------------

static void update_all(u32 dt)
{
	for (u32 d = 0; d < ui_depth(); d++) {
		struct ui_view *v = ui_view_at(d);
		for (u32 i = 0; i < v->count; i++) {
			u8 n = (u8)(v->first + i);
			const struct ui_ops *ops = ui_kind_ops[v->spec->nodes[i].kind];
			if (ops->update)
				ops->update(n, dt);
		}
	}
}

static void draw_all(void)
{
	int popping = ui.pop_wait == 1 || (ui.pop_wait == 2 && frame_count() == ui.pop_frame);
	ui.lists_hidden = 0;
	for (u32 d = 0; d < ui_depth(); d++) {
		struct ui_view *v = ui_view_at(d);
		int unfinished = 0;
		if (!(v->flags & UI_V_SHOWN)) {
			if (popping) {
				ui_wake();  // next frame
				continue;
			}
			if (!(v->flags & UI_V_CLEARED)) {
				// New maps, on no screen: cleared directly
				for (u32 p = 0; p < UI_PLANES; p++) {
					if (v->maps[p] == RES_NONE)
						continue;
					u32 blank = ui_shared_entry(p, UI_TILE_BLANK);
					blank |= blank << 16;
					CpuFastSet(&blank, res_bg_addr(v->maps[p]),
						   (v->tall >> p & 1 ? 1024u : 512u) | 1 << 24);
				}
				v->flags |= UI_V_CLEARED;
			}
		}
		for (u32 i = 0; i < v->count; i++) {
			u8 n = (u8)(v->first + i);
			struct ui_node *node = ui_node(n);
			u32 kind = v->spec->nodes[i].kind;
			if (kind == UI_LIST && (node->flags & UI_N_HIDDEN))
				ui.lists_hidden |= (u8)(1u << v->spec->nodes[i].plane);
			if (!(node->flags & UI_N_DIRTY))
				continue;
			node->flags &= (u8)~UI_N_DIRTY;
			const struct ui_ops *ops = ui_kind_ops[v->spec->nodes[i].kind];
			if (ops->draw)
				ops->draw(n);
			node->flags |= UI_N_DRAWN;
			// A node that dirtied itself again goes on next frame
			unfinished |= node->flags & UI_N_DIRTY;
		}
		if (!(v->flags & UI_V_SHOWN) && !unfinished) {
			// Its planes go on together, once what it queued is in VRAM
			v->flags |= UI_V_SHOWN;
			ui.shown_frame[d] = frame_count();
			// Its first frame shows a step of its fade (not of its slide:
			// its lists have set their scroll from the offset already)
			if (animated(v) && v->opening > 0)
				v->open_blend = step_towards(v->open_blend, 1, 1, ui_theme()->dialog_in);
			// A transition's blend goes with them: its first step shows with the
			// screen. A zoom shows it only once its uploads are done (zoom_ready),
			// and its sprite moves on meanwhile
			if (!ui.zoom)
				gfx_hold_for_queue(GFX_DIRTY_DISPCNT | GFX_DIRTY_BGCNT | GFX_DIRTY_BGOFS |
						   (ui.tr_phase != TR_IDLE || animated(v) ? GFX_DIRTY_BLEND : 0));
		}
	}
}

// 1 if view v (the top one) is on screen and still: nothing to draw or
// upload, no transition
static int top_still(const struct ui_view *v)
{
	if (!(v->flags & UI_V_SHOWN) || gfx_queue_len() || gfx_pending() || ui.tr_phase != TR_IDLE || v->opening)
		return 0;
	for (u32 i = 0; i < v->count; i++)
		if (ui_node((u8)(v->first + i))->flags & UI_N_DIRTY)
			return 0;
	return 1;
}

int ui_settled(void)
{
	struct ui_view *v = ui_top();
	return v && !ui.pop_wait && top_still(v);
}

// Logs "screen ready" once the top view is on screen and still
static void ready_check(void)
{
	struct ui_view *v = ui_top();
	if (!v || (v->flags & UI_V_READY) || !top_still(v))
		return;
	v->flags |= UI_V_READY;
	tlog_debug("screen ready: %s", v->spec->name);
}

u32 ui_frame(void)
{
	u32 dt = frame_begin();
	if (!gfx_queue_len()) {
		ui.stage_used = 0;
		ui.span_count = 0;
	}
	if (ui.pop_wait == 2 && frame_count() != ui.pop_frame && !(gfx_dirty_regs & GFX_DIRTY_DISPCNT))
		ui.pop_wait = 0;
	reveal_tick(dt);
	ready_check();
	const struct ui_view *top = ui_top();
	input_page_keys(&ui.in, top && (top->spec->flags & UI_VIEW_LR_PAGES) ? INPUT_LEFT | INPUT_RIGHT : 0);
	input_poll(&ui.in, dt);
	u16 fire = ui.in.fire;
	if (dim_step(dt))
		fire = 0;
	fire &= (u16)~ui.wake_keys;
	// While no page shows (a screen going out, the gap, the zoom's sprite),
	// A, START and SELECT would act on one the user hasn't seen: A, A on
	// home would start Recent's first game. They're dropped; the d-pad, L,
	// R and B wait for the next screen. Once it shows (coming in, the
	// zoom's card) every key acts
	u16 unseen = ui.tr_phase == TR_OUT || ui.tr_phase == TR_GAP || (ui.zoom && ui.zoom_phase == ZP_SPRITE)
			     ? (u16)(INPUT_A | INPUT_START | INPUT_SELECT)
			     : 0;
	fire &= (u16)~unseen;
	if (ui.capture || ui.tr_phase == TR_OUT || ui.tr_phase == TR_GAP) {
		// A screen going out or a dialog closing: for the view after it
		keyq_hold(&ui.keys, fire, ui.in.pressed);
		fire = 0;
	} else {
		u16 queued = keyq_take(&ui.keys);
		if (queued) {
			// Pressed while the screen was awake: if DIM came on since, they
			// wake it, and act
			ui.idle = 0;
			if (ui.dimmed)
				dim_off();
			fire |= queued;
			// Handlers that act on a press (L, R) see a held-back one as one
			ui.in.pressed |= queued;
		}
		fire &= (u16)~unseen;  // queued ones too (ui_queue_keys)
	}
	// A menu or dialog still opening takes no A or START, which pick and
	// confirm: a quick double A would open a game's menu and pick its
	// first item (CLEAN BOOT). The d-pad and B act at once
	top = ui_top();
	if (top && (top->spec->flags & UI_VIEW_MODAL) && (!(top->flags & UI_V_SHOWN) || top->opening > 0))
		fire &= (u16) ~(INPUT_A | INPUT_START);
	if (fire) {
		frame_wake();
		ui_dispatch_keys(fire);
		ui_apply_pops();
	}
	frame_split();  // keys
	if (motion_update(dt))
		frame_wake();
	modal_tick(dt);
	tr_tick(dt);
	wipe_tick(dt);
	// Their frames are measured, in mGBA only (the tests' figures); a zoom's
	// and a wipe's by their own names
	if (((ui.tr_phase != TR_IDLE && !ui.zoom) || (ui_top() && ui_top()->opening)) && tlog_in_emulator())
		frame_measure(ui.tr_phase != TR_IDLE ? "ui-transition" : "ui-dialog-motion");
	obj_levels();
	gfx_oam_begin();
	update_all(dt);
	zoom_obj();
	gfx_oam_end();
	ui_apply_pops();  // asked for by handlers of events in the update (autoscroll)
	frame_split();  // motion and update
	// A view still to be shown (one pushed after a pop waits a frame) wakes
	// the frames until it is: nothing else may (a view without a list)
	top = ui_top();
	if (top && !(top->flags & UI_V_SHOWN))
		frame_wake();
	if (!frame_idle()) {
		// None under the wipe: a job's card read holds DMA3 for lines,
		// which would hold the split's interrupt back (a jumping edge)
		if (!ui.wipe)
			jobs_run(JOB_DEADLINE);
		frame_split();  // jobs
		draw_all();
		frame_split();  // draw
		regs_update();
	}
	frame_end();
	return ui_depth();
}
