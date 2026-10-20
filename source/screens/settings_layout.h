// The Settings page's layout and hotkey capture as data (ticket B16;
// docs/design/v0.4/mockups-b16-b18.md), hardware-free so tests/host runs
// them; screens/settings_page.c draws and drives them.
//
// The page is one list of 12 px slots: each section is its label's slot,
// its visible rows (settings_section_rows), and a gap slot under its card,
// so every line of the page scrolls as one list. The cursor stays on rows.
#ifndef TORII_SETTINGS_LAYOUT_H
#define TORII_SETTINGS_LAYOUT_H

#include <gba_base.h>

#include "../settings_model.h"

enum settings_slot_kind {
	SET_SLOT_LABEL,  // a section's label, on the paper over its card
	SET_SLOT_ROW,    // a row of its card
	SET_SLOT_GAP,    // under its card: its last padding, its shadow, the paper
};

struct settings_slot {
	u8 kind;     // enum settings_slot_kind
	u8 section;  // enum settings_section
	u8 id;       // SET_SLOT_ROW: enum settings_id
};

#define SET_LAYOUT_SLOTS (2 * SET_SECTIONS + SET_ROWS)
#define SET_LAYOUT_NONE 0xFF

struct settings_layout {
	struct settings_slot slot[SET_LAYOUT_SLOTS];
	u8 count;  // slots
	u8 rows;   // of them rows
	u8 first[SET_SECTIONS], last[SET_SECTIONS];  // each section's first and last row's slot
};

// The slots for the rows v shows (LED off hides the LED colours)
void settings_layout_build(struct settings_layout *l, const struct settings_values *v);
// Row id's slot, SET_LAYOUT_NONE if it doesn't show
u32 settings_layout_slot_of(const struct settings_layout *l, u32 id);
// The row at slot s, or the nearest row after it (before it at the end): a
// slot the cursor may rest on
u32 settings_layout_row_at(const struct settings_layout *l, u32 s);
// Row slot s's place among the rows, 1 to rows
u32 settings_layout_position(const struct settings_layout *l, u32 s);
// The row slot after (dir > 0) or before row slot s; at an end, the other
// end's with wrap, else s
u32 settings_layout_step(const struct settings_layout *l, u32 s, int dir, int wrap);
// The first row of the section after (dir > 0) or before the one row slot s
// is in, wrapping; back to its own first row first when s is past it and
// dir < 0
u32 settings_layout_section(const struct settings_layout *l, u32 s, int dir);

// --- Hotkey capture -----------------------------------------------------------
// A starts it on a key row: the keys then held (A) are left out until let
// go. Each frame takes the keys held. The largest set held for hold frames
// in a row is the answer, offered once every key is up. Nothing held for
// timeout frames, or B alone, cancels.
enum {
	SET_CAPTURE_WAIT,
	SET_CAPTURE_DONE,    // best holds the keys
	SET_CAPTURE_CANCEL,
};
struct settings_capture {
	u16 ignore;  // keys held at the start, until let go
	u16 held;    // the keys held now (ignore left out): what the row shows
	u16 run;     // the set held since run_frames
	u16 run_frames;
	u16 best;    // the largest set held long enough
	u16 seen;    // every key pressed since the start
	u16 idle;    // frames with nothing held, since the last change
	u16 hold, timeout;
};
void settings_capture_start(struct settings_capture *c, u16 held, u32 hold, u32 timeout);
u32 settings_capture_step(struct settings_capture *c, u16 held, u32 dt);
// Frames left before it gives up (timeout while keys are held)
u32 settings_capture_left(const struct settings_capture *c);

#endif
