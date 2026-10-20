// The hardware side of the UI (ui.h): the frame loop, planes, windows and
// blending, the VRAM staging area, palettes and fades, and DIM.
#include <gba_systemcalls.h>
#include <string.h>

#include "ui_priv.h"
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
#define BGCNT_MOSAIC 0x0040

// Screen transitions (ui_leave)
enum {
	TR_IDLE,
	TR_OUT,   // the screen goes out (ui_leave runs these frames)
	TR_GAP,   // nothing shows: the next screen is set up and drawn
	TR_IN,    // the next screen comes in; it takes keys already
};
// How a transition fades
enum {
	TR_ALPHA,    // the screen's planes over the backdrop
	TR_DARKEN,   // BLDY towards black: under a dialog, and out of the engine
};
#define TR_MOSAIC_MAX 7  // mosaic blocks of 8 px when a screen has gone
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
static void load_palette(u32 banks)
{
	u16 pal[UI_BANKS * 16];
	ui_theme_banks(ui_theme(), ui_item(), pal);
	gfx_pal_load(UI_THEME_RUN_FIRST / 16, pal, banks * 16);
	gfx_pal_load(16 + ui_obj_bank(), pal, 16);  // the sprites': bank 0's roles
	if (gfx_pal_faded_on)
		fade_set(0, motion_value(ui.fade));
}

void ui_theme_set(const struct ui_theme *theme)
{
	ui_theme_select(theme);
	ui_list_cache_clear();  // rows may draw differently
	ui.in.timing = &ui_theme()->input;
	load_palette(UI_BANKS);
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

int ui_dimmed(void)
{
	return ui.dimmed;
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

void ui_enter(u32 kind, s32 dir)
{
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
	const struct ui_transition *t = &ui_theme()->transition[kind];
	s32 from = ui.tr_phase == TR_IN ? ui.tr_level : 0;
	u8 mode = kind == UI_TR_LEAVE ? TR_DARKEN : TR_ALPHA;
	// A dialog or menu goes at once; the dim under it carries on into the
	// fade, which is then a darken over everything
	if (ui_depth() > 1 && (ui_top()->spec->flags & UI_VIEW_MODAL)) {
		s32 y = modal_dim_now() * (UI_OPEN / 16);
		from = y > from ? y : from;
		mode = TR_DARKEN;
		while (ui_depth() > 1 && (ui_top()->spec->flags & UI_VIEW_MODAL))
			ui_pop();
	}
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

// One frame of the transition, before the widgets' update (lists read their
// view's offset there)
static void tr_tick(u32 dt)
{
	if (ui.tr_phase == TR_OUT) {
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
	if (ui.tr_phase == TR_GAP && ui.tr_pushed && all_shown())
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
	u16 mosaic = (u16)(tr && (ui.tr_effects & UI_TR_MOSAIC) ? BGCNT_MOSAIC : 0);
	for (u32 p = 0; p < UI_PLANES; p++) {
		u32 d;
		struct ui_view *v = plane_owner(p, &d);
		if (!v)
			continue;
		u32 prio = ui.level[d] + (ui.obj_prio[d] != PRIO_NONE && !(v->front >> p & 1));
		if (prio > 3)
			prio = 3;
		gfx_set_bgcnt(p, (u16)(prio | ui_plane_cb(p) << 2 | (u32)res_bg_screenblock(v->maps[p]) << 8 | mosaic |
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
	if (tr && (ui.tr_effects & UI_TR_MOSAIC)) {
		u32 m = (u32)ui.tr_level * TR_MOSAIC_MAX / UI_OPEN;
		gfx_set_mosaic((u16)(m | m << 4));
	} else if (gfx_regs.mosaic) {
		gfx_set_mosaic(0);
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
			// Under a dialog its planes stay as they are and the dim carries on
			s32 y = (ui.tr_level * 16 + UI_OPEN / 2) / UI_OPEN;
			if (modal && modal_y > y)
				y = modal_y;
			u32 targets = modal ? lower | LAYER_BD : LAYER_ALL | LAYER_BD;
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
	if (prio == PRIO_NONE)
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
			// A transition's blend goes with them: its first step shows with the screen
			gfx_hold_for_queue(
				GFX_DIRTY_DISPCNT | GFX_DIRTY_BGCNT | GFX_DIRTY_BGOFS |
				(ui.tr_phase != TR_IDLE || animated(v) ? GFX_DIRTY_BLEND | GFX_DIRTY_MOSAIC : 0));
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
	// Their frames are measured, in mGBA only (the tests' figures)
	if ((ui.tr_phase != TR_IDLE || (ui_top() && ui_top()->opening)) && tlog_in_emulator())
		frame_measure(ui.tr_phase != TR_IDLE ? "ui-transition" : "ui-dialog-motion");
	obj_levels();
	gfx_oam_begin();
	update_all(dt);
	gfx_oam_end();
	ui_apply_pops();  // asked for by handlers of events in the update (autoscroll)
	frame_split();  // motion and update
	// A view still to be shown (one pushed after a pop waits a frame) wakes
	// the frames until it is: nothing else may (a view without a list)
	top = ui_top();
	if (top && !(top->flags & UI_V_SHOWN))
		frame_wake();
	if (!frame_idle()) {
		jobs_run(JOB_DEADLINE);
		frame_split();  // jobs
		draw_all();
		frame_split();  // draw
		regs_update();
	}
	frame_end();
	return ui_depth();
}
