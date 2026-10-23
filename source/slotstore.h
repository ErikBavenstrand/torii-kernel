// The slots of a game's saves and their history, on the card (v0.5). The pure
// helpers (names, paths, backup names, pruning) are saveslot.h's; the game's
// record (current slot) is gamesave.h's. This is everything that touches the
// card: no screen, no cart.
//
// A game is named by its ROM path from the root (rom_path, as saveslot.h
// says), a slot by its clean name. A slot's file is
// /torii/saves/<rom path>/<slot>.sav; the history of a slot is always
// /torii/backups/<rom path>/<slot>/<n>-<YYYYMMDD-HHMMSS>.sav, n rising, the
// order by n alone. A slot without a file does not exist: the first launch of
// a game makes its Main.sav.
//
// Safety, for every call here:
//  - A file that exists before the call is never changed in place. New files
//    are written to a <name>.new beside the final name, read back and
//    compared, then renamed; a file replaced goes through <file>.tmp and
//    <file>.bak exactly as the save copy does (ezkernelnew.c: Save_recover
//    finishes or undoes it). A stray .new is harmless and goes the next time
//    its folder is touched.
//  - Every step that must survive a power cut is followed by cutpoint()
//    (source/cutpoint.h); tests/slotstore.sh cuts the power at each.
//  - A call that moves or renames more than one thing writes a journal first
//    (/torii/slotop.txt, with a CRC), and slotstore_recover() at boot finishes
//    it: a slot is never lost, a version is never lost, and a rename or delete
//    is either done or not done.
//  - Nothing here deletes a version or a slot for good except pruning the
//    history to its limit (oldest first, after the new version is safe) and
//    the trash's purge after SAVESLOT_TRASH_DAYS.
//
// Where it runs: in the menu only (ROM code), where FatFs calls may run and
// the kernel's ROM is mapped. Each call borrows READCACHE_SLOTS of pReadCache
// (ez_define.h) and is done with it when it returns; none may run inside
// another FatFs operation of the caller, and none while the SD card page's
// listing (SDLIST) is in use. Paths are checked: a slot whose file or history
// folder path would not fit the pending-save record (SAV_INFO_NAME_MAX,
// SAV_INFO_HIST_MAX) is refused with SLOTSTORE_PATH, so every slot can launch.
#ifndef TORII_SLOTSTORE_H
#define TORII_SLOTSTORE_H

#include <gba_base.h>

#include "saveslot.h"

/* ---- Results ---- */

// Every call returns SLOTSTORE_OK (0) or a negative code (or, where it says so,
// a count >= 0)
enum {
	SLOTSTORE_OK = 0,
	SLOTSTORE_CARD = -1,        // the card said no (a full card, a read or write error); nothing was lost
	SLOTSTORE_NAME = -2,        // a name that is empty once cleaned (saveslot_clean_name)
	SLOTSTORE_TAKEN = -3,       // the name is a slot's already (ignoring case), or the file is there
	SLOTSTORE_NO_SLOT = -4,     // there is no such slot
	SLOTSTORE_LAST = -5,        // it is the game's only slot
	SLOTSTORE_PATH = -6,        // a path would be too long (see above), or the ROM path is bad
	SLOTSTORE_SIZE = -7,        // an import of a size the save type can't have
	SLOTSTORE_NO_VERSION = -8,  // the history has no version n
	SLOTSTORE_NO_FILE = -9,     // the file to import isn't there
	SLOTSTORE_FULL = -10,       // a game has SLOTSTORE_MAX_SLOTS slots already
};

/* ---- Slots ---- */

#define SLOTSTORE_MAX_SLOTS 16

struct slotstore_slot {
	char name[SAVESLOT_NAME_BYTES];
	u32 size;  // the file's bytes
};

// A game's slots, from its .sav files (in its folder under /torii/saves). Files whose name is
// not a clean slot name, and .tmp/.bak/.new files, are not slots. Fills out
// with up to max, in the folder's order, and returns how many there are (more
// than max if it did not all fit), or a negative code. A game with no saves
// folder has 0.
int slotstore_list(const char *rom_path, struct slotstore_slot *out, u32 max);

// The game's current slot (the record's: "Main" for a game with none) into slot
// (SAVESLOT_NAME_BYTES); it may have no file yet. Returns SLOTSTORE_OK.
int slotstore_current(const char *rom_path, char *slot);
// Makes slot the one the next launch plays. It must exist (a file).
// SLOTSTORE_NO_SLOT otherwise. Keeps the rest of the record.
int slotstore_set_current(const char *rom_path, const char *slot);

// Makes a slot called name (cleaned; the result goes to clean_out,
// SAVESLOT_NAME_BYTES, if not NULL). With copy_from NULL it is blank: a file
// of blank_size bytes of 0xFF (the caller passes the game's save size,
// Get_savefilesize; at least 512, a multiple of 512), else a copy of that
// slot's file, byte for byte. Does not change the current slot. Folder
// created as needed. SLOTSTORE_NAME, SLOTSTORE_TAKEN (also a name differing
// only in case), SLOTSTORE_NO_SLOT (copy_from), SLOTSTORE_PATH, SLOTSTORE_CARD.
int slotstore_create(const char *rom_path, const char *name, const char *copy_from, u32 blank_size, char *clean_out);
// A copy of slot named name: slotstore_create with copy_from = slot.
int slotstore_duplicate(const char *rom_path, const char *slot, const char *name, char *clean_out);

// Renames slot to name (cleaned; the result to clean_out, may be NULL): the
// file and the history folder move, and the record follows if slot was the
// current one. Journaled. A name that differs only in case is allowed (FAT
// can't rename in one step; done through a temporary name, journaled too).
// Renaming to the same name is OK and does nothing.
int slotstore_rename(const char *rom_path, const char *slot, const char *name, char *clean_out);

// Deletes slot: its file and its history move to /torii/trash/<date>-<n>/ (see
// saveslot_trash_path) for SAVESLOT_TRASH_DAYS. SLOTSTORE_LAST when it is the
// game's only slot. If it was the current slot another becomes current first
// ("Main" if that remains, else the first listed). Journaled.
int slotstore_delete(const char *rom_path, const char *slot);

/* ---- History ---- */

// The versions the History setting keeps (0 = Off)
u32 slotstore_keep(void);

#define SLOTSTORE_MAX_VERSIONS 64

struct slotstore_version {
	u32 n;                      // the counter; higher is newer, whatever the clock says
	struct saveslot_stamp when;  // the stamp in its name (all zero: the clock wasn't set)
	u32 size;
};

// The versions of slot, newest first (by n), at most max of them. Returns how
// many there are (more than max if they did not all fit), or a negative code.
// A slot with no history has 0.
int slotstore_history(const char *rom_path, const char *slot, struct slotstore_version *out, u32 max);

// Puts a copy of the file into the history folder hist_dir as the next
// version, then prunes the folder to the newest keep versions (keep 0 prunes
// nothing). Nothing is done if the newest version is already identical to the
// file (so a repeat after a power cut adds none). The file is left as it is.
// This is what the save copy calls with the path in the pending-save record.
int slotstore_push_file(const char *file, const char *hist_dir, u32 keep);
// The same, but the file itself goes into the folder (a rename: nothing is
// copied, so it costs the same for any size) and is gone after SLOTSTORE_OK;
// one identical to the newest version is removed instead. A repeat after a
// power cut inside the rename drops the file's second name. Save_bak_retire
// calls it for the save a copy or install replaced.
int slotstore_move_file(const char *file, const char *hist_dir, u32 keep);
// The same for the current file of slot, always (whatever the setting), pruning
// to the setting or to 1 version if it is Off. SLOTSTORE_NO_SLOT if no file.
int slotstore_history_push(const char *rom_path, const char *slot);

// Makes version n the slot's file; the file it replaces is pushed into the
// history first, and version n then leaves it (it is the file now), so nothing
// is lost and no copy of it stays. SLOTSTORE_NO_VERSION, SLOTSTORE_NO_SLOT.
int slotstore_restore(const char *rom_path, const char *slot, u32 n);

// Whether a file of size bytes can be imported for a save of want bytes (the
// size the game's save type has; 0 if not known, then any size a save chip can
// have: 512, 8192, 32768, 65536, 131072). The same as saveslot_import_size_ok.
int slotstore_size_ok(u32 size, u32 want);
// Makes the .sav at path (any file on the card) the slot's file; the file it
// replaces goes through the history push. The file must be exactly want bytes
// (slotstore_size_ok), else SLOTSTORE_SIZE; SLOTSTORE_NO_FILE if missing.
int slotstore_import(const char *rom_path, const char *slot, const char *path, u32 want);
// Copies the slot's file to path (any folder that exists), as a .sav someone
// can take away. SLOTSTORE_TAKEN if path exists (nothing is overwritten).
int slotstore_export(const char *rom_path, const char *slot, const char *path);

/* ---- The save type changed ---- */

// A slot's file is bigger than the save type the game launches with now
// (Process_savefile). It is never cut down or converted: the file is pushed
// into the history untouched (always, whatever the History setting) and then
// leaves the slot, so the launch makes the new type's save from the blank
// chip. Returns SLOTSTORE_OK once the file is in the history and gone from
// the slot (or was not there), else the code of what failed, and then the file is
// still where it was. Idempotent after a power cut.
int slotstore_retire_file(const char *file, const char *hist_dir);

/* ---- Boot ---- */

// Finishes what a power cut left of a rename or delete (the journal), once per
// power-on, before any slot is listed. Returns 1 if there was something to
// finish, 0 if not, negative if the card did not answer (the journal stays).
int slotstore_recover(void);

// Removes the trash folders older than SAVESLOT_TRASH_DAYS at now (NULL: the
// clock). An unset clock, or one before the folder's date, purges nothing.
// At most SLOTSTORE_PURGE_MAX folders per call. Cheap when /torii/trash is
// absent or empty; call it once per power-on. Returns how many it removed.
#define SLOTSTORE_PURGE_MAX 4
int slotstore_trash_purge(const struct saveslot_stamp *now);

// The clock as a stamp: all zero when it is not set or reads wrong
void slotstore_now(struct saveslot_stamp *out);

#endif
