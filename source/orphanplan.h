// The global save manager's hardware-free parts (v0.5): the name an orphan's
// slot takes when the game it is relinked to has that name already, the
// journal text of a relink or a delete, and the order of the relink picker's
// rows. Nothing here touches the card, so tests/host runs it
// (tests/host/test_orphanplan.c); saveorphan.c is the card side.
#ifndef TORII_ORPHANPLAN_H
#define TORII_ORPHANPLAN_H

#include <gba_base.h>

#include "saveslot.h"

/* ---- Names ---- */

// The slot a file of a save folder belongs to: "Main.sav" and its leftovers
// "Main.sav.tmp", "Main.sav.bak", "Main.sav.new", "Main.sav.ren" (the suffix
// ignoring case) are slot "Main". Returns the length of the slot's name
// (bytes), or 0 if name is none of those (game.ini, an empty slot name, any
// other file). *rest points at what follows the name: ".sav" and the leftover's
// ending.
u32 orphanplan_slot_of(const char *name, const char **rest);

// Whether a candidate name is taken
typedef int (*orphanplan_taken_fn)(void *ctx, const char *name);

// The name a slot called name takes at its new game (out, SAVESLOT_NAME_BYTES):
// name itself if taken says it is free, else name + " 2", " 3" ... the first
// that is, with name cut (whole characters) so that the result stays within
// SAVESLOT_NAME_CHARS. Returns 1, or 0 if none of 2 to 99 is free (out is then
// empty). The rule is stable: for the same names it gives the same result.
int orphanplan_pick_name(char *out, const char *name, orphanplan_taken_fn taken, void *ctx);

/* ---- The journal ---- */

#define ORPHANPLAN_MAX 32  // slot names one relink moves
// A kind: relink the orphan folder of a game to another game, or delete it to the trash
#define ORPHANPLAN_RELINK 'L'
#define ORPHANPLAN_TRASH 'T'

// A job as the journal holds it; the strings point into the buffer
// orphanplan_read parsed
struct orphanplan_job {
	char kind;
	const char *from;  // the orphan's ROM path
	const char *to;    // ORPHANPLAN_RELINK: the ROM path it moves to; ORPHANPLAN_TRASH: the trash folder
	u32 n;             // ORPHANPLAN_RELINK: the slots below
	const char *x[ORPHANPLAN_MAX];  // a slot's name in the orphan ...
	const char *y[ORPHANPLAN_MAX];  // ... and the name it takes at the target
};

// Writes the journal into out (size bytes): header, kind, from, to, n, one
// "x<TAB>y" line a slot, and the CRC-32 of all before it. Returns its length,
// or 0 if it does not fit or a string is empty, has a control character, or
// there are more than ORPHANPLAN_MAX slots.
u32 orphanplan_write(char *out, u32 size, char kind, const char *from, const char *to, const char *const *x,
		     const char *const *y, u32 n);
// Reads a journal of len bytes at data (writable, NUL-terminated by the
// caller at data[len]; the lines are cut in place) into job. 0 if it is cut,
// damaged or not a journal: the operation then never started.
int orphanplan_read(char *data, u32 len, struct orphanplan_job *job);

/* ---- The relink picker ---- */

#define ORPHANPICK_NAMES 16  // games with the orphan's file name listed first

// The order of the games a save folder can be relinked to. The library lists
// its games by console, then by name: "positions" 0 to total - 1. The picker
// puts first the games with the orphan's own file name (names, in the order
// given), then the rest of the orphan's console (positions cs to ce - 1)
// in the library's order, then every other game in it.
struct orphanpick {
	u32 total;
	u32 cs, ce;  // the console's run (cs == ce: none)
	u32 nn;      // games listed first
	u32 first[ORPHANPICK_NAMES];   // their positions, as to be shown
	u32 sorted[ORPHANPICK_NAMES];  // the same, ascending
};
// Starts a picker for total games; the console's run is [cs, ce)
void orphanpick_begin(struct orphanpick *p, u32 total, u32 cs, u32 ce);
// Lists position pos first (the next of the names), ignored past
// ORPHANPICK_NAMES; a position outside 0 to total - 1, or listed already, is
// ignored
void orphanpick_add(struct orphanpick *p, u32 pos);
// The library position of row (0 to total - 1) of the picker. Each position
// is one row.
u32 orphanpick_pos(const struct orphanpick *p, u32 row);
// How many rows are in the first group (the same file name) and the second
// (the same console), the rest being the third
u32 orphanpick_group(const struct orphanpick *p, u32 row);

#endif
