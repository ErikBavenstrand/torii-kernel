// The card side of the global save manager (v0.5): the space the saves use,
// the orphaned saves (a game's save folder under /torii/saves whose ROM is no
// longer at that path), relinking one to another ROM, deleting it to the
// trash, and emptying the trash. No screen, no cart (screens/saves.c is the
// screen). The slot store (slotstore.h) lists, renames and deletes the slots
// of a game that is there; this is for the folders of games that are not.
//
// A game folder is a folder under /torii/saves holding .sav files or a
// game.ini; its ROM path is the folder's path below /torii/saves (the NOR
// games' pseudo paths, /torii/nor/..., are never orphans). It is an orphan
// when f_stat finds no file at that ROM path.
//
// Relink moves the orphan's save folder and history folder to the target's.
// The target must be a file on the card. The target's own slots, record
// (game.ini) and history are never changed or overwritten. A slot of the
// orphan whose name the target has already, as a slot file or a history
// folder and ignoring case, takes the first free name "<name> 2", "<name> 3",
// ... (orphanplan_pick_name); its history and leftover files follow it. The
// orphan's game.ini goes to the target only if the target has none, else it is
// dropped (it holds the save type and the current slot, which the target keeps).
// A slot name that would make a path too long to launch (the pending-save
// record's room, SAV_INFO_NAME_MAX) refuses the whole relink before anything
// moves.
//
// Safety, as the slot store's (slotstore.h): nothing is overwritten, every
// move is a rename (so a save is in the old place or the new one, never
// neither), and a call that moves more than one thing writes a journal first
// (/torii/orphanop.txt, with a CRC; the slot store keeps /torii/slotop.txt) that
// saveorphan_recover() finishes at boot, always forward: the plan (which slot
// takes which name) is in the journal, so a repeat after a cut gives the same
// result. Each step is followed by cutpoint(); tests/saveorphan.sh cuts the
// power at each.
//
// Where it runs: in the menu only, outside ui_frame, like the slot store. Each
// call borrows READCACHE_ORPHAN of pReadCache (ez_define.h) and is done with it
// when it returns; none may run inside another FatFs operation of the caller.
#ifndef TORII_SAVEORPHAN_H
#define TORII_SAVEORPHAN_H

#include <gba_base.h>

#include "orphanplan.h"

enum {
	ORPHAN_OK = 0,
	ORPHAN_CARD = -1,       // the card said no; nothing was lost
	ORPHAN_NO_SOURCE = -2,  // the orphan's save folder isn't there (any more)
	ORPHAN_NO_TARGET = -3,  // the target ROM isn't a file on the card
	ORPHAN_SAME = -4,       // the target is the orphan's own path
	ORPHAN_PATH = -5,       // a path or a name would be too long, or is bad; nothing moved
	ORPHAN_STOPPED = -6,  // two different things in one place; nothing was overwritten, the rest stays where it is
};

/* ---- The scan ---- */

#define ORPHAN_MAX 32  // orphans listed
#define ORPHAN_POOL 2560  // bytes of their ROM paths

struct orphan_scan {
	// The space used, in file bytes (not clusters): the saves under /torii/saves,
	// the history under /torii/backups, the trash under /torii/trash
	u32 saves_bytes, backups_bytes, trash_bytes;
	u32 saves_files, backups_files, trash_files;
	u32 games;  // save folders seen (NOR games' too)
	u32 count;  // orphans listed
	u32 more;  // orphans past ORPHAN_MAX or the pool: they show after the first are dealt with
	u8 partial;  // a folder too deep or too long, or a read that failed: the figures are low
	u16 off[ORPHAN_MAX];  // the ROM path in pool
	u16 slots[ORPHAN_MAX];  // slot files (.sav)
	u32 bytes[ORPHAN_MAX];  // its saves and history, file bytes
	u16 pool_used;
	char pool[ORPHAN_POOL];
};

// Called now and then during a scan: phase 0 the saves, 1 the history, 2 the
// trash, 3 the orphans' history (ORPHAN_PHASES), with the entries read so far
// in the phase. Dialogs may run in it.
#define ORPHAN_PHASES 4
typedef void (*orphan_progress_fn)(void *ctx, u32 phase, u32 entries);

// Walks /torii/saves, /torii/backups and /torii/trash (read only: f_readdir
// and one f_stat for each save folder) and fills s. Bounded: folders more than
// 14 deep are skipped (partial). Returns ORPHAN_OK or ORPHAN_CARD (a card that
// does not answer at all); the pieces missing then are zero.
int saveorphan_scan(struct orphan_scan *s, orphan_progress_fn progress, void *ctx);

// The orphan i's ROM path
static inline const char *saveorphan_path(const struct orphan_scan *s, u32 i)
{
	return s->pool + s->off[i];
}

/* ---- Relink, delete ---- */

// Moves the save and history folders of the orphan from to the ROM at to (see
// above). Journaled. *renamed, if not NULL, gets how many slots took another
// name. ORPHAN_NO_SOURCE, ORPHAN_NO_TARGET, ORPHAN_SAME, ORPHAN_PATH,
// ORPHAN_CARD, ORPHAN_STOPPED.
int saveorphan_relink(const char *from, const char *to, u32 *renamed);
// Moves the save and history folders of the orphan from to the trash, as
// /torii/trash/<YYYYMMDD-HHMMSS>-<n>/<rom path> and .../backups/<rom path>,
// where slotstore_trash_purge removes them after SAVESLOT_TRASH_DAYS. Journaled.
// ORPHAN_NO_SOURCE, ORPHAN_PATH (a ROM path over ORPHAN_TRASH_ROM_MAX bytes),
// ORPHAN_CARD, ORPHAN_STOPPED.
#define ORPHAN_TRASH_ROM_MAX 160
int saveorphan_trash(const char *from);

// Removes everything in /torii/trash (the folder stays), a folder at a time,
// calling progress (phase 0, the folders removed so far) between them. Not
// journaled: a cut leaves some folders and the rest go next time. *removed
// gets how many folders went. ORPHAN_OK or ORPHAN_CARD.
int saveorphan_trash_empty(orphan_progress_fn progress, void *ctx, u32 *removed);

/* ---- Boot ---- */

// Finishes what a power cut left of a relink or a delete, once per power-on,
// after slotstore_recover(). Returns 1 if there was something to finish, 0 if
// not, negative if the card did not answer (the journal stays).
int saveorphan_recover(void);

#endif
