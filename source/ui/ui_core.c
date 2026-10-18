// The hardware-free core of the UI (ui.h): the node pool, the view stack
// and its arena, layouts and patches, focus and key dispatch. tests/host
// runs it with widget ops of its own.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "../engine/jobs.h"
#include "../engine/res.h"
#include "../tlog.h"

#define SCREEN_W 240
#define SCREEN_H 160
#define OWNER_BASE 0x40  // motion and job owners of views: OWNER_BASE + depth

static struct ui_node nodes[UI_NODE_CAP] EWRAM_BSS;
static struct ui_view views[UI_VIEW_CAP] EWRAM_BSS;
static u8 arena[UI_ARENA_BYTES] EWRAM_BSS ALIGN(4);
static struct {
	u16 arena_used;
	u8 node_count;
	u8 depth;
	u8 pops;  // ui_pop calls waiting for the end of the event phase
} core EWRAM_BSS;
u8 ui_dispatching EWRAM_BSS;

void ui_set_error(const char *format, ...)
{
	char msg[100];
	va_list args;
	va_start(args, format);
	vsnprintf(msg, sizeof(msg), format, args);
	va_end(args);
	res_set_error("ui: %s", msg);
}

const char *ui_error(void)
{
	return res_error();
}

void ui_core_reset(void)
{
	memset(nodes, 0, sizeof(nodes));
	memset(views, 0, sizeof(views));
	memset(&core, 0, sizeof(core));
	ui_dispatching = 0;
}

u32 ui_depth(void)
{
	return core.depth;
}

struct ui_view *ui_view_at(u32 depth)
{
	return depth < core.depth ? &views[depth] : 0;
}

struct ui_view *ui_top(void)
{
	return core.depth ? &views[core.depth - 1] : 0;
}

u32 ui_arena_used(void)
{
	return core.arena_used;
}

u32 ui_nodes_used(void)
{
	return core.node_count;
}

u8 ui_owner(const struct ui_view *view)
{
	return (u8)(OWNER_BASE + (view - views));
}

struct ui_node *ui_node(u8 index)
{
	return &nodes[index];
}

struct ui_view *ui_node_view(u8 index)
{
	return &views[nodes[index].view];
}

const struct ui_spec *ui_node_spec(u8 index)
{
	const struct ui_view *v = &views[nodes[index].view];
	return &v->spec->nodes[index - v->first];
}

void *ui_node_state(u8 index)
{
	return &arena[nodes[index].state];
}

u8 ui_find(const struct ui_view *view, u8 id)
{
	for (u32 i = 0; i < view->count; i++)
		if (view->spec->nodes[i].id == id)
			return (u8)(view->first + i);
	return UI_NONE;
}

void ui_dirty(u8 index, u8 bits)
{
	nodes[index].dirty |= bits;
	nodes[index].flags |= UI_N_DIRTY;
	ui_wake();
}

void ui_show(u8 index, int shown)
{
	u8 hidden = shown ? 0 : UI_N_HIDDEN;
	if ((nodes[index].flags & UI_N_HIDDEN) == hidden)
		return;
	nodes[index].flags = (u8)((nodes[index].flags & ~UI_N_HIDDEN) | hidden);
	ui_dirty(index, 0xFF);
}

static u8 patched(u8 node, u8 field, u8 value)
{
	const struct ui_view *v = ui_node_view(node);
	const struct ui_spec *s = ui_node_spec(node);
	s16 p;
	if (s->id && v->spec->id && ui_theme_patch(ui_theme(), v->spec->id, s->id, field, &p))
		return (u8)p;
	return value;
}

u8 ui_ink(u8 node, u8 def)
{
	u8 ink = patched(node, UI_FIELD_INK, ui_node_spec(node)->ink);
	return ink ? ink : def;
}

u8 ui_paper(u8 node, u8 def)
{
	u8 paper = patched(node, UI_FIELD_PAPER, ui_node_spec(node)->paper);
	return paper ? paper : def;
}

// One axis: start and length within the parent's, by anchor (2 bits)
static void place(u32 anchor, s16 pos, s16 len, s16 pstart, s16 plen, s16 *start, s16 *size)
{
	switch (anchor & 3) {
	case UI_RIGHT:
		*start = (s16)(pstart + plen - pos - len);
		*size = len;
		break;
	case UI_HFILL:
		*start = (s16)(pstart + pos);
		*size = (s16)(plen - pos - len);
		break;
	case UI_HCENTER:
		*start = (s16)(pstart + (plen - len) / 2 + pos);
		*size = len;
		break;
	default:
		*start = (s16)(pstart + pos);
		*size = len;
		break;
	}
}

void ui_layout(const struct ui_spec *spec, u8 view_id, const struct ui_node *parent, struct ui_node *out)
{
	s16 v[4] = {spec->x, spec->y, spec->w, spec->h};
	s16 anchor = spec->anchor, p;
	const struct ui_theme *t = ui_theme();
	if (spec->id && view_id) {
		for (u32 f = UI_FIELD_X; f <= UI_FIELD_H; f++)
			if (ui_theme_patch(t, view_id, spec->id, (u8)f, &p))
				v[f] = p;
		if (ui_theme_patch(t, view_id, spec->id, UI_FIELD_ANCHOR, &p))
			anchor = p;
	}
	s16 px = 0, py = 0, pw = SCREEN_W, ph = SCREEN_H;
	if (parent) {
		px = parent->x;
		py = parent->y;
		pw = parent->w;
		ph = parent->h;
	}
	place((u32)anchor, v[0], v[2], px, pw, &out->x, &out->w);
	place((u32)anchor >> 2, v[1], v[3], py, ph, &out->y, &out->h);
}

// Checks a layout's caps before anything is taken; 0 with the error recorded.
static int check_caps(const struct ui_view_spec *spec, u32 *state_bytes)
{
	if (!spec->nodes || !spec->count) {
		ui_set_error("view %s has no nodes", spec->name);
		return 0;
	}
	if (core.depth == UI_VIEW_CAP) {
		ui_set_error("view %s: the stack is full (%d views)", spec->name, UI_VIEW_CAP);
		return 0;
	}
	if (core.node_count + spec->count > UI_NODE_CAP) {
		ui_set_error("view %s needs %u nodes, %u of %d free", spec->name, spec->count,
			     (unsigned)(UI_NODE_CAP - core.node_count), UI_NODE_CAP);
		return 0;
	}
	u32 bytes = 0;
	for (u32 i = 0; i < spec->count; i++) {
		const struct ui_spec *s = &spec->nodes[i];
		if (s->kind >= UI_KINDS || s->plane >= UI_PLANES || (s->parent != UI_NONE && s->parent >= i)) {
			ui_set_error("view %s, node %lu: bad kind, plane or parent (parents come first)", spec->name,
				     (unsigned long)i);
			return 0;
		}
		bytes += (ui_kind_ops[s->kind]->state_size + 3u) & ~3u;
	}
	if (bytes > (u32)(UI_ARENA_BYTES - core.arena_used)) {
		ui_set_error("view %s needs %lu bytes of widget state, %u of %d free", spec->name, (unsigned long)bytes,
			     (unsigned)(UI_ARENA_BYTES - core.arena_used), UI_ARENA_BYTES);
		return 0;
	}
	*state_bytes = bytes;
	return 1;
}

// Undoes a push that failed half way, or pops the top view
static void unwind(struct ui_view *v)
{
	u8 owner = ui_owner(v);
	motion_free_owner(owner);
	jobs_cancel_owner(owner);
	res_release_to(v->res_mark);
	core.arena_used = v->arena;
	core.node_count = v->first;
	memset(v, 0, sizeof(*v));
}

// Lays out the view's nodes and links them; 0 on a bad rectangle
static int place_nodes(struct ui_view *v)
{
	const struct ui_view_spec *spec = v->spec;
	u16 offset = v->arena;
	for (u32 i = 0; i < spec->count; i++) {
		const struct ui_spec *s = &spec->nodes[i];
		u8 n = (u8)(v->first + i);
		struct ui_node *node = &nodes[n];
		memset(node, 0, sizeof(*node));
		node->view = (u8)(v - views);
		node->parent = s->parent == UI_NONE ? UI_NONE : (u8)(v->first + s->parent);
		node->child = node->next = UI_NONE;
		node->state = offset;
		offset = (u16)(offset + ((ui_kind_ops[s->kind]->state_size + 3u) & ~3u));
		ui_layout(s, spec->id, node->parent == UI_NONE ? 0 : &nodes[node->parent], node);
		s16 hidden;
		if ((s->flags & UI_HIDDEN) ||
		    (s->id && spec->id && ui_theme_patch(ui_theme(), spec->id, s->id, UI_FIELD_HIDDEN, &hidden) &&
		     hidden))
			node->flags |= UI_N_HIDDEN;
		if ((node->x | node->y | node->w | node->h) & 7 || node->w <= 0 || node->h <= 0 || node->x < 0 ||
		    node->y < 0 || node->x + node->w > SCREEN_W || node->y + node->h > SCREEN_H) {
			ui_set_error("view %s, node %lu: rectangle %d,%d %dx%d isn't whole tiles on the screen",
				     spec->name, (unsigned long)i, node->x, node->y, node->w, node->h);
			return 0;
		}
		// Children in spec order: append to the parent's list
		if (node->parent != UI_NONE) {
			u8 *link = &nodes[node->parent].child;
			while (*link != UI_NONE)
				link = &nodes[*link].next;
			*link = n;
		}
		v->planes |= (u8)(1u << s->plane);
	}
	return 1;
}

struct ui_view *ui_core_push(const struct ui_view_spec *spec, void *ctx)
{
	u32 bytes;
	if (!check_caps(spec, &bytes))
		return 0;
	struct ui_view *v = &views[core.depth];
	memset(v, 0, sizeof(*v));
	v->spec = spec;
	v->ctx = ctx;
	v->arena = core.arena_used;
	v->first = core.node_count;
	v->count = spec->count;
	v->focus = UI_NONE;
	v->res_mark = res_mark();
	for (u32 p = 0; p < UI_PLANES; p++)
		v->maps[p] = RES_NONE;
	// Taken now, so unwind gives them back
	core.depth++;
	core.node_count = (u8)(core.node_count + spec->count);
	core.arena_used = (u16)(core.arena_used + bytes);
	memset(&arena[v->arena], 0, bytes);

	struct engine_budget need;
	memset(&need, 0, sizeof(need));
	int ok = place_nodes(v);
	for (u32 i = 0; ok && i < spec->count; i++) {
		const struct ui_ops *ops = ui_kind_ops[spec->nodes[i].kind];
		if (ops->measure)
			ok = ops->measure((u8)(v->first + i), &need);
	}
	for (u32 p = 0; p < UI_PLANES; p++)
		need.bg_maps = (u8)(need.bg_maps + (v->planes >> p & 1));
	ok = ok && budget_check_layout(spec->name, &need) == BUDGET_OK;
	for (u32 p = 0; ok && p < UI_PLANES; p++) {
		if (!(v->planes >> p & 1))
			continue;
		v->maps[p] = res_bg_map(1, 0);
		if (v->maps[p] == RES_NONE) {
			ui_set_error("view %s: no room for plane %lu's map", spec->name, (unsigned long)p);
			ok = 0;
		}
	}
	for (u32 i = 0; ok && i < spec->count; i++) {
		u8 n = (u8)(v->first + i);
		const struct ui_ops *ops = ui_kind_ops[spec->nodes[i].kind];
		if (ops->init && !ops->init(n))
			ok = 0;
		nodes[n].flags |= UI_N_DIRTY;
		nodes[n].dirty = 0xFF;
	}
	if (!ok) {
		core.depth--;
		unwind(v);
		return 0;
	}
	if (spec->focus < spec->count) {
		v->focus = (u8)(v->first + spec->focus);
		nodes[v->focus].flags |= UI_N_FOCUS;
	}
	ui_wake();
	return v;
}

void ui_core_pop(void)
{
	if (!core.depth)
		return;
	core.depth--;
	unwind(&views[core.depth]);
}

void ui_pop(void)
{
	if (ui_dispatching) {
		core.pops++;
		return;
	}
	if (!core.depth)
		return;
	ui_core_pop();
	ui_hook_popped();
}

void ui_apply_pops(void)
{
	while (core.pops) {
		core.pops--;
		ui_pop();
	}
}

void ui_focus(u8 index)
{
	struct ui_view *v = ui_node_view(index);
	u8 old = v->focus;
	if (old == index)
		return;
	struct ui_event e = {UI_EV_BLUR, old, 0, 0};
	if (old != UI_NONE) {
		nodes[old].flags &= (u8)~UI_N_FOCUS;
		const struct ui_ops *ops = ui_kind_ops[ui_node_spec(old)->kind];
		if (ops->event)
			ops->event(old, &e);
		ui_dirty(old, 0);
	}
	v->focus = index;
	nodes[index].flags |= UI_N_FOCUS;
	e.type = UI_EV_FOCUS;
	e.node = index;
	const struct ui_ops *ops = ui_kind_ops[ui_node_spec(index)->kind];
	if (ops->event)
		ops->event(index, &e);
	ui_dirty(index, 0);
}

u8 ui_focus_next(int dir)
{
	struct ui_view *v = ui_top();
	if (!v)
		return UI_NONE;
	u32 start = v->focus == UI_NONE ? (dir > 0 ? v->count - 1u : 0u) : (u32)(v->focus - v->first);
	for (u32 step = 1; step <= v->count; step++) {
		u32 i = dir > 0 ? (start + step) % v->count : (start + v->count - step % v->count) % v->count;
		u8 n = (u8)(v->first + i);
		if ((v->spec->nodes[i].flags & UI_FOCUSABLE) && !(nodes[n].flags & UI_N_HIDDEN)) {
			ui_focus(n);
			return n;
		}
	}
	return UI_NONE;
}

int ui_emit(u8 index, u8 type, u32 value)
{
	struct ui_view *v = ui_node_view(index);
	if (!v->spec->event)
		return 0;
	struct ui_event e = {type, index, 0, value};
	return v->spec->event(v, &e);
}

int ui_dispatch_keys(u16 keys)
{
	struct ui_view *v = ui_top();
	if (!v || !keys)
		return 0;
	struct ui_event e = {UI_EV_KEYS, UI_NONE, keys, 0};
	u8 depth = core.depth;
	int done = 0;
	ui_dispatching = 1;
	for (u8 n = v->focus; n != UI_NONE && !done; n = nodes[n].parent) {
		if (nodes[n].flags & UI_N_HIDDEN)
			continue;
		const struct ui_ops *ops = ui_kind_ops[ui_node_spec(n)->kind];
		if (ops->event)
			done = ops->event(n, &e);
		if (core.depth != depth)  // a widget's event pushed a view
			break;
	}
	if (!done && core.depth == depth && v->spec->event)
		done = v->spec->event(v, &e);
	ui_dispatching = 0;
	return done;
}
