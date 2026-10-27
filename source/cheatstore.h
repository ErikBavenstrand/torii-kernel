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
#ifndef TORII_CHEATSTORE_H
#define TORII_CHEATSTORE_H

#include <gba_base.h>

#include "cheatfile.h"
#include "safefile.h"

#define CHEATS_DIR "/torii/cheats"
// The largest copy read: the text and its CRC line
#define CHEATSTORE_FILE_MAX (CHEATFILE_TEXT_MAX + SAFEFILE_CRC_LINE_LEN + 2)
// A part of the text on its way between the card and PSRAM, and between two
// places in PSRAM
#define CHEATSTORE_PART 1024
// The cheat screen's detail view: its text and wrapped rows
#define CHEATSTORE_DETAIL 2048
#define CHEATSTORE_DETAIL_ROWS 64

struct cheatstore {
	struct cheatfile file;
	struct cheatfile_work work;
	struct cheatfile_cheat cheats[CHEATFILE_MAX];
	char path[SAFEFILE_PATH_MAX];  // the file's, as cheatstore_read read it
	u16 rows[CHEATFILE_MAX];       // the cheat screen's: the cheat each row shows
	u32 part[CHEATSTORE_PART / 4];  // a part read from the card (word-aligned for f_read)
	u16 stage[CHEATSTORE_PART / 2 + 2];  // a part on its way to PSRAM, a byte each side for odd ends
	u8 room_failed;  // a PSRAM write of the text failed since the read: never written back
	// The cheat screen's detail view (ui/ui_reading.h): a cheat's name, code
	// and what decoding found
	char detail[CHEATSTORE_DETAIL];
	u16 detail_start[CHEATSTORE_DETAIL_ROWS];
	u8 detail_len[CHEATSTORE_DETAIL_ROWS], detail_kind[CHEATSTORE_DETAIL_ROWS];
};
struct cheatstore *cheatstore(void);
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
// file, made on cheatstore_write); else why not, the file left alone:
// CHEATSTORE_BIG (a copy over CHEATSTORE_FILE_MAX, or a text over
// CHEATFILE_TEXT_MAX), CHEATSTORE_OTHER_DUMP (its "# for crc32" names a dump
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
// The decoder as it is before cheat i (the file's count: after the last),
// decoding the file again from the first unless it has just got there
void cheatstore_decoder_before(u32 i, struct cheat_decoder *out);
// Decodes the read file's cheats from cheatstore()->work.next on, until
// budget cycles have passed (0: all of them). 1 once all are decoded.
int cheatstore_decode(u32 budget);
// Once the read file is decoded whole: how many cheats its cheat screen
// lists (all but master codes, which only decoding tells apart), kept for
// cheatstore_peek with the file's size and date on the card. -1 (nothing
// kept) if some cheats aren't decoded yet.
s32 cheatstore_listed(void);
// The code keypad's edits (cheatfile.h's) to the read file's text in PSRAM:
// enum cheatfile_edit, CHEATFILE_EDIT_FAILED once a PSRAM write has failed.
// The caller decodes again and writes the file.
u32 cheatstore_add(const char *desc, const char *code, u8 format, u32 *index);
u32 cheatstore_set_code(u32 i, const char *desc, const char *code, u8 format);
u32 cheatstore_remove(u32 i);
// cheatfile_can_add for the read file in its PSRAM room (CHEATFILE_EDIT_FAILED
// once a PSRAM write has failed)
u32 cheatstore_can_add(const char *desc);
// Writes the picks back into the read file, power-cut safe, if one changed
// or an edit (cheatfile.h's) changed its text. 1 if it is as the picks say;
// 0 if not written, as when a PSRAM write of the text failed.
int cheatstore_write(void);
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
// kept in EWRAM: the game's load takes PSRAM, where the text is, and all of
// pReadCache. With psram_bytes, the game is in PSRAM
// (that many bytes) and the kept ROM patches of the same ROM's first pass are
// written into it, nothing read again, but for any past its end or over the
// kernel's own patches (rom_guard_hits, GBApatch.h): left out and logged. A
// game without a cheat file, or with none picked, gets none.
// The first pass also writes the in-game list (source/ingame.h: every pick,
// then other cheats up to INGAME_CHEATS, in file order) to cart SRAM, and
// puts the listed cheats that weren't picked in the table after the picks,
// off (gl_cheat_off), as many as fit its bytes, the toggles' 64 bits and the
// cycle cap with every pick on (each off costs CHEAT_COST_SKIP).
#define CHEATSTORE_ROM_PATCHES CHEATFILE_PICKED_PATCHES_MAX
void cheatstore_launch(const char *rom_path, u32 psram_bytes);
extern u32 gl_cheat_patches;
// No cheats for this launch: the table, the patches and the in-game list
void cheatstore_launch_none(void);
// The table without its extras (the patch area has no room for them): the
// in-game list's extras lose their toggles
void cheatstore_drop_extras(void);

#endif
