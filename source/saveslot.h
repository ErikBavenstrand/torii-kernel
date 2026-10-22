// Save slots, their files' paths, the history of backups, the rename
// journal and sanity checks on a save, as plain functions over buffers and
// strings. Nothing here touches the card or the cart, so it runs on the host
// (tests/host/test_saveslot.c).
//
// A game's saves are slots with a name each. The first is "Main". A slot's
// file is /torii/saves/<rom path>/<slot>.sav (SAVESLOT_CENTRAL) or, by a
// setting, next to the ROM (SAVESLOT_BESIDE_ROM). Its history of backups is
// in /torii/backups/<rom path>/<slot>/, and a deleted slot waits in
// /torii/trash/ for SAVESLOT_TRASH_DAYS. <rom path> is the ROM's path from
// the root, extension and all, so Game.gb and Game.gbc have slots of their own.
#ifndef TORII_SAVESLOT_H
#define TORII_SAVESLOT_H

#include <gba_base.h>

/* ---- Slot names ---- */

// A slot name is at most this many characters (code points). Four bytes of
// UTF-8 each at worst, so SAVESLOT_NAME_BYTES with the NUL. Characters, not
// bytes: a Japanese name gets as many characters as an English one.
#define SAVESLOT_NAME_CHARS 24
#define SAVESLOT_NAME_BYTES (SAVESLOT_NAME_CHARS * 4 + 1)
// The first slot of a game
#define SAVESLOT_MAIN "Main"

// Cleans a name typed by the user into out (SAVESLOT_NAME_BYTES bytes):
// broken UTF-8, control characters (C0, DEL, C1) and \ / : * ? " < > | are
// dropped, spaces and dots are trimmed from both ends (a name starting with a
// dot would hide, one ending in one is lost on a PC), and what is left is cut
// after SAVESLOT_NAME_CHARS characters. Returns the length in bytes; 0 means
// nothing usable is left and the name must be refused. It is idempotent.
u32 saveslot_clean_name(char *out, const char *name);

// 1 if a and b are the same name ignoring case (ASCII and Latin-1 letters;
// other scripts compare exactly). Both should be clean.
int saveslot_names_equal(const char *a, const char *b);

// 1 if name matches any of the count names (pointers), ignoring case, except
// the one at index skip (a rename to its own name, or to its case, is allowed;
// pass count or more to skip none). Call this on a cleaned name.
int saveslot_name_taken(const char *name, const char *const *names, u32 count, u32 skip);

/* ---- Paths ---- */

// A time as the clock gave it. A clock that was never set (or wrong) is no
// problem: the stamp is only a label, the counter orders the history. All
// zero means unknown.
struct saveslot_stamp {
	u16 year;
	u8 month, day, hour, min, sec;
};

#define SAVESLOT_CENTRAL 0
#define SAVESLOT_BESIDE_ROM 1

#define SAVESLOT_SAVES_DIR "/torii/saves"
#define SAVESLOT_BACKUPS_DIR "/torii/backups"
#define SAVESLOT_TRASH_DIR "/torii/trash"
#define SAVESLOT_TRASH_DAYS 30

// The longest ROM path the kernel opens: its path buffers (MAX_path_len)
// hold 256 bytes with the NUL.
#define SAVESLOT_ROM_PATH_MAX 255

// Every builder writes its path into out (size bytes) and returns its length,
// or -1 with out empty when the path would not fit or an argument is bad:
// the ROM path must start with '/', and neither it nor the slot may have an
// empty or ".." part or a slot may have a '/'. Never use a path after -1.

// The slot's save file. SAVESLOT_CENTRAL: /torii/saves/<rom path>/<slot>.sav.
// SAVESLOT_BESIDE_ROM: <rom dir>/<rom name without extension>.<slot>.sav, so
// Game.gba with slot Main saves as Game.Main.sav in the ROM's folder.
int saveslot_save_path(char *out, u32 size, int mode, const char *rom_path, const char *slot);
// The slot's history folder, whatever the mode: /torii/backups/<rom path>/<slot>
int saveslot_backup_dir(char *out, u32 size, const char *rom_path, const char *slot);
// The game's folder of slots under /torii/saves/<rom path>
int saveslot_game_dir(char *out, u32 size, const char *rom_path);
// A deleted slot's file: /torii/trash/<YYYYMMDD-HHMMSS>-<n>/<rom path>/<slot>.sav.
// The first part is the day it went there (see saveslot_trash_expired), n
// (from 1) keeps two deletes in one second apart. Its history, if kept, goes
// to the same folder under backups/.
int saveslot_trash_path(char *out, u32 size, const char *rom_path, const char *slot, const struct saveslot_stamp *when,
			u32 n);

// The length of the longest path above for a ROM path of rom_len bytes in
// SAVESLOT_CENTRAL mode (which is the longest save path): all slot names of
// SAVESLOT_NAME_BYTES - 1 bytes.
u32 saveslot_longest_save_path(u32 rom_len);
// The same with slots of slot_len bytes.
u32 saveslot_save_path_len(u32 rom_len, u32 slot_len);
// The longest ROM path whose central save path fits limit bytes (without the
// NUL) with slots of slot_len bytes; 0 if none does.
u32 saveslot_max_rom_path_for(u32 limit, u32 slot_len);

/* ---- History ---- */

// "YYYYMMDD-HHMMSS" plus the NUL
#define SAVESLOT_STAMP_BYTES 16
// A backup's name: <n>-<YYYYMMDD-HHMMSS>.sav, n at least 4 digits, up to 9
#define SAVESLOT_BACKUP_NAME_BYTES 32
#define SAVESLOT_COUNTER_MAX 999999999u

// Writes the backup's name for counter n (1 to SAVESLOT_COUNTER_MAX) into out
// (SAVESLOT_BACKUP_NAME_BYTES bytes) and returns its length, or 0 for a bad n.
u32 saveslot_backup_name(char *out, u32 n, const struct saveslot_stamp *when);
// Reads a backup's name into *n and *when. 0 if name is malformed (wrong
// shape, n 0 or past the maximum, an impossible date): such a file is not
// part of the history, and nothing prunes or reuses it.
int saveslot_parse_backup_name(const char *name, u32 *n, struct saveslot_stamp *when);
// The counter for the next backup: one past the highest n among names (the
// malformed ignored), 1 for none; 0 if the counter has run out.
u32 saveslot_next_counter(const char *const *names, u32 count);
// Which of names to delete to keep the newest keep backups. Writes their
// indexes to out (room for count), oldest first, and returns how many. Order
// is by n alone (ties, which only a copied file makes, by name), never by
// the stamp.
u32 saveslot_prune(const char *const *names, u32 count, u32 keep, u32 *out);

// 1 if a trash folder made by saveslot_trash_path dated then is older than
// SAVESLOT_TRASH_DAYS at now. An unknown date (all zero), or now unknown, or
// now before then, never expires: a wrong clock must not empty the trash.
int saveslot_trash_expired(const struct saveslot_stamp *then, const struct saveslot_stamp *now);

/* ---- Rename journal ---- */

// Renaming a slot is two renames: the save file, then the history folder. The
// journal is written before the first and removed after the second, so a cut
// leaves it behind and boot recovery (saveslot_recover) finishes or undoes the
// rename.
#define SAVESLOT_JOURNAL_BYTES 256

// Writes the journal into out (size bytes): a header line, the old name, the
// new name, and a CRC-32 of both, in text. Returns its length, or 0 if a name
// is not clean or empty or out is too small.
u32 saveslot_journal_write(char *out, u32 size, const char *old_name, const char *new_name);
// Reads a journal of len bytes into old_name and new_name (SAVESLOT_NAME_BYTES
// each). 0 if it is cut, damaged or has a name that is not clean; the rename
// then never started, and the file can go.
int saveslot_journal_read(const char *data, u32 len, char *old_name, char *new_name);

// What exists of a rename's four files
#define SAVESLOT_OLD_SAVE 1
#define SAVESLOT_NEW_SAVE 2
#define SAVESLOT_OLD_HIST 4
#define SAVESLOT_NEW_HIST 8

enum saveslot_recovery {
	SAVESLOT_RECOVER_NOTHING, // nothing to rename; remove the journal
	SAVESLOT_RECOVER_FINISH,  // rename the history folder old to new, then remove the journal
	SAVESLOT_RECOVER_UNDO,    // rename the save file new back to old, then remove the journal
};

// What to do for the files that exist (OR of the flags above). The save is
// renamed first, so the rename went part of the way only when the save is
// under the new name alone: then the history folder follows if it can, and
// if the new name's folder is in the way, the save goes back. Every other
// state (nothing renamed yet, both saves present, no save) is left as it is.
// After the action, the same call returns SAVESLOT_RECOVER_NOTHING.
enum saveslot_recovery saveslot_recover(u32 exists);

/* ---- Sanity checks ---- */

// Starting state for saveslot_blank_scan
#define SAVESLOT_BLANK_START 3u
// Continues a scan of a save in chunks: state is SAVESLOT_BLANK_START, then
// each call's result. The save is blank, as an unwritten chip is (all 0xFF,
// or all 0x00 for a chip that was cleared), when the last state is not 0.
u32 saveslot_blank_scan(u32 state, const void *data, u32 size);
// 1 if all size bytes are 0xFF, or all 0x00 (also if size is 0)
int saveslot_is_blank(const void *data, u32 size);
// 1 if both buffers hold the same size bytes
int saveslot_identical(const void *a, const void *b, u32 size);

#endif
