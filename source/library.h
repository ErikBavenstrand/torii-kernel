// The library index: every game on the card, found by extension, kept in
// /torii/library.idx and, while the menu runs, in PSRAM (docs/design/
// v0.4/decisions.md, Library and data, and R1; docs/research/v0.4/research-v04-library.md).
//
// This file is the hardware-free part, which tests/host builds too: the
// extension table, the sort key, the index image (its records, folder table,
// string pool and sort order) and the queries on it. library_scan.c finds
// the games (a job between frames), and loads and writes the file.
//
// The image ("slot") is the same in PSRAM and on the card, so the file is
// the slot's sections one after another:
//
//	header   64 B (struct lib_header): magic TLIB, version, counts, the walk
//	         hash, the volume serial, a generation, CRC32s of the body and of
//	         the header
//	records  16 B each (struct lib_rec), in walk order
//	order    u16 record numbers: every game sorted by console, then by name
//	         (so each console's games are one run, A-Z), padded to 4 bytes
//	folders  u32 pool offsets of each folder's path ("/" for the root)
//	pool     NUL-ended strings, each starting on a halfword: a game's file
//	         name, then its sort key (the record has both offsets, so a
//	         comparison never measures the name), then its display name
//	         (right after the key: the name the lists show, a custom one
//	         or the cleaned file name); and the folder paths. Padded to 4.
//
// In a slot each section starts at a fixed offset (LIB_OFF_*), so a slot
// holds an index of any size up to the caps; in the file they follow each
// other. All numbers are little-endian. The file is a cache: a bad magic,
// version or CRC means a full scan. LIBRARY_VERSION changes whenever the
// layout, the sort key or the name cleaning changes, since the stored order
// depends on them.
//
// Reads go straight to the slot's memory (the PSRAM window reads like ROM);
// writes go through a lib_put_fn (psram_write in the kernel, memcpy on the
// host), since PSRAM takes writes only through it.
#ifndef TORII_LIBRARY_H
#define TORII_LIBRARY_H

#include <gba_base.h>

#define LIBRARY_MAGIC 0x42494C54u  // "TLIB"
#define LIBRARY_VERSION 4
#define LIBRARY_GAMES_MAX 4096
#define LIBRARY_FOLDERS_MAX 2048  // folders holding games
#define LIBRARY_CONSOLE_SLOTS 8  // header room, for the v0.6 systems
#define LIBRARY_NONE 0xFFFFu
// The longest sort key kept (bytes, without the NUL); ties past it fall to
// the file name
#define LIBRARY_KEY_MAX 255
// A game's file name or a folder's path, without the NUL
#define LIBRARY_NAME_MAX 255

// The consoles, in carousel order. The values are Check_file_type's (0 GBA,
// 1 GBC, 2 GB, 3 NES), which the launch path and save names use.
enum {
	LIB_GBA = 0,
	LIB_GBC = 1,
	LIB_GB = 2,
	LIB_NES = 3,
	LIB_CONSOLES,
};
#define LIB_CONSOLE_NONE 0xFFu

// lib_rec.flags
#define LIB_F_CONSOLE 0x000Fu  // the console (LIB_GBA...)
#define LIB_F_SAVE 0x0010u  // /SAVER holds its save
#define LIB_F_GONE 0x0020u  // deleted or renamed away since the scan (lib_forget)
#define LIB_F_CUSTOM 0x0040u  // its display name is a custom one (names.txt), never deduplicated

#define LIB_NAME_OFFSET 0x00FFFFFFu  // lib_rec.name's offset bits

// lib_header.flags
#define LIB_H_TRUNCATED 0x0001u  // more games than LIBRARY_GAMES_MAX, or the pool filled

struct lib_rec {
	u16 folder;  // its folder's number in the folder table
	u16 flags;
	u32 size;  // bytes
	u32 name;  // bits 0-23: pool offset of the file name; 24-31: half the bytes from it to its sort key
	u32 prefix;  // the sort key's first 4 bytes, big-endian: most comparisons end here
};

struct lib_header {
	u32 magic;  // LIBRARY_MAGIC
	u16 version;  // LIBRARY_VERSION
	u16 header_bytes;  // sizeof(struct lib_header), 64
	u16 record_bytes;  // sizeof(struct lib_rec), 16
	u16 flags;  // LIB_H_*
	u16 records;  // records, with any marked LIB_F_GONE
	u16 folders;
	u16 counts[LIBRARY_CONSOLE_SLOTS];  // games per console in the order (not gone)
	u32 total_files;  // every file the walk saw, games or not (the SD card's "1,204 files")
	u32 pool_bytes;  // used, a multiple of 2
	u32 walk_hash;  // the walk's fingerprint (library_scan.c)
	u32 volume_serial;
	u32 body_crc;  // CRC32 of the file after the header
	u32 games_seen;  // games the walk found: more than records when truncated
	u32 generation;  // which of the two files is newer (library_scan.c)
	u32 header_crc;  // CRC32 of the 60 bytes before it
};

_Static_assert(sizeof(struct lib_rec) == 16, "a record is 16 bytes");
_Static_assert(sizeof(struct lib_header) == 64, "the header is 64 bytes");

// A slot's layout
#define LIB_OFF_HEADER 0x0u
#define LIB_OFF_RECORDS 0x100u
#define LIB_OFF_ORDER (LIB_OFF_RECORDS + LIBRARY_GAMES_MAX * 16u)
#define LIB_OFF_FOLDERS (LIB_OFF_ORDER + LIBRARY_GAMES_MAX * 2u)
#define LIB_OFF_POOL (LIB_OFF_FOLDERS + LIBRARY_FOLDERS_MAX * 4u)
#define LIB_SLOT_BYTES 0x140000u  // 1.25 MB: two end at 3.5 MB, where B5's user data starts
#define LIB_POOL_MAX (LIB_SLOT_BYTES - LIB_OFF_POOL)

// Writes bytes (even) from src (RAM, halfword-aligned) to dst (halfword-
// aligned) in a slot
typedef void (*lib_put_fn)(void *dst, const void *src, u32 bytes);

// --- Names ------------------------------------------------------------------

// The console a file name's extension says (.gba, .gbc, .gb, .nes, any
// case), or LIB_CONSOLE_NONE. The one table of extensions.
u32 library_console_of(const char *name);
// The sort key of a display name (gamename_clean's: lists sort by the name
// shown, R3), into out (size bytes, at least 2): ASCII
// and Latin-1 letters folded to lower case without accents (ß to ss), each
// run of digits as 0x01, its digit count without leading zeros, then the
// digits (so "2" sorts before "10"), other UTF-8 kept as it is (after z).
// Byte order of two keys is the list order. Returns the key's length.
u32 library_sort_key(char *out, u32 size, const char *display);
// The first 4 bytes of a key, big-endian (lib_rec.prefix).
u32 library_key_prefix(const char *key);

// --- Building a slot --------------------------------------------------------

// A game's name, key and display name on their way to the pool: the name
// from the start, its key after it, and the display name at LIB_TEXT_DISPLAY
// (lib_build_display) until it moves to the key's end
#define LIB_TEXT_DISPLAY (LIBRARY_NAME_MAX + 1 + LIBRARY_KEY_MAX + 1)
#define LIB_TEXT_BYTES (LIB_TEXT_DISPLAY + LIBRARY_NAME_MAX + 1 + 2)
struct lib_build {
	u8 *slot;
	lib_put_fn put;
	u16 records;
	u16 folders;
	u16 counts[LIBRARY_CONSOLE_SLOTS];
	u32 pool;  // bytes used
	u32 games_seen;
	u16 flags;  // LIB_H_TRUNCATED once full
	char text[LIB_TEXT_BYTES] ALIGN(4);
};
// Where a caller writes a display name (GAMENAME_MAX + 1 bytes) to hand to
// lib_build_game
static inline char *lib_build_display(struct lib_build *b)
{
	return b->text + LIB_TEXT_DISPLAY;
}

void lib_build_begin(struct lib_build *b, u8 *slot, lib_put_fn put);
// Adds a folder's path ("/" or "/GBA/RPG"); returns its number, or
// LIBRARY_NONE if the table or the pool is full (then LIB_H_TRUNCATED) or
// the path is over LIBRARY_NAME_MAX bytes (no flag).
u16 lib_build_folder(struct lib_build *b, const char *path);
// Adds a game in folder, sorted by display (the name the lists show, kept
// beside it; NULL: gamename_clean's), with flags LIB_F_SAVE (/SAVER holds its
// save) and LIB_F_CUSTOM (display is a custom name). Returns LIB_ADDED; LIB_FULL if the table or the pool
// is full (then LIB_H_TRUNCATED; every such call counts in games_seen);
// LIB_BAD_NAME for a name over LIBRARY_NAME_MAX bytes or no console (not
// counted, no flag: the caller logs it).
enum {
	LIB_FULL = 0,
	LIB_ADDED = 1,
	LIB_BAD_NAME = 2,
};
int lib_build_game(struct lib_build *b, u16 folder, const char *name, const char *display, u32 size, u32 console,
		   u32 flags);
// Fills a header for what was built (its order still to sort, lib_sort),
// and writes it to the slot. body_crc and header_crc stay 0 (the file's).
void lib_build_header(struct lib_build *b, struct lib_header *h, u32 walk_hash, u32 volume_serial, u32 total_files);

// --- Sorting a slot's order, a few comparisons at a time ---------------------

#define LIB_SORT_BUF 64
struct lib_sort {
	const u8 *slot;
	lib_put_fn put;
	u8 *tmp;  // room for n u16s, beside the slot
	u32 n, width, lo, mid, hi, i, j, k;
	u8 in_tmp;  // the pass reads tmp (and writes the slot's order)
	u8 done;
	u16 buffered;
	u16 buf[LIB_SORT_BUF];
};

// Starts sorting the slot's n records into its order (writes 0..n-1 first).
void lib_sort_begin(struct lib_sort *s, u8 *slot, u8 *tmp, u32 n, lib_put_fn put);
// Merges up to budget entries; returns 1 once the order is sorted.
int lib_sort_step(struct lib_sort *s, u32 budget);
// The list order of records a and b (<0, 0 only for a == b, >0): console,
// sort key, file name, folder path, record number.
int lib_compare(const u8 *slot, u32 a, u32 b);

// --- A slot's file image ----------------------------------------------------

// The sections in file order, after the header: where each is in the slot
// and its bytes in the file (each a multiple of 4).
struct lib_section {
	u32 off;
	u32 bytes;
};
#define LIB_SECTIONS 4
void lib_sections(const struct lib_header *h, struct lib_section out[LIB_SECTIONS]);
// The file's size for this header.
u32 lib_file_bytes(const struct lib_header *h);
u32 lib_header_crc(const struct lib_header *h);
// 1 if h is a header this kernel reads: magic, version, sizes, counts within
// the caps, and its CRC.
int lib_header_ok(const struct lib_header *h);

// 1 if a slot's body agrees with its header: every offset and number in
// range, the order listing each game in its console's run (a loaded file;
// the CRC catches damage, this a bug). seen: LIBRARY_GAMES_MAX / 8 bytes
// of scratch, word-aligned.
int lib_body_ok(const u8 *slot, u32 *seen);

// --- Queries on a slot -------------------------------------------------------

static inline const struct lib_header *lib_hdr(const u8 *slot)
{
	return (const struct lib_header *)(slot + LIB_OFF_HEADER);
}
static inline const struct lib_rec *lib_rec_at(const u8 *slot, u32 rec)
{
	return (const struct lib_rec *)(slot + LIB_OFF_RECORDS) + rec;
}
static inline const u16 *lib_order(const u8 *slot)
{
	return (const u16 *)(slot + LIB_OFF_ORDER);
}
static inline const char *lib_str(const u8 *slot, u32 off)
{
	return (const char *)(slot + LIB_OFF_POOL + off);
}
// Where a console's games start in the order.
u32 lib_console_start(const u8 *slot, u32 console);
// The record of a console's nth game in list order, or LIBRARY_NONE.
u32 lib_nth(const u8 *slot, u32 console, u32 n);
// The other way: where record rec is in its console's list (n), or
// LIBRARY_NONE if it is gone. A binary search.
u32 lib_position(const u8 *slot, u32 rec);
// A record's file name, sort key and display name.
const char *lib_name(const u8 *slot, u32 rec);
const char *lib_key(const u8 *slot, u32 rec);
const char *lib_display(const u8 *slot, u32 rec);
// A record's folder path.
const char *lib_folder(const u8 *slot, u32 rec);
// Writes a record's full path into out (size bytes); returns its length, or
// 0 if it doesn't fit.
u32 lib_path(const u8 *slot, u32 rec, char *out, u32 size);
// The record whose path is path (exactly, as lib_path writes it), not gone;
// LIBRARY_NONE if none.
u32 lib_find(const u8 *slot, const char *path);
// For each of names[0..count) (bare file names), the first record not gone
// with that file name, byte for byte, into recs (LIBRARY_NONE for none):
// one pass over the records, a name's first byte picking its candidates.
// With areas (a NOR game's area, bytes), one whose size takes that area
// comes first, then any of the name.
void lib_find_names(const u8 *slot, const char *const *names, const u32 *areas, u32 count, u16 *recs);
// Takes a record out of the lists: marks it gone, removes it from the order
// and counts. Returns 0 if it was gone already.
int lib_forget(u8 *slot, lib_put_fn put, u32 rec);
// Gives a record a new file name in its folder (a rename on the SD card):
// the name and its key go to the end of the pool and the record moves to its
// place in the order. b's text buffers are the scratch (b->slot unused).
// Returns 0 if the pool is full or the new name has no console (then the
// caller rescans).
int lib_rename(u8 *slot, lib_put_fn put, u32 rec, const char *new_name, struct lib_build *scratch);
// The same with a display name: display shows (a custom name, LIB_F_CUSTOM)
// in place of new_name's cleaned one. new_name may be the record's own name
// (a game renamed in names.txt, its file left alone).
int lib_rename_shown(u8 *slot, lib_put_fn put, u32 rec, const char *new_name, const char *display,
		     struct lib_build *scratch);

// --- The kernel's index (library_scan.c) --------------------------------------
//
// The boot loads /torii/library.idx into PSRAM (library_boot) and the menu
// shows it at once; then a job walks the card between frames (f_readdir
// only) and hashes every folder's game names, sizes and dates, the save
// names in /SAVER and the volume serial. Only when that differs from the
// index's does a second walk build a new index in the other slot, sort it
// and switch to it (library_generation changes, and the frame wakes), and a
// job writes the file. With no index, or a damaged one, the boot builds it
// at once, behind a progress dialog once that takes over half a second.
//
// The job runs only from the menu's frames (library_poll restarts it when an
// engine_init dropped it). It pauses while covers load, keeps its state in
// EWRAM of its own (never pReadCache), and writes PSRAM only between FatFs
// calls. Any code that overwrites PSRAM page 0 (a launch) calls
// library_before_launch first; the menu loads the index again afterwards.

// The boot, once the card is mounted: the index from the card, or the first
// scan. Runs dialogs (dialog.h) only if the scan is slow.
void library_boot(void);
// Each pass of the menu's loop: starts the job when there is work.
void library_poll(void);
// Before PSRAM page 0 is overwritten: finishes a write under way or due
// (the card then has the index), stops the job and forgets the index.
void library_before_launch(void);
// Settings' "Rescan library": a full walk that rebuilds and writes the index
// whatever the hash says.
void library_rescan(void);

// 1 once an index is in PSRAM (after the boot, unless the card failed).
int library_ready(void);
// 1 while a walk, sort or write is under way or due.
int library_busy(void);
// 1 while the index loads, or a walk that builds one or its sort is under
// way (not the boot's hash-only check walk, a walk only due, or a write):
// box art waits for it.
int library_walking(void);
// Changes whenever the index changes (a new one, a forget, a rename, a
// launch): a screen holding record numbers lists again.
u32 library_generation(void);
// Games of a console (LIB_GBA...) and the record of its nth in A-Z order
// (LIBRARY_NONE past the end).
u32 library_count(u32 console);
u32 library_game(u32 console, u32 n);
// Where a record is in its console's A-Z list, or LIBRARY_NONE.
u32 library_position(u32 rec);
// A record's file name, size, flags (LIB_F_*) and full path (its length, 0
// if it doesn't fit). The name points into PSRAM: read it, never write it,
// and copy it to RAM before handing it to FatFs (the cart hides PSRAM while
// it talks to the card) or keeping it past a launch.
const char *library_name(u32 rec);
// Its display name (the name the lists show, before telling apart games
// that clean to the same name: LIB_F_CUSTOM says it is a custom one) and
// sort key, in PSRAM too ("" for no such record)
const char *library_display(u32 rec);
const char *library_key(u32 rec);
u32 library_size(u32 rec);
u32 library_flags(u32 rec);
u32 library_path(u32 rec, char *out, u32 size);
// The record with this full path, or LIBRARY_NONE.
u32 library_find(const char *path);
// lib_find_names in the index: the records of games named names (a NOR
// game's file name, the SD file it was written from), LIBRARY_NONE for none.
void library_find_names(const char *const *names, const u32 *areas, u32 count, u16 *recs);
// Files on the card outside the hidden folders, games or not.
u32 library_total_files(void);
// 1 if the card has more games than the index holds ("showing the first
// N").
int library_truncated(void);
// Records in the index, in the walk's order (some may be gone: library_flags
// says LIB_F_GONE). A rebuild keeps the order while the card's files stay.
u32 library_records(void);
// The menu deleted or renamed a game's file: the index follows at once, and
// a walk then writes it.
void library_forget(const char *path);
void library_renamed(const char *old_path, const char *new_name);
// A game's display name changed (Rename in a game list, written to
// /torii/names.txt): display, or with NULL its cleaned name; its list
// re-sorts at once, and a walk then writes the index.
void library_named(const char *path, const char *display);
// Something else on the card changed that the lists sort by (a custom name
// written to /torii/names.txt): a walk follows, and rebuilds.
void library_changed(void);

#endif
