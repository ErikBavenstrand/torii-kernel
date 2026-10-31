// A game's cheat file on the card (v0.7): /torii/cheats/<rom path without
// its extension>.cht, a safe file (safefile.h: NAME and NAME.bak) in
// libretro's format (cheatfile.h). tools/cheats.py writes it; a libretro file
// dropped there by hand, named after the ROM, works too. It moves, is
// renamed and is deleted with its game (actions.c).
//
// The file's text is read whole into PSRAM (PSRAM_CHEATS_BASE,
// engine/psram.h), in RAM-sized parts; its cheats, the decoder's work and the
// parts on their way are in pReadCache (READCACHE_CHEATS, ez_define.h):
// struct cheatstore below, which lasts until another owner borrows that part
// of pReadCache. The text lasts until the next read, or a game's load over
// PSRAM. The code keypad's edits change it in place (cheatstore_add, _set_code,
// _remove), through PSRAM writes.
//
// A larger file (over CHEATFILE_TEXT_MAX bytes or CHEATFILE_MAX cheats, up
// to CHEATSTORE_BIG_MAX) is read in windows of about CHEATINDEX_CHEATS
// cheats (cheatindex.h), through its index, <stem>.cdx: an open reads the
// index and the first window, and a window is read from the card when the
// cheat screen goes to it (or before, ahead of the cursor), its CRC checked
// against the index's, into its slot of CHEATSTORE_SLOT bytes on PSRAM page
// 1 (CHEATSTORE_SLOT_PAGE, free while the menu runs; a launch fills it with
// the game). The index keeps the decoder at each window's first cheat, so
// any window decodes on its own. One window at a time is the text the cheat
// file model reads (cheatfile_parse_part, its cheats numbered as in the
// file), as a small file's whole text is. A file with no index that fits
// (none, or the file changed since: `just cheats` wrote it anew, or it was
// edited on a PC) is read whole once, decoded, and its index written.
//
// A larger file's picks are the index's: a list (struct cheatstore_pick)
// with what each takes of the budget, so the budget counts every window's
// picks without decoding them, and a pick saves the index alone, a few KB,
// never the whole file. Its enable lines are written only when the file
// is written whole: after an edit, or Settings' clean-up.
#ifndef TORII_CHEATSTORE_H
#define TORII_CHEATSTORE_H

#include <gba_base.h>

#include "cheatfile.h"
#include "cheatindex.h"
#include "ff.h"
#include "safefile.h"

#define CHEATS_DIR "/torii/cheats"
// The largest copy read: the text and its CRC line
#define CHEATSTORE_FILE_MAX (CHEATFILE_TEXT_MAX + SAFEFILE_CRC_LINE_LEN + 2)
// A part of the text on its way between the card and PSRAM, and between two
// places in PSRAM; and what one read from the card takes
#define CHEATSTORE_PART 1024
#define CHEATSTORE_CHUNK 4096
// The cheat screen's Guide page: its text and wrapped rows
#define CHEATSTORE_DETAIL 2048
#define CHEATSTORE_DETAIL_ROWS 64
// A larger file's windows: at most CHEATSTORE_WINDOWS, each in a slot of
// CHEATSTORE_SLOT bytes on PSRAM page 1 (the page's 8 MB), room for its text
// and the keypad's edits up to CHEATFILE_TEXT_MAX
#define CHEATSTORE_WINDOWS 64
#define CHEATSTORE_SLOT 0x20000
#define CHEATSTORE_SLOT_PAGE 0x1000
// The largest windowed text, and copy read
#define CHEATSTORE_BIG_MAX 0x400000
#define CHEATSTORE_BIG_FILE_MAX (CHEATSTORE_BIG_MAX + SAFEFILE_CRC_LINE_LEN + 2)
// A larger file's index, the largest read
#define CHEATSTORE_INDEX_MAX 0x8000
// A larger file's picks at most (the budget allows far fewer)
#define CHEATSTORE_PICKS 512

// Settings' clean-up's walk of CHEATS_DIR: a folder open at each depth, and
// the path it has got to
#define CHEATSTORE_DEPTH 8
struct cheatstore_walk {
	DIR dirs[CHEATSTORE_DEPTH];
	u32 len[CHEATSTORE_DEPTH];
	FILINFO info;
	char at[SAFEFILE_PATH_MAX];
};

// A window of a larger file: its index entry (its text's place in the file,
// its CRC, first cheat's number, cheats, and the decoder at its start)
struct cheatstore_window {
	struct cheatindex_window ix;
	u8 loaded;  // its slot holds its text
	u8 edited;  // its text was changed (an edit, the clean-up): the file is written whole
	u8 failed;  // read, and not as its index says: never read again (the index set aside, its size 0, its picks kept)
};

// A larger file's pick: its number and what it takes of the budget
struct cheatstore_pick {
	u16 number, bytes, patches, pad;
	u32 cost, hash;
};

// A window being read ahead (cheatstore_ahead): which, and how far
struct cheatstore_ahead {
	u32 window, done, crc;
	u8 busy;
};

struct cheatstore {
	struct cheatfile file;
	struct cheatfile_work work;
	struct cheatfile_cheat cheats[CHEATFILE_MAX];
	char path[SAFEFILE_PATH_MAX];  // the file's, as cheatstore_read read it
	u16 rows[CHEATFILE_MAX];  // the cheat screen's: the cheat each row shows
	u32 part[CHEATSTORE_CHUNK / 4];  // a part read from the card (word-aligned for f_read)
	u16 stage[CHEATSTORE_PART / 2 + 2];  // a part on its way to PSRAM, a byte each side for odd ends
	u8 room_failed;  // a PSRAM write of the text failed since the read: never written back
	u8 windows;  // a larger file's windows (cheatstore_window_go); 0 for a small one, read whole
	u8 window;  // the one whose text is read now
	u8 picks_changed;  // a larger file's picks differ from its index's: the index is written
	u16 n_picks;
	u32 file_size;  // a larger file's bytes, as its index says
	struct cheatstore_window win[CHEATSTORE_WINDOWS];
	struct cheatstore_pick picks[CHEATSTORE_PICKS];
	struct cheatstore_ahead ahead;
	FIL fil;  // a window read from the card
	// cheatstore_clean's: each staying cheat's status and hash, as read before
	u32 sig[CHEATFILE_MAX];
	struct cheatstore_walk walk;  // cheatstore_clean_all's
	// The cheat screen's Guide page (ui/ui_reading.h): a cheat's name, activation, guide, code
	// and what decoding found
	char detail[CHEATSTORE_DETAIL];
	u16 detail_start[CHEATSTORE_DETAIL_ROWS];
	u8 detail_len[CHEATSTORE_DETAIL_ROWS], detail_kind[CHEATSTORE_DETAIL_ROWS];
};
struct cheatstore *cheatstore(void);
// A long call's progress (decoding a larger file's windows, cleaning cheat
// files): done of total, for a progress bar
typedef void (*cheatstore_progress_fn)(u32 done, u32 total);
// n bytes from src (RAM or PSRAM) to dst in PSRAM, as the text's edits write
// it (struct cheatfile_room's put); a failed write sets room_failed.
void cheatstore_put(char *dst, const char *src, u32 n);

// The cheat file's path for the ROM at rom_path (from the root, with its
// extension) into out (size bytes); its length, or -1 if it doesn't fit
// SAFEFILE_PATH_MAX with ".bak".
int cheatstore_path(char *out, u32 size, const char *rom_path);
// Reads and parses the game's cheat file into cheatstore(), nothing decoded
// yet: 1 if it has one with a cheat in it. Call only where FatFs calls may
// run.
int cheatstore_read(const char *rom_path);
// The same for the cheat screen: CHEATSTORE_READ if the file was read
// (cheats in it or not), CHEATSTORE_NEW if the game has none (an empty
// file, made on cheatstore_write; a larger file read in windows, cheatstore_windows
// says); else why not, the file left alone: CHEATSTORE_BIG (no copy of at
// most CHEATSTORE_BIG_FILE_MAX bytes, or a larger file whose cheats aren't
// in windowable order), CHEATSTORE_OTHER_DUMP (its "# for crc32" names a dump
// the CRC cache knows the ROM isn't), CHEATSTORE_UNREADABLE (the card
// couldn't read it, or PSRAM take it), CHEATSTORE_NONE (its path too long).
enum {
	CHEATSTORE_NONE,
	CHEATSTORE_READ,
	CHEATSTORE_NEW,
	CHEATSTORE_BIG,
	CHEATSTORE_OTHER_DUMP,
	CHEATSTORE_UNREADABLE,
};
int cheatstore_open(const char *rom_path);
// The same for the cheat file at path (from the root, its name included),
// for no game: nothing checks which dump it is for
int cheatstore_open_file(const char *path);
// Settings' clean-up of the cheat file at path: removes the cheats the
// cheat screen greys (unreadable, too big, can't run, doing nothing, doing
// what an earlier one does, and master codes; never a code typed on the
// cart) and writes it back, the file deleted if nothing is left. Only when
// every cheat that stays reads as before: else it tries again keeping the
// master codes (whose keys may decode the codes after them), and failing
// that leaves the file as it is; a larger file's windows are cleaned one at
// a time, master codes kept, each only if the decoder leaves it as before.
// How many cheats went, or -1 if the file couldn't be read or written.
s32 cheatstore_clean(const char *path);
// cheatstore_clean on every cheat file under CHEATS_DIR (NAME.cht, never a
// .bak), through progress (files done of all); what it did in *sum
struct cheatstore_sum {
	u32 files;  // cheat files found
	u32 cleaned;  // of them, with cheats removed
	u32 removed;  // cheats removed in all
	u32 failed;  // files that couldn't be read or written
};
void cheatstore_clean_all(cheatstore_progress_fn progress, struct cheatstore_sum *sum);
// The game popup's look at the game's cheat file, never reading it: 0 if
// the game can have none (its path too long), else 1, with *listed the
// count the cheat screen lists if it has counted the file as it is on the
// card now (the same path, size and date: this session, else as
// /torii/cheats/counts.txt keeps it), else -1. An f_stat of the copy the
// count was kept for (NAME, or NAME.bak without it), and a read of
// counts.txt (a few hundred bytes) when this session hasn't counted it.
int cheatstore_peek(const char *rom_path, s32 *listed);
// Keeps the count cheatstore_listed found last in /torii/cheats/counts.txt
// (a safe file, its latest 32 files), for the popup after a power-off;
// nothing written if it is kept already. The cheat screen calls it as it
// leaves. Borrows the detail view's buffer.
void cheatstore_keep_listed(void);
// Starts decoding the text read now from its first cheat (a larger file's
// window with the decoder at its start), for cheatstore_decode
void cheatstore_begin(void);
// The decoder as it is before cheat i (the file's count: after the last),
// decoding the file again from the first unless it has just got there
void cheatstore_decoder_before(u32 i, struct cheat_decoder *out);
// Decodes the read file's cheats from cheatstore()->work.next on, until
// budget cycles have passed (0: all of them). 1 once all are decoded.
int cheatstore_decode(u32 budget);
// Decodes on through cheat i (A on a row the background hasn't got to)
void cheatstore_decode_to(u32 i);
// How many cheats the read file's cheat screen lists (every cheat, the ones
// that can't be picked greyed: known once it is parsed), kept for
// cheatstore_peek with the file's size and date on the card
s32 cheatstore_listed(void);
// The code keypad's and the Cheats page's edits (cheatfile.h's) to the read file's text in PSRAM:
// enum cheatfile_edit, CHEATFILE_EDIT_FAILED once a PSRAM write has failed.
// The caller decodes again and writes the file.
u32 cheatstore_add(const char *desc, const char *code, u8 format, u32 *index);
u32 cheatstore_set_code(u32 i, const char *desc, const char *code, u8 format);
u32 cheatstore_rename(u32 i, const char *desc);
u32 cheatstore_set_keys(u32 i, u16 keys);
u32 cheatstore_set_guide(u32 i, const char *guide);
u32 cheatstore_remove(u32 i);
// cheatfile_can_add for the read file in its PSRAM room (CHEATFILE_EDIT_FAILED
// once a PSRAM write has failed)
u32 cheatstore_can_add(const char *desc);
// Writes the picks back into the read file, power-cut safe, if one changed
// or an edit (cheatfile.h's) changed its text. 1 if it is as the picks say;
// 0 if not written, as when a PSRAM write of the text failed.
int cheatstore_write(void);
// A larger file's windows (0: a small file, read whole), the one read now,
// and where window k's cheats start among the file's (all its cheats: k =
// cheatstore_windows())
u32 cheatstore_windows(void);
u32 cheatstore_window(void);
u32 cheatstore_window_start(u32 k);
// The file's picked cheats, every window's; and window k's
u32 cheatstore_picks(void);
u32 cheatstore_window_picks(u32 k);
// The progress a long call shows (a larger file read whole and decoded the
// first time, or written whole): set by the cheat screen, NULL elsewhere
void cheatstore_progress(cheatstore_progress_fn progress);
// Goes to window k of a larger file: the window read now keeps its picks
// (and, edited, its text in its slot), and k's text is read (from the card
// if it isn't in its slot yet), parsed and its decoder started at the
// index's state (cheatstore_decode decodes it), its picks the file's. 0 if
// it couldn't be read: the card's copy no longer matches its index.
int cheatstore_window_go(u32 k);
// A pick or drop of cheat i of the text read now (cheatfile_toggle), a
// larger file's pick list kept with it: 1 if its pick changed
int cheatstore_toggle(u32 i);
// Cheat i's run of variants (cheatfile_group), decoded on through its end
// first: from the first (returned) to *end
u32 cheatstore_run(u32 i, u32 *end);
// The same as cheatstore_toggle, but a pick drops the picked variants of
// cheat i's run: one at a time. If i doesn't fit even so, they stay.
int cheatstore_toggle_one(u32 i);
// Turns every pick off, in every window; the window read now stays
int cheatstore_all_off(void);
// Reads window k of a larger file ahead into its slot, bytes at most a
// call; 1 once it is there (or there's nothing to read)
int cheatstore_ahead(u32 k, u32 bytes);
// Deletes the game's cheat file (both copies); 1 if none is left
int cheatstore_delete(const char *rom_path);
// After an edit left the read file with no cheat and nothing else worth
// keeping (cheatfile_empty): deletes it, both copies, the .bak first, so a
// power cut leaves the file as last written or none; the picks count as
// written. 1 if none is left.
int cheatstore_drop(void);

// A launch of the GBA game at rom_path: its picked cheats into the
// interpreter's table (gl_cheat_table, cheat.h), in file order, as many as fit
// its budget and the ROM patches kept (a cheat that doesn't fit either is
// left out whole, logged: never its table part without its patches, or
// some of its patches); gl_cheat_count gets the cheats that run in game and
// gl_cheat_patches their ROM patches' count, at most CHEATSTORE_ROM_PATCHES,
// kept in cart SRAM page 0xF0 (cartlog.h's CARTLOG_CHEAT_PATCHES): the
// game's load takes PSRAM, where the text is, and all of pReadCache, and
// EWRAM has no room for them. With psram_bytes, the game is in PSRAM
// (that many bytes) and the kept ROM patches of the same ROM's first pass are
// written into it, nothing read again, but for any past its end or over the
// kernel's own patches (rom_guard_hits, GBApatch.h): left out and logged. A
// game without a cheat file, or with none picked, gets none.
// The first pass also writes the in-game list (source/ingame.h: the picks
// shown, up to INGAME_CHEATS, in file order, each with its activation) to
// cart SRAM.
#define CHEATSTORE_ROM_PATCHES CHEATFILE_PICKED_PATCHES_MAX
void cheatstore_launch(const char *rom_path, u32 psram_bytes);
extern u32 gl_cheat_patches;
// No cheats for this launch: the table, the patches and the in-game list
void cheatstore_launch_none(void);

#endif
