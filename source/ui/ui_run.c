// The hardware side of the UI (ui.h): the frame loop, planes, windows and
// blending, the VRAM staging area, palettes and fades, and DIM.
#include <gba_systemcalls.h>
#include <string.h>

#include "ui_priv.h"
#include "sdtheme.h"
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
#define BLEND_DARKEN 0x00C0

// A page's fade-in (ui_plane_reveal)
enum {
	REVEAL_OFF,
	REVEAL_ASKED,    // the next regs show the plane, faded all the way
	REVEAL_WRITTEN,  // those regs wait for their commit (DISPCNT may be held)
	REVEAL_RUN,      // on screen: one step a frame
};

#define TAG_PALETTE 0x5500
#define TAG_SHARED 0x5510  // + character block
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
	u8 palette;        // res handle of the two theme banks
	u8 shared[3];      // shared tiles of each character block planes use
	u16 shared_first[3];
	struct stage_span spans[STAGE_SPANS];
	u8 span_count;
	u8 skip;           // the list skips its scroll animation (fast repeat)
	u8 hidden;         // planes hidden for a page change
	u8 hide_pending;   // hidden, but no DISPCNT without them written yet
	u32 hide_frame[UI_PLANES];  // the frame whose regs hid them
	u8 pop_wait;       // 1: popped, regs not updated; 2: updated in pop_frame
	u8 lists_hidden;   // planes of lists hidden with ui_show
	u16 wake_keys;     // keys that woke the screen from DIM, ignored until let go
	u16 queued_keys;   // ui_queue_keys: dispatched next frame
	u32 pop_frame;
	// A page's fade-in
	u8 reveal;        // REVEAL_*
	u8 reveal_plane;
	u8 reveal_step;   // frames of it shown, of reveal_frames
	u8 reveal_frames; // the theme's page_fade when it started
	u32 reveal_frame; // the frame whose regs showed the plane
	// Fades
	MotionId fade;
	u16 fade_colour;
	// DIM and the modal dim
	u32 idle;          // frames with no key held
	u32 dim_after;     // frames idle before dimming; 0 off
	MotionId dim_motion;
	MotionId modal_motion;
	s16 dim_y;         // BLDY of DIM (0-16)
	s16 modal_y;       // BLDY under a modal view
	u8 dimmed;
	u8 wall_retry;     // a view was popped: put the wallpaper back if a screen took its room
} ui EWRAM_BSS;

void ui_wake(void)
{
	frame_wake();
}

void ui_queue_keys(u16 keys)
{
	ui.queued_keys |= keys;
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
	u8 h = res_bg_tiles((u16)count, 0, (u8)cb, tag);
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
	ui.reveal_plane = (u8)plane;
	ui.reveal_step = 1;
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

int ui_draw_room(u32 cycles)
{
	return frame_elapsed() + cycles <= UI_DRAW_BUDGET;
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
	palette_fade(gfx_pal_faded, gfx_pal_unfaded, 512, ui.fade_colour, (u32)f);
	gfx_pal_mark(0xFFFFFFFF);
	gfx_pal_use_faded(1);
}

static void load_palette(void)
{
	u16 pal[32];
	ui_theme_palette(ui_theme(), ui_accent(), pal);
	gfx_pal_load(0, pal, 32);
	if (gfx_pal_faded_on)
		fade_set(0, motion_value(ui.fade));
}

void ui_theme_set(const struct ui_theme *theme, u16 accent)
{
	ui_theme_select(theme, accent);
	ui.in.timing = &ui_theme()->input;
	ui_wallpaper_setup();
	load_palette();
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

// Counts idle frames and dims or brightens; 1 if a key woke the screen (it
// does nothing else)
static int dim_step(u32 dt)
{
	ui.wake_keys &= ui.in.held;
	if (ui.in.held) {
		ui.idle = 0;
		if (ui.dimmed) {
			ui.wake_keys = ui.in.held;  // they don't repeat either
			ui.dimmed = 0;
			motion_free(ui.dim_motion);
			ui.dim_motion = MOTION_NONE;
			ui.dim_y = 0;
			ui_wake();
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

static void regs_update(void)
{
	u32 top = ui_depth();
	u16 dispcnt = DISP_MODE0_OBJ1D | DISP_OBJ;
	u32 lower = 0;  // planes of views under a modal top view
	const struct ui_view *tv = ui_top();
	int modal = tv && (tv->spec->flags & UI_VIEW_MODAL) && top > 1;
	for (u32 p = 0; p < UI_PLANES; p++) {
		u32 d;
		struct ui_view *v = plane_owner(p, &d);
		if (!v)
			continue;
		u32 prio = top - 1 - d;
		if (prio > 3)
			prio = 3;
		gfx_set_bgcnt(p, (u16)(prio | ui_plane_cb(p) << 2 | (u32)res_bg_screenblock(v->maps[p]) << 8));
		if (!((ui.hidden | ui.lists_hidden) >> p & 1))
			dispcnt |= (u16)DISP_BG(p);
		if (modal && d < top - 1)
			lower |= LAYER_BG(p);
	}
	// The SD theme's wallpaper, behind everything, while no view takes plane 3
	u32 d3;
	u16 wall;
	if (!plane_owner(3, &d3) && ui_wallpaper_bgcnt(&wall)) {
		gfx_set_bgcnt(3, wall);
		gfx_set_scroll(3, 0, 0);
		dispcnt |= (u16)DISP_BG(3);
		if (modal)
			lower |= LAYER_BG(3);
	}
	// Static planes follow their view's slide; lists set their own scroll
	u32 scrollers = 0, list_planes = 0, win_plane[UI_SCROLLERS] = {0, 0};
	const struct ui_node *win_node[UI_SCROLLERS] = {0, 0};
	for (u32 d = top; d-- > 0;) {
		struct ui_view *v = ui_view_at(d);
		for (u32 i = 0; i < v->count; i++) {
			const struct ui_spec *s = &v->spec->nodes[i];
			const struct ui_node *n = ui_node((u8)(v->first + i));
			if (s->kind != UI_LIST && s->kind != UI_GRID)
				continue;
			u32 d2;
			if (plane_owner(s->plane, &d2) != v)
				continue;
			list_planes |= 1u << s->plane;
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
			gfx_set_scroll(p, 0, (u16)(-v->offset));
	}
	if (scrollers) {
		u32 outside = LAYER_ALL;
		for (u32 w = 0; w < scrollers; w++)
			outside &= ~LAYER_BG(win_plane[w]);
		u16 inside[2] = {0, 0};
		for (u32 w = 0; w < scrollers; w++) {
			const struct ui_node *n = win_node[w];
			const struct ui_view *v = ui_view_at(n->view);
			s32 y0 = n->y + v->offset, y1 = y0 + n->h;
			y0 = y0 < 0 ? 0 : y0 > 160 ? 160 : y0;
			y1 = y1 < 0 ? 0 : y1 > 160 ? 160 : y1;
			gfx_set_win(w, (u16)(n->x << 8 | (n->x + n->w)));
			gfx_set_win(2 + w, (u16)(y0 << 8 | y1));
			inside[w] = (u16)((outside | LAYER_BG(win_plane[w])) | WIN_EFFECTS);
			dispcnt |= w ? DISP_WIN1 : DISP_WIN0;
		}
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
	// Blending: DIM darkens everything; a modal view darkens what is under it
	s32 y = 0;
	u32 targets = 0;
	if (modal && ui.modal_y > 0) {
		y = ui.modal_y;
		targets = lower | LAYER_BD;
	}
	if (ui.dim_y > 0) {
		targets = LAYER_ALL | LAYER_BD;
		if (ui.dim_y > y)
			y = ui.dim_y;
	}
	if (y > 16)
		y = 16;
	if (targets && y) {
		ui.reveal = REVEAL_OFF;  // one blend at a time: the page just shows
		gfx_set_blend((u16)(BLEND_DARKEN | targets), 0, (u16)y);
	} else if (ui.reveal != REVEAL_OFF) {
		// The list's plane over everything behind it
		u32 p = LAYER_BG(ui.reveal_plane), eva = 16u * ui.reveal_step / ui.reveal_frames;
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

// --- The stack ----------------------------------------------------------------

const struct ui_theme *ui_theme_setting(u16 *accent)
{
	// Stored as index + 1, so 0 and erased (0xFFFF) both mean the theme's own
	u16 a = Read_SET_info(assress_accent);
	*accent = a >= 1 && a <= ui_accent_count ? ui_accents[a - 1].colour : UI_ACCENT_THEME;
	u16 t = Read_SET_info(assress_theme);
	if (t == UI_THEME_SD && ui_theme_sd())
		return ui_theme_sd();
	return t == 1 ? &ui_theme_light : &ui_theme_dark;
}

void ui_init(const struct ui_theme *theme)
{
	u16 accent = UI_ACCENT_THEME;
	memset(&ui, 0, sizeof(ui));
	ui_core_reset();
	if (!theme)
		theme = ui_theme_setting(&accent);
	ui_theme_select(theme, accent);
	input_init(&ui.in, &ui_theme()->input);
	// Keys already held (from the screen before) count as held, not pressed
	input_poll(&ui.in, 1, 0);
	ui.palette = res_palette(RES_BG_PAL, 2, TAG_PALETTE);
	TLOG_ASSERT(ui.palette != RES_NONE && res_start(RES_BG_PAL, ui.palette) == 0);
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
	ui_wallpaper_setup();
	load_palette();
	gfx_set_dispcnt(DISP_MODE0_OBJ1D | DISP_OBJ);
	ui_wake();
}

void ui_shutdown(void)
{
	while (ui_depth())
		ui_core_pop();
	motion_free(ui.fade);
	motion_free(ui.dim_motion);
	motion_free(ui.modal_motion);
	res_release(RES_BG_VRAM, ui.stage_map);
	for (u32 cb = 0; cb < 3; cb++)
		res_release(RES_BG_VRAM, ui.shared[cb]);
	res_release(RES_BG_PAL, ui.palette);
	gfx_set_blend(0, 0, 0);
}

struct ui_view *ui_push(const struct ui_view_spec *spec, void *ctx)
{
	u16 wall;
	int wall_up = ui_wallpaper_bgcnt(&wall);
	struct ui_view *v = ui_core_push(spec, ctx);
	// A screen the SD theme breaks (patches, row height) gets its base theme
	const struct ui_theme *base = v ? 0 : ui_theme_fallback_base();
	if (base) {
		const struct ui_theme *sd_theme = ui_theme();
		u16 accent = ui_accent();
		ui_theme_set(base, accent);
		v = ui_core_push(spec, ctx);
		if (v)
			ui_theme_fell_back(ui_error());
		else
			ui_theme_set(sd_theme, accent);
	}
	if (!v)
		return 0;
	// A push that took the wallpaper's room (sdtheme.h) waits as after a
	// pop: it draws once regs without the wallpaper are committed, or its
	// tiles would show on plane 3 for a frame
	if (wall_up && !ui_wallpaper_bgcnt(&wall))
		ui.pop_wait = 1;
	// Its maps are cleared and drawn once nothing popped shows their VRAM
	if ((spec->flags & UI_VIEW_MODAL) && ui_depth() > 1) {
		const struct ui_theme *t = ui_theme();
		if (t->slide) {
			v->offset = t->slide;
			v->slide = motion_spring(MOTION_TO_S16(&v->offset), FIX(t->slide), SPRING_FAST, ui_owner(v));
			motion_spring_target(v->slide, 0);
		}
		motion_free(ui.modal_motion);
		ui.modal_motion = motion_tween(MOTION_TO_S16(&ui.modal_y), FIX(ui.modal_y), FIX(t->modal_dim), 8,
					       EASE_OUT_CUBIC, 0);
	}
	tlog_debug("ui: pushed %s (%u nodes, %lu bytes of state)", spec->name, spec->count,
		   (unsigned long)ui_arena_used());
	return v;
}

void ui_hook_popped(u8 planes)
{
	// Its maps and tiles may go to the next view at once, but they show
	// until regs without them are committed: a view pushed now waits
	ui.pop_wait = 1;
	ui.wall_retry = 1;
	ui.queued_keys = 0;
	ui.hidden &= (u8)~planes;
	ui.hide_pending &= (u8)~planes;
	if (planes >> ui.reveal_plane & 1)
		ui.reveal = REVEAL_OFF;
	// The view under it shows again as it was: its maps were left alone
	const struct ui_view *v = ui_top();
	if (!v || !(v->spec->flags & UI_VIEW_MODAL)) {
		motion_free(ui.modal_motion);
		ui.modal_motion = MOTION_NONE;
		ui.modal_y = 0;
	}
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
					CpuFastSet(&blank, res_bg_addr(v->maps[p]), 512 | 1 << 24);
				}
				v->flags |= UI_V_CLEARED;
			}
		}
		for (u32 i = 0; i < v->count; i++) {
			u8 n = (u8)(v->first + i);
			struct ui_node *node = ui_node(n);
			u32 kind = v->spec->nodes[i].kind;
			if ((kind == UI_LIST || kind == UI_GRID) && (node->flags & UI_N_HIDDEN))
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
			gfx_hold_for_queue(GFX_DIRTY_DISPCNT | GFX_DIRTY_BGCNT | GFX_DIRTY_BGOFS);
		}
	}
}

// Logs "screen ready" once the top view is on screen and still
static void ready_check(void)
{
	struct ui_view *v = ui_top();
	if (!v || (v->flags & (UI_V_SHOWN | UI_V_READY)) != UI_V_SHOWN)
		return;
	if (gfx_queue_len() || gfx_pending() || motion_running(v->slide) || motion_running(ui.modal_motion))
		return;
	for (u32 i = 0; i < v->count; i++)
		if (ui_node((u8)(v->first + i))->flags & UI_N_DIRTY)
			return;
	v->flags |= UI_V_READY;
	tlog_debug("screen ready: %s", v->spec->name);
}

static int skip_animation(void)
{
	const InputState *s = &ui.in;
	for (u32 k = 4; k < 8; k++)  // the d-pad
		if ((s->held >> k & 1) && s->held_frames[k] >= s->timing->fast_after)
			return 1;
	return 0;
}

u32 ui_frame(void)
{
	u32 dt = frame_begin();
	if (!gfx_queue_len()) {
		ui.stage_used = 0;
		ui.span_count = 0;
	}
	if (ui.pop_wait == 2 && frame_count() != ui.pop_frame)
		ui.pop_wait = 0;
	reveal_tick(dt);
	ready_check();
	input_poll(&ui.in, dt, ui.skip);
	ui.skip = (u8)skip_animation();
	u16 fire = ui.in.fire;
	if (dim_step(dt))
		fire = 0;
	fire &= (u16)~ui.wake_keys;
	if (!ui.dimmed)
		fire |= ui.queued_keys;
	ui.queued_keys = 0;
	if (fire) {
		frame_wake();
		ui_dispatch_keys(fire);
		ui_apply_pops();
	}
	if (motion_update(dt))
		frame_wake();
	update_all(dt);
	// A view still to be shown (one pushed after a pop waits a frame) wakes
	// the frames until it is: nothing else may (a view without a list)
	const struct ui_view *top = ui_top();
	if (top && !(top->flags & UI_V_SHOWN))
		frame_wake();
	// Once nothing popped shows its VRAM any more (sdtheme.h)
	if (ui.wall_retry) {
		if (!ui.pop_wait) {
			ui.wall_retry = 0;
			ui_wallpaper_setup();
		}
		frame_wake();
	}
	if (!frame_idle()) {
		jobs_run(JOB_DEADLINE);
		draw_all();
		regs_update();
	}
	frame_end();
	return ui_depth();
}

int ui_skipping(void)
{
	return ui.skip;
}
