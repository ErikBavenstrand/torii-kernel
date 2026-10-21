// The simple widgets (ui.h): label, panel, image, progress bar, scroll bar,
// and the table of every kind's ops. Lists are in ui/ui_list.c.
#include <string.h>

#include "art.h"
#include "ui_priv.h"
#include "../engine/gfx.h"
#include "../engine/res.h"
#include "../engine/text.h"
#include "../tlog.h"

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
	static const struct ui_label_config none = {"", UI_FONT_BODY, UI_ALIGN_LEFT, 0, 0};
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

void ui_icon8(const struct text_window *win, u32 x, u32 y, u32 id, u32 ink)
{
	if (id >= ART_ICON8_COUNT || win->height * 8u < 8 || y > win->height * 8u - 8 || win->width * 8u < 8 ||
	    x > win->width * 8u - 8)
		return;
	const u8 *bits = art_icon8_bits[id];
	u32 column = win->height * 8u;
	for (u32 r = 0; r < 8; r++)
		for (u32 c = 0; c < 8; c++)
			if (bits[r] >> c & 1) {
				u32 px = x + c, *word = win->tiles + (px / 8) * column + y + r, sh = (px & 7) * 4;
				*word = (*word & ~(15u << sh)) | (ink & 15) << sh;
			}
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
	if (flip && ui_map_busy(node, (u32)n->y / 8, (u32)n->h / 8)) {
		ui_dirty(node, 0xFF);  // the last flip hasn't landed: its set still shows
		return;
	}
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
	if (c->icon8 && pad + 8 <= w) {
		// A section label: its 8x8 icon first, centred on the first line
		ui_icon8(&win, pad, y + (font->line_height > 8 ? (font->line_height - 8) / 2 : 0), c->icon8 - 1u, ink);
		u32 used = 8u + t->look.section_icon_gap;
		pad += used;
		clip = clip > used ? clip - used : 0;
	}
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
					e = ui_shared_entry(plane,
							    UI_TILE_BORDER + (right ? 1u : 0u) + (bottom ? 2u : 0u));
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
	if (!c || !c->tiles) {
		ui_set_error("view %s: an image needs tiles", ui_node_view(node)->spec->name);
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
	// From ROM, so the queue may read it whenever it gets there. With the
	// queue full, the new block goes, not to the cache: revived by the next
	// push, it would look loaded with nothing in it
	if (res_fresh(RES_BG_VRAM) && !gfx_queue_copy(res_bg_addr(st->tiles), c->tiles, count * 32)) {
		res_release(RES_BG_VRAM, st->tiles);
		pool_drop(res_pool(RES_BG_VRAM), st->tiles);
		st->tiles = RES_NONE;
		return 0;
	}
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

u32 ui_image_bank(u8 node)
{
	return ((const struct image_state *)ui_node_state(node))->bank;
}

int ui_image_show(u8 node, const void *tiles, const u16 *palette, u32 colours)
{
	const struct image_state *st = ui_node_state(node);
	const struct ui_image_config *c = ui_node_spec(node)->data;
	const struct ui_node *n = ui_node(node);
	TLOG_ASSERT(c && !c->tag);  // its tiles are its own: never cached for another push
	if (!c || c->tag || st->tiles == RES_NONE)
		return 0;
	u32 bytes = (u32)(n->w / 8) * (u32)(n->h / 8) * 32;
	if (!gfx_queue_copy(res_bg_addr(st->tiles), tiles, bytes))
		return 0;
	if (palette && colours)
		gfx_pal_load(st->bank, palette, colours > 16 ? 16 : colours);
	return 1;
}

static const struct ui_ops image_ops = {sizeof(struct image_state), image_measure, image_init, 0, 0, image_draw};

// --- Progress bar --------------------------------------------------------

struct bar_state {
	u16 first;
	u8 tiles;
	u16 drawn1;  // the filled span on screen, [0, drawn1)
	u16 want1;
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
	*rows = h >= 8 ? 6 : h;
	*top = (h - *rows) / 2;
}

// Columns [a, b) of a progress bar w px wide in role: a pill, its four
// corner pixels in back, what is behind it (bars 3 px or more thick)
static void progress_span(const struct text_window *win, u32 a, u32 b, u32 w, u32 top, u32 rows, u32 role, u32 back)
{
	if (a >= b)
		return;
	text_clear(win, a, top, b - a, rows, role);
	if (rows < 3)
		return;
	if (a == 0) {
		text_clear(win, 0, top, 1, 1, back);
		text_clear(win, 0, top + rows - 1, 1, 1, back);
	}
	if (b == w) {
		text_clear(win, w - 1, top, 1, 1, back);
		text_clear(win, w - 1, top + rows - 1, 1, 1, back);
	}
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
	u32 ink = ui_ink(node, UI_ROLE_ACCENT), back = ui_paper(node, UI_ROLE_NONE), w = (u32)n->w;
	if (!(n->flags & UI_N_DRAWN)) {
		text_clear(&win, 0, 0, w, (u32)n->h, back);
		progress_span(&win, 0, w, w, top, rows, UI_ROLE_TRACK, back);
		st->drawn1 = 0;
		if (!map_window(node, st->first, UI_BANK_NORMAL)) {
			ui_dirty(node, 0xFF);
			return;
		}
	}
	// Only the pixels that change: no frame shows the bar half redrawn
	if (st->want1 > st->drawn1)
		progress_span(&win, st->drawn1, st->want1, w, top, rows, ink, back);
	else if (st->want1 < st->drawn1)
		progress_span(&win, st->want1, st->drawn1, w, top, rows, UI_ROLE_TRACK, back);
	st->drawn1 = st->want1;
}

// The column value of max fills a bar w px wide to
static u32 bar_px(u32 w, u32 value, u32 max)
{
	u32 px = max ? (value >= max ? w : (u32)((unsigned long long)value * w / max)) : 0;
	if (value && max && px < 2)
		px = 2;  // any progress shows: the bar's rounded end
	return px;
}

void ui_progress_set(u8 node, u32 value, u32 max)
{
	struct bar_state *st = ui_node_state(node);
	u32 px = bar_px((u32)ui_node(node)->w, value, max);
	if (px != st->want1) {
		st->want1 = (u16)px;
		ui_dirty(node, 1);
	}
}

int ui_progress_pixels(u8 node, u32 value, u32 max, struct ui_bar_pixels *out)
{
	const struct bar_state *st = ui_node_state(node);
	const struct ui_node *n = ui_node(node);
	if ((n->flags & (UI_N_HIDDEN | UI_N_DRAWN | UI_N_DIRTY)) != UI_N_DRAWN || st->drawn1 != st->want1)
		return 0;
	struct text_window win = window_of(node, st->first);
	out->tiles = win.tiles;
	out->column_words = (u32)win.height * 8;
	bar_rows((u32)n->h, &out->top, &out->rows);
	out->ink = ui_ink(node, UI_ROLE_ACCENT);
	out->x = st->drawn1;
	out->px = bar_px((u32)n->w, value, max);
	return 1;
}

void ui_progress_drew(u8 node, u32 x)
{
	struct bar_state *st = ui_node_state(node);
	st->drawn1 = st->want1 = (u16)x;
}

static const struct ui_ops progress_ops = {sizeof(struct bar_state), bar_measure, bar_init, 0, 0, progress_draw};

// --- Scroll bar ---------------------------------------------------------
//
// Two tile sets, as a label with UI_LABEL_SWAP has: the thumb is drawn into
// the set the map doesn't show, then the map flips to it in VBlank, with the
// list's scroll. Drawn into the shown set, its moves tore while the screen
// scanned the bar out, a frame ahead of the list (the tear watch, -T).

struct scrollbar_state {
	u16 first[2];
	u8 tiles[2];
	u8 target;  // the list's node
	u8 cur;     // the set the map shows
	u16 want0, want1;  // the thumb's rows, [want0, want1)
	u16 drawn0, drawn1;  // the thumb the set cur shows
};

static int scrollbar_measure(u8 node, struct engine_budget *need)
{
	const struct ui_scrollbar_config *c = ui_node_spec(node)->data;
	const struct ui_view *v = ui_node_view(node);
	u32 kind = c && c->target < v->count ? v->spec->nodes[c->target].kind : UI_KINDS;
	if (kind != UI_LIST) {
		ui_set_error("view %s: a scroll bar's target must be a list", v->spec->name);
		return 0;
	}
	return measure_window(node, need, 2);
}

static int scrollbar_init(u8 node)
{
	struct scrollbar_state *st = ui_node_state(node);
	const struct ui_scrollbar_config *c = ui_node_spec(node)->data;
	const struct ui_node *n = ui_node(node);
	st->target = (u8)(ui_node_view(node)->first + c->target);
	for (u32 i = 0; i < 2; i++) {
		st->tiles[i] = ui_tiles(node, (u32)(n->w / 8) * (u32)(n->h / 8), 0, &st->first[i]);
		if (st->tiles[i] == RES_NONE)
			return 0;
	}
	return 1;
}

static void scrollbar_update(u8 node, u32 dt)
{
	(void)dt;
	struct scrollbar_state *st = ui_node_state(node);
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

// A scroll bar row: the track's 1 px line (the card's hairline colour), the
// thumb's 3 px (the item's base), or a thumb's rounded end (its first and
// last rows, the middle pixel only). The thumb's left column in its 8 px
// column is the look's scrollbar_x.
enum {
	SB_TRACK,
	SB_THUMB,
	SB_END
};
static u32 scrollbar_row(u32 y, u32 t0, u32 t1)
{
	if (y < t0 || y >= t1)
		return SB_TRACK;
	return (y == t0 || y == t1 - 1) && t1 - t0 > 2 ? SB_END : SB_THUMB;
}

// The whole bar, its thumb at [t0, t1), into set's tiles: each row of its
// first tile column one word (8 pixels), the track's, the thumb's or a
// thumb end's, on paper
static void scrollbar_paint_set(u8 node, const struct scrollbar_state *st, u32 set, u32 t0, u32 t1)
{
	const struct ui_node *n = ui_node(node);
	struct text_window win = window_of(node, st->first[set]);
	u32 ink = ui_ink(node, UI_ROLE_ACCENT) & 15, paper = ui_paper(node, UI_ROLE_NONE) & 15, h = (u32)n->h;
	text_clear(&win, 8, 0, (u32)n->w, h, paper);  // columns past the bar's, if any
	u32 x = ui_theme()->look.scrollbar_x, sh = 4 * (x > 5 ? 5 : x), base = paper * 0x11111111u;
	u32 mask = 0xFFFu << sh, word[3];
	word[SB_TRACK] = (base & ~mask) | (paper | (u32)UI_ROLE_BORDER << 4 | paper << 8) << sh;
	word[SB_THUMB] = (base & ~mask) | (ink * 0x111u) << sh;
	word[SB_END] = (base & ~mask) | (paper | ink << 4 | paper << 8) << sh;
	for (u32 y = 0; y < h; y++)
		win.tiles[y] = word[scrollbar_row(y, t0, t1)];
}

static void scrollbar_draw(u8 node)
{
	struct scrollbar_state *st = ui_node_state(node);
	struct ui_node *n = ui_node(node);
	if (n->flags & UI_N_HIDDEN) {
		if (!ui_map_fill(node, ui_shared_entry(ui_node_spec(node)->plane, UI_TILE_BLANK)))
			ui_dirty(node, 0xFF);
		n->flags &= (u8)~UI_N_DRAWN;
		return;
	}
	u32 n0 = st->want0, n1 = st->want1;
	// A shown bar draws into the set the map doesn't show, then flips
	int flip = ui_node_shown(node) && (n->flags & UI_N_DRAWN);
	if (flip && n0 == st->drawn0 && n1 == st->drawn1)
		return;
	if (flip && ui_map_busy(node, (u32)n->y / 8, (u32)n->h / 8)) {
		ui_dirty(node, 0xFF);  // the last flip hasn't landed: its set still shows
		return;
	}
	u32 set = flip ? st->cur ^ 1u : st->cur;
	scrollbar_paint_set(node, st, set, n0, n1);
	if (!map_window(node, st->first[set], UI_BANK_NORMAL)) {
		ui_dirty(node, 0xFF);
		return;
	}
	st->cur = (u8)set;
	st->drawn0 = (u16)n0;
	st->drawn1 = (u16)n1;
}

static const struct ui_ops scrollbar_ops = {
	sizeof(struct scrollbar_state), scrollbar_measure, scrollbar_init, 0, scrollbar_update, scrollbar_draw};

// --- Every kind ---------------------------------------------------------

const struct ui_ops *const ui_kind_ops[UI_KINDS] = {
	[UI_LABEL] = &label_ops,        [UI_PANEL] = &panel_ops,         [UI_LIST] = &ui_list_ops,
	[UI_IMAGE] = &image_ops,        [UI_SCROLLBAR] = &scrollbar_ops, [UI_PROGRESS] = &progress_ops,
	[UI_CARD] = &ui_card_ops,       [UI_TITLE] = &ui_title_ops,      [UI_HINTS] = &ui_hints_ops,
	[UI_PATH] = &ui_path_ops,       [UI_STORAGE] = &ui_storage_ops,  [UI_POPUP] = &ui_popup_ops,
	[UI_CONFIRM] = &ui_confirm_ops, [UI_LETTER] = &ui_letter_ops,    [UI_CAROUSEL] = &ui_carousel_ops,
};
