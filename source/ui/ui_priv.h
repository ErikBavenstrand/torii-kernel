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

// Cycles into the frame after which drawing that nobody sees yet (a page
// change, a view not shown) waits for the next frame, so no frame runs over
// (a frame is 280,896 cycles; a list row takes up to about 40,000).
#define UI_DRAW_BUDGET 180000

// 1 while the d-pad repeats at the speed that skips scroll animation.
int ui_skipping(void);

// The ops of lists and grids (ui/ui_list.c).
extern const struct ui_ops ui_list_ops;

// The current font for UI_FONT_*.
const struct torii_font *ui_font(u32 which);

#endif
