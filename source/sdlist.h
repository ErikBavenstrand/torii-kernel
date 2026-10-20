// A folder's entries as the SD card page lists them (v0.4 decisions, B14):
// raw file names, folders first, then files, each A-Z by R3's sort key
// (case and accents folded, numbers by value) of the name without its
// extension, ties by the raw name. Up to SDLIST_MAX files and, apart from
// them, up to SDLIST_MAX folders (so a folder past the files' cap still
// lists every subfolder): past either the list says it is truncated (the
// "showing the first N" note).
//
// The entries live in an area of their own (PSRAM in the kernel, the SD
// names area of engine/psram.h; plain memory in tests/host), written only
// through a lib_put_fn, as the library index is:
//
//	[0, SDLIST_ORDER_BYTES)  the order: u32 offsets of each entry's record,
//	                         in list order
//	then                     records, one after another, each starting on a
//	                         word: struct sdlist_rec, the name (NUL-ended,
//	                         halfword-padded), then its sort key (likewise)
//
// Reads go straight to the area. Building needs scratch in RAM (struct
// sdlist_scratch, the kernel's pReadCache): the order is gathered and
// sorted there, then written to the area in one go. The sort compares each
// entry's first SDLIST_SORT_WORDS words of key from the scratch, and reads
// the area only for entries alike that far. Hardware-free: tests/host
// builds it.
#ifndef TORII_SDLIST_H
#define TORII_SDLIST_H

#include <gba_base.h>

#include "library.h"

#define SDLIST_MAX 1024  // files a folder lists (D16), and folders, apart
#define SDLIST_ENTRIES (2 * SDLIST_MAX)
#define SDLIST_ORDER_BYTES (SDLIST_ENTRIES * 4u)
#define SDLIST_NONE 0xFFFFFFFFu
#define SDLIST_NAME_MAX LIBRARY_NAME_MAX  // a file name's bytes, without the NUL

// sdlist_rec.flags
#define SDLIST_DIR 0x0001u
#define SDLIST_CONSOLE_SHIFT 4  // bits 4-7: library_console_of's value, 0xF none
#define SDLIST_CONSOLE_MASK 0x00F0u

// sdlist_rec.count: a folder's entries, once counted (sdlist_set_count)
#define SDLIST_COUNT_UNKNOWN 0xFFFFu
#define SDLIST_COUNT_MORE 0xFFFEu  // more than SDLIST_MAX
#define SDLIST_COUNT_FAILED 0xFFFDu  // it didn't open: shown as uncounted

struct sdlist_rec {
	u32 size;    // bytes (files)
	u32 prefix;  // the sort key's first 4 bytes, big-endian (library_key_prefix)
	u16 flags;   // SDLIST_*
	u16 count;   // folders: SDLIST_COUNT_*, or the entries it lists
	u16 key;     // bytes from the name's start to the sort key's
	u16 reserved;
	// the name, then the key
};
_Static_assert(sizeof(struct sdlist_rec) == 16, "a record is 16 bytes");

// The longest record: the header, a name and a key, each NUL-ended and
// padded to a halfword, the whole padded to a word
#define SDLIST_REC_MAX ((sizeof(struct sdlist_rec) + 2 * (SDLIST_NAME_MAX + 2) + 3) & ~3u)

#define SDLIST_SORT_WORDS 3  // key words the sort keeps in RAM

struct sdlist_scratch {
	u32 offset[SDLIST_ENTRIES];  // each entry's record, in the order added
	u32 key[SDLIST_ENTRIES][SDLIST_SORT_WORDS];  // its key's first bytes, big-endian, 0 past its end
	u8 dir[SDLIST_ENTRIES];      // 1 for a folder
	u16 order[SDLIST_ENTRIES];   // entries, sorting
	u16 tmp[SDLIST_ENTRIES];
	u16 rec[SDLIST_REC_MAX / 2];  // a record on its way to the area
};

struct sdlist {
	u8 *base;  // the area
	u32 cap;   // its bytes
	lib_put_fn put;
	struct sdlist_scratch *scratch;  // while building
	u32 top;    // bytes of records used, from SDLIST_ORDER_BYTES
	u32 count;  // entries listed
	u32 folders;  // of them folders (the first ones, once finished)
	u8 truncated;  // more entries than fitted (a cap or the area)
	u8 full;       // nothing more fits: the area is full, or both caps reached
};

// Starts a new listing in the area base (cap bytes, at least
// SDLIST_ORDER_BYTES; base word-aligned) with scratch.
void sdlist_begin(struct sdlist *l, u8 *base, u32 cap, lib_put_fn put, struct sdlist_scratch *scratch);
// Adds an entry: 1 if listed, 0 if it didn't fit (then truncated; full once
// nothing more can), and 0 with nothing set for a name over SDLIST_NAME_MAX
// bytes or empty.
int sdlist_add(struct sdlist *l, const char *name, u32 size, int is_dir);
// Sorts what was added and writes the order: the list reads from then on,
// and the scratch is free.
void sdlist_finish(struct sdlist *l);

static inline const struct sdlist_rec *sdlist_at(const struct sdlist *l, u32 i)
{
	return (const struct sdlist_rec *)(l->base + ((const u32 *)l->base)[i]);
}
static inline const char *sdlist_name(const struct sdlist_rec *r)
{
	return (const char *)(r + 1);
}
static inline int sdlist_is_dir(const struct sdlist_rec *r)
{
	return (r->flags & SDLIST_DIR) != 0;
}
// The console a file's extension says (LIB_GBA...), LIB_CONSOLE_NONE for a
// folder or another file
static inline u32 sdlist_console(const struct sdlist_rec *r)
{
	u32 c = (r->flags & SDLIST_CONSOLE_MASK) >> SDLIST_CONSOLE_SHIFT;
	return c == 0xF ? LIB_CONSOLE_NONE : c;
}
// The entry named name (exactly), or SDLIST_NONE
u32 sdlist_find(const struct sdlist *l, const char *name);
// Records how many entries folder i lists (SDLIST_COUNT_MORE past
// SDLIST_MAX), or SDLIST_COUNT_FAILED
void sdlist_set_count(struct sdlist *l, u32 i, u32 count);
// The list order of two entries' records (<0, 0 only for the same, >0)
int sdlist_compare(const struct sdlist_rec *a, const struct sdlist_rec *b);

// --- Texts ------------------------------------------------------------------

// n with a comma every three digits ("1,204") into out (size bytes)
void sdlist_number(char *out, u32 size, u32 n);
// A file's size as the page shows it: "300 B", "3 KB", "1.5 MB", "16 MB"
// (1,024 to the next unit; one decimal under 10 when it isn't whole,
// rounded down)
void sdlist_size_text(char *out, u32 size, u32 bytes);
// A folder's count: "" uncounted (or failed), then one ("1 item"), many ("176 items",
// "1,024+ items" for SDLIST_COUNT_MORE) or none ("empty")
void sdlist_count_text(char *out, u32 size, u32 count, const char *one, const char *many, const char *none);

#endif
