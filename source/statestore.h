// A game's save states on the card (v0.8 P3, tier A): the in-game menu
// saves a state into cart SRAM's buffer (INGAME_SRAM_STATE, 448 KB, lost at
// power-off), and the kernel's next boot after Back to Torii files it here:
//
//	/torii/states/<rom path>/<save slot>/<n>.state     n = 1 to INGAME_STATE_SLOTS
//
// one set of slots for each of the game's save slots (gamesave.h). Before a
// launch with Save states on, the kernel puts the newest slot (or the one
// asked for) back in the buffer, so Load state works in game.
//
// A slot's file is binary: struct statestore_head, then the buffer in
// 32 KB chunks, each a word of its size, a word of its packed size and the
// GBA BIOS's LZ77 (lz77_compress_fast). Power-cut safe as safefile.h's text
// files are, without renames: each slot has two copies, NAME and NAME.bak;
// a write goes to the copy a reader wouldn't take, and its head (with the
// CRCs) is written last. A reader takes the valid copy with the higher seq.
//
// Call these only where FatFs calls may run, with the kernel's ROM mapped.
// They borrow pReadCache at READCACHE_STATES (ez_define.h).
#ifndef TORII_STATESTORE_H
#define TORII_STATESTORE_H

#include <gba_base.h>

#include "ingame_abi.h"
#include "saveslot.h"

#define STATESTORE_DIR "/torii/states"
#define STATESTORE_SLOTS INGAME_STATE_SLOTS
// The longest path: the folder, a ROM path, a save slot and "8.state.bak"
#define STATESTORE_PATH_BYTES 400
#define STATESTORE_CHUNK 0x8000

#define STATESTORE_MAGIC "TORIIST1"
struct statestore_head {
	char magic[8];  // STATESTORE_MAGIC
	u32 head_bytes;  // sizeof(struct statestore_head)
	u32 raw_bytes;  // INGAME_STATE_BYTES
	u32 data_bytes;  // the chunks', after the head
	u32 data_crc;  // CRC32 of the chunks as written
	u32 seq;  // this copy's: the higher of the two is the newer
	u32 order;  // the save slot's states': the newest is the highest
	struct saveslot_stamp when;  // when the kernel filed it (all zero: the clock wasn't set)
	u8 slot;  // 1 to STATESTORE_SLOTS
	u8 reserved[7];
	u32 reserved2[3];
	u32 head_crc;  // CRC32 of the head's bytes before it
};

// A save slot's state slots as the card has them (index 1 to STATESTORE_SLOTS)
struct statestore_slot {
	u8 have;  // a valid head in either copy
	u8 bak;  // the copy taken is NAME.bak
	u32 seq, order;
	u32 data_crc;  // the copy taken's
	struct saveslot_stamp when;
};
struct statestore_list {
	struct statestore_slot slot[STATESTORE_SLOTS + 1];
};

// The folder of the game's states for its save slot, or a slot's file
// (slot 0: the folder; bak: NAME.bak) into out (STATESTORE_PATH_BYTES);
// its length, or -1 if it doesn't fit
int statestore_path(char *out, const char *rom_path, const char *save_slot, u32 slot, u32 bak);
// Reads each slot's heads into l; returns how many slots have a state
u32 statestore_scan(const char *rom_path, const char *save_slot, struct statestore_list *l);
// The slot with the highest order in l, 0 for none
u32 statestore_newest(const struct statestore_list *l);
// Puts slot's state into the buffer, its tag (kept; autoload as asked) and
// its flag last; with no state there (or none that reads whole), the
// buffer's flag is cleared. 1 if the buffer holds the slot's state.
// rom_crc is the session's (ingame.h). Shows nothing.
int statestore_load(const char *rom_path, const char *save_slot, u32 slot, u32 rom_crc, u32 autoload);
// Files the buffer's state as slot (its tag says not kept), under a
// progress dialog (open one first), then marks it kept and clears the
// buffer's flag (statestore_buffer then says none). 1 if written.
int statestore_keep(const char *rom_path, const char *save_slot, u32 slot);
// Once for a game with no states for its save slot: an old /RTS/<rom
// name>.rts (the official kernel's single state, keyed by file name) with
// a state's flag becomes slot 1, through the buffer (which it leaves
// holding it). 1 if it did.
int statestore_import_rts(const char *rom_path, const char *save_slot, const char *rom_name, u32 rom_crc);
// The buffer holds no state: its tag and flag cleared (a launch that puts
// none there, so no game loads another's, nor autoloads it)
void statestore_buffer_clear(void);
// The buffer's tag and flag: 1 with *tag if the buffer holds a state
int statestore_buffer(struct ingame_state_tag *tag);
// n bytes between cart SRAM's window at sram (its page set) and RAM, in
// IWRAM (the 8-bit bus a byte at a time, three times as fast as from the cart)
void IWRAM_CODE statestore_sram_bytes(vu8 *sram, u8 *ram, u32 n, u32 to_sram);
// "Slot 1 · 27 Oct 14:03" and the like into out (size bytes), for the
// in-game menu's rows (lang.c's words)
void statestore_row(char *out, u32 size, u32 slot, const struct statestore_slot *s, int unkept);

#endif
