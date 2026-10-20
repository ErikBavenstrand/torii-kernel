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
// (on planes it doesn't use). A list needs a plane of its own, as
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
#include "palette.h"

// --- Caps --------------------------------------------------------------------
// Hitting one makes ui_push fail with a readable error (ui_error()).

#define UI_NODE_CAP 48       // nodes across every view on the stack
#define UI_VIEW_CAP 4        // views on the stack
#define UI_ARENA_BYTES 1536  // widget state across every view on the stack (a list's is about 256)
#define UI_SCROLLERS 2      // lists resident at once (hardware windows)
#define UI_PLANES 4
#define UI_NONE 0xFF

// --- Palette roles -----------------------------------------------------------
// Widgets draw with palette indices, never colours: a theme maps each index
// (a role) to a colour, so retheming is a palette change and nothing is
// redrawn. The UI owns BG banks 0-2 (UI_BANKS). Every bank holds the
// theme's neutral surfaces (palette.json's names in brackets) at indices
// 1-8, so paper and text look the same in any bank; 9-15 are the bank's own.
enum ui_role {
	UI_ROLE_NONE = 0,  // transparent
	UI_ROLE_BG,        // the page's paper [bg]
	UI_ROLE_PANEL,     // cards [card]
	UI_ROLE_BORDER,    // card hairlines [edge]
	UI_ROLE_SHADOW,    // card shadows [shadow]
	UI_ROLE_TRACK,     // key chips, empty bars [soft]
	UI_ROLE_TEXT,      // [ink]
	UI_ROLE_DIM,       // secondary text: sizes, values [ink2]
	UI_ROLE_FAINT,     // hint labels, disabled text [ink3]
	// Bank 0: the current item's accent (ui_item_set), and more
	UI_ROLE_ACCENT,       // 16x16 icons, scroll bar thumb, bars, solid buttons [base]
	UI_ROLE_TINT,         // the selection pill, the current path pill [tint]
	UI_ROLE_ACCENT_INK,   // 8x8 icons, section labels, stars, marks [ink]; never base
	UI_ROLE_ACCENT_TEXT,  // text on an ACCENT fill [on]
	UI_ROLE_GROUND,       // the carousel backdrop: bg with backdrop_mix of base
	UI_ROLE_DANGER,       // Delete, Format, failures: the danger item's ink
	UI_ROLE_FLOAT,        // popups and confirmations: card with float_mix of ink
	// Bank 1: the console inks, for the marks of Recent's and Favourites' rows
	UI_ROLE_INK_GBA = UI_ROLE_ACCENT,
	UI_ROLE_INK_GBC,
	UI_ROLE_INK_GB,
	UI_ROLE_INK_NES,
	// Bank 2: danger and the NOR storage bar
	UI_ROLE_ALERT_INK = UI_ROLE_ACCENT,  // the danger item's ink
	UI_ROLE_ALERT_TINT,                  // and its tint
	UI_ROLE_NOR_FILL,                    // used space, one shade a game: NOR's base,
	UI_ROLE_NOR_FILL_MID,                // then bar_shade_mix of card,
	UI_ROLE_NOR_FILL_LIGHT,              // then twice that
	UI_ROLE_NOR_INK,
};
enum ui_bank {
	UI_BANK_NORMAL,    // neutrals and the current item
	UI_BANK_CONSOLES,  // neutrals and the console inks
	UI_BANK_ALERT,     // neutrals, danger and the NOR bar
	UI_BANKS,
};
// The colours a theme switch changes in BG palette RAM: one contiguous run,
// so the theme wipe (B19, research override R7) can swap it in one HBlank
// DMA (24 words). The UI's banks are always banks 0-2 (ui_init), nothing in
// them is the same in both themes worth leaving out, and nothing outside them
// depends on the theme. OBJ colours that follow the theme go in OBJ banks:
// the UI's sprite bank (ui_obj_bank, a copy of bank 0: the list's pill) and
// the carousel's fog (B12).
#define UI_THEME_RUN_FIRST 0
#define UI_THEME_RUN_COLOURS (UI_BANKS * 16)

// --- Themes ------------------------------------------------------------------
// Two built-in themes, light and dark, with one layout: switching between
// them is a palette change. Their colours are docs/design/v0.4/palette.json
// (ui/palette.h, generated); their sizes and motions are the shared look of
// docs/design/v0.4/spec.md, in one place so a tweak is a one-line change.

// Screen transitions (ui_leave): the screen shown goes out, then the next one
// comes in, each an eased step of a few frames. Two screens can't share
// the screen while one replaces the other (both want the same planes and
// most of BG VRAM), so the new one is drawn while nothing shows, between the
// two halves. What a transition does is a theme's, per kind of change:
enum ui_tr_kind {
	UI_TR_TAB,     // to the screen beside it: the SD list, the NOR list, SETTINGS' pages
	UI_TR_DEEPER,  // into a folder or a screen under this one (Recently Played,
		       // cheats, help), or back out
	UI_TR_FADE,    // the same screen redrawn (after a delete, a format, a failed launch)
	UI_TR_LEAVE,   // out of the engine to black (a launch), or in from black
	UI_TR_KINDS,
};
// ui_transition.effects; with no effect a screen just cuts to the next
enum {
	UI_TR_FADES = 1,    // fades out to the background (black under a
			    // dialog, and for UI_TR_LEAVE, black) and the next one in from it
	UI_TR_SLIDE_X = 2,  // slides px sideways: forward to the left, back to the right
	UI_TR_SLIDE_Y = 4,  // slides px up going forward, down going back
	UI_TR_MOSAIC = 8,   // breaks up into blocks (the mosaic registers)
};
struct ui_transition {
	u8 effects;  // UI_TR_FADES ...
	u8 out;      // frames going out (linear: it answers the key at once)
	u8 in;       // frames coming in (ease out); 0 for either: no frames
	u8 px;       // how far a slide goes
};

// The Paper clean look's sizes, in px (spec.md's grid)
struct ui_look {
	u8 margin;          // outer margin
	u8 title_h;         // the title bar, from the top
	u8 title_icon_x;    // its 16x16 icon
	u8 title_icon_y;
	u8 title_text_x;    // its heading
	u8 title_text_y;    // its heading's line top
	u8 title_right;     // where the position or clock ends
	u8 title_right_y;   // its line top
	u8 title_right_w;   // px kept for it (two tile sets: whole tiles)
	u8 content_top;     // the page between the title and hint bars
	u8 content_bottom;  // its last row
	u8 list_rows;       // rows in a list card
	u8 list_pitch;      // a list row's height
	u8 settings_pitch;  // a Settings row's height
	u8 pill_h;          // the selection pill's height, in its row
	u8 pill_radius;
	u8 pill_out_l;      // how far the pill reaches past a list's left edge
	u8 pill_out_r;      // and past its right edge
	u8 card_radius;
	u8 button_radius;
	u8 edge;            // a card's hairline
	u8 shadow_offset;   // how far a card's shadow falls, down and right
	u8 scrollbar_w;
	u8 scrollbar_x;     // its thumb's left column, in its 8 px column
	u8 section_icon_gap;  // between a section label's 8x8 icon and its text
	u8 path_gap;        // between a path's pill and its "/"
	u8 storage_h;       // a storage bar's thickness
	u8 reading_key_w;   // a reading line's key chip column (ui_reading)
	u8 hint_y;          // the hint bar's key chips
	u8 hint_chip_h;
	u8 hint_chip_radius;
	u8 hint_chip_pad;   // across, each side of the key's name
	u8 hint_label_gap;  // between a chip and its label
	u8 hint_gap;        // between one hint and the next
	u8 hint_max;        // hints in the bar
};

// A motion: how many frames it takes (60 a second) and its curve (Ease)
struct ui_move {
	u8 frames;
	u8 ease;
};

// Every motion of the new menu (spec.md, "Motion"; motion.png)
struct ui_motions {
	struct ui_move step;         // the carousel turning one item
	u8 label_slide;              // px its label slides while it fades
	struct ui_move open;         // opening an item, start to end
	u8 open_zoom_end;            // the frame the zoom and fog end and the card starts
	u16 open_zoom;               // the front object's scale at the zoom's end, in 256ths
	u8 open_rise;                // px the page card rises while it fades in
	struct ui_move open_card;    // its rise and fade
	struct ui_move close;        // B: the open played backwards
	struct ui_move popup_open;   // a popup opening from its row
	struct ui_move popup_close;
	u8 popup_slide;              // px it slides in from
	struct ui_move theme_wipe;   // the top-down switch between light and dark
	u8 wipe_edge;                // px of its dithered edge
	s32 pill_spring;             // the selection pill's slide to the cursor's row: a
				     // spring's stiffness (16.16, motion.h SPRING_*); 0 jumps
};

// A theme, in ROM. Fields:
//   name                for settings and logs
//   surface, accent     its colours in ui/palette.h: the surfaces, and each
//                       item's accent (accent[enum pal_item])
//   pill_mix            how much of base an accent's tint mixes into a card,
//                       in 256ths (palette.json's; the tints are made with it)
//   backdrop_mix        how much of the item's base the carousel backdrop
//                       mixes into bg (GROUND), in 256ths
//   float_mix           how much ink a popup's card mixes in (FLOAT), in 256ths
//   bar_shade_mix       how much card each next shade of the NOR bar mixes in
//   look, motion        the shared sizes and motions
//   body, heading       fonts for rows and labels, and for titles
//   row_h               list row height, 16 or 32 px
//   pad                 text inset from a row's or label's edges, px
//   modal_dim           how far a modal view darkens the views under it (BLDY, 0-16)
//   idle_dim            how far DIM darkens the screen (BLDY, 0-16)
//   fade_frames         frames of a palette fade (ui_fade)
//   slide               px a modal view slides up from as it opens (0: none)
//   dialog_in           frames a modal view takes to open: it fades in over the
//                       screen, then the screen dims, while it slides up by slide px
//   dialog_out          frames it takes to close: the same backwards
//   marquee_delay       frames the cursor rests before a long name scrolls
//   marquee_speed       px a second it scrolls, 1-60 (60: a pixel every frame;
//                       slower speeds step a pixel some frames)
//   marquee_pause       frames it rests at the end before it starts over
//   marquee_gap         px it scrolls past the name's end before that pause
//   page_fade           frames a list fades in over after a page change
//                       (0: it just shows)
//   page_fade_out       frames the old page fades out over before it
//                       (0: it goes at once)
//   transition          how screens replace each other, per ui_tr_kind (ui_leave)
//   input               key repeat and wrap timings (engine/input.h)
struct ui_theme {
	const char *name;
	u8 pill_mix;
	u8 backdrop_mix;
	u8 float_mix;
	u8 bar_shade_mix;
	const struct pal_surfaces *surface;
	const struct pal_accent *accent;
	struct ui_look look;
	struct ui_motions motion;
	const struct torii_font *body;
	const struct torii_font *heading;
	u8 row_h;
	u8 pad;
	u8 modal_dim;
	u8 idle_dim;
	u8 fade_frames;
	u8 slide;
	u8 marquee_delay;
	u8 marquee_speed;
	u8 marquee_pause;
	u8 marquee_gap;
	u8 page_fade;
	u8 page_fade_out;
	u8 dialog_in;
	u8 dialog_out;
	struct ui_transition transition[UI_TR_KINDS];
	InputTiming input;
};

extern const struct ui_theme ui_theme_dark;   // the default
extern const struct ui_theme ui_theme_light;

// The item whose accent colours the menu until a screen picks one
#define UI_ITEM_DEFAULT PAL_SD

// The UI's palette banks (UI_BANKS x 16 colours) for theme with item's
// accent (enum pal_item). Hardware-free.
void ui_theme_banks(const struct ui_theme *theme, u32 item, u16 out[UI_BANKS * 16]);
// a mixed with amount/256 of b, per 5-bit channel, rounded. Hardware-free.
u16 ui_colour_mix(u16 a, u16 b, u32 amount);

// Records theme as the current one, without touching the palette
// (hardware-free: ui_theme_set does both).
void ui_theme_select(const struct ui_theme *theme);
// Records item (enum pal_item) as the current one, the same way
// (ui_item_set does both).
void ui_item_select(u32 item);
// Makes theme the current one: a palette change, committed in the next
// VBlank; nothing is redrawn. The built-ins share their fonts and layout,
// so they can switch while views are up.
void ui_theme_set(const struct ui_theme *theme);
// Makes item's accent the current one: loads bank 0 (16 colours, 7 of
// them the item's) and the UI's sprite bank, its copy, in the next VBlank;
// nothing is redrawn.
void ui_item_set(u32 item);
const struct ui_theme *ui_theme(void);
u32 ui_item(void);

// --- Layouts -----------------------------------------------------------------

enum ui_kind {
	UI_LABEL,
	UI_PANEL,
	UI_LIST,
	UI_IMAGE,
	UI_SCROLLBAR,
	UI_PROGRESS,
	// The Paper clean widgets (ui/ui_paper.c)
	UI_CARD,
	UI_TITLE,
	UI_HINTS,
	UI_PATH,
	UI_STORAGE,
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
	UI_FOCUSABLE = 1,  // takes focus (lists)
	UI_HIDDEN = 2,     // starts hidden (ui_show; a hidden list's plane is off)
};

// One widget of a layout, in ROM. Parents come before their children.
struct ui_spec {
	u8 kind;     // ui_kind
	u8 id;       // for ui_find: unique in its layout, 0 none
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
	u8 id;       // unused since SD themes went (B10 drops it with the old screens)
	u8 focus;    // index of the node focused first, UI_NONE none
	u8 flags;    // UI_VIEW_*
	// The screen's logic: keys no widget took, and widgets' events.
	// Returns 1 if it handled the event.
	int (*event)(struct ui_view *view, const struct ui_event *e);
};

enum {
	UI_VIEW_MODAL = 1,  // a dialog: dims the views under it and slides in
	// LEFT and RIGHT move a page: held, they repeat on the paging schedule
	// (InputTiming.page_*) rather than the d-pad's ramp
	UI_VIEW_LR_PAGES = 2,
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
// With an 8x8 icon (a section label), the icon comes first, in the ink.
struct ui_label_config {
	const char *text;  // UTF-8; ui_label_set replaces it
	u8 font;           // UI_FONT_*
	u8 align;          // UI_ALIGN_*
	u8 flags;          // UI_LABEL_*
	u8 icon8;          // an 8x8 icon before the text: ART_ICON8_* + 1; 0 none
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

// What a list row shows (ui_list_model.fetch fills it).
struct ui_item {
	const char *text;    // UTF-8
	u16 len;             // 0: up to the NUL
	u8 ink;              // role of the text, 0: TEXT
	u8 key_len;          // key's bytes: 0 draws no chip (the text still at its column)
	const char *detail;  // right-aligned in DIM (a size, a date); NULL none
	const u32 *icon;     // a list with UI_LIST_ICONS: 2x2 4bpp tiles in row order,
			     // colours 10-15 (0 shows the row's paper); NULL none
	const char *key;     // a reading row's key chip (ui/ui_reading.h): the text
			     // starts at the look's reading_key_w; NULL none
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
	UI_LIST_ICONS = 4,    // list: rows keep UI_LIST_ICON_W px at the left for item.icon
	UI_LIST_FIXED = 8,    // list: never more items than whole rows fit, so it never scrolls
			      // and needs no spare row slots (a menu in a dialog); more are cut
	UI_LIST_PLAIN = 16,   // no row shows as selected (a log): the cursor only scrolls
	UI_LIST_SCROLL = 32,  // the cursor is the top row: UP and DOWN scroll a row (a
			      // reading view, with PLAIN), up to the last page
};
#define UI_LIST_ICON_W 16

// A list. ink defaults to TEXT. The viewport is the node's rectangle.
// paper 0 (none): rows are drawn transparent over what is behind them (a
// card on a plane further back), and the selection is a pill of sprites
// between the two that slides to the cursor's row. A paper: rows are drawn
// on it, the selected one with the pill under its text (a menu on a panel).
struct ui_list_config {
	const struct ui_list_model *model;
	u8 row_h;     // px, 8 to 32; 0: the theme's row_h; UI_ROW_PITCH_*: the look's
	u8 flags;     // UI_LIST_*
	u8 detail_w;  // list: px kept for the detail at the right
	u8 pad;       // the text's inset from a row's edges; 0: the theme's pad
};
#define UI_ROW_PITCH_LIST 0xFF      // row_h: the look's list_pitch (14 px)
#define UI_ROW_PITCH_SETTINGS 0xFE  // row_h: the look's settings_pitch (12 px)

// An image from 4bpp tiles in ROM (a sprite or cover image comes later, T7).
struct ui_image_config {
	const u32 *tiles;     // w x h tiles in row order
	const u16 *palette;   // 16 colours in a bank of its own; NULL: the theme's bank 0
	u16 tag;              // shares tiles and palette between views (nonzero)
};

// A vertical scroll bar showing a list's position: a 1 px track in the
// card's hairline colour (BORDER) and a thumb in ink (the item's base) on
// paper (none: transparent; PANEL on a card), at the look's scrollbar_x.
struct ui_scrollbar_config {
	u8 target;  // index of the list in the same layout
};

// A progress bar: ink (ACCENT) fills paper's track (TRACK). data may be NULL.

// A card (spec.md: cards r4 with a soft shadow): a rounded surface of paper
// (PANEL; FLOAT for a popup's) inside the rectangle, its body inset from
// the rectangle's edges by px, so a card can sit off the tile grid (the
// list card at y 20). The shadow falls shadow_offset px below the body,
// into the bottom inset. Each inset plus the corner radius stays within a
// tile (8 px). Widgets drawn over it go on the same plane, later in the
// layout, with its paper (a path, a storage bar, a section's rows), or on
// a plane in front (a list).
enum {
	UI_CARD_EDGE = 1,  // a hairline round the body, in BORDER (floating cards)
};
struct ui_card_config {
	u8 inset_l, inset_t, inset_r, inset_b;
	u8 flags;  // UI_CARD_*
};

// The title bar: the item's 16x16 icon (a sprite), the heading, and a
// right-hand text (the position or the clock) in DIM, at the look's
// title_* places. Its rectangle spans the screen's width from the top,
// whole tiles under the heading's line; both texts change without
// tearing (two tile sets each).
#define UI_TITLE_NO_ICON 0xFFFF
struct ui_title_config {
	const char *text;   // the heading; ui_title_set replaces it
	const char *right;  // ui_title_set_right replaces it; NULL none
	u16 icon;           // ART_ICON16_* (ui/art_ids.h), UI_TITLE_NO_ICON none
};

// The hint bar: up to the look's hint_max key chips with their labels, from
// the right edge in (spec.md: soft-grey key pill, ink3 label), or one line of
// text in their place (the selected row's explanation, T25). Changes swap
// whole tile sets, so they never tear.
struct ui_hint {
	const char *key;    // the chip's text: "A", "START", "«»"
	const char *label;  // what it does: "play"
};
struct ui_hints_config {
	const struct ui_hint *hints;
	u8 count;
};

// A path as pills (the SD card page's): "SD/GBA/RPG", each folder in a pill
// on the card, the last (the current one) tinted; leading folders give way
// to "..." when they don't fit. paper is the card's (PANEL). data may be
// NULL; ui_path_set sets it.

// A storage bar (the NOR page's): parts of a whole, each in the next of
// the NOR bar's three shades, on a soft track with rounded ends.
// ui_storage_set sets it. data may be NULL.

// --- Views and nodes ------------------------------------------------------

struct ui_node {
	s16 x, y, w, h;  // on screen, after anchors
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
	s16 offset;       // px it is drawn below its place (a dialog sliding, a transition)
	u16 open;         // a modal view: how far it has opened, 0-UI_OPEN (slide)
	u16 open_blend;   // and its fade and the dim under it, 0-UI_OPEN
	s8 opening;       // 1 opening, -1 closing (ui_close), 0 still
	u8 front;         // with UI_V_OBJS: planes in front of its sprites (bit p)
	u8 tall;          // planes whose map is 32 x 64 tiles (a list's, bit p)
};
#define UI_OPEN 256

// ui_view.flags
enum {
	UI_V_SHOWN = 1,   // its first draw is done and its planes are on
	UI_V_READY = 2,   // "screen ready" logged
	UI_V_CLEARED = 4,  // its maps were cleared for the first draw
	UI_V_ANIM = 8,     // a modal view that opens and closes with motion
	UI_V_OBJS = 16,    // it shows sprites (a list's pill): its planes in front of
			   // them take a better priority than the rest (ui_obj_add)
};

// --- Events -------------------------------------------------------------------

enum ui_event_type {
	UI_EV_KEYS,      // keys fired this frame (input.h): .keys
	UI_EV_ACTIVATE,  // A on a list item: .index
	UI_EV_CURSOR,    // a list's cursor moved: .index
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
// staging area, the input state, with theme (NULL: the THEME setting,
// ui_theme_setting).
void ui_init(const struct ui_theme *theme);
// The theme the THEME setting picks (settings slot assress_theme: 1 light,
// anything else dark).
const struct ui_theme *ui_theme_setting(void);
// Pops every view and releases the UI's resources.
void ui_shutdown(void);

// Pushes a view; NULL if it doesn't fit (caps, the layout's budget, a bad
// layout), with the reason in ui_error() and the log. The view is drawn
// hidden and shown in one VBlank. From an event handler, ui_pop() then
// ui_push() replaces the handler's view (the pop goes first); the handler
// must return straight after either. Tile uploads a view queued (images)
// are not cancelled by its pop: pop such a view only once they are done.
struct ui_view *ui_push(const struct ui_view_spec *spec, void *ctx);
// Pushes still to fail as if the view didn't fit, for the tests (a golden
// pokes it with mgba-shot's -p, which takes the symbol); 0 on a cart.
extern u32 ui_push_fail_test;
// Pops the top view (at the end of the event phase, if called from an event
// handler): its motions stop, its jobs are cancelled, its tiles and maps go.
void ui_pop(void);
u32 ui_depth(void);
struct ui_view *ui_top(void);
// The last error, as a sentence ("" if none).
const char *ui_error(void);

// One frame of the loop (see the top). Returns ui_depth().
u32 ui_frame(void);
// 1 between ui_init and ui_shutdown: a screen started from another runs in
// its session (pops the caller's views, pushes its own) instead of its own.
int ui_active(void);

// --- Transitions -------------------------------------------------------------
// A screen replacing another (the theme's transition[kind]):
//
//	ui_leave(UI_TR_TAB, 1);   // the screen goes out: frames run until it has
//	read_the_new_list();      // nothing shows meanwhile
//	while (ui_depth())
//		ui_pop();
//	ui_push(&next, ctx);      // drawn hidden, then it comes in
//
// dir: 1 forward (the next tab, a folder in), -1 back, 0 neither. Keys
// pressed while the screen goes out are held for the next one, which gets
// them once it shows. A modal view on top is popped first, with no motion
// (its dim carries on into the fade). Call these outside ui_frame, like
// dialogs. A transition coming in can be interrupted: ui_leave goes out
// from wherever it is.
void ui_leave(u32 kind, s32 dir);
// The next view shown comes in as after ui_leave(kind, dir), from black: a
// new session's first screen
void ui_enter(u32 kind, s32 dir);
// 1 while a transition goes out, waits for the next screen or comes in.
int ui_transitioning(void);
// 1 once the top view is on screen and still: nothing left to draw or
// upload, no transition or pop under way. Slow work that shares the frame
// (tlog_flush_soon's job) lets the screen go first while it isn't.
int ui_settled(void);
// Closes the top view: a modal one over a screen plays its way out first
// (theme dialog_out frames, keys meanwhile held for the view under it),
// then it is popped. Outside ui_frame only; anywhere else ui_pop.
void ui_close(void);

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
// The Paper clean widgets' contents. Texts and arrays are kept by pointer,
// as a label's text is: valid and unchanged until replaced or the view pops.
void ui_title_set(u8 node, const char *text);
void ui_title_set_right(u8 node, const char *text);
void ui_hints_set(u8 node, const struct ui_hint *hints, u32 count);
void ui_hints_text(u8 node, const char *text);  // NULL: back to the chips
void ui_path_set(u8 node, const char *path);
void ui_storage_set(u8 node, const u32 *parts, u32 count, u32 total);
// A drawn progress bar's pixels, for code that fills it from an interrupt
// while the kernel waits in IWRAM (progress.c, during the cart's ROM copy):
// pixel (x, y) is nibble x % 8 of word tiles[x / 8 * column_words + y],
// and the fill is rows [top, top + rows) in palette index ink. x is where
// the fill ends now; px the column value of max would fill to.
struct ui_bar_pixels {
	vu32 *tiles;
	u32 column_words, top, rows, ink, x, px;
};
// 0 if the bar isn't drawn as ui_progress_set last set it.
int ui_progress_pixels(u8 node, u32 value, u32 max, struct ui_bar_pixels *out);
// The fill now ends at column x (drawn by that code): the widget goes on
// from there.
void ui_progress_drew(u8 node, u32 x);
// The list's model changed (a new folder): the cursor goes to cursor and
// the list redraws as a page change.
void ui_list_reset(u8 node, u32 cursor);
// Items lo to hi have loaded (or changed): rows showing them redraw. A row
// on screen redraws in place, so it may tear for one frame. Lists keep
// drawn rows in a cache keyed by item (ui/rowcache.h) and show them again
// when they scroll back: an item whose row would draw differently must be
// refreshed even while off screen (or the list reset).
void ui_list_refresh(u8 node, u32 lo, u32 hi);
void ui_list_set_cursor(u8 node, u32 cursor);
// As ui_list_set_cursor, for a move key made: a repeat of a held key makes
// the list glide at its speed rather than spring a row at a time.
void ui_list_move(u8 node, u32 cursor, u16 key);
// Moves the cursor by itself, rate items a frame (16.16: INPUT_ONE is one;
// negative goes up), the list gliding with it as with a held key, until
// rate 0 or the list's end. Each move emits UI_EV_CURSOR, as a key's does,
// but from the update: its handler must not push or pop views.
void ui_list_autoscroll(u8 node, s32 rate);
u32 ui_list_cursor(u8 node);
// 1 while a page change hides or draws the list's page: a handler taking A
// itself waits until it is up, as the list's own A does
int ui_list_paging(u8 node);
// Sets label's text (ui_label_set; a UI_TITLE's right-hand text,
// ui_title_set_right) in the frame the list's page change draws its new
// page, so a title changes with the page: a label with UI_LABEL_SWAP (or a
// title's right-hand text) then flips in the same VBlank as the page shows.
// At once if no page change is hiding the old page.
void ui_list_page_label(u8 node, u8 label, const char *text);
// 1 once no page change is under way and the scroll is at rest
int ui_list_still(u8 node);
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
// Frames since a key was last held.
u32 ui_idle_frames(void);
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
// The node's ink or paper role; def if the spec says 0.
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

// Resolves a spec's rectangle against its parent's.
void ui_layout(const struct ui_spec *spec, const struct ui_node *parent, struct ui_node *out);

#endif
