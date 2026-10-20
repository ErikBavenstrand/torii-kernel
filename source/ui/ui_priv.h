// Helpers the widgets (ui/ui_widgets.c, ui/ui_list.c) share with ui/ui_run.c:
// tiles, map entries, the VRAM staging area. Not for screens.
#ifndef TORII_UI_PRIV_H
#define TORII_UI_PRIV_H

#include "ui.h"

// The shared tiles each character block has (ui_shared_entry): blank, a
// solid tile per role, panel outlines and rounded corners.
enum {
	UI_TILE_BLANK = 0,
	UI_TILE_SOLID = 0,  // + role (1-9)
	UI_TILE_BORDER = 10,  // corners TL, TR, BL, BR, then edges T, B, L, R
	UI_TILE_ROUND = 18,   // corners TL, TR, BL, BR
	UI_SHARED_TILES = 22,
};

// Character block of plane: 1 for plane 1 and 2 for plane 3 (the planes
// lists use), 0 for planes 0 and 2. A BG reaches 1024 tiles from its block.
static inline u32 ui_plane_cb(u32 plane)
{
	return plane == 3 ? 2 : plane & 1;
}

// A map entry: tile (counted from the plane's character block) in bank.
static inline u16 ui_entry(u32 tile, u32 bank)
{
	return (u16)(tile | bank << 12);
}

// The map entry of shared tile tile (UI_TILE_*) on plane.
u16 ui_shared_entry(u32 plane, u32 tile);

// count tiles on the node's plane, tagged (0: untagged). RES_NONE on failure
// (logged). *first gets the first tile's number for map entries.
u8 ui_tiles(u8 node, u32 count, u16 tag, u16 *first);

// The node's plane map in VRAM (32 x 32 entries).
u16 *ui_map(u8 node);

// Rows [ty, ty + rows) of the node's plane map, to change: the map itself
// while its view isn't shown yet, otherwise a copy in the staging area
// (filled with the map's current entries) that the commit copies over the
// map in the next VBlank, so the change shows all at once. NULL if the
// staging area or the queue is full: try again next frame.
u16 *ui_map_rows(u8 node, u32 ty, u32 rows);

// 1 while rows [ty, ty + rows) of the node's map wait for a copy staged in
// an earlier frame: what the map shows there isn't known yet (a label
// flipping its tile sets checks it before drawing into the hidden set).
int ui_map_busy(u8 node, u32 ty, u32 rows);

// 1 if the node's view is the topmost with its plane (shown or not yet).
int ui_plane_owned(u8 node);

// Fills the node's rectangle in its map with entry (through ui_map_rows);
// 0 if it must try again.
int ui_map_fill(u8 node, u16 entry);

// 1 once the node's view has been drawn and shown.
int ui_node_shown(u8 node);

// Hides or shows a plane for a list's page change (hidden from the next
// commit on).
void ui_plane_hide(u32 plane, int hidden);
// 1 if a hide asked for earlier has been committed: the plane is off screen.
int ui_plane_hidden_now(u32 plane);
// Shows a plane hidden for a page change, fading it in over the theme's
// page_fade frames from the next VBlank: alpha blending over what is behind
// it, so the new page comes up softly instead of popping. A plain show while
// a darken blend is on (a modal view's dim, DIM) or with page_fade 0.
void ui_plane_reveal(u32 plane);
// Fades a shown plane out over the theme's page_fade_out frames before a
// page change hides it (alpha blending, as the fade-in): 0 if it can't (no
// frames, another plane's fade running, a darken blend on), and the caller
// hides it at once. Once ui_plane_fading_out goes 0 the blend is off: the
// caller hides the plane in that frame.
int ui_plane_fade_out(u32 plane);
int ui_plane_fading_out(u32 plane);
// Keys a widget held back (A during a list's page change), dispatched with
// the next frame's keys as if pressed then. A pop forgets them. They count
// as the user's: they wake the screen from DIM, so queue only keys just
// pressed.
void ui_queue_keys(u16 keys);

// Cycles into the frame by which drawing that nobody sees yet ends, so the
// frame still ends before the next VBlank (a frame is 280,896 cycles) with
// room for the nodes drawn after: a view not shown yet (nothing moves while
// a screen comes in, so it takes most of the frame and shows sooner),
#define UI_DRAW_BUDGET 230000
// and a list's page change (its rows drawn ahead into the row cache while
// the old page fades out, then copied in), which leaves room for motion
// running alongside and a slower frame on the cart.
#define UI_PAGE_BUDGET 140000
// 1 if such drawing may start work of about cycles now: it would end by
// budget. Otherwise it waits for the next frame.
int ui_draw_room(u32 cycles, u32 budget);

// --- Sprites ----------------------------------------------------------------
// Widgets show sprites by adding them in their update, every frame they
// show (ui_frame rebuilds OAM around the update): the view's OBJ priority
// goes into attr2, under the view's front planes (a list's text) and over
// the rest (its card). Colours: the UI's sprite bank (ui_obj_bank) holds
// bank 0's roles (UI_ROLE_*), following the theme and item.

// Adds a sprite for node's view (attr2 with its tile and palette bank, the
// priority bits set here: the view's); 0 if the
// view's sprites don't show now (a view above blends over it with sprites
// of its own, or no priority is left under its text) or OAM is full.
int ui_obj_add(u8 node, u16 attr0, u16 attr1, u16 attr2);
// The UI's sprite palette bank, for attr2's palette bits.
u32 ui_obj_bank(void);
// Marks the view as showing sprites behind plane front (and plane 0).
void ui_view_uses_objs(struct ui_view *v, u32 front);
// px the view's planes are moved sideways (a transition's slide): its
// sprites move with them.
s32 ui_view_dx(const struct ui_view *v);
// 1 while plane fades in or out for a page change (ui_plane_reveal,
// ui_plane_fade_out): sprites drawn with it fade too as semi-transparent
// ones (the blend's first target is the plane alone).
int ui_plane_blending(u32 plane);

// The inset of the row e px from a rounded end of radius px, at the edge
// (e 0) and inwards (0 from e = radius on): the menu's one rounding, for
// cards, pills and chips. Hardware-free (ui/ui_theme.c).
u32 ui_round_inset(u32 radius, u32 e);

// The ops of lists (ui/ui_list.c), the Paper clean widgets (ui/ui_paper.c)
// and the popups (ui/ui_popup.c).
extern const struct ui_ops ui_list_ops;
extern const struct ui_ops ui_card_ops, ui_title_ops, ui_hints_ops, ui_path_ops, ui_storage_ops;
extern const struct ui_ops ui_popup_ops, ui_confirm_ops, ui_letter_ops;
// The home carousel's (screens/carousel.c)
extern const struct ui_ops ui_carousel_ops;

// --- Overlays ----------------------------------------------------------------
// What a popup, a confirmation or the letter card (ui/ui_popup.c) asks of
// the registers (research override R9), in the next regs:
//   window  1: plane 0 shows only inside [x0, x1) x [y0, y1) (WIN0; lists
//           clip with WIN1 meanwhile, one of them), while it opens or closes
//   dx      px plane 0 is moved right (a popup sliding in)
//   dim     the brightness blend on every plane but 0, and the sprites and
//           backdrop (0-16): towards white or black, as the theme's
//           confirm_brighten says
//   owner   whose it is (ui_overlay_drop)
struct ui_overlay {
	s16 x0, y0, x1, y1;
	s16 dx;
	u8 window;
	u8 dim;
	const void *owner;
};
// Sets it. One at a time: the last set wins. A pop of a view with plane 0
// drops it.
void ui_overlay_set(const struct ui_overlay *overlay);
// Drops it if owner's is the one set.
void ui_overlay_drop(const void *owner);

// The hint bar's keys and text (ui_hints_set, ui_hints_text), to give them
// back after a popup
void ui_hints_get(u8 node, const struct ui_hint **hints, u32 *count, const char **text);
// An 8x8 icon (art.h, ART_ICON8_*) at pixel (x, y) of a window, in ink
struct text_window;
void ui_icon8(const struct text_window *win, u32 x, u32 y, u32 id, u32 ink);

struct text_window;
// A key chip at x on a band of the look's hint_chip_h from line top of a
// window: a soft pill with the key's name, then the label in FAINT (NULL
// or "": none); returns the px it takes, drawn (draw 1) or not.
u32 ui_key_chip(const struct text_window *win, u32 x, u32 top, const char *key, const char *label, int draw);
// Forgets the rows lists keep drawn in the row cache (a theme change).
void ui_list_cache_clear(void);

// The current font for UI_FONT_*.
const struct torii_font *ui_font(u32 which);

#endif
