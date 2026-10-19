// Views, widgets and themes on the v0.3 tile engine (docs/plans/v0.3-decisions.md,
// "Views and widgets").
//
// A screen is a const layout in ROM: an array of ui_spec, one per widget,
// with fixed rectangles and anchors, plus a ui_view_spec naming it and its
// event handler. ui_push(&spec, ctx) shows it on top of the view stack:
// every widget gets a node from a flat pool, its state from the view's
// arena, and its tiles and maps from the engine's allocators (res.h), all
// released together by ui_pop. The top view is modal: it alone gets keys.
//
//	static const struct ui_spec nodes[] = {
//		{UI_PANEL, 1, UI_NONE, 2, UI_HFILL, 0, 0, UI_ROLE_PANEL, 0, 0, 0, 16},
//		{UI_LABEL, 2, 0, 2, UI_HFILL, 0, UI_ROLE_TEXT, UI_ROLE_PANEL, 0, 0, 0, 16, &title},
//		{UI_LIST, 3, UI_NONE, 1, UI_HFILL, UI_FOCUSABLE, 0, UI_ROLE_BG, 0, 16, 0, 128, &files},
//	};
//	static int on_event(struct ui_view *v, const struct ui_event *e)
//	{
//		if (e->type == UI_EV_ACTIVATE) { open(e->index); return 1; }
//		if (e->type == UI_EV_KEYS && (e->keys & INPUT_B)) { ui_pop(); return 1; }
//		return 0;
//	}
//	static const struct ui_view_spec files_view = {"sd-list", nodes, 3, 1, 2, 0, on_event};
//	...
//	ui_push(&files_view, &my_state);
//	while (ui_depth())
//		ui_frame();
//
// Each frame (ui_frame) follows the engine's phases: commit (frame_begin),
// input, motion, the widgets' update, jobs, then the widgets' draw. Draw
// never touches a hardware register: widgets write the shadow state, tiles
// that aren't on screen, and map changes through a VRAM staging area and
// the commit queue, so nothing half-drawn shows. Draw runs only for dirty
// nodes, and running it twice draws the same thing (idempotent).
//
// Planes: a widget draws on one of the four BGs (spec.plane). Within a view
// a lower plane number is in front; a pushed view is in front of the ones
// under it, which stay resident and show only where it leaves them visible
// (on planes it doesn't use). A list or grid needs a plane of its own, as
// it scrolls it, and one of the two hardware windows to clip it. Planes 1
// and 3 take tiles from character blocks 1 and 2 (lists), planes 0 and 2
// from block 0.
//
// Rectangles are in pixels and multiples of 8 (everything is tiles); text
// inside them lands on any pixel.
#ifndef TORII_UI_H
#define TORII_UI_H

#include <gba_base.h>

#include "../engine/budget.h"
#include "../engine/font.h"
#include "../engine/input.h"
#include "../engine/motion.h"

// --- Caps --------------------------------------------------------------------
// Hitting one makes ui_push fail with a readable error (ui_error()).

#define UI_NODE_CAP 24      // nodes across every view on the stack
#define UI_VIEW_CAP 4       // views on the stack
#define UI_ARENA_BYTES 448  // widget state across every view on the stack
#define UI_SCROLLERS 2      // lists and grids resident at once (hardware windows)
#define UI_PLANES 4
#define UI_NONE 0xFF

// --- Palette roles -----------------------------------------------------------
// Widgets draw with palette indices, never colours: a theme maps each index
// (a role) to a colour, so retheming is a palette change and nothing is
// redrawn. Bank 0 holds the normal colours; bank 1 is the same roles as a
// selected row shows them (behind it the selection colour, text on it the
// selection's text colour). Indices 10-15 are icon colours, the same in
// both banks.
enum ui_role {
	UI_ROLE_NONE = 0,  // transparent
	UI_ROLE_BG,        // the screen's background
	UI_ROLE_PANEL,     // panels, title bars, dialogs
	UI_ROLE_TEXT,
	UI_ROLE_DIM,       // secondary text: sizes, hints
	UI_ROLE_ACCENT,    // highlights, progress fill, scroll bar thumb
	UI_ROLE_ACCENT_TEXT,  // text on the accent colour
	UI_ROLE_BORDER,    // panel outlines
	UI_ROLE_TRACK,     // the empty part of a progress or scroll bar
	UI_ROLE_SHADOW,
	UI_ROLES,
	UI_ROLE_ICON = 10,  // first of the 6 icon colours
};
#define UI_BANK_NORMAL 0
#define UI_BANK_SELECTED 1

// --- Themes ------------------------------------------------------------------

// The theme's colours (RGB15). Bank 0 takes BG to SHADOW at their role's
// index; bank 1 maps BG and PANEL to SELECTION, TEXT and ACCENT to
// SELECTION_TEXT, and DIM to a mix of the two.
enum ui_colour {
	UI_C_BG,
	UI_C_PANEL,
	UI_C_TEXT,
	UI_C_DIM,
	UI_C_ACCENT,
	UI_C_ACCENT_TEXT,
	UI_C_BORDER,
	UI_C_TRACK,
	UI_C_SHADOW,
	UI_C_SELECTION,       // behind the selected row
	UI_C_SELECTION_TEXT,  // text on it
	UI_C_ICON,            // 6 icon colours, palette indices 10-15
	UI_COLOURS = UI_C_ICON + 6,
};

// Layout patches: a theme moves, resizes, hides or recolours a node of a
// built-in layout without code. A patch names the view (ui_view_spec.id),
// the node (ui_spec.id) and one field of this closed set, and replaces the
// spec's value when the view is pushed (inks when drawn).
enum ui_field {
	UI_FIELD_X,       // the spec's x (pixels, a multiple of 8)
	UI_FIELD_Y,
	UI_FIELD_W,
	UI_FIELD_H,
	UI_FIELD_ANCHOR,  // UI_LEFT ... | UI_TOP ...
	UI_FIELD_HIDDEN,  // 1: not shown
	UI_FIELD_INK,     // a ui_role
	UI_FIELD_PAPER,   // a ui_role
	UI_FIELDS,
};
#define UI_PATCH_CAP 16

struct ui_patch {
	u8 view;   // ui_view_spec.id
	u8 node;   // ui_spec.id
	u8 field;  // ui_field
	u8 pad;
	s16 value;
};

// A theme, in ROM (the built-ins) or RAM (T14's theme.ini). Fields:
//   name                for settings and logs
//   colour[UI_COLOURS]  RGB15, see ui_colour
//   body, heading       fonts for rows and labels, and for titles
//   row_h               list row height, 16 or 32 px
//   pad                 text inset from a row's or label's edges, px
//   modal_dim           how far a modal view darkens the views under it (BLDY, 0-16)
//   idle_dim            how far DIM darkens the screen (BLDY, 0-16)
//   fade_frames         frames of a palette fade (ui_fade)
//   slide               px a modal view slides up from as it opens (0: none)
//   marquee_delay       frames the cursor rests before a long name scrolls
//   input               key repeat and wrap timings (engine/input.h)
//   patches             layout patches, patch_count of them
struct ui_theme {
	const char *name;
	u16 colour[UI_COLOURS];
	const struct torii_font *body;
	const struct torii_font *heading;
	u8 row_h;
	u8 pad;
	u8 modal_dim;
	u8 idle_dim;
	u8 fade_frames;
	u8 slide;
	u8 marquee_delay;
	u8 patch_count;
	InputTiming input;
	struct ui_patch patches[UI_PATCH_CAP];
};

extern const struct ui_theme ui_theme_dark;   // the default
extern const struct ui_theme ui_theme_light;

// Accent colours: palette swaps of a theme's accent and selection colours.
#define UI_ACCENT_THEME 0xFFFF  // the theme's own
struct ui_accent {
	const char *name;
	u16 colour;
};
extern const struct ui_accent ui_accents[];
extern const u32 ui_accent_count;

// The two palette banks (32 colours) a theme and accent give. Hardware-free.
void ui_theme_palette(const struct ui_theme *theme, u16 accent, u16 out[32]);
// The value a theme patches field of node in view to, into *value; 0 if
// none does (the last matching patch wins).
int ui_theme_patch(const struct ui_theme *theme, u8 view, u8 node, u8 field, s16 *value);

// Records theme and accent as the current ones, without touching the
// palette (hardware-free: ui_theme_set does both).
void ui_theme_select(const struct ui_theme *theme, u16 accent);
// Makes theme (with accent, or UI_ACCENT_THEME) the current one: a palette
// change, committed in the next VBlank; nothing is redrawn. Switch only
// between themes with the same fonts, row height and pad while views are
// up (the built-ins are): text already drawn keeps the old metrics, and
// patches and row heights apply to views pushed after.
void ui_theme_set(const struct ui_theme *theme, u16 accent);
const struct ui_theme *ui_theme(void);
u16 ui_accent(void);

// --- Layouts -----------------------------------------------------------------

enum ui_kind {
	UI_LABEL,
	UI_PANEL,
	UI_LIST,
	UI_GRID,
	UI_IMAGE,
	UI_SCROLLBAR,
	UI_PROGRESS,
	UI_KINDS,
};

// Anchors, relative to the parent's rectangle (the screen's for a root):
// horizontal in bits 0-1, vertical in bits 2-3.
//   LEFT / TOP:     at x from the parent's left, w wide
//   RIGHT / BOTTOM: x from the parent's right edge to the node's, w wide
//   FILL:           x from the left, w from the right: the width follows the parent
//   CENTER:         centred, moved by x
enum {
	UI_LEFT = 0,
	UI_RIGHT = 1,
	UI_HFILL = 2,
	UI_HCENTER = 3,
	UI_TOP = 0,
	UI_BOTTOM = 4,
	UI_VFILL = 8,
	UI_VCENTER = 12,
	UI_FILL = UI_HFILL | UI_VFILL,
};

// ui_spec.flags
enum {
	UI_FOCUSABLE = 1,  // takes focus (lists, grids)
	UI_HIDDEN = 2,     // starts hidden (ui_show; a hidden list's plane is off)
};

// One widget of a layout, in ROM. Parents come before their children.
struct ui_spec {
	u8 kind;     // ui_kind
	u8 id;       // for patches and ui_find: unique in its layout, 0 none
	u8 parent;   // index in the same array, UI_NONE: the screen
	u8 plane;    // BG 0-3
	u8 anchor;
	u8 flags;
	u8 ink;      // role it draws with (0: the kind's default)
	u8 paper;    // role behind it (0: transparent)
	s16 x, y, w, h;
	const void *data;  // the kind's config, below
};

struct ui_view;
struct ui_event;

// A screen, in ROM.
struct ui_view_spec {
	const char *name;  // logged as "screen ready: NAME" once shown
	const struct ui_spec *nodes;
	u8 count;
	u8 id;       // for patches, 0 none
	u8 focus;    // index of the node focused first, UI_NONE none
	u8 flags;    // UI_VIEW_*
	// The screen's logic: keys no widget took, and widgets' events.
	// Returns 1 if it handled the event.
	int (*event)(struct ui_view *view, const struct ui_event *e);
};

enum {
	UI_VIEW_MODAL = 1,  // a dialog: dims the views under it and slides in
};

// --- Widget configs (ui_spec.data) ---------------------------------------

enum {
	UI_ALIGN_LEFT,
	UI_ALIGN_CENTER,
	UI_ALIGN_RIGHT,
};
enum {
	UI_FONT_BODY,
	UI_FONT_HEADING,
};
enum {
	UI_LABEL_SWAP = 1,  // two tile sets: text changes in place without tearing
};

// A label: one or more lines ("\n"), clipped with "..." to its width. ink
// defaults to TEXT. Lines are laid out from the top, centred in the height.
struct ui_label_config {
	const char *text;  // UTF-8; ui_label_set replaces it
	u8 font;           // UI_FONT_*
	u8 align;          // UI_ALIGN_*
	u8 flags;          // UI_LABEL_*
};

enum {
	UI_PANEL_FLAT,
	UI_PANEL_BORDER,  // a 1 px outline in ink (default BORDER)
	UI_PANEL_ROUND,   // rounded corners, no outline
};
// A panel: paper (default PANEL) filling its rectangle. data may be NULL.
struct ui_panel_config {
	u8 style;
};

// What a list row or grid cell shows (ui_list_model.fetch fills it).
struct ui_item {
	const char *text;    // UTF-8
	u16 len;             // 0: up to the NUL
	u8 ink;              // role of the text, 0: TEXT
	u8 pad;
	const char *detail;  // right-aligned in DIM (a size, a date); NULL none
	const u32 *icon;     // grid: 2x2 4bpp tiles in row order, colours 10-15; NULL none
};

enum {
	UI_ITEM_READY = 1,
	UI_ITEM_PENDING = 0,  // not loaded yet: drawn blank until ui_list_refresh
};

// Where a list's rows come from. count and fetch run in draw; fetch must
// not call FatFs or switch the ROM page: slow data comes through prefetch
// and jobs (engine/jobs.h), which then call ui_list_refresh. ctx is the
// view's (ui_push).
struct ui_list_model {
	u32 (*count)(void *ctx);
	int (*fetch)(void *ctx, u32 index, struct ui_item *out);
	void (*prefetch)(void *ctx, u32 lo, u32 hi);  // items about to show; may be NULL
};

enum {
	UI_LIST_WRAP = 1,     // the cursor wraps at the ends (input.h's rules)
	UI_LIST_MARQUEE = 2,  // the selected row's name scrolls if it is cut
};

// A list (cols 1) or a grid. ink defaults to TEXT and paper to BG. The
// viewport is the node's rectangle.
struct ui_list_config {
	const struct ui_list_model *model;
	u8 row_h;     // 16 or 32 px; 0: the theme's
	u8 cell_w;    // grid: cell width, px (a multiple of 8); list: 0
	u8 flags;     // UI_LIST_*
	u8 detail_w;  // list: px kept for the detail at the right
};

// An image from 4bpp tiles in ROM (a sprite or cover image comes later, T7).
struct ui_image_config {
	const u32 *tiles;     // w x h tiles in row order
	const u16 *palette;   // 16 colours in a bank of its own; NULL: the theme's bank 0
	u16 tag;              // shares tiles and palette between views (nonzero)
};

// A vertical scroll bar showing a list's or grid's position.
struct ui_scrollbar_config {
	u8 target;  // index of the list or grid in the same layout
};

// A progress bar: ink (ACCENT) fills paper's track (TRACK). data may be NULL.

// --- Views and nodes ------------------------------------------------------

struct ui_node {
	s16 x, y, w, h;  // on screen, after patches and anchors
	u8 view;         // its view's depth in the stack
	u8 parent, child, next;  // pool indices, UI_NONE
	u8 flags;        // UI_N_*
	u8 dirty;        // the kind's dirty bits
	u16 state;       // offset of its state in the arena
};

// ui_node.flags
enum {
	UI_N_HIDDEN = 1,
	UI_N_FOCUS = 2,
	UI_N_DIRTY = 4,   // draw it
	UI_N_DRAWN = 8,   // drawn at least once
};

struct ui_view {
	const struct ui_view_spec *spec;
	void *ctx;
	u16 arena;        // its first byte in the arena
	u8 first, count;  // its nodes in the pool
	u8 focus;         // pool index, UI_NONE
	u8 res_mark;      // res_mark() when pushed
	u8 planes;        // bit p: it shows plane p
	u8 flags;         // UI_V_*
	u8 maps[UI_PLANES];  // res handles of its maps, RES_NONE if unused
	MotionId slide;   // a modal view's slide-in spring
	s16 offset;       // px it is drawn below its place (sliding in)
};

// ui_view.flags
enum {
	UI_V_SHOWN = 1,   // its first draw is done and its planes are on
	UI_V_READY = 2,   // "screen ready" logged
	UI_V_CLEARED = 4,  // its maps were cleared for the first draw
};

// --- Events -------------------------------------------------------------------

enum ui_event_type {
	UI_EV_KEYS,      // keys fired this frame (input.h): .keys
	UI_EV_ACTIVATE,  // A on a list or grid item: .index
	UI_EV_CURSOR,    // a list's or grid's cursor moved: .index
	UI_EV_FOCUS,     // the node got focus
	UI_EV_BLUR,      // the node lost focus
};

struct ui_event {
	u8 type;
	u8 node;  // pool index of the node it is about (UI_NONE for keys)
	u16 keys;
	u32 index;
};

// --- The stack -------------------------------------------------------------

// Sets up the UI on the engine (call engine_init first): palette banks, the
// staging area, the input state, with theme (NULL: the THEME and ACCENT
// settings, ui_theme_setting).
void ui_init(const struct ui_theme *theme);
// The theme and accent the THEME and ACCENT settings pick (settings slots
// assress_theme, 1 for light, and assress_accent, 1 + an index into
// ui_accents; erased or unknown values give the dark theme with its own
// accent). *accent gets what ui_theme_set takes: the accent's colour, or
// UI_ACCENT_THEME.
const struct ui_theme *ui_theme_setting(u16 *accent);
// Pops every view and releases the UI's resources.
void ui_shutdown(void);

// Pushes a view; NULL if it doesn't fit (caps, the layout's budget, a bad
// layout), with the reason in ui_error() and the log. The view is drawn
// hidden and shown in one VBlank. From an event handler, ui_pop() then
// ui_push() replaces the handler's view (the pop goes first); the handler
// must return straight after either. Tile uploads a view queued (images)
// are not cancelled by its pop: pop such a view only once they are done.
struct ui_view *ui_push(const struct ui_view_spec *spec, void *ctx);
// Pops the top view (at the end of the event phase, if called from an event
// handler): its motions stop, its jobs are cancelled, its tiles and maps go.
void ui_pop(void);
u32 ui_depth(void);
struct ui_view *ui_top(void);
// The last error, as a sentence ("" if none).
const char *ui_error(void);

// One frame of the loop (see the top). Returns ui_depth().
u32 ui_frame(void);

// --- Nodes --------------------------------------------------------------------

// The node with id in view (ui_spec.id), or UI_NONE.
u8 ui_find(const struct ui_view *view, u8 id);
struct ui_node *ui_node(u8 index);
const struct ui_spec *ui_node_spec(u8 index);
struct ui_view *ui_node_view(u8 index);
void *ui_node_state(u8 index);
// Marks a node for drawing (and wakes the frame).
void ui_dirty(u8 index, u8 bits);
void ui_show(u8 index, int shown);
// Moves the view's focus (UI_EV_BLUR, then UI_EV_FOCUS).
void ui_focus(u8 index);
// The next (dir 1) or previous (-1) focusable shown node of the top view,
// wrapping; the focus moves there. Returns its index or UI_NONE.
u8 ui_focus_next(int dir);
// Sends an event to the node's view handler; returns what it returned.
int ui_emit(u8 index, u8 type, u32 value);

// --- Widgets -------------------------------------------------------------------

// The label keeps the pointer and may draw from it in any later frame:
// text must stay valid and unchanged until the next ui_label_set or the
// view's pop (a static or the view's ctx buffer, never the stack).
void ui_label_set(u8 node, const char *text);
// value of max (max 0 counts as empty).
void ui_progress_set(u8 node, u32 value, u32 max);
// The list's model changed (a new folder): the cursor goes to cursor and
// the list redraws as a page change.
void ui_list_reset(u8 node, u32 cursor);
// Items lo to hi have loaded (or changed): rows showing them redraw. A row
// on screen redraws in place, so it may tear for one frame.
void ui_list_refresh(u8 node, u32 lo, u32 hi);
void ui_list_set_cursor(u8 node, u32 cursor);
u32 ui_list_cursor(u8 node);
// For a scroll bar: content and viewport height, and scroll, in px.
void ui_list_scroll_info(u8 node, u32 *content, u32 *view, u32 *scroll);

// --- Effects ----------------------------------------------------------------

// Fades the palette towards colour by to (0-256: 0 none, 256 all of it)
// over frames frames (0: the theme's fade_frames).
void ui_fade(u16 colour, u32 to, u32 frames);
int ui_fading(void);

// DIM: after minutes idle (no key held), the screen darkens (theme
// idle_dim); any key brightens it, and that key does nothing else. 0 turns
// it off. ui_init reads the setting (ui_dim_setting).
void ui_dim_set_minutes(u32 minutes);
// The same in frames, for tests and demos.
void ui_dim_set_frames(u32 frames);
int ui_dimmed(void);
// The DIM setting from the kernel NOR (settings slot assress_dim): idle
// minutes, 1-60; 0 (off) for anything else, the erased slot included.
u32 ui_dim_setting(void);

// The input state ui_frame reads (theme timings).
const InputState *ui_input(void);

// --- For widget code (ui/ui_widgets.c, ui/ui_list.c) -------------------------------

// A widget kind: state_size bytes of arena per node, and
//   measure: adds what the node (laid out, not yet initialised) needs to its
//            layout's budget (tiles, palette banks); 0 if its spec can't
//            work (the reason recorded with ui_set_error);
//   init:    takes its tiles and motions at push; 0 if it can't (the
//            reason recorded with ui_set_error);
//   event:   a key while it (or a child) has focus; 1 if it took it;
//   update:  once a frame, after motion: scroll, polls (may be NULL);
//   draw:    draws what its dirty bits say; must be idempotent (NULL: nothing).
struct ui_ops {
	u16 state_size;
	int (*measure)(u8 node, struct engine_budget *need);
	int (*init)(u8 node);
	int (*event)(u8 node, const struct ui_event *e);
	void (*update)(u8 node, u32 dt);
	void (*draw)(u8 node);
};
// The ops of each kind (ui/ui_widgets.c; tests/host has its own).
extern const struct ui_ops *const ui_kind_ops[UI_KINDS];

void ui_set_error(const char *format, ...) __attribute__((format(printf, 1, 2)));
// The node's ink or paper role, after patches; def if the spec says 0.
u8 ui_ink(u8 node, u8 def);
u8 ui_paper(u8 node, u8 def);
// Wakes the frame (frame_wake; a stub in tests/host).
void ui_wake(void);
// Called after a view is popped, to show the one under it again (ui/ui_run.c;
// a stub in tests/host).
// planes: the popped view's.
void ui_hook_popped(u8 planes);
// ui_pop calls waiting for the end of the event phase.
int ui_pops_waiting(void);
// The owner id of a view's motions and jobs.
u8 ui_owner(const struct ui_view *view);
// 1 while the event phase runs (ui_pop waits for its end).
extern u8 ui_dispatching;

// Hardware-free core (ui/ui_core.c), used by ui/ui_run.c and the host tests.
struct ui_view *ui_view_at(u32 depth);
// Pushes the nodes and arena state of a view (no VRAM or planes): the
// part ui_push shares with the host tests.
struct ui_view *ui_core_push(const struct ui_view_spec *spec, void *ctx);
// Pops the top view's nodes, arena, motions, jobs and resources.
void ui_core_pop(void);
void ui_core_reset(void);
// Sends keys to the top view: its focused node, the node's parents, then
// the view's handler. Returns 1 if anything took them.
int ui_dispatch_keys(u16 keys);
// Pops waiting since the event phase.
void ui_apply_pops(void);
// Arena bytes and nodes in use (Diagnostics, tests).
u32 ui_arena_used(void);
u32 ui_nodes_used(void);

// Resolves a spec's rectangle against its parent's, with patches.
void ui_layout(const struct ui_spec *spec, u8 view_id, const struct ui_node *parent, struct ui_node *out);

#endif
