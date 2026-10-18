// The simple widgets (ui.h): label, panel, image, progress bar, scroll bar,
// and the table of every kind's ops. Lists and grids are in ui/list.c.
#include <string.h>

#include "ui_priv.h"
#include "../engine/gfx.h"
#include "../engine/res.h"
#include "../engine/text.h"

// The node's tiles as a text window (column-major, its rectangle's size)
static struct text_window window_of(u8 node, u16 first)
{
	const struct ui_node *n = ui_node(node);
	u32 cb = ui_plane_cb(ui_node_spec(node)->plane);
	struct text_window w = {(u32 *)(0x06000000 + cb * 0x4000 + first * 32u), (u8)(n->w / 8), (u8)(n->h / 8)};
	return w;
}

// Points the node's rectangle in its map at its tiles (column-major from
// first, in bank); 0 if it must try again
static int map_window(u8 node, u16 first, u32 bank)
{
	const struct ui_node *n = ui_node(node);
	u32 tx = (u32)n->x / 8, ty = (u32)n->y / 8, w = (u32)n->w / 8, h = (u32)n->h / 8;
	u16 *rows = ui_map_rows(node, ty, h);
	if (!rows)
		return 0;
	for (u32 y = 0; y < h; y++)
		for (u32 x = 0; x < w; x++)
			rows[y * 32 + tx + x] = ui_entry(first + x * h + y, bank);
	return 1;
}

static int measure_window(u8 node, struct engine_budget *need, u32 sets)
{
	const struct ui_node *n = ui_node(node);
	need->bg_tiles = (u16)(need->bg_tiles + sets * (u32)(n->w / 8) * (u32)(n->h / 8));
	return 1;
}

// --- Label ----------------------------------------------------------------

struct label_state {
	const char *text;
	u16 first[2];
	u8 tiles[2];
	u8 cur;   // the set the map shows
	u8 sets;  // 1, or 2 with UI_LABEL_SWAP
};

static const struct ui_label_config *label_config(u8 node)
{
	static const struct ui_label_config none = {"", UI_FONT_BODY, UI_ALIGN_LEFT, 0};
	const struct ui_label_config *c = ui_node_spec(node)->data;
	return c ? c : &none;
}

static int label_measure(u8 node, struct engine_budget *need)
{
	return measure_window(node, need, (label_config(node)->flags & UI_LABEL_SWAP) ? 2 : 1);
}

static int label_init(u8 node)
{
	struct label_state *st = ui_node_state(node);
	const struct ui_label_config *c = label_config(node);
	st->text = c->text ? c->text : "";
	st->sets = (c->flags & UI_LABEL_SWAP) ? 2 : 1;
	const struct ui_node *n = ui_node(node);
	for (u32 i = 0; i < st->sets; i++) {
		st->tiles[i] = ui_tiles(node, (u32)(n->w / 8) * (u32)(n->h / 8), 0, &st->first[i]);
		if (st->tiles[i] == RES_NONE)
			return 0;
	}
	return 1;
}

static void label_draw(u8 node)
{
	struct label_state *st = ui_node_state(node);
	struct ui_node *n = ui_node(node);
	if (n->flags & UI_N_HIDDEN) {
		if (!ui_map_fill(node, ui_shared_entry(ui_node_spec(node)->plane, UI_TILE_BLANK)))
			ui_dirty(node, 0xFF);
		n->flags &= (u8)~UI_N_DRAWN;
		return;
	}
	// A shown label with two sets draws into the hidden one, then flips
	int flip = st->sets == 2 && ui_node_shown(node) && (n->flags & UI_N_DRAWN);
	u32 set = flip ? st->cur ^ 1u : st->cur;
	const struct ui_label_config *c = label_config(node);
	const struct torii_font *font = ui_font(c->font);
	const struct ui_theme *t = ui_theme();
	struct text_window win = window_of(node, st->first[set]);
	u32 w = (u32)n->w, h = (u32)n->h;
	text_clear(&win, 0, 0, w, h, ui_paper(node, UI_ROLE_NONE));
	u32 ink = ui_ink(node, UI_ROLE_TEXT);

	const char *s = st->text;
	u32 lines = 1;
	for (const char *p = s; *p; p++)
		lines += *p == '\n';
	u32 y = lines * font->line_height < h ? (h - lines * font->line_height) / 2 : 0;
	u32 pad = c->align == UI_ALIGN_CENTER ? 0 : t->pad;
	u32 clip = w > 2 * pad ? w - 2 * pad : w;
	while (y + font->line_height <= h) {
		u32 len = 0;
		while (s[len] && s[len] != '\n')
			len++;
		struct text_line line;
		text_layout(font, s, len, clip, &line);
		u32 x = pad;
		if (c->align == UI_ALIGN_CENTER)
			x = (w - line.extent) / 2;
		else if (c->align == UI_ALIGN_RIGHT)
			x = w - pad - line.extent;
		text_draw_line(&win, font, x, y, ink, &line);
		y += font->line_height;
		if (!s[len])
			break;
		s += len + 1;
	}
	if (flip || !(n->flags & UI_N_DRAWN)) {
		if (!map_window(node, st->first[set], UI_BANK_NORMAL)) {
			ui_dirty(node, 0xFF);  // the text is in the hidden set: try the flip again
			return;
		}
	}
	st->cur = (u8)set;
}

void ui_label_set(u8 node, const char *text)
{
	struct label_state *st = ui_node_state(node);
	st->text = text ? text : "";
	ui_dirty(node, 0xFF);
}

static const struct ui_ops label_ops = {sizeof(struct label_state), label_measure, label_init, 0, 0, label_draw};

// --- Panel ----------------------------------------------------------------

static void panel_draw(u8 node)
{
	const struct ui_spec *s = ui_node_spec(node);
	struct ui_node *n = ui_node(node);
	u32 plane = s->plane;
	if (n->flags & UI_N_HIDDEN) {
		if (!ui_map_fill(node, ui_shared_entry(plane, UI_TILE_BLANK)))
			ui_dirty(node, 0xFF);
		return;
	}
	const struct ui_panel_config *c = s->data;
	u32 style = c ? c->style : UI_PANEL_FLAT;
	u32 tx = (u32)n->x / 8, ty = (u32)n->y / 8, w = (u32)n->w / 8, h = (u32)n->h / 8;
	u16 *rows = ui_map_rows(node, ty, h);
	if (!rows) {
		ui_dirty(node, 0xFF);
		return;
	}
	u16 fill = ui_shared_entry(plane, UI_TILE_SOLID + ui_paper(node, UI_ROLE_PANEL));
	for (u32 y = 0; y < h; y++) {
		for (u32 x = 0; x < w; x++) {
			u16 e = fill;
			int left = x == 0, right = x == w - 1, top = y == 0, bottom = y == h - 1;
			if (style == UI_PANEL_BORDER) {
				if ((left || right) && (top || bottom))
					e = ui_shared_entry(plane, UI_TILE_BORDER + (right ? 1u : 0u) + (bottom ? 2u : 0u));
				else if (top || bottom)
					e = ui_shared_entry(plane, UI_TILE_BORDER + 4 + (bottom ? 1u : 0u));
				else if (left || right)
					e = ui_shared_entry(plane, UI_TILE_BORDER + 6 + (right ? 1u : 0u));
			} else if (style == UI_PANEL_ROUND && (left || right) && (top || bottom)) {
				e = ui_shared_entry(plane, UI_TILE_ROUND + (right ? 1u : 0u) + (bottom ? 2u : 0u));
			}
			rows[y * 32 + tx + x] = e;
		}
	}
}

static const struct ui_ops panel_ops = {0, 0, 0, 0, 0, panel_draw};

// --- Image ----------------------------------------------------------------

struct image_state {
	u16 first;
	u8 tiles;
	u8 bank;
};

static int image_measure(u8 node, struct engine_budget *need)
{
	const struct ui_image_config *c = ui_node_spec(node)->data;
	if (!c || !c->tiles || !c->tag) {
		ui_set_error("view %s: an image needs tiles and a tag", ui_node_view(node)->spec->name);
		return 0;
	}
	if (c->palette)
		need->bg_banks++;
	return measure_window(node, need, 1);
}

static int image_init(u8 node)
{
	struct image_state *st = ui_node_state(node);
	const struct ui_image_config *c = ui_node_spec(node)->data;
	const struct ui_node *n = ui_node(node);
	u32 count = (u32)(n->w / 8) * (u32)(n->h / 8);
	st->tiles = ui_tiles(node, count, c->tag, &st->first);
	if (st->tiles == RES_NONE)
		return 0;
	// From ROM, so the queue may read it whenever it gets there
	if (res_fresh(RES_BG_VRAM) && !gfx_queue_copy(res_bg_addr(st->tiles), c->tiles, count * 32))
		return 0;
	st->bank = UI_BANK_NORMAL;
	if (c->palette) {
		u8 pal = res_palette(RES_BG_PAL, 1, c->tag);
		if (pal == RES_NONE)
			return 0;
		st->bank = (u8)res_start(RES_BG_PAL, pal);
		if (res_fresh(RES_BG_PAL))
			gfx_pal_load(st->bank, c->palette, 16);
	}
	return 1;
}

static void image_draw(u8 node)
{
	struct image_state *st = ui_node_state(node);
	struct ui_node *n = ui_node(node);
	if (n->flags & UI_N_HIDDEN) {
		if (!ui_map_fill(node, ui_shared_entry(ui_node_spec(node)->plane, UI_TILE_BLANK)))
			ui_dirty(node, 0xFF);
		return;
	}
	u32 tx = (u32)n->x / 8, ty = (u32)n->y / 8, w = (u32)n->w / 8, h = (u32)n->h / 8;
	u16 *rows = ui_map_rows(node, ty, h);
	if (!rows) {
		ui_dirty(node, 0xFF);
		return;
	}
	for (u32 y = 0; y < h; y++)
		for (u32 x = 0; x < w; x++)
			rows[y * 32 + tx + x] = ui_entry(st->first + y * w + x, st->bank);
}

static const struct ui_ops image_ops = {sizeof(struct image_state), image_measure, image_init, 0, 0, image_draw};

// --- Progress bar --------------------------------------------------------

struct bar_state {
	u16 first;
	u8 tiles;
	u8 target;  // scroll bar: the list's node
	u16 drawn0, drawn1;  // the filled span on screen (progress: [0, drawn1))
	u16 want0, want1;
};

static int bar_measure(u8 node, struct engine_budget *need)
{
	return measure_window(node, need, 1);
}

static int bar_init(u8 node)
{
	struct bar_state *st = ui_node_state(node);
	const struct ui_node *n = ui_node(node);
	st->tiles = ui_tiles(node, (u32)(n->w / 8) * (u32)(n->h / 8), 0, &st->first);
	return st->tiles != RES_NONE;
}

// The bar's thickness and top within its rectangle
static void bar_rows(u32 h, u32 *top, u32 *rows)
{
	*rows = h >= 8 ? 4 : h;
	*top = (h - *rows) / 2;
}

static void progress_draw(u8 node)
{
	struct bar_state *st = ui_node_state(node);
	struct ui_node *n = ui_node(node);
	if (n->flags & UI_N_HIDDEN) {
		if (!ui_map_fill(node, ui_shared_entry(ui_node_spec(node)->plane, UI_TILE_BLANK)))
			ui_dirty(node, 0xFF);
		n->flags &= (u8)~UI_N_DRAWN;
		return;
	}
	struct text_window win = window_of(node, st->first);
	u32 top, rows;
	bar_rows((u32)n->h, &top, &rows);
	u32 ink = ui_ink(node, UI_ROLE_ACCENT), track = ui_paper(node, UI_ROLE_TRACK);
	if (!(n->flags & UI_N_DRAWN)) {
		text_clear(&win, 0, 0, (u32)n->w, (u32)n->h, UI_ROLE_NONE);
		text_clear(&win, 0, top, (u32)n->w, rows, track);
		st->drawn1 = 0;
		if (!map_window(node, st->first, UI_BANK_NORMAL)) {
			ui_dirty(node, 0xFF);
			return;
		}
	}
	// Only the pixels that change: no frame shows the bar half redrawn
	if (st->want1 > st->drawn1)
		text_clear(&win, st->drawn1, top, (u32)(st->want1 - st->drawn1), rows, ink);
	else if (st->want1 < st->drawn1)
		text_clear(&win, st->want1, top, (u32)(st->drawn1 - st->want1), rows, track);
	st->drawn1 = st->want1;
}

void ui_progress_set(u8 node, u32 value, u32 max)
{
	struct bar_state *st = ui_node_state(node);
	u32 w = (u32)ui_node(node)->w;
	u32 px = max ? (value >= max ? w : (u32)((unsigned long long)value * w / max)) : 0;
	if (px != st->want1) {
		st->want1 = (u16)px;
		ui_dirty(node, 1);
	}
}

static const struct ui_ops progress_ops = {sizeof(struct bar_state), bar_measure, bar_init, 0, 0, progress_draw};

// --- Scroll bar ---------------------------------------------------------

static int scrollbar_measure(u8 node, struct engine_budget *need)
{
	const struct ui_scrollbar_config *c = ui_node_spec(node)->data;
	const struct ui_view *v = ui_node_view(node);
	u32 kind = c && c->target < v->count ? v->spec->nodes[c->target].kind : UI_KINDS;
	if (kind != UI_LIST && kind != UI_GRID) {
		ui_set_error("view %s: a scroll bar's target must be a list or grid", v->spec->name);
		return 0;
	}
	return measure_window(node, need, 1);
}

static int scrollbar_init(u8 node)
{
	struct bar_state *st = ui_node_state(node);
	const struct ui_scrollbar_config *c = ui_node_spec(node)->data;
	st->target = (u8)(ui_node_view(node)->first + c->target);
	return bar_init(node);
}

static void scrollbar_update(u8 node, u32 dt)
{
	(void)dt;
	struct bar_state *st = ui_node_state(node);
	u32 content, view, scroll, h = (u32)ui_node(node)->h;
	ui_list_scroll_info(st->target, &content, &view, &scroll);
	u32 t0 = 0, t1 = 0;
	if (content > view) {
		u32 len = h * view / content;
		if (len < 8)
			len = 8;
		t0 = (h - len) * scroll / (content - view);
		if (t0 > h - len)
			t0 = h - len;
		t1 = t0 + len;
	}
	if (t0 != st->want0 || t1 != st->want1) {
		st->want0 = (u16)t0;
		st->want1 = (u16)t1;
		ui_dirty(node, 1);
	}
}

// Rows [a, b) of the bar as track (thumb 0) or thumb (1)
static void scrollbar_paint(const struct text_window *win, u32 a, u32 b, int thumb, u32 ink, u32 track)
{
	if (a >= b)
		return;
	if (thumb) {
		text_clear(win, 2, a, 4, b - a, ink);
	} else {
		text_clear(win, 2, a, 1, b - a, UI_ROLE_NONE);
		text_clear(win, 3, a, 2, b - a, track);
		text_clear(win, 5, a, 1, b - a, UI_ROLE_NONE);
	}
}

static void scrollbar_draw(u8 node)
{
	struct bar_state *st = ui_node_state(node);
	struct ui_node *n = ui_node(node);
	if (n->flags & UI_N_HIDDEN) {
		if (!ui_map_fill(node, ui_shared_entry(ui_node_spec(node)->plane, UI_TILE_BLANK)))
			ui_dirty(node, 0xFF);
		n->flags &= (u8)~UI_N_DRAWN;
		return;
	}
	struct text_window win = window_of(node, st->first);
	u32 ink = ui_ink(node, UI_ROLE_ACCENT), track = ui_paper(node, UI_ROLE_TRACK);
	u32 h = (u32)n->h;
	if (!(n->flags & UI_N_DRAWN)) {
		text_clear(&win, 0, 0, (u32)n->w, h, UI_ROLE_NONE);
		scrollbar_paint(&win, 0, h, 0, ink, track);
		st->drawn0 = st->drawn1 = 0;
		if (!map_window(node, st->first, UI_BANK_NORMAL)) {
			ui_dirty(node, 0xFF);
			return;
		}
	}
	u32 o0 = st->drawn0, o1 = st->drawn1, n0 = st->want0, n1 = st->want1;
	// Only rows whose state changes: old thumb rows the new one leaves
	// become track, new ones thumb, in runs
	for (u32 y = 0; y < h;) {
		int was = y >= o0 && y < o1, now = y >= n0 && y < n1;
		u32 end = y + 1;
		while (end < h && (end >= o0 && end < o1) == was && (end >= n0 && end < n1) == now)
			end++;
		if (was != now)
			scrollbar_paint(&win, y, end, now, ink, track);
		y = end;
	}
	st->drawn0 = (u16)n0;
	st->drawn1 = (u16)n1;
}

static const struct ui_ops scrollbar_ops = {sizeof(struct bar_state), scrollbar_measure, scrollbar_init, 0,
					    scrollbar_update, scrollbar_draw};

// --- Every kind ---------------------------------------------------------

const struct ui_ops *const ui_kind_ops[UI_KINDS] = {
	[UI_LABEL] = &label_ops,     [UI_PANEL] = &panel_ops,         [UI_LIST] = &ui_list_ops,
	[UI_GRID] = &ui_list_ops,    [UI_IMAGE] = &image_ops,         [UI_SCROLLBAR] = &scrollbar_ops,
	[UI_PROGRESS] = &progress_ops,
};
